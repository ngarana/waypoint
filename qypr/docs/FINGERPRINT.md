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
- `LockController` owns the race policy: the first `Success` (password or
  finger) calls `RenderHost::requestUnlock()` exactly once via the existing
  `unlocking_` guard; the loser is stopped. A wrong password does not stop the
  finger thread, and a finger mismatch/timeout (`PAM_AUTH_ERR`) retries with a
  short back-off until `stop()`.
- A hard misconfiguration (missing `/etc/pam.d/qypr-fingerprint`,
  missing `pam_fprintd.so`, no device, unknown user) reports `Unavailable`
  once and the lock quietly stays password-only.
- Finger prompts only fill the status line while the password field is empty
  and no password attempt is in flight — typing or `Authenticating…` always
  wins. The pill shows a fingerprint glyph + "Touch sensor or enter
  password…" hint while the worker waits.

## Setup

1. Enroll: `fprintd-enroll "$USER"` (check with `fprintd-verify`).
2. Install the service: `cp pam/qypr-fingerprint /etc/pam.d/qypr-fingerprint`
   (or let your distro package do it).
3. Lock. Touching the reader unlocks; the password field keeps working.

## Threading notes

`stop()` is non-blocking by design: a `pam_authenticate()` already in flight
cannot be cancelled portably, so the worker runs to its `timeout=` and its
late result is dropped via a generation counter. The destructor joins a
finished worker and detaches a still-blocked one (process exit reaps it);
either way no `requestUnlock()` can fire after `stop()`.
