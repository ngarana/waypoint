# qypr-lock — Lock screen redesign

> **Status:** proposal. Nothing in this document is implemented yet.
> **Scope:** the *appearance* of the lock screen — layout, typography, colour,
> scrim, motion, and the shape of the peripheral controls. It does not change
> the authentication policy, the PAM/fingerprint flow, the lock-screen
> interaction allow-list, or anything else governed by
> `docs/LOCK_SECURITY_REVIEW.md`.
>
> **Date:** 2026-10-19.

---

## 0. Summary

The current lock screen is a competent widget layout that never settles into a
composition. It stacks five different panels on screen at once (status strip,
clock/date, password pill, media card, notification stack) in three different
widths, at three different alignments, over a flat 15–35 % black wash that
darkens the wallpaper without buying enough contrast. The result reads as
"several widgets on a dark background" rather than as a single screen with a
purpose.

This document proposes **Meridian** — a flush-left, single-axis composition:

| Change | Today | Meridian |
|---|---|---|
| Alignment | centre column + 4 corners | one left axis at `x = 96` |
| Clock | 96 px **Bold**, centred | 124 px **Light**, flush left, tight tracking, accent colon |
| Date | 18–20 px Bold sentence, centred | 12 px uppercase, `+1.6 px` tracking, above the clock |
| Password field | 420 × 56 glass capsule, hidden until first keypress | 340 px hairline-underlined input, always visible |
| Media | 420 × 280 opaque card on the main axis | one 13 px transport line, revealed on interaction |
| Notifications | 4 × 360 px glass cards stacked bottom-left | horizontal row of content-sized cards, bottom-left, max 3 + "+N" |
| Power | vertical pill of 5 unlabelled circles | one 40 px anchor; labelled row on expand |
| Status strip | solid bar with a duplicate clock | chromeless glyphs, no clock |
| Scrim | flat `rgba(0,0,0,0.15→0.35)` over the whole frame | left-anchored falloff + bottom band + top band |
| Accent | palette primary on a 2 px focus ring | palette primary on the rule, the colon, the hairline |

The palette still drives the accent, so the lock screen re-themes with the
wallpaper (matugen / Material You) exactly like the bar does.

---

## 1. Method

Three sources of evidence, all reproducible:

1. **The shipped binary.** `qypr-lock --preview out.png` renders the real
   screen without a compositor (`qypr/src/core/App.cpp:126`). The frames it
   writes are the authentic current appearance —
   `images/lockscreen-current-preview.jpg` is a verbatim capture of that path.
2. **The source.** Every claim in §3 is anchored to a file and line in
   `qypr/src/ui/`.
3. **A faithful re-render.** `docs/lock-mockup.py` ports `common/render/Painter.cpp`,
   the `theme::State` palette and the current lock geometry to pycairo, so the
   *current* screen and the *proposed* screen can be rendered over the same
   wallpaper frame and compared directly. The current-layout renders are
   `images/lockscreen-current-wallpaper.jpg`; the proposal renders are
   `images/lockscreen-proposed-*.jpg`.

The wallpaper frame used for the comparison is a real frame from the shipped
night playlist (`qypr/videos/comp_A103_C002_0205DG_*.mov`) — deliberately a
bright, high-contrast one, because that is where a lock screen either holds up
or falls apart.

```
# regenerate the mockups (needs pycairo + PyGObject)
cd qypr/docs
ffmpeg -i ../videos/comp_A103_C002_0205DG_v12_SDR_FINAL_20180706_SDR_2K_HEVC.mov \
       -ss 45 -vf scale=1920:1080 wall_bright.png
python3 lock-mockup.py .
# the committed copies in images/ are JPEG, downscaled to 1600x900
```

---

## 2. What is on screen today

Inventoried from `qypr/src/ui/Shell.cpp`, `LockRenderer.cpp`, `LockLayout.cpp`:

| Layer | Geometry | Source |
|---|---|---|
| Video wallpaper (or 3-stop gradient) | full frame | `Shell.cpp:87` |
| Flat black veil | `rgba(0,0,0, 0.15→0.35)` on reveal | `Shell.cpp:95` |
| Status strip | 36 px tall, 48 px side margins, opaque | `StatusBar` |
| Clock + date | centred, `top = 0.18·h` | `LockLayout.cpp:23` |
| Password pill | 420 × 56, radius 28 | `PasswordField.cpp`, `LockLayout.hpp:18` |
| Status / error line | centred, 16 px | `StatusMessage.cpp` |
| Media card | 420 px wide, full player | `AudioController.cpp` |
| Notification cards | 360 px, up to 4, bottom-left | `Notification.cpp` |
| Power pill | 5 × 52 px circles, bottom-right | `LockLayout.cpp:47` |

---

## 3. Findings

### F1 — Two clocks, two formats, two weights
The strip carries `%-I:%M %p` ("10:44 PM") at 15 px regular; the hero carries
`%H:%M` ("22:44") at 96 px bold (`Clock.cpp:12`, `Clock.cpp:31`). The bar clock
is *not* session-sensitive (`StatusIndicator::sensitive()` defaults to false,
`StatusIndicator.hpp:181`), so it survives onto the lock screen. The user reads
the same information twice, in two conventions, and neither is the one they
want.

### F2 — The primary affordance is invisible at rest
`LockRenderer.cpp:67` wraps the password field in `withAlpha(state.reveal, …)`
and `reveal` starts at 0 (`LockController.hpp:116`). Until the first keypress
the screen shows a clock and nothing else. The unlock affordance is therefore
discoverable only by trial. The field itself is the one element that must
never be hidden.

### F3 — The media card owns the centre
A 420 × 280 opaque card ("Mozilla firefox / Now Playing / Mad / Ne-Yo", a
progress bar, three transport buttons and a volume slider) sits directly under
the password field, on the same axis, in the same width. It is the largest
object in the composition and it is not part of the lock screen's job. It also
discloses what is playing to anyone standing at the machine — the opposite of
what a lock screen is for.

### F4 — The veil is a sledgehammer and still not enough
`Shell.cpp:95` lays one flat black rectangle over the whole frame. It darkens
the entire wallpaper to buy contrast for a 420 px column in the middle, and
even then the 96 px bold clock still sits on the bright horizon with only a
2 px drop shadow to carry it (`Clock.cpp:49`).

### F5 — The typography has no scale
96 → 18 → 16 px. The clock is 4.8× the body text and 5.3× the date, both of
which are **bold** (`Clock.cpp:12`, `Clock.cpp:16`). Bold at 96 px is the
2015-era GNOME lock screen look; it has no thin counterpart and cannot be
made to feel contemporary without changing weight, tracking and scale
together.

### F6 — The focus ring is the loudest thing on screen
`PasswordField.cpp:27` strokes a 2 px `primaryGlow` ring 3 px outside the pill.
Two rings (glow + 1 px border) around a capsule, plus a Nerd Font padlock
inside it (`PasswordField.cpp:12`), plus a placeholder that changes wording
depending on whether a fingerprint reader is present
(`"Touch sensor or enter password..."`, `PasswordField.cpp:15`).

### F7 — The power pill is five unlabelled glyphs
`PowerMenuController.cpp:19-22` defines Suspend / Hibernate / Reboot /
Shutdown / Power. They are drawn as five identical circles in a vertical glass
column with no labels and no tooltips; the last one flips between a power
glyph and an ✕. A user cannot tell a moon from a hibernation.

### F8 — Notification cards are a pile of grey rectangles
360 × 68 px glass cards, title only, no body unless expanded, four of them
**stacked vertically** in the lower-left corner. Each one carries its own fill
*and* its own border *and* a 36 px coloured tile, so a stack of four reads as
four separate objects rather than as one group — and it eats 270 px of height
that the wallpaper would rather have. See §5 for the full treatment.

### F9 — The palette's text colours are used over a photograph
`theme::State::colors.text` is derived from the Material You `on-surface` token
(`PaletteReader.cpp:93`) — a colour chosen to sit on a *solid* bar chip. The
same value is used for 124 px text over a moving video. There is no separate
overlay ramp for the lock screen.

### F10 — The hero font is not the one that was designed for
`theme::State::Fonts::family` is `"Inter"` (`Theme.hpp:150`). Inter is not
installed on this host, so Pango silently falls back to Noto Sans. The design
therefore renders in a font nobody chose, and — because there is no fallback
list — a different font on every other machine.

---

## 4. The proposed design — "Meridian"

### 4.1 Concept

**One axis, one type stack, one accent.**

The screen is a poster: everything that matters is flush-left on a single
column at `x = 96`, and the rest of the frame is picture. The password field
is not a capsule and not a card — it is a **hairline-underlined input**, the
oldest minimal-input idiom there is, and the only thing on the screen that
changes appearance when you touch it.

The accent is the palette's `primary`, and it appears in exactly three places:
the rule above the date, the colon in the clock, and the hairline under the
input. That is enough to make the screen feel authored; more would make it
feel decorated.

![Meridian — idle](images/lockscreen-proposed-idle.jpg)

### 4.2 Layout

All values are logical pixels at 1920 × 1080 and scale with the output.

| Element | Rule |
|---|---|
| Column origin | `x = 96` (fixed, not a fraction) |
| Column top | `0.30 · height`, clamped to stay clear of the status strip |
| Accent rule | 40 × 3 px, radius 1.5, `primary`, at the column origin |
| Date label | 12 px uppercase, tracking `+1.6 px`, `overlay.subtle` |
| Clock | 124 px, `PANGO_WEIGHT_LIGHT`, tracking `-3 px`, tabular figures |
| Field | 340 px wide, text baseline 6 px below the field top, hairline 46 px below the field top |
| Hint / status | 13 px, 20 px below the hairline |
| Media line | 13 px, 30 px below the hint, `reveal`-faded |
| Notification row | bottom-left, starting on the same axis, cards 56 px tall, content-sized width, 8 px gap, 56 px bottom margin (see §5) |
| Power anchor | bottom-right, 40 px circle, 56 px margin |
| Status glyphs | top-right, 38 px pitch, 50 % alpha, 64 px margin |

The clock sits at 0.30 h rather than 0.18 h: the column needs room for the
rule, label, clock, field, hint and media line, and a 0.30 h origin puts the
*whole stack* in the upper third rather than clipping the clock near the top.

### 4.3 Type scale

| Role | Size | Weight | Tracking | Colour |
|---|---|---|---|---|
| Clock | 124 | 300 (Light) | −3 px | `overlay.text` (white 96 %) |
| Date label | 12 | 600 | +1.6 px | `overlay.subtle` (white 62 %) |
| Field text | 17 | 400 | 0 | `overlay.text` / `overlay.muted` when empty |
| Hint / status | 13 | 400 / 500 | 0 | `overlay.muted` / `error` |
| Media line | 13 | 400 | 0 | `overlay.subtle` |
| Notification title | 13 | 500 | 0 | white 92 % |
| Notification body | 11.5 | 400 | 0 | white 55 % |
| Status glyph label | 13 | 500 | 0 | white 50 % |

Two details that matter more than the sizes:

- **Light weight, not bold.** A 124 px Light clock reads as typography; a
  124 px Bold clock reads as a widget. The theme must ship a family that has
  a Light face — hence the fallback list below.
- **Tabular figures.** Without `tnum`, the clock's width changes every minute
  and the whole column jitters. Enable it with
  `pango_attr_font_features_new("tnum", 0, 1)` on the clock layout.

### 4.4 Colour

The lock screen gets its **own** foreground ramp, because the wallpaper is a
video of unknown brightness and the bar's palette colours are tuned for a
solid chip. The ramp is fixed; only the accent follows the palette.

```cpp
// theme::State
struct Overlay {
    Color text    = Color::fromHex("#ffffff").withAlpha(0.96);
    Color subtle  = Color::fromHex("#ffffff").withAlpha(0.62);
    Color muted   = Color::fromHex("#ffffff").withAlpha(0.42);
    Color hairline = Color::fromHex("#ffffff").withAlpha(0.28);
    Color scrim   = Color::fromHex("#000000").withAlpha(0.66);
};
```

The **accent** is `colors.primary` — the matugen/Material You primary, so the
rule, the colon and the focus hairline re-theme with the wallpaper and follow
the solar day/night flip exactly like the bar.

**Font fallback.** `Fonts::family` becomes a Pango family *list*:

```
Inter, Noto Sans, Cantarell, DejaVu Sans, sans-serif
```

Pango parses a comma-separated list natively, so one string change fixes
F10 on every machine instead of only the one that happens to have Inter.

### 4.5 Scrim

Replace the flat veil with three gradients, each earning its keep:

| Layer | Geometry | Purpose |
|---|---|---|
| Horizontal falloff | `x: 0 → 0.62 w`, alpha `0.66 → 0` | darkens only where the type is; the right 38 % of the picture stays untouched |
| Bottom band | `y: 0.62 h → h`, alpha `0 → 0.55` | anchors the notification stack and the power control |
| Top band | `y: 0 → 96`, alpha `0.42 → 0` | gives the status glyphs something to sit on |

The horizontal falloff is the load-bearing one: it is what makes a flush-left
composition legible over *any* frame of the video without dimming the picture
the user chose to look at.

### 4.6 States and motion

| State | Appearance | Motion |
|---|---|---|
| Idle | rule, date, clock, empty field (`Password` placeholder, muted), accent hairline | — |
| Focused / typing | placeholder → `••••••`, hairline stays accent | clock lifts 10 px and scales to 0.96 over 600 ms `easeOutCubic`; hint and media line fade in over 240 ms |
| Authenticating | field unchanged | status line reads `Authenticating…` |
| Failed | hairline and status line switch to `error` | 320 ms shake, 2 oscillations, ±6 px |
| Fingerprint armed | hint line reads `Touch sensor to unlock` | — |
| Idle (deep) | `Shell`'s existing dim veil unchanged | — |

The reveal animation keeps its meaning but changes what it drives: today it
fades *the field in*; in Meridian the field is always there and the reveal
drives the **hint, the media line and the clock's lift** — i.e. the screen
promotes the input rather than conjuring it.

### 4.7 The periphery

- **Status strip** — chromeless (no strip background), clock module removed on
  the lock screen, remaining glyphs at 50 % alpha. The strip stays because the
  I1 allow-list needs a surface for volume/brightness scroll; it just stops
  competing with the hero.
- **Notifications** — see §5: a horizontal row of compact cards along the
  bottom-left, starting on the hero axis.
- **Media** — one line: `♪ Mad · Ne-Yo` plus three transport glyphs, 13 px,
  revealed on interaction. The media *transport* stays reachable; the card
  goes away.
- **Power** — a single 40 px anchor in the bottom-right. Expanding it reveals
  the four actions as a **horizontal row of labelled buttons** (icon + text)
  to the left of the anchor, so "Suspend" is a word, not a moon. The
  confirmation popover and its fail-closed timeout are untouched.

### 4.8 Accessibility

- The hero is 124 px Light — large, but Light weights are the weakest strokes
  in a UI. The scrim is what guarantees contrast; the accent colon and the
  124 px size give the eye an anchor.
- Every text colour in the overlay ramp is white at 42–96 % over a scrim that
  is at least 0.42 black under the type column. The worst case (13 px muted
  text at 42 %) lands at roughly 4.6:1 against the scrimmed wallpaper — at the
  WCAG AA floor for body text.
- The error state is not colour-only: the hairline doubles in weight and the
  message text appears where the hint was.
- The fingerprint hint is text, not just the glyph swap inside the field.

---

## 5. Notifications — form and place

Notifications are the lock screen's only "ambient" content: the owner wants to
know that something arrived without unlocking, and a passer-by should not be
able to read their mail subjects from across the room. Those two requirements
pull in opposite directions, so the design has to be explicit about which
trade it makes.

![The notification row](images/lockscreen-notifications-detail.jpg)

### 5.1 What is wrong with the current cards

| # | Problem | Evidence |
|---|---|---|
| N1 | The cards are **stacked vertically**. Four of them occupy ~270 px of the lower-left corner — the second-heaviest object on the screen after the media card, and a column of boxes where a row would do. | `Notification.cpp:180` |
| N2 | The stack lives on its own axis — `x = 48` — while the hero column is centred. Two vertical axes on one screen. | `LockRenderer.cpp:78` |
| N3 | Each card is a self-contained object: a 65 % dark fill **and** a 1 px border **and** a 36 px coloured tile. | `Notification.cpp:107` |
| N4 | The 36 px accent tile is the highest-contrast thing in the card and carries the least information — the title already says who sent it. | `Notification.cpp:117` |
| N5 | The card is 360 × 68 px for a **single line of text**: with the body hidden, most of the card is empty padding, and the title is still truncated at 360 px. | `Notification.cpp:65` |
| N6 | Overflow is **silent**: past `maxVisible` the oldest cards are dropped and the user is never told there were more. | `Notification.cpp:187` |
| N7 | The stack **jumps**: `cardHeight()` ignores `appear`, so a new notification reflows every card below it instantly while only the new card fades in. | `Notification.cpp:73` |
| N8 | There is no way to turn the content down. `sensitive_apps.conf` is an allow-list of *apps* to redact, but a non-sensitive app's title is always shown at full size. | `NotificationPolicy.cpp:65` |

### 5.2 The new row

**Place: a horizontal row along the bottom-left, starting on the hero axis.**

- *Horizontal, not a stack.* A row costs 56 px of height instead of 270, and
  it turns the lower-left corner into a **footer band** — notifications on the
  left, the power control on the right — instead of a column of boxes hanging
  off the corner. The wallpaper keeps the space that the stack was eating.
- *It starts at `x = 96`*, the same left edge as the clock, the date and the
  input, so the whole screen has one left edge even though the footer runs
  horizontally.
- *Cards grow to the right* and are content-sized, so a short notification
  ("Daemon idle") does not get the same box as a long one.
- *It shares the foot line with the power control*, so the two peripheral
  systems read as one band rather than as two competing corners.

**Form: the same card language, tightened.**

| Element | Today | New |
|---|---|---|
| Card | 360 × 68, radius 12 | 56 tall, radius 12, width = content (clamped 150–212) |
| Tile | 36 × 36, radius 8 | 28 × 28, radius 8 |
| Title | 14 px bold, top line | 13 px medium, top line |
| Body | hidden until expanded | 11.5 px, second line, one line, ellipsised |
| Fill | `glass` 65 % | `rgba(5,10,14,0.42)` |
| Border | 1 px, white 8 % | 1 px, white 12 % |
| Gap | 12 px | 8 px |
| Overflow | oldest silently dropped | a `+N` chip at the end of the row |

The card keeps its identity — a rounded, faintly filled chip with a hairline
and a coloured app tile — but it is now half the height, shows the body line
without needing a click, and sits in a row instead of a column. The tile is
still there because it is the one piece of real app identity the lock screen
has (the app's own icon via `IconResolver`, falling back to the app's initial).

### 5.3 Behaviour

| Case | Behaviour |
|---|---|
| Overflow | max 3 cards, then a `+N` chip (same 56 px height) — replaces the silent truncation (N6) |
| Sensitive | title and body replaced by "New notification", the app tile is **not** drawn (it would disclose the app) |
| Expand | click a card → the body grows to up to 3 lines and the card widens to fit; one card at a time, as today |
| Dismiss | a `×` on the card's right edge, on hover only; the card collapses and the row closes the gap |
| New card | fades in and **slides 8 px from the left** over 200 ms; the cards to its right glide over rather than jump (N7) |
| Hover | the card's fill lifts to 55 % and the border to `primary` |

### 5.4 How much detail to show

The current model has exactly two states: show everything, or redact the
whole card for the apps listed in `sensitive_apps.conf`. That is too coarse —
it cannot express "show me the titles but not the bodies", and it cannot
express "this machine is shared".

A new `[lock]` section in `bar.conf` (the lock screen already loads it —
`App.cpp:68`):

```ini
[lock]
# How much notification detail the lock screen shows.
#   full   — title and body, body expandable
#   preview — title and a one-line body (default)
#   titles — title only
#   count  — only the number of notifications
#   off    — nothing
notifications = preview
```

- `preview` (default) — the card shows the title and a one-line body, exactly
  like the reference: "Claude Code / Task in progress". Same information as
  today at half the height.
- `titles` — the body line is dropped and the card shrinks to one line.
- `count` — the whole row collapses to a single "3 notifications" chip.
- `off` — nothing. For a wall display, or a machine whose lock screen is a
  screensaver.

`sensitive_apps.conf` is unchanged and still wins: a sensitive notification
is masked in every mode, `full` included. The mode only ever reduces what is
shown; it never reveals more than the per-app policy allows.

---

## 6. Why this shape

The brief was *minimal, slick, modern* — and explicitly **not** a macOS
lock screen. Four decisions follow from that:

**Not centred, not a capsule.** A centred clock over a floating rounded
rectangle is the macOS lock screen. Meridian is flush-left with an underlined
input, which is the same minimality budget spent on a different axis: one
alignment rule instead of one glass blob.

**Not glass.** `theme::State::Style::mode` already has a `glass`/`solid`
switch, and the whole suite is built on glassmorphism. But the lock surface is
an `ext-session-lock-v1` surface, **not** a `wlr-layer-shell` surface
(`LockSession.cpp:29`) — it therefore cannot be blurred by the compositor's
`layerrule = blur` the way the bar can. Translucency on the lock screen is
always "fake glass over a video", which is exactly what makes it read muddy.
Hairlines and a scrim are honest, cheap and crisp.

**The palette stays in, but only as an accent.** The suite's strongest idea is
that the UI re-themes from the wallpaper (matugen + the solar flip). Keeping
that promise on the lock screen — as the accent rule, the accent colon, the
accent hairline and the notification ticks — makes the lock screen feel part
of the same product, and it costs no contrast, because the accent is only ever
on non-text or large-text elements.

**One axis, not three.** The current screen has a centred column and two
orphan corners at two different left edges. Meridian puts everything that
carries text on the same vertical line, so the screen reads as a single
object rather than as a layout that happens to contain a clock.

---

## 7. Implementation plan

Ordered so that each step is independently shippable and the suite stays green.

| # | Change | Files | Notes |
|---|---|---|---|
| 1 | Overlay colour ramp + `Overlay` struct | `qypr/src/ui/Theme.hpp`, `Theme.cpp` | new `theme::State::Overlay`; no behaviour change |
| 2 | Font family list | `qypr/src/ui/Theme.hpp`, `Config.cpp` | `font-family` accepts a comma list |
| 3 | Scrim | `qypr/src/ui/Shell.cpp` | replace the flat veil with the three gradients |
| 4 | Clock | `qypr/src/ui/Clock.cpp` | 124 px Light, tracking, tabular figures, accent colon; new `Clock::draw` signature (origin + width instead of centre) |
| 5 | Date label | `qypr/src/ui/Clock.cpp` | uppercase + tracked, moves above the clock |
| 6 | Password field | `qypr/src/ui/PasswordField.cpp` | hairline input, no glyph, no glow ring; **always visible** — remove it from the `reveal` gate in `LockRenderer.cpp:67` |
| 7 | Layout | `qypr/src/ui/LockLayout.cpp` | left axis at 96; recompute `Result` (add `columnX`, `ruleY`, `fieldLineY`) |
| 8 | Status strip | `qypr/src/ui/statusbar/StatusBar.cpp`, `qypr/src/ui/indicators/ClockIndicator.hpp` | hide the clock module while locked; chromeless strip on the lock surface |
| 9 | Notifications | `qypr/src/ui/Notification.cpp`, `Notification.hpp` | the horizontal row of §5: 56 px cards, content-sized width, 28 px tile, body line visible, `+N` chip, animated slide-in |
| 10 | Notification detail modes | `qypr/src/core/Config.cpp`, `Config.hpp`, `qypr/src/ui/Notification.cpp` | new `[lock] notifications = full\|preview\|titles\|count\|off`; the filter lives in the view, the per-app `sensitive_apps.conf` policy still wins |
| 11 | Media | `qypr/src/ui/AudioController.cpp` | replace the card with the one-line transport |
| 12 | Power | `qypr/src/ui/PowerMenuController.cpp`, `LockLayout.cpp` | horizontal labelled row on expand |
| 13 | Motion | `qypr/src/ui/LockController.cpp`, `LockRenderer.cpp` | reveal drives the hint/media fade and the clock lift; add the error shake |

Steps 1–7 are the visual core and can land together. 8–13 are independent of
each other and can be split across commits.

**Painter additions** (`common/render/Painter.{hpp,cpp}`):

- `drawTextTracked()` — letter spacing via `PangoAttrInt::letter_spacing`
  (Pango's `letter_spacing` is an *attribute*, not a layout setter).
- `measureInk()` — the tight glyph box, so the field can be positioned under
  the digits rather than under the line box.
- `tabularFigures()` — `PangoAttrFontFeatures` with `tnum`.
- `fillLinearGradient()` / `fillRadialGradient()` — the scrim layers.

`common/` is shared with `waylaunch`, so every painter change is rebuilt and
retested in both consumers before push (AGENT.md §3.6).

### Tests

- `LockLayoutGeometryMultipleSizes` (`tests/test_lock.cpp:109`) must be
  extended: the new geometry is asserted at 800×600, 1920×1080 and 3440×1440,
  including the short-output clamp.
- A new golden test for the lock layout would be the right guard, but the
  existing suite has no image comparison for the lock screen — the `--preview`
  frames are the de-facto review artefact. Consider adding one.
- `LockInteractionAllowList` (`tests/test_lock.cpp:197`) must keep passing
  unchanged: hiding the strip's clock does not touch the allow-list, and the
  media transport stays reachable (it moves to the one-line transport).
- The notification test in `LockInteractionAllowList` already exercises the
  expand press, the `×` dismiss and the resurrection guard. It must be
  re-pointed at the new row geometry — the dismiss target moves from a
  24 × 24 corner button inside a 360 px card to a hover `×` at the right edge
  of a content-sized card, and the card is now measured from its content, so
  the sweep in `tests/test_lock.cpp:243` has to compute the card's width
  instead of assuming 360.
- New: the detail modes. `off` must render nothing and consume no press;
  `count` must render one chip and never a title; `titles` must not render a
  body line; a sensitive notification must stay masked in every mode.

---

## 8. Invariants that must survive

These are **not** design decisions; the redesign must not weaken them.

| Invariant | Where |
|---|---|
| PAM success is the only path to unlock | `LockController.cpp:110` |
| The field never holds the secret, only a character count | `PasswordField.hpp` |
| The secret stays in a `SecureBuffer`, wiped after use | `core/SecureBuffer.cpp` |
| Lock-screen interactivity is default-deny, with the I1 allow-list | `StatusBar.cpp:109`, `tests/test_lock.cpp:197` |
| The Bluetooth pairing agent never starts while locked | `App.cpp:49` |
| Power actions require explicit confirmation and fail closed | `ConfirmPopover` |
| The session fails closed if the locker dies | `ext-session-lock-v1` |

Two of them interact with the redesign and need explicit handling:

- **I1 allow-list.** The media *transport* is allow-listed
  (`tests/test_lock.cpp:202`). Moving it from a card to a one-line transport
  keeps it reachable; the allow-list test does not change.
- **Session content on the lock screen.** Hiding the bar's clock is a
  *visibility* change, not an interaction one, so it does not touch I1 — but
  it must be implemented as a visibility rule rather than by unregistering the
  module, or the unlocked bar loses its clock too.

---

## 9. Open questions

1. **Media on the lock screen at all.** The one-line transport is the
   compromise. The privacy-first answer is to drop media from the lock screen
   entirely and keep it only in the (still-allow-listed) strip applet. This
   document assumes the one-line transport; say the word and it goes.
2. **Notifications.** §5 proposes a horizontal row of three content-sized
   cards plus a `+N` chip, with the body line visible by default. The
   stricter alternative is to make `count` the default, so a lock screen
   never shows a title until it is touched.
3. **Light palettes.** The overlay ramp is fixed light-on-dark. If the lock
   screen is ever expected to run a light wallpaper, the ramp needs a second
   variant — the accent and the geometry would not change.
4. **Portrait / small outputs.** The left axis at a fixed 96 px and the 124 px
   clock need a fallback below ~1000 px wide (scale the clock down, or switch
   to the centred arrangement). The current layout already has a clamp
   (`LockLayout.cpp:26`); the new one needs a second one.
5. **Notification row position.** Bottom-left on the hero axis is the
   proposal. The alternative is to keep the cards but float them higher, just
   under the hint line, so the bottom of the screen is left entirely to the
   picture.

---

## 10. Appendix — mockups

`docs/lock-mockup.py` renders all of these. The wallpaper is a real frame from
the shipped night playlist, chosen for its brightness.

| Image | What it is |
|---|---|
| `images/lockscreen-current-preview.jpg` | **Authentic** capture of `qypr-lock --preview` (shipped binary, gradient fallback background) |
| `images/lockscreen-current-wallpaper.jpg` | the current layout re-rendered over the wallpaper frame |
| `images/lockscreen-proposed-idle.jpg` | Meridian, at rest |
| `images/lockscreen-proposed-typing.jpg` | Meridian, typing |
| `images/lockscreen-proposed-error.jpg` | Meridian, after a failed attempt |
| `images/lockscreen-notifications-detail.jpg` | the notification row of §5 at 2× — 28 px app tile, title, body line, `+N` overflow chip |
