# qypr-lock — Security Review

> **Scope:** the `qypr-lock` binary — what an unauthenticated person can do from
> the lock screen, how the password is handled in memory, and how the process
> spawns children. Status-bar code is in scope wherever it is compiled into
> `qypr-lock` (everything outside `QYPR_LOCK_ONLY_SOURCES` is).
>
> **Method:** static review of `qol@27d7de1`, plus inspection of the host's PAM,
> polkit, D-Bus, core-dump and swap configuration on the development machine.
> **Nothing here was exercised live** — running `qypr-lock` locks the session.
> Every finding lists the test that would confirm it and close it (§6).
>
> **Provenance:** QL-3, QL-5 and QL-6 were raised during the waylaunch ↔ qypr
> integration analysis (`waylaunch/docs/INTEGRATION.md`). QL-1, QL-2, QL-4 and
> QL-7 were found while verifying that analysis's claim that pre-authentication
> actions are structurally blocked on the lock screen — **that claim was wrong.**
>
> **Date:** 2026-09-15. **Status:** all findings **Open**.

---

## 1. Summary

| ID | Severity | Finding | Location |
|---|---|---|---|
| QL-1 | **High** | Bluetooth pair + trust + connect reachable before authentication | `BluetoothIndicator.cpp:367`, `BluetoothBackend.cpp:540-549` |
| QL-2 | **Medium** | Wi-Fi radio, disconnect and saved-network switching reachable before authentication | `WifiIndicator.cpp:246-375` |
| QL-3 | **Medium** (this host) | Password copies are never wiped; host swap and core dumps are unencrypted on disk | `LockScreen.cpp:197-231`, `PamAuthenticator.cpp:69-80` |
| QL-4 | Low | SNI tray items can be activated before authentication | `SNITrayHost.cpp:127, 307, 322, 341` |
| QL-5 | Low | Process-wide `SIGCHLD = SIG_IGN` leaks into spawned programs and is mutated concurrently by `pam_unix` | `SystemActions.cpp:10` (file renamed from `PowerManager.cpp`; finding since remediated) |
| QL-6 | Low | `fork()` + `execlp()` in a multithreaded process | `SystemActions.cpp:22-24` (file renamed from `PowerManager.cpp`; finding since remediated) |
| QL-7 | Info | Shell-command Quick Settings tiles are built into the lock process, unreachable only by accident | `StatusBar.cpp:88`, `QuickSettingsPanel.cpp:103, 274` |

QL-1, QL-2, QL-4 and QL-7 share one root cause (§3) and one fix (§6, step 1).

---

## 2. Threat model

A lock screen protects an **unattended, logged-in session** from someone with
physical access to the machine — keyboard, pointer, and radio range — but not
the password.

Assets, in priority order:

1. **Session integrity.** Nothing done at the lock screen may act as the user,
   or change what the unlocked session will later trust.
2. **The password.** It must not outlive authentication — in memory or on disk.
3. **Liveness.** The owner must be able to get back in. `ext-session-lock-v1`
   fails closed: *"If the client dies while the session is locked, the
   compositor must not unlock the session … It is acceptable for the session to
   be permanently locked."* For a locker, a crash or a hang is a lockout.

| Severity | Meaning |
|---|---|
| **High** | Pre-authentication action with a **persistent** effect on the unlocked session |
| **Medium** | Pre-authentication action with a transient effect, or a secret that can persist beyond the process |
| **Low** | Hygiene or robustness defect with narrow or unverified security impact |
| **Info** | Latent — not reachable today, one refactor away |

**Out of scope:** malware already running as the user. On this host it is
partly constrained by `kernel.yama.ptrace_scope = 1`, which blocks same-uid
`ptrace` attach from non-ancestor processes.

---

## 3. Root cause — lock-screen interactivity is opt-out

The lock screen composes the same `StatusBar` as `qypr-bar`. Two mechanisms
decide what it exposes, and **neither is an interaction policy**:

1. **Visibility is a deny-list.** `StatusIndicator::sensitive()` defaults to
   `false` (`StatusIndicator.hpp:159`). Seven indicators override it — Media,
   ActiveWindow, PowerMenu, Workspaces, Pager, Taskbar, Notification. Every other
   indicator, **including every future one**, is visible on the lock screen by
   default.
2. **Capability is keyed on a null pointer.** The lock has no config, so
   `IndicatorRegistry::createAll(backends, nullptr)` (`StatusBar.cpp:48`) builds
   *every* registered indicator, and the ones whose backend is null stay hidden.
   `App.hpp:74-80` passes `.wifi`, `.bluetooth` and `.sni`, so those are live.

Nothing then separates "visible" from "interactive". `Shell::onPointerButton`
(`Shell.cpp:126`) routes a click to `statusBar_.handlePointerButton` (`:131`)
**before** the lock screen sees it, and `StatusBar::activateIndicator`
(`StatusBar.cpp:563`) opens any indicator's detail popover with no session check.

The controls that do hold today (§5) hold because a pointer happens to be null,
not because the lock screen denies anything.

---

## 4. Findings

### QL-1 — Bluetooth pair + trust + connect before authentication · **High**

**Path.**
`App.hpp:78` passes `.bluetooth = &bluetooth_` →
`BluetoothIndicator::hasDetailedView()` returns `backend_ != nullptr`
(`BluetoothIndicator.hpp:28`) → a click opens the device picker (§3) →
`clickDevice` (`BluetoothIndicator.cpp:367`) calls `pairDevice` for any unpaired
device → `onPairReply` (`BluetoothBackend.cpp:540`) calls `setTrusted` (`:547`)
and then `connectDevice` (`:549`).

**Why it succeeds.**

- BlueZ lets any local process call it: `<policy context="default"><allow
  send_destination="org.bluez"/>` (`/usr/share/dbus-1/system.d/bluetooth.conf:26-28`).
  There is no polkit check on `Pair` or on setting `Trusted`.
- The attacker controls their own device's IO capability, so they can choose
  "Just Works" pairing, which needs no prompt at all.
- For devices that do ask for a passkey or confirmation, the lock process
  registers a `KeyboardDisplay` agent (`BluetoothBackend.cpp:177`,
  `BluetoothAgent.cpp:105`), and its prompt renders **inside the lock-screen
  picker**. The comment at `BluetoothBackend.hpp:98` — "No org.bluez.Agent1 is
  registered" — is stale.

**Impact.** Given a minute at the locked machine, an attacker pairs **and
trusts** a Bluetooth HID keyboard they control. Trusted, bonded devices reconnect
unattended. Once the owner unlocks, that device types into the unlocked session
from radio range. The same picker also lets the attacker forget the owner's
devices (`handleSecondaryClick` → `forgetDevice`) and power the radio off.

**Remediation.** Fix the root cause (§3), not only this indicator:

- Add an explicit lock-screen interaction policy that **defaults to deny** — e.g.
  `StatusIndicator::lockInteractive()` returning `false`, checked in
  `StatusBar::activateIndicator` and in the click, middle-click and scroll
  dispatch whenever session content is hidden. Allow-list only what the lock
  deliberately offers (volume and brightness scroll, media transport).
- Do not start `BluetoothAgent` in `qypr-lock`.
- Correct the stale comment at `BluetoothBackend.hpp:98`.

### QL-2 — Wi-Fi control before authentication · **Medium**

**Path.** As QL-1, via `App.hpp:77` `.wifi = &wifi_` →
`WifiIndicator::hasDetailedView()` (`WifiIndicator.hpp:28`) → picker actions
(`WifiIndicator.cpp:246-375`).

**What succeeds on this host.** Locking does not change logind's
`Active` property, so a locked session is still `Active=yes` — and NetworkManager's
polkit defaults for active sessions are permissive:

| Picker action | NetworkManager call | polkit action | `allow_active` | Before authentication |
|---|---|---|---|---|
| Radio on/off | `setEnabled` | `enable-disable-wifi` | `yes` | **Succeeds** |
| Disconnect | `Device.Disconnect` | `network-control` | `yes` | **Succeeds** |
| Join a saved network | `ActivateConnection` | `network-control` | `yes` | **Succeeds** |
| Join a new network | `AddAndActivateConnection` — no `connection.permissions`, so a system connection | `settings.modify.system` | `auth_admin_keep` | Needs admin auth — **unless an authorization from the last few minutes is still cached** |
| Forget a network | `Settings.Connection.Delete` | `settings.modify.system` | `auth_admin_keep` | As above |

**Impact.** Connectivity denial; moving the machine onto a different saved
network; and, inside a cached-authorization window, joining an
attacker-controlled network that the unlocked session will then use.

**Remediation.** Covered by the QL-1 interaction policy.

### QL-3 — Password copies are never wiped · **Medium on this host**

`std::string::clear()` and `erase()` change the length; they do not overwrite
the bytes. Every copy of the secret in qypr's own code:

| # | Copy | Created | Released | Wiped? |
|---|---|---|---|---|
| 1 | Keystroke UTF-8: `char buf[64]` + `std::string` temporary | `Seat.cpp:171-174` | End of key handler | No |
| 2 | `LockScreen::password_` | `LockScreen.hpp:92`; grown by `+=` at `LockScreen.cpp:231` | `clear()` at `:198` | No. Each growth **reallocation frees the old buffer, which holds a prefix of the password**; Backspace (`utf8PopBack` → `erase`, `:31`) leaves the removed bytes in place |
| 3 | `std::string pw = password_` | `LockScreen.cpp:197` | End of `submitPassword` | No |
| 4 | Lambda capture `password` | `PamAuthenticator.cpp:69` | Worker thread exit | No |
| 5 | `ConvData::password` | `PamAuthenticator.cpp:70` | `clear()` at `:80` | No |
| 6 | `strdup` reply handed to PAM | `PamAuthenticator.cpp:30` | By libpam | Yes — libpam overwrites responses and `PAM_AUTHTOK` |

The README's security note — *"copied straight into the PAM worker and cleared
right after"* (`README.md:255`) — overstates what the code does.

**Where the leftover bytes can end up, on this host:**

- **Core dumps.** `core_pattern` pipes to `systemd-coredump` (default
  `Storage=external`, i.e. on disk) and `RLIMIT_CORE` is `unlimited` in the user
  session. A `qypr-lock` crash writes its heap to disk.
- **Swap.** A 20 GB disk-backed `/swapfile`.
- **Both unencrypted at rest.** `/` is plain ext4 on `nvme1n1p2` with no
  dm-crypt layer, so a stolen disk can be read offline.
- **Forked children.** Each `fork()` (QL-6) copies the address space, remnants
  included, until `exec` replaces it.

**Remediation.**

- One `SecureBuffer` type for the secret: fixed capacity reserved up front (no
  reallocation), `mlock`ed (never swapped), `explicit_bzero` on every shrink,
  clear and destruction. Move it into the PAM worker rather than copying it.
- `explicit_bzero(buf, sizeof buf)` after use in `Seat.cpp`.
- `prctl(PR_SET_DUMPABLE, 0)` at startup — no core dumps and no same-uid
  `ptrace`. It also makes `/proc/self/*` root-owned; confirm libmpv and libpulse
  still work.
- Correct the README note once this lands.

### QL-4 — SNI tray activation before authentication · **Low**

**Path.** `App.hpp:79` passes `.sni = &sni_`, so `SNITrayHost` is visible on the
lock screen. Left-click → `Activate` (`SNITrayHost.cpp:322`, and `:127` in the
overflow popover); middle-click → `SecondaryActivate` (`:341`); scroll → `Scroll`
(`:307`). Only the dbusmenu is withheld on the lock screen (`:329`, "by design").

**Impact.** Runs third-party tray applications' own handlers, as the user, from
an unauthenticated session. The effect depends on the application — usually a
window shown behind the lock, sometimes a state toggle (mute, VPN) or a quit.

**Remediation.** Draw the tray on the lock screen but don't make it interactive,
consistent with the existing dbusmenu decision — or allow-list it explicitly
under the QL-1 policy.

### QL-5 — Process-wide `SIGCHLD = SIG_IGN` · **Low**

`SystemActions`' constructor (formerly `PowerManager`) calls `signal(SIGCHLD, SIG_IGN)`
(`SystemActions.cpp:10`, ex-`PowerManager.cpp:10`) for the whole process, so its `systemctl` children are
reaped automatically.

**(a) It leaks into every spawned program.** Linux preserves ignored signal
dispositions across `execve`, so `systemctl` and `loginctl` start with `SIGCHLD`
ignored, and any child *they* `waitpid()` for returns `ECHILD`. qypr has already
hit exactly this: `QuickSettingsPanel.cpp:56-63` records grimblast failing with
"Clipboard error", and fixes it there by restoring `SIG_DFL` in the child before
`exec`. **`SystemActions::runCmd` (formerly `PowerManager::runCmd`) never got the same fix.** Whether `systemctl`
itself trips over it (e.g. on a polkit helper path) has not been verified.

**(b) `pam_unix` changes it from another thread.** `qypr-lock` does not run as
root and `/etc/shadow` is `0600 root`, so `pam_unix`
(`/etc/pam.d/system-auth:7`) runs the setuid helper `/usr/bin/unix_chkpwd`.
Without the `noreap` module option, `pam_unix` switches `SIGCHLD` to `SIG_DFL`
around that call and restores it afterwards — from the PAM worker thread. A
power-action child that exits inside that window is not reaped and remains a
zombie until `qypr-lock` exits. Harmless on its own, but it is process-global
state changed concurrently by two threads.

**Remediation.** Stop setting a process-wide disposition. Spawn with
`posix_spawn` (QL-6) and reap through the event loop with `pidfd_open` +
`EventLoop::addFd` — push-based, and a natural fit for the epoll design.

### QL-6 — `fork()` + `execlp()` in a multithreaded process · **Low**

`SystemActions::runCmd` (formerly `PowerManager::runCmd`) forks and then calls `execlp` (`SystemActions.cpp:22-24`, ex-`PowerManager.cpp:22-24`).
`qypr-lock` has other threads alive while this can run: the PAM worker during
authentication, and libmpv's internal threads whenever the video wallpaper is
playing. After `fork()` only the calling thread exists in the child, and any
lock another thread held stays held forever. Until `exec`, the child may only
call async-signal-safe functions.

`execlp` searches `PATH` and is not on POSIX's async-signal-safe list (`execl`,
`execle`, `execv` and `execve` are). glibc's current implementation happens to
build its candidates in stack buffers, so the practical risk today is low — but
that is an implementation detail, not a guarantee.

**Remediation.** `posix_spawn` with an absolute path (resolve `systemctl` and
`loginctl` once at startup) and `POSIX_SPAWN_SETSIGDEF` for `SIGCHLD` and
`SIGPIPE`. The same change fixes QL-5(a). waylaunch's `src/search/subprocess.cpp`
already spawns through `posix_spawn` and is usable prior art.

### QL-7 — Dormant shell-command tiles in the lock process · **Info**

`StatusBar` always calls `qsPanel_.buildTiles` (`StatusBar.cpp:88`). With
`config == nullptr`, the lock gets the compiled-in defaults, both run through
`sh -c`: `"waylaunch --power"` (`QuickSettingsPanel.cpp:103`) and the
grimblast/flameshot screenshot command (`:274`).

They are **unreachable today.** Quick Settings opens only when the `power`
indicator is activated (`StatusBar.cpp:563`), and that indicator is invisible on
the lock screen because `backends.power` is null (`PowerMenuIndicator.cpp:147`).
Keyboard focus cycling visits only visible indicators.

**Risk.** A pre-authentication `sh -c` is one refactor away, guarded by a null
pointer rather than by policy.

**Remediation.** Skip `buildTiles` when the `StatusBar` is hosted by the lock,
or cover it with the QL-1 policy.

---

## 5. Controls verified to hold

| Control | Evidence |
|---|---|
| No launcher on the lock screen | `desktopIndex` is not set in `App.hpp:74-80`, so `LauncherIndicator` hides itself and its `spawnDetached` launch path is unreachable; the `spawnDetached` path in `NotificationIndicator.cpp:325` is unreachable (no `notifications` backend) |
| No dbusmenu on the lock screen | `dbusMenu_` is null — `SNITrayHost.cpp:329` |
| No bar power menu or Quick Settings | `PowerMenuIndicator.cpp:147` |
| Session-sensitive indicators hidden | `StatusBar.hpp:132` |
| UI thread never blocks on PAM | Worker thread — `PamAuthenticator.hpp` |
| The widget never holds the secret | `PasswordField` stores only a character count — `PasswordField.hpp` |
| Typing ignored while the power dialog is up | `LockScreen::handleTextInput` |
| qypr-created fds don't leak into children | `EPOLL_CLOEXEC`, `EFD_CLOEXEC` (`EventLoop.cpp:16-17`), `TFD_CLOEXEC` (`:57`), `IN_CLOEXEC` (`ConfigWatcher.cpp:37`), `MFD_CLOEXEC` (`ShmBuffer.cpp:18`). Fds opened inside libraries were **not** audited |
| The session fails closed if the locker dies | `ext-session-lock-v1` protocol guarantee |

Offering suspend, hibernate, reboot and shut down before authentication is a
**deliberate policy**, not a finding — GNOME and Windows do the same.

---

## 6. Remediation plan

Ordered by severity, grouping findings that share a fix:

| Step | Closes | Change |
|---|---|---|
| 1 | QL-1, QL-2, QL-4, QL-7 | Default-deny lock-screen interaction policy (§3). Don't start `BluetoothAgent` or build Quick Settings tiles in `qypr-lock` |
| 2 | QL-3 | `SecureBuffer` + `mlock` + `explicit_bzero`; `PR_SET_DUMPABLE 0`; correct the README |
| 3 | QL-5, QL-6 | `posix_spawn` with absolute paths and `POSIX_SPAWN_SETSIGDEF`; `pidfd` reaping in `EventLoop`; remove `signal(SIGCHLD, SIG_IGN)` |

### Verification

Per ROADMAP gate 4, a unit test alone does not close a finding — each also needs
a live or `--preview` check. Every test below should **fail on `27d7de1`**.

| ID | Unit test | Live check |
|---|---|---|
| QL-1, QL-2, QL-4 | Build a lock-hosted `StatusBar` with the lock's backend set (mocks) and session content hidden; click `bluetooth`, `wifi` and `sni`; assert no popover opens and no backend method is called | Lock, click each indicator: nothing opens, and `bluetoothctl devices Paired` is unchanged |
| QL-3 | `SecureBuffer`: storage is zeroed after `clear()`, shrink and destruction; `PamAuthenticator::authenticate` accepts only `SecureBuffer&&` (no `std::string` overload) | `/proc/<pid>/status` shows `VmLck` > 0; `/proc/<pid>/` entries are owned by root (non-dumpable) |
| QL-5 | Spawn `/bin/sh -c 'grep SigIgn /proc/self/status'` through the power spawn helper; the `0x10000` bit (`SIGCHLD`) must be clear | `SigIgn` of the running `qypr-lock` has `SIGCHLD` clear |
| QL-6 | — | No `fork`/`execlp` remains in `src/power/` |
| QL-7 | A lock-hosted `StatusBar` has no Quick Settings command tiles | — |

---

## Appendix A — host evidence (2026-09-15)

| Setting | Observed |
|---|---|
| BlueZ D-Bus policy | `context="default"` may `send_destination="org.bluez"` |
| NetworkManager polkit | `network-control` and `enable-disable-wifi`: `allow_active=yes`; `settings.modify.system`: `auth_admin_keep` |
| logind | Session `Active=yes`; session locking does not change `Active` |
| PAM `login` stack | `pam_unix.so try_first_pass nullok` — no `noreap` |
| `unix_chkpwd` | `-rwsr-sr-x root` |
| `/etc/shadow` | `-rw------- root` |
| Core dumps | `\|/usr/lib/systemd/systemd-coredump …`, default `Storage=external`; `RLIMIT_CORE=unlimited` in the user session |
| `ptrace` | `kernel.yama.ptrace_scope = 1` |
| Swap | `/swapfile`, 20 GB, disk-backed |
| Root filesystem | `/dev/nvme1n1p2`, ext4, no dm-crypt |

```sh
grep -n -A2 'context="default"' /usr/share/dbus-1/system.d/bluetooth.conf
grep -A12 'org.freedesktop.NetworkManager.network-control' \
     /usr/share/polkit-1/actions/org.freedesktop.NetworkManager.policy
loginctl show-session "$XDG_SESSION_ID" -p Active -p LockedHint
grep pam_unix /etc/pam.d/system-auth; ls -l /usr/bin/unix_chkpwd /etc/shadow
cat /proc/sys/kernel/core_pattern; ulimit -c
cat /proc/sys/kernel/yama/ptrace_scope
swapon --show; findmnt -no SOURCE,FSTYPE /; lsblk -o NAME,TYPE,FSTYPE
```
