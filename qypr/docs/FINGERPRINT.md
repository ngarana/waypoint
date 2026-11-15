# Fingerprint unlock

qypr-lock unlocks with either the password **or** a fingerprint — whichever
succeeds first. The password goes through PAM (`PamAuthenticator`, service
`qypr-lock`); the finger goes straight to fprintd over D-Bus
(`FingerprintAuthenticator` + `FprintdDbus`), the way hyprlock and gnome-shell
do it.

## Why D-Bus and not pam_fprintd

The first version followed swaylock PR #445: a second PAM stack
(`qypr-fingerprint`, only `pam_fprintd.so`) driven on a worker thread in
parallel to the password. It worked, but a `pam_authenticate()` blocked inside
`pam_fprintd` cannot be cancelled, and that showed up in three ways:

- **Suspend.** fprintd aborts an in-flight verify when the machine sleeps
  ("Cannot run while suspended"), and `pam_fprintd` then races fprintd's own
  teardown ("ReleaseDevice failed: The device is still busy"). Measured on
  2026-11-11 with `--fingerprint-test`: after resume the restarted worker had
  not reached the reader 3 s later.
- **stop() could not stop.** The thread was detached and kept the reader
  claimed until `pam_fprintd`'s `timeout=` ran out.
- **A reset every 30 s.** Each PAM attempt opens and closes the reader, and the
  Goodix sensor logs a USB reset on each open (`usb 3-9: reset full-speed USB
  device`). With `timeout=30` that happened every 30.5 s for as long as the
  lock was up; `timeout=-1` stopped it but made the stuck-claim problem worse.

A D-Bus client owns the scan: `VerifyStop` ends it at any moment, `Release`
hands the reader back, and a scan has no timeout of its own, so one stays open
for the whole lock (the reader is opened once). No PAM file is needed for
fingerprint any more; `/etc/pam.d/qypr-fingerprint` from older installs is
unused and can be deleted.

## How it works

- `App` owns `FprintdDbus` (the transport, on the shared system-bus
  connection), `FingerprintAuthenticator` (the state machine) and
  `SleepMonitor`. Fingerprint starts once `SleepMonitor` knows the machine is
  awake (see *Suspend and resume*). `[lock] fingerprint = false` in `bar.conf`
  turns it off.
- `FingerprintAuthenticator::reconcile()` compares what is wanted (a live scan
  while `start()` is in effect and the machine is awake) with what is held
  (claim, scan) and issues **one** fprintd call at a time: end a scan first,
  release before re-claiming, then `Claim` → `VerifyStart("any")`. So
  stop/suspend/resume can never put two calls on the reader at once.
  Everything runs on the loop thread; there are no worker threads.
- `LockController` owns the race policy: the first `Success` (password or
  finger) calls `RenderHost::requestUnlock()` exactly once via the
  `unlocking_` guard; the loser is stopped. A wrong password does not stop the
  scan.
- Finger prompts only fill the status line while the password field is empty
  and no password attempt is in flight — typing or `Authenticating…` always
  wins. The pill shows a fingerprint glyph + "Touch sensor or enter
  password…" hint while a scan waits.

### VerifyStatus handling

| Status | Meaning | Action |
|---|---|---|
| `verify-match` | right finger | report Success; `VerifyStop`, `Release` |
| `verify-no-match` | wrong finger | error prompt; new scan after 500 ms — the 5th locks out (below) |
| `verify-retry-scan`, `-swipe-too-short`, `-finger-not-centered`, `-remove-and-retry` | touch again | prompt; same scan continues (`done` = false) |
| `verify-disconnected` | reader gone | silent failure; release and claim again |
| `verify-unknown-error` | fprintd aborted the scan | silent failure; new scan (ignored while suspending) |

### Giving up

Failures that show no sign of a working reader — `Claim` refused because sudo
or polkit holds the reader (`AlreadyInUse`), `VerifyStart` failing, a scan
aborted, fprintd restarting — back off 0.5 → 1 → 2 → … → 16 s and after 7 in a
row (~30 s) report `Unavailable`, so the lock quietly stays password-only. Any
finger status (match, no-match, "try again") resets that count, and so does
every resume. Permanent conditions report `Unavailable` at once: no reader
(`NoSuchDevice`), fprintd not installed (`ServiceUnknown`), nothing enrolled
(`NoEnrolledPrints`, after which the claim is released), or a polkit denial
(`PermissionDenied`). Verifying needs `net.reactivated.fprint.device.verify`,
which fprintd's policy grants to the active session.

### Wrong fingers: lockout after 5

The password is rate-limited by `pam_faillock`; the reader was not, so an
unattended locked laptop accepted an unlimited number of attempts, with a
fresh scan 500 ms after each miss. Now the 5th `verify-no-match` reports `LockedOut`: the scan is
stopped, the reader released, the status line reads *Too many fingerprint
attempts. Enter your password.*, and the lock stays password-only until it
is unlocked. The count (`FingerprintAuthenticator::kMaxMisses`) is kept for
the life of the lock process and is not reset by `stop()`/`start()` or by
suspend/resume, so sleeping the machine does not buy fresh attempts; after
a lockout `start()` reports `LockedOut` without touching the reader. Five
matches Touch ID, which asks for the password after five failed matches. Only wrong fingers count — "try again" statuses (bad placement)
do not.

## Audit log

hypridle runs qypr-lock with stdout and stderr on `/dev/null`, so the events
that matter afterwards go to syslog (facility `authpriv`, like `login`):
`session locked` (when the compositor confirms every output is covered),
`unlocked by password`, `unlocked by fingerprint`, and the lockout. Read them
with `journalctl -t qypr-lock`; failed passwords are already logged by
`pam_unix`/`pam_faillock`.

## Suspend and resume

`SleepMonitor` (`src/system/SleepMonitor.cpp`) implements the standard logind
sequence:

- **Awake**: hold a `delay` inhibitor on `sleep` ("Releasing the fingerprint
  reader before suspend"; `systemd-inhibit --list` shows it).
- **`PrepareForSleep(true)`**: `FingerprintAuthenticator::suspend()` sends
  `VerifyStop` (keeping the claim) and calls `ready` once fprintd has replied;
  the inhibitor is closed then, or after 2 s at most (logind's own
  `InhibitDelayMaxSec` is 5 s). Status reports for the dying scan are ignored.
- **`PrepareForSleep(false)`**: take a new inhibitor, then `resume()` starts a
  new scan on the claim it kept; if the scan had given up earlier, a fresh one
  starts with a full retry budget.
- **Launched mid-suspend**: hypridle's `before_sleep_cmd = loginctl
  lock-session` starts qypr-lock *after* `PrepareForSleep(true)`. `start()`
  reads logind's `PreparingForSleep` first and, if a suspend is under way,
  touches neither the reader nor the inhibitor until the machine has resumed.
- **Locked before the machine sleeps**: that is hypridle's job, not the
  lock's — logind refuses new delay inhibitors once a suspend has started.
  Set `inhibit_sleep = 3` in hypridle's `general` block: hypridle then holds
  sleep until Hyprland reports the session locked (hyprland-lock-notify-v1),
  up to logind's 5 s cap. The default (`2`, auto) picks that mode only when
  `lock_cmd` mentions hyprlock; with qypr-lock it fell back to mode 1, which
  lets go as soon as `before_sleep_cmd` is spawned, so the lock merely raced
  the other inhibitors (NetworkManager, UPower, …). Measured on 2026-11-11 the
  lock was up ~0.5 s before `PM: suspend entry` in both modes — mode 3 makes
  that a guarantee rather than luck. Check it: while locked, `systemd-inhibit
  --list` shows no hypridle sleep inhibitor; after unlock it is back.

Measured on this machine (Goodix 27c6:659a, fprintd 1.94.5, s2idle, RTC wake,
2026-11-11):

| Case | Before (pam_fprintd thread) | After (D-Bus) |
|---|---|---|
| scan running when suspend starts | fprintd "Cannot run while suspended" + `ReleaseDevice … busy`; worker idle after 10 ms | `VerifyStop` acknowledged 2 ms after `PrepareForSleep`; no fprintd errors |
| after resume | no scan reached the reader within 3 s | scanning 17 ms after resume, no reader re-open (no USB reset) |
| lock launched during suspend | — | waited for resume; claimed and scanning 1.3 s after it |

Diagnose on any machine with `qypr-lock --fingerprint-test [seconds]`: it runs
the same code without locking and logs every call, status and sleep transition
with wall-clock timestamps that line up with `journalctl -o short-precise`.

## Trust: only fprintd can unlock

A `VerifyStatus("verify-match", true)` signal unlocks the session, and the
system bus's default policy lets any local process send signals; a signal
addressed to qypr-lock reaches it no matter what it subscribed to. So
`FprintdDbus` takes the unique bus name of fprintd from the `GetDefaultDevice`
reply (only the called peer can reply, and the broker stamps the sender),
subscribes to `VerifyStatus` for that unique name and the claimed device path
only, re-checks both in the handler, and sends every device call to that
unique name — if fprintd restarts, the old claim and its signals are dead
rather than silently re-pointed. Tested: `busctl emit` of a forged
`verify-match`, both broadcast and with `--destination` set to the lock's own
connection, is ignored.

## Why a separate password stack

Enabling fingerprint login usually means adding `auth sufficient
pam_fprintd.so` to the top of the login stack — on Arch, the wiki puts it in
`/etc/pam.d/system-local-login`, which `/etc/pam.d/login` includes. If the
lock screen checked passwords on `login`, that line would run first:

- the typed password waits behind a fingerprint scan ("Place your finger on
  the reader") until a finger is touched or fprintd times out (30 s per try,
  3 tries by default), so Enter appears to do nothing; and
- that scan claims the reader, so the lock's own scan cannot, and vice versa —
  fprintd allows one claim per device.

`pam/qypr-lock` includes `system-auth` directly (pam_faillock + pam_unix), so
the password is checked immediately and the reader belongs to the lock's scan
alone. Keep `pam_fprintd.so` out of it and out of the stacks it includes. When
`/etc/pam.d/qypr-lock` is missing qypr-lock falls back to `login` (never to a
missing service, which PAM would resolve to the deny-all `other` stack) and
prints a warning on stderr.

## Setup

1. Enroll: `fprintd-enroll "$USER"` (check with `fprintd-verify`).
2. Install the password service (or let your distro package do it):

   ```sh
   install -m644 pam/qypr-lock /etc/pam.d/qypr-lock
   ```

3. Lock. Touching the reader unlocks; so does typing the password, at any
   time, without touching the reader.
