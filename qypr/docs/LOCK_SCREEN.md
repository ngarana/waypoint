# LOCK_SCREEN.md — The redesigned lock screen

`qypr-lock` renders the composition of the redesign mock
(`qypr/docs/mock/code.html`, the "AeroOS Terminal" HUD) in qypr's own design
language: Catppuccin/Material colours from the theme, glass surfaces, hairline
rules and the shared spacing scale — no sci-fi chrome, no hard-coded palette.

## The composition

```
┌───────────────────────────────────────────────────────────────────────────┐
│ status bar (shared with qypr-bar, dimmed while the lock is idle)         │
│                                                                           │
│                                                                           │
│ // 01  TUESDAY, OCTOBER 20            ┌─ [ AUTH // PASSWORD ] ─────────┐ │
│ ────────────────────────────            │ SESSION LOCKED              │ │
│ 21 / 06   UTC+03                       │ OPERATOR          ┌───────┐  │ │
│          LOCAL TIME                    │ arch              │  (fp) │  │ │
│          .36 SEC                       │ archfield · LOCAL │       │  │ │
│                                       │ > ••••••••   [UNLOCK ↵]       │ │
│ ● FINGERPRINT OFF / ● PAM qypr-lock    │ ● PAM QYPR-LOCK              │ │
│                                       └───────────────────────────────┘ │
│ ┌ chip ┐┌ chip ┐┌ chip ┐┌ chip ┐                                      │
└───────────────────────────────────────────────────────────────────────────┘
```

Two columns on one shared baseline, with a footer band at the bottom:

- **Left column** — the chronometer lockup (`Clock`): an index + date line,
  a fading hairline, the oversized monospace time with an accent slash, and a
  side stack carrying the zone, the local-time label and the seconds. Under
  it the facts line (`LockFactsRow`).
- **Right column** — the authentication card (`AuthPanel`) and, when something
  is playing, the now-playing card above it (`AudioController`).
- **Footer band** — the notification chips (`NotificationView`) along the
  bottom-left, the power pill in the bottom-right corner.

The status bar stays the top telemetry row (it already carries the clock, the
system icons and the media/volume affordances, and the I1 allow-list keeps
its scroll/transport interactions live while locked).

## What the mock's sci-fi became

The mock reports fictional telemetry. Every slot is filled with a fact the
lock screen actually owns:

| Mock | qypr |
|---|---|
| `AERO // OS / STATION_NODE.04` | the operator name and the node in the auth card |
| `LAT/LON/ALT 420.8 KM` | — (no sensor backends on the lock screen) |
| `SYS // STATE STANDBY` | `SESSION LOCKED` in the auth card header |
| `LINK STARLINK_X9` / `PWR 94%` | the shared status bar's wifi/battery indicators |
| `EXT TEMP / ATMOS / RAD FLUX` | the facts line: fingerprint state, PAM stack, pending alerts |
| `OPERATOR ID / Alex Morgan` | `getenv("USER")` and `gethostname()` |
| `AUTH // REQ_KEY`, `LEVEL_3 SECURE` | `[ AUTH // PASSWORD ]` + the PAM stack in use |
| `SHA-512 ENCRYPTED BUS` | `● PAM QYPR-LOCK` (the stack that guards the session) |
| the fingerprint ring | the biometric tile (fingerprint glyph while the worker waits, padlock otherwise) |
| `CLAUDE_CODE:` / `SSH.TOKYO-01:` daemon chips | the notification chips |
| `SOLARIS ECHOES` media strip | the now-playing card in the right column |

## Widgets and seams

The screen keeps the lock screen's seam split (policy / layout / render /
input) and adds three view widgets:

| File | Role |
|---|---|
| `src/ui/Clock.{hpp,cpp}` | the chronometer lockup (index, rule, time, side stack) |
| `src/ui/LockFactsRow.{hpp,cpp}` | the facts line under the lockup |
| `src/ui/AuthPanel.{hpp,cpp}` | the glass auth card (header, operator, input notch, status) |
| `src/ui/LockLayout.{hpp,cpp}` | the geometry: columns, the shared baseline, the footer band |
| `src/ui/LockRenderer.{hpp,cpp}` | composes the widgets, owns the reveal animation |

`PasswordField` is gone: the mock folds the input into the card, so the card
owns the input notch, the blinking cursor and the inline `UNLOCK` affordance.
The affordance is hit-tested by `LockInput` exactly like the notification
chips, and submits the password like the Enter key.

The card never sees the secret — it renders the typed character count only,
and the count comes from the controller, as before.

## Theme tokens

Everything comes from `theme::State`:

- `colors.*` — accent for the index, the slash and the `>` prompt, muted for
  the micro-labels, `error` for the failed-auth line.
- `spacing.*` / `radius.*` — the card padding, the gaps and the card radius.
- `font.family` / `font.monoFamily` — the micro-labels use the sans family,
  the chronometer, the PAM line and the status line use the monospace one.
  `font-family-mono` in `[theme]` overrides it (default: JetBrains Mono).
- `notification.bandHeight` — the height the notification band reserves at
  the bottom of the screen; the lock layout keeps the clock column clear of
  it.

Two shared render primitives were added for the mock's typography:
`Painter::horizontalGradient` (the fading hairline rules) and
`TextStyle::letterSpacing` (the wide tracking of the technical micro-labels).

## Behaviour that did not change

- Reveal-on-interaction: the chronometer and the facts line are always
  visible, the auth card and the now-playing card fade in with the reveal.
- PAM authentication is unchanged; the card only names the stack.
- The fingerprint race is unchanged; the tile mirrors the worker's state.
- The notification chips keep the expand/collapse and dismiss interactions.
- The power pill keeps the bottom-right corner.

## Verifying

```sh
cmake -S qypr -B qypr/build -G Ninja && cmake --build qypr/build --parallel
./qypr/build/qypr-lock --preview /tmp/lock.png   # idle + revealed frames
ctest --test-dir qypr/build --output-on-failure
```
