# Fingerprint unlock

qypr-lock unlocks with either the password **or** a fingerprint — whichever
succeeds first. The design follows two prior implementations:

## Inspiration

**SDDM** (`src/helper/backend/PamBackend.cpp`, `PamBackend::converse()`): the PAM
conversation forwards `PAM_TEXT_INFO` / `PAM_ERROR_MSG` to the greeter while
`pam_authenticate()` stays blocked. `pam_fprintd` reports all of its progress
this way ("Place your right index finger on the reader", "Verification timed
out", …). `PamAuthenticator` mirrors this with its `Info` handler, and
`FingerprintAuthenticator` uses the same forwarding for its background attempt.

**swaylock PR #445** ("pam: add optional fingerprint authentication"): a single
PAM stack cannot race `pam_unix` against `pam_fprintd` — the modules run
sequentially and whichever comes first blocks waiting for its input. swaylock
therefore spawns a parallel child driving a dedicated `swaylock-fingerprint`
service (only `pam_fprintd.so`) with an empty password. qypr-lock does the
same with a worker thread on the `qypr-fingerprint` service; `kscreenlocker`
runs an equivalent background attempt in parallel to its password prompt.

## How it works

- `App` owns a `FingerprintAuthenticator` next to the password
  `PamAuthenticator` (separate PAM handles on separate threads — no shared
  `busy()` gate) and starts it once the session lock is up.
- The password is checked on the dedicated `qypr-lock` service, not `login`
  (`PamAuthenticator::passwordService()`; it falls back to `login` only when
  `/etc/pam.d/qypr-lock` is missing). See *Why a separate password stack*.
- `LockController` owns the race policy: the first `Success` (password or
  finger) calls `RenderHost::requestUnlock()` exactly once via the existing
  `unlocking_` guard; the loser is stopped. A wrong password does not stop the
  finger thread.
- `App` only starts the worker when `pam_fprintd.so` and
  `/etc/pam.d/qypr-fingerprint` are both installed. Without the service file
  PAM would run the deny-all `other` stack instead, which fails instantly and
  logs a `pam_warn` line on every attempt.
- The worker sorts failed attempts by whether `pam_fprintd` *reached the
  reader* (see *Classifying failures*): a real mismatch or timeout retries
  after 500 ms for as long as the lock is up; an attempt that never reached
  the reader (no device, no enrolled finger, reader claimed by another client)
  backs off 0.5 → 1 → 2 → … → 16 s and after 7 in a row (~30 s) reports
  `Unavailable`, so the lock quietly stays password-only. A PAM load error
  (`PAM_SERVICE_ERR`, `PAM_MODULE_UNKNOWN`, …) reports `Unavailable` at once.
- Finger prompts only fill the status line while the password field is empty
  and no password attempt is in flight — typing or `Authenticating…` always
  wins. The pill shows a fingerprint glyph + "Touch sensor or enter
  password…" hint while the worker waits.

## Why a separate password stack

Enabling fingerprint login usually means adding `auth sufficient
pam_fprintd.so` to the top of the login stack — on Arch, the wiki puts it in
`/etc/pam.d/system-local-login`, which `/etc/pam.d/login` includes. If the
lock screen checked passwords on `login`, that line would run first:

- the typed password waits behind a fingerprint scan ("Place your finger on
  the reader") until a finger is touched or fprintd times out (30 s per try,
  3 tries by default), so Enter appears to do nothing; and
- that scan claims the reader, so the parallel `qypr-fingerprint` worker
  cannot, and vice versa — fprintd allows one claim per device.

`pam/qypr-lock` includes `system-auth` directly (pam_faillock + pam_unix), so
the password is checked immediately and the reader belongs to the
fingerprint worker alone. Keep `pam_fprintd.so` out of it and out of the
stacks it includes. When `/etc/pam.d/qypr-lock` is missing qypr-lock falls
back to `login` (never to a missing service, which PAM would resolve to the
deny-all `other` stack) and prints a warning on stderr.

## Classifying failures

PAM return codes cannot tell a missed or timed-out scan from a reader that is
out of reach. Measured on fprintd 1.94.5 / Linux-PAM 1.7.3:

| Attempt | `required` stack | shipped `sufficient` stack | Messages |
|---|---|---|---|
| 10 s verification timeout | `PAM_AUTHINFO_UNAVAIL` after 10.8 s | `PAM_PERM_DENIED` | "Place your … finger on the …", "Verification timed out" |
| reader claimed by another client | `PAM_AUTHINFO_UNAVAIL` after 0.01 s | `PAM_PERM_DENIED` | none |
| service file missing (`other` stack) | `PAM_AUTH_ERR` | `PAM_AUTH_ERR` | none |

`pam_fprintd` returns the same code for a timeout as for a missing reader, and
a stack whose only module is `sufficient` reports every module failure as
`PAM_PERM_DENIED` (nothing decided the stack). So the code alone would either
never give up (the old behaviour: retry twice a second forever when no finger
is enrolled) or give up on the first idle timeout.

What does differ is the conversation: once it holds the reader, `pam_fprintd`
always announces "Place your … finger on the …". The worker's conversation
records whether any `PAM_TEXT_INFO` / `PAM_ERROR_MSG` arrived. An attempt
that spoke is a real miss and is retried indefinitely; a silent attempt counts
toward the give-up limit. The ~30 s window outlasts another `pam_fprintd`
client (sudo, polkit) that still holds the reader when the lock starts.

## Setup

1. Enroll: `fprintd-enroll "$USER"` (check with `fprintd-verify`).
2. Install both services (or let your distro package do it):

   ```sh
   install -m644 pam/qypr-lock        /etc/pam.d/qypr-lock
   install -m644 pam/qypr-fingerprint /etc/pam.d/qypr-fingerprint
   ```

3. Lock. Touching the reader unlocks; so does typing the password, at any
   time, without touching the reader.

## Threading notes

`stop()` is non-blocking by design: a `pam_authenticate()` already in flight
cannot be cancelled portably, so the worker runs to its `timeout=` and its
late result is dropped via a generation counter. The destructor joins a
finished worker and detaches a still-blocked one (process exit reaps it);
either way no `requestUnlock()` can fire after `stop()`.
