# qypr Development Roadmap

qypr is a native C++ Wayland lockscreen with an integrated, **general-purpose
status bar** — and, as of Phase 7, that same bar also ships as a standalone
desktop panel (`qypr-bar`, a waybar replacement) via wlr-layer-shell. The bar
targets Hyprland *and every other Wayland compositor*: it uses only standard
Wayland protocols and freedesktop/systemd interfaces — never compositor-specific
APIs (no `hyprctl`, no Hyprland IPC) and never daemon-specific interfaces. Widget
specs live in [STATUS_BAR.md](STATUS_BAR.md); this document sequences the work.

## Non-negotiable gates (every phase)

1. **Strict decoupling** — StatusBar ⟂ LockScreen; `Shell` is the sole
   composition point; bar code depends only on shared foundations
   (`EventLoop`, `Painter`, `Theme`, `Widget`, `Invalidator`).
2. **Minimal footprint** — no spawned processes, no polling (push via fds in
   the epoll loop), one shared bus connection per bus, no threads.
3. **Native only** — standard protocols/daemons (UPower, logind,
   NetworkManager, BlueZ, PipeWire, udev); works on any Wayland WM.
4. **Live verification** — every backend is proven against the real daemon
   (harness + `--preview` frames) before its phase is called done. Unit
   tests alone do not close a phase.

## Status

| Phase | Scope | State |
|-------|-------|-------|
| 1 | Core framework: StatusBar, registry, QS panel, popovers | **Done** |
| 2 | Clock + Battery (UPower push over shared `SystemBus`) | **Done** — verified live |
| 2.5 | Lockscreen integration: chromeless, frame-aligned, reveal-dimmed | **Done** |
| 3a | **Brightness** — sysfs read, udev-push change events, logind write; scroll-to-adjust; QS slider | **Done** — verified live |
| 3b | **WiFi** — NetworkManager D-Bus on `SystemBus`; signal icon; QS toggle | **Done** — verified live |
| 3c | **Bluetooth** — BlueZ D-Bus on `SystemBus`; device count; QS toggle | **Done** — verified live |
| 3d | **DND** — qypr-local state, Shell-mediated suppression; QS toggle | **Done** — suppression verified in preview |
| 4 | **Volume** — libpulse against pipewire-pulse via the `PulseLoop` `pa_mainloop_api` adapter over `EventLoop`; QS slider + scroll | **Done** — verified live (mute toggle UI deferred to Phase 6 polish) |
| 5 | **SNI tray host** — host mode against the running `StatusNotifierWatcher` on the session bus; themed icons via inheritance-aware `IconResolver`; left-click `Activate` | **Done** — verified live (nm-applet + blueman icons render; blueman `Activate(ii)` targeted) |
| 6 | **Polish** (cosmetic only) — full keyboard nav (Home/End, slider arrows); battery charging pulse; icon crossfades; per-indicator tooltips; volume mute-toggle UI. *Tray context menus + async fetch graduated to Phase 13 — they are the tray's real blocker, not cosmetics* | **Done** — verified live (slider arrows adjust ±5%; tooltip fades in after 500ms hover; battery charging draws a 1Hz success-tinted glow ring; icon glyph swaps crossfade 300ms; mute icon click toggles + greys the row) |
| 7 | **Standalone `qypr-bar`** — separate binary hosting the same StatusBar on `wlr-layer-shell` for daily (unlocked) use on any Wayland WM. Reserves an exclusive zone (a real panel), enables session-sensitive widgets (`setSessionContentVisible(true)`), starts the WM backends, and draws a subtle backdrop (`setBackdrop`) so the chromeless glyphs stay legible over any wallpaper | **Done** — verified live on Hyprland (reserves 66px zone, stacks with waybar; QS opens with real WiFi/BT/battery data; surface grows 66→full for overlays and shrinks back) |
| 8 | **WM widgets** — workspaces (`ext-workspace-v1`) + active window (`wlr-foreign-toplevel-management`) | **Done** — verified live; **gated hidden while locked** (session-sensitive); now surface in the unlocked qypr-bar (Phase 7) |
| 9 | **Config & panel geometry** — hand-rolled INI at `$XDG_CONFIG_HOME/qypr/bar.conf`; module set/order per zone (any module in any zone), position (top/bottom), height, margins, backdrop, clock formats | **Done** — verified live (bottom bar at y=1138 h=62; `CFG %H:%M:%S` clock; modules reordered/dropped/re-homed; typo diagnosed; no-config still yields the shipped default). *Auto-hide and per-output selection deferred — see below* |
| 10a | **Session surface: notifications + power** — bell + count + history popover over the existing `NotificationMonitor`; session menu (Lock/Suspend/Hibernate/Restart/Shut Down) over the existing `PowerManager`, destructive rows arm-then-confirm | **Done** — verified live (bell counts real `notify-send`s 1→3; history lists them newest-first, critical accented red; menu renders). Double-gated: session-sensitive **and** absent without their backends, so `qypr-lock` has neither |
| 10b | **Session surface: media (MPRIS)** — now-playing applet (click=play/pause, scroll=track) + transport popover; **poll→push conversion (D4)** via sdbus-c++ `addMatch` + `getEventLoopPollData()`/`processPendingEvent()` on the `EventLoop` (no thread, no timer); push is opt-in so the lock screen's polled path is untouched | **Done** — verified live (harness: 6 arg0namespace signals delivered on real bus changes; bar shows a real paused phone track). Also fixed `pickActive`: a *stopped* preferred player no longer outranks a *playing/paused* one |
| 11 | **Task manager** — icons-only window list over the existing `ToplevelBackend` (already tracks every toplevel); click-to-focus, minimize, close, grouping, `.desktop` icons | **Done** — click-to-focus, click-focused→minimize, middle-click close, `.desktop` icon + initial-letter fallback; minimized dimming. *Grouping / pinning still deferred (low value: flat list of every toplevel suffices on most panels)* |
| 12 | **Clock calendar popover** — config-driven format, month calendar, timezones | **Done** — verified live (config drives `format` / `tooltip-format` / `timezones`; month grid with week numbers, prev/next/Today nav, scroll between months; secondary timezones listed below the grid) |
| 13 | **Tray completeness** — `com.canonical.dbusmenu` right-click menus (absorbs Phase 6's tray items), `SecondaryActivate`, scroll, overflow "hidden items", async fetch | **Done** — dbusmenu opens, item activation fires, `SecondaryActivate` on middle-click, `Scroll(dx,dy)` forwarded to the under-cursor item, and an overflow chevron + popover list the "Passive" SNI items hidden from the strip per spec |
| 14 | **Widget depth** — network connection picker, BT device list, per-app audio + device switching, power profiles, multi-display brightness | Planned |
| 15 | **Utility indicators** — launcher, clipboard, keyboard layout, idle inhibitor, system monitors | Planned |

**Objective check.** Phases 1–13 closed the *engine* and the *session + widget
depth* gaps against the stated objective — **a full KDE-Plasma-calibre panel
replacement**:

1. ~~**Zero user configuration**~~ — **closed (9)**: `bar.conf` drives the module
   set/order, geometry, and formats. *Open: auto-hide, per-output.*
2. ~~**No session surface**~~ — **closed (10–11)**: notification centre, session
   / power menu, now-playing applet, and the icons-only taskbar are all live
   and session-sensitive (never leak onto the lock screen).
3. ~~**Widget depth stops at "status"**~~ — **mostly closed (14, landed)**: WiFi
   AP picker, BT device list, per-app audio + device switching, and
   power-profile switching landed. Anything still open is depth polish, not
   "shows state only".

Full matrix and the architectural decisions (D1–D6) are in
[STATUS_BAR.md § KDE-Panel Parity](STATUS_BAR.md#kde-panel-parity--gap-analysis).

## Sequencing rationale

- **D-Bus widgets first (3a–3c):** they reuse the proven `SystemBus` +
  push pattern from Battery — no new architecture per widget, one new
  backend each. Brightness leads because it also plumbs the pointer-scroll
  event path (Seat → InputSink → Shell → bar) that Volume reuses later.
- **Volume as its own phase (4):** libpulse is the only backend needing new
  event-loop machinery (a `pa_mainloop_api` adapter with io/time/defer
  support). Isolating it keeps that risk out of the D-Bus widgets.
- **Standalone bar (7):** the `Invalidator`-only dependency and the decoupling
  gate meant StatusBar was already hostable, so the new work was a layer-shell
  platform host (`BarDisplay` + `BarWindow`, parallel to `WaylandDisplay` +
  `Output`; the lock path is untouched) plus a thin `BarApp` that owns the same
  backends and routes input — the StatusBar itself did not change. `Seat` was
  decoupled from the concrete `Output` (a surface→size resolver) so it serves
  both hosts. The bar surface anchors top with an exclusive zone, and grows to
  the full output height only while an overlay is open (then shrinks back so the
  desktop keeps its clicks) — `StatusBar::hasOpenOverlay()` drives that. Landing
  it after the widget set made the first `qypr-bar` release useful on day one.
- **WM widgets (8) built ahead of the bar host, but gated:** the backends
  (`WorkspaceBackend`, `ToplevelBackend`) and indicators
  (`WorkspacesIndicator`, `ActiveWindowIndicator`) are done and verified live,
  but they are **session-sensitive** — they reveal the current workspace and
  focused window — so `StatusBar` hides them unless the host calls
  `setSessionContentVisible(true)`. The lock screen never does, so nothing
  leaks while locked; only the unlocked qypr-bar (Phase 7) turns them on and
  starts the backends. `ext-workspace-v1` is a standard protocol; the active
  window uses `wlr-foreign-toplevel-management` because it is the only widely
  supported protocol carrying per-window *focus* state (the standard
  `ext-foreign-toplevel-list-v1` is list-only). Still no per-WM IPC — portable
  across Hyprland, Sway, river, ….
- **Config first (9):** it is the only gap that *blocks* others — module
  selection, geometry, formats and per-module spawn commands are inputs every
  later phase needs. Building 10–15 first would bake in more compile-time
  constants to retrofit. It was also cheap, as predicted: `IndicatorRegistry`
  already keyed factories by id/zone/priority, so selection became a filter in
  `createAll()` and the indicators were untouched. Config reaches indicators via
  a `const Config*` on the existing `SystemBackends` aggregate — no new plumbing,
  and `qypr-lock` simply passes `nullptr` (every key has a compiled default, so a
  config-less host is always valid).
  **Deferred from 9 (not built):** `auto-hide`/dodge-windows and per-output
  (`outputs = …`) selection — both are real panel features, but neither blocks
  10–15, and auto-hide needs a pointer-proximity + reveal state machine that is
  its own piece of work. Per-module `spawn-on-click` (D1) lands with the
  launcher in 15, which is the first thing that needs it.
- **Session surface next (10):** the highest value per line of code in the whole
  plan — `MprisController`, `NotificationMonitor` (+`NotificationLog`) and
  `PowerManager` are **already written and verified**, just never instantiated by
  `BarApp`. The work is applets + wiring, not backends. The one real task is
  converting MPRIS from 1s polling to `PropertiesChanged` push (decision D4)
  before it runs in an always-on panel.
- **Taskbar (11) before the long tail:** it is the signature panel feature and
  the data layer is already there — `ToplevelBackend` tracks every toplevel and
  `ActiveWindowIndicator` merely discards all but the focused one. The new work
  is list UI + `activate`/`minimize`/`close` + `.desktop` icon lookup (D5).
- **Tray completeness (13) folds in Phase 6:** dbusmenu is the tray's real
  blocker (menu-only items like nm-applet are inert without it), so it moved out
  of generic "polish" into its own phase with the overflow popup.
- **Depth (14) before the long tail (15):** making the existing widgets actually
  *work* beats adding more widgets that only show state.

## Per-phase verification

| Phase | Proof required |
|-------|---------------|
| 3a | Real backlight % shown; logind write path exercised; udev event observed on external change |
| 3b | Real SSID/strength; toggle round-trip via NetworkManager |
| 3c | Real adapter/device state; toggle round-trip via BlueZ |
| 3d | Cards suppressed while DND on; reappear intact when off |
| 4 | Live sink volume/mute mirror; slider/scroll writes audible in `wpctl status` |
| 5 | A real SNI app shows a themed icon (nm-applet `nm-signal-75`, blueman); left-click targets the item's real `Activate(ii)` (verified present on blueman) |
| 7 | Bar renders on Hyprland (verified: reserves a 66px exclusive zone, full width, stacks below waybar; workspaces + active window + system indicators all draw; gear-click opens Quick Settings with real WiFi/BT/battery/brightness/volume, growing the surface 66→1200 then shrinking back); session-sensitive widgets appear only here, never on the lock screen. Cross-WM (Sway/river) still to spot-check |
| 8 | Live workspace list with active highlight + switch-on-click; active-window title tracks focus — both **absent from the locked bar** (verified: preview shows neither), present only under `setSessionContentVisible(true)` |
| 9 | ✅ A module is dropped, zones reordered, a module re-homed across zones, the bar moved to the bottom and the clock format changed — **all with no recompile** (verified live: `position=bottom` → layer at y=1138 h=62; `format = CFG %H:%M:%S` → `CFG 12:47:41`; wifi/bt/brightness/dnd dropped; clock ahead of workspaces; empty centre); a typo names the bad id and lists the valid ones; **no config still yields the shipped default bar** (top, h=66, silent) |
| 10a | ✅ A real notification lands in the history popover and the bell count tracks it live (verified: 3 × `notify-send` → bell "3", all three listed newest-first with app/title/body, `-u critical` accented red); the power menu renders and destructive rows require a confirming second click. **Both absent from the locked bar** (verified: preview shows no bell, no power button) |
| 10b | ✅ Real track from a live player, pushed with no polling (harness on the real session bus: fd + `processPendingEvent` delivered 12 broad / 6 narrow `arg0namespace` signals; the bar rendered "▶ RUPIE EDWARDS — …" from a phone via kdeconnect that the old `pickActive` hid behind a stopped browser); **absent from the locked bar** (verified: preview shows no media applet) |
| 11 | Every open window appears; click focuses, click-active minimizes, middle-click closes; `.desktop` icons render (initial-letter fallback otherwise); minimized windows are dimmed; tracks open/close live. *Grouping / pinning deferred — flat list sufficient on most panels.* |
| 12 | Calendar matches `cal` for the current month; prev/next/Today nav moves the displayed month; scroll advances; week numbers + secondary timezones render; config drives `format` / `tooltip-format` / `timezones` |
| 13 | **nm-applet's real menu opens and an item actuates** (the item that Phase 5 could not reach); overflow chevron shows/hides items; middle-click `SecondaryActivate` fires; `Scroll(dx,dy)` reaches the under-cursor item |
| 14 | Connect to a real network from the picker; connect a real BT device; move a live stream to another sink and mute one app |
| 15 | Each indicator proven against its real source (launcher spawns; clipboard captures a copy; layout switch reflects; inhibitor blocks a real idle cycle; monitors match `top`/`df`) |
