# qypr Decomposition Plan

Reviewed: 2026-09-22 — status re-checked against `main` at `9954082`.

This document tracks the qypr modules that still need decomposition beyond the
theme migration described in [`ARCHITECTURE_REVIEW.md`](ARCHITECTURE_REVIEW.md).
The first pass of that migration has landed (see *What has landed* below), so
this revision records per-module status: what is done, what is partial, and
what is still open. The goal is unchanged: create smaller, testable seams
without weakening the security boundary between `qypr-lock` and `qypr-bar`.

Status legend: **Done** — the seam exists and is exercised by tests;
**Partial** — the seam exists but ownership is still mixed; **Open** — not
started.

Large files are not automatically bad. Decomposition is recommended where one
module combines several of the following:

- platform protocol parsing and application state
- state management and Cairo rendering
- layout and input routing
- lifecycle orchestration and business policy
- lock-screen policy and unlocked-session capabilities
- multiple competing representations of the same data

## What has landed

- **Theme as an injected value** (`01da531`, `2e88b39`, `e57963f`): the design
  lives in [`theme::State`](../qypr/src/ui/Theme.hpp#L48) values; hosts own one
  and widgets read it through `ThemeAware::setTheme()/theme()`
  ([`Theme.hpp`](../qypr/src/ui/Theme.hpp#L263)). The mutable
  `theme::color/font/spacing/...` namespaces, `toGlobals()`, and `loadTheme()`
  are deleted; [`loadThemeState()`](../qypr/src/ui/Theme.cpp#L47) is a pure
  state builder. `AutoPalette` is an owned day/night value, ticked by each host
  and fed by its own GeoClue fix.
- **Stable quick-settings tile identity** (`b0657f6`): `QSTile::Role`
  ([`QSTile.hpp`](../qypr/src/ui/statusbar/QSTile.hpp#L31)) replaced title
  matching for ownership; the panel dedupes and replaces by role
  ([`QuickSettingsPanel.cpp`](../qypr/src/ui/statusbar/QuickSettingsPanel.cpp#L114)).
  Tests: `QuickSettingsPanelRoleDedup`, `DNDIndicatorVisibilityAndTile`.
- **Capability interfaces for applets** (`fcc0dea`): `ICompactView`,
  `ITileProvider`, `IDetailProvider`, `IIndicatorLifecycle`, `IIndicatorInput`,
  and `IIndicatorPolicy`
  ([`IndicatorCapabilities.hpp`](../qypr/src/ui/statusbar/IndicatorCapabilities.hpp#L25))
  are composed into `StatusIndicator`; `IndicatorCapabilitySeam` consumes the
  narrow types.
- **Shared matugen token lookup** (`b3793ce`): candidate-token resolution lives
  once in [`MatugenTokens.hpp`](../common/render/MatugenTokens.hpp#L19); qypr
  and waylaunch keep their own format parsers.
- **Palette source/readers extracted** (step 4): path resolution and file I/O
  in [`PaletteSource`](../qypr/src/ui/PaletteSource.hpp); CSS/GTK/JSON decode
  and token→theme mapping in
  [`PaletteReader`](../qypr/src/ui/PaletteReader.hpp). `loadThemeState()` is
  defaults + typed overrides only. Tests: `PaletteReaderParsesMatugenFormats`,
  `ThemePropagatesToStatusBarChildren` (widget-level cascade).
- **Build, test, and doc gates** (`a25653c`, `f662b89`): explicit qypr source
  lists, the B1 lock-only-object gate, qypr's CTest registration
  (`ctest --test-dir qypr/build` runs `qypr-test`), and a
  documentation-path gate.
- **Lock-screen seams** (step 9): `LockController`, `LockInput`, `LockLayout`,
  `LockRenderer`, and `PowerMenuController` now separate authentication and
  reveal policy, event mapping, geometry, Cairo drawing, and fixed power
  actions while preserving the `LockScreen` host contract and `SecureBuffer`
  password path. `LockLayoutGeometryMultipleSizes` and
  `PowerMenuRequiresExplicitConfirmation` exercise the extracted seams.
- Earlier review findings closed alongside those: one shared process API
  (`a3443ba`), unified desktop-entry and toplevel models (`f179bfc`), and the
  stale nested waylaunch test graph removed (`60c60a9`).

## Target dependency shape

The desired direction is:

```text
application host
  -> runtime/controller objects
      -> capability-specific backend ports
          -> D-Bus / Wayland / Pulse / filesystem adapters
      -> view models and layout engines
          -> Cairo/Pango rendering
```

Backends should publish typed snapshots and operation results. Controllers
should coordinate lifecycles and user actions. Views should render and hit-test
without knowing D-Bus object paths or protocol reply phases.

That publication contract already exists — `WifiSnapshot`, `BluetoothSnapshot`,
`BatterySnapshot`, `VolumeSnapshot`, `BrightnessSnapshot`, `theme::State` — and
is what the extractions below must preserve. The remaining work is deciding
where parsing, policy, and lifecycle live, not inventing a new data contract.

The lock path must continue to receive only explicitly safe capabilities. A
decomposition is incorrect if it makes qypr-lock able to reach launcher,
session-window, tray-menu, pairing, or shutdown operations merely because a
shared aggregate contains those pointers.

## Priority overview

| Priority | Module | Status | Main remaining problem | Next seam |
|---|---|---|---|---|
| P0 | StatusBar | Done | Extracted host, layout, input, tooltip controller, and popover manager | — (complete) |
| P0 | Quick settings | Done | Role lookup, tile factory, layout, input, and renderer extracted | — (complete) |
| P0 | StatusIndicator | Done | Capability bundles and bundle-based factories on both hosts | — (complete) |
| P1 | Theme (residual) | Done | Palette source/readers extracted from `Theme.cpp`; widget-level cascade test added | — (complete) |
| P1 | BarApp / App / Shell | Done | Runtime controllers extracted (`BarRuntime`, `LockRuntime`, `ConfigRuntime`, etc.) | — (complete) |
| P1 | LockScreen | Done | Authentication, reveal state, idle timers, power actions, layout, and drawing are separated behind lock-only seams | — (complete) |
| P1 | WifiBackend | Open | D-Bus chains, discovery, state reduction, and connect operations share one class | `NetworkManagerClient`, `WifiSnapshotReducer`, `WifiOperations` |
| P1 | BluetoothBackend | Open | BlueZ parsing, discovery, pairing, and operation state share one class | `BluezClient`, `BluetoothSnapshotReducer`, `BluetoothOperations` |
| P1 | NotificationMonitor | Open | Transport, parsing, privacy policy, storage, and backlog correlation share one class | `NotificationTransport`, `NotificationParser`, `NotificationStore` |
| P2 | SNI stack | Open | Protocol mode, item registry, icons, and UI behavior split across mismatched layers | `SniProtocol`, `SniItemStore`, `TrayView` |
| P2 | Wayland display stack | Open | Bar and lock hosts duplicate connection/registry concerns while owning surface policy | shared connection/registry layer, separate surface hosts |
| P2 | StateCache | Open | Serialization, persistence, debouncing, and backend sampling share one class | `StateCacheCodec`, `StateCacheStore`, coordinator |

## P0: decompose `StatusBar` (Done)

[`StatusBar.cpp`](../qypr/src/ui/statusbar/StatusBar.cpp#L42) is 995 lines (plus
a 235-line header) and is still both a view and the interaction/application
host for every indicator. It currently handles:

- indicator construction and zone bucketing in the constructor **and** in
  [`reloadModules()`](../qypr/src/ui/statusbar/StatusBar.cpp#L169), which also
  triplicates the tile-attach loop;
- module reloads and theme propagation (`setTheme`, `cascadeTheme`,
  `applyBackdropAlpha`);
- geometry and three-zone layout
  ([`layout()`](../qypr/src/ui/statusbar/StatusBar.cpp#L330), ~160 lines);
- drawing and animation scheduling;
- hover, click, scroll, keyboard, and text routing;
- popover anchoring (`anchorPopoverY`) and auto-dismiss;
- tooltips ([`drawTooltip()`](../qypr/src/ui/statusbar/StatusBar.cpp#L942));
- quick-settings activation (`activateIndicator`, `toggleQuickSettings`);
- focus traversal.

Two pieces of the original proposal already exist under different names:

- [`PopoverManager`](../qypr/src/ui/statusbar/PopoverManager.hpp#L9) (58+163
  lines) already owns popover lifetime, animation, input forwarding, and
  backdrop policy. The proposed `OverlayController` should be this class
  extended with anchoring and auto-dismiss, not a parallel component.
- [`IndicatorRegistry::createAll()`](../qypr/src/ui/statusbar/IndicatorRegistry.hpp#L37)
  is already the single construction entry; what is missing is a host object
  that owns bucketing, reload, and tile attachment once.

### Proposed components

```text
StatusBar
  IndicatorHost       owns indicators, zone bucketing, module reloads, tile attachment
  StatusBarLayout     measures and positions left/center/right zones
  StatusBarInput      hit-tests and routes pointer/keyboard events
  PopoverManager      (exists) popover selection, anchoring, dismissal
  TooltipController   owns hover dwell, fade animation, and tooltip geometry
```

`StatusBar` should retain only composition, invalidation, and the public host
contract. `StatusBarLayout` should accept indicator measurements and return
geometry without knowing how indicators are implemented.

### Completion criteria

- `IndicatorHost::load()` serves both the constructor and `reloadModules()`
  through one bucketing/attachment path.
- Existing behavior tests stay green without touching Wayland or D-Bus:
  `StatusBarConstructionAndDraw`, `StatusBarPointerInput`,
  `StatusBarFocusCycling`, `StatusBarGeometryTopAndBottom`,
  `PopoverGrowsAwayFromBarEdge`, `PopoverManagerLifecycle`.
- New layout and hit-test unit tests cover multiple widths and both bar edges.

## P0: decompose quick settings and tiles (Done)

What landed: `QSTile::Role` and role-based dedupe/replacement. What remains:
[`QuickSettingsPanel.cpp`](../qypr/src/ui/statusbar/QuickSettingsPanel.cpp#L79)
is 631 lines that still constructs tiles, reads environment data, binds backend
callbacks, applies lock/session policy (the null-backend fallback tiles),
lays out the panel (private `layoutTiles()`), paints, and handles every input
path. [`QSTile.cpp`](../qypr/src/ui/statusbar/QSTile.cpp) (602 lines) contains
repeated drawing and slider interaction logic for Wi-Fi, volume, brightness,
media, toggles, headers, and power across the eight tile types declared in
[`QSTile.hpp`](../qypr/src/ui/statusbar/QSTile.hpp#L16) (329 lines).

Residual title coupling to remove:
[`findTileBounds(title)`](../qypr/src/ui/statusbar/QuickSettingsPanel.hpp#L69)
still matches display strings, and its production caller is the preview path in
[`BarApp.cpp`](../qypr/src/core/BarApp.cpp#L260) asking for
`"Do Not Disturb"` (tests call it too). It should become a role-based lookup
(`boundsFor(QSTile::Role)`), with callers and tests updated.

### Proposed components

- `QSTileFactory`: creates the configured tile set from capability-specific
  services, including the lock-safe fallback tiles used when a backend is
  absent. That fallback is why `buildTiles()` works on the lock screen today;
  preserve it as a factory invariant.
- `QuickSettingsModel`: owns tile visibility, ordering, and session policy.
- `QuickSettingsLayout`: computes panel, header, grid, slider, and media
  bounds.
- `QuickSettingsInput`: performs hit testing and dispatches tile actions and
  drags.
- `TileRenderer`: shared rounded-card, icon, label, slider, and focus-ring
  primitives (the rounded-rect path already lives in `common/render/Painter`).
- Individual tile views for complex layouts such as media and Wi-Fi.

The panel should not remove indicator-created tiles by comparing titles (done),
and should not look tiles up by title (open). A tile should be created once,
identified by a stable role, and owned by either the bar module host or the
quick-settings model.

## P0: split `StatusIndicator` capabilities (Done)

What landed:
[`IndicatorCapabilities.hpp`](../qypr/src/ui/statusbar/IndicatorCapabilities.hpp#L25)
defines `ICompactView`, `ITileProvider`, `IDetailProvider`,
`IIndicatorLifecycle`, `IIndicatorInput`, and `IIndicatorPolicy`;
[`StatusIndicator`](../qypr/src/ui/statusbar/StatusIndicator.hpp#L88) composes
all six, and tests consume the narrow types instead of the whole applet.

What remains: the [`SystemBackends`](../qypr/src/ui/statusbar/StatusIndicator.hpp#L45)
pointer bag still hands every service to every indicator, so unsupported
behavior appears as harmless default no-ops, and
[`IndicatorRegistry::Factory`](../qypr/src/ui/statusbar/IndicatorRegistry.hpp#L19)
still takes the whole aggregate. The lock host builds a bag of mostly null
session pointers ([`App.hpp`](../qypr/src/core/App.hpp#L84)); the bar fills
nearly every field ([`BarApp.hpp`](../qypr/src/core/BarApp.hpp#L158)). The
registry factory should request the bundle it needs, and the lock host should
construct only safe bundles rather than the full unlocked-session aggregate
with most fields null.

### Proposed bundles

- `ConnectivityServices` for Wi-Fi and Bluetooth
- `MediaServices` for MPRIS and volume
- `SessionServices` for workspaces, toplevels, and keyboard layout
- `NotificationServices` for notification data and actions
- `SafeLockServices` for the explicitly permitted lock-screen actions

### Completion criteria

- A test proves unsafe capabilities are unavailable in the lock runtime, not
  merely invisible — extend the existing gating tests
  (`SensitiveIndicatorsGatedByDefault`, `SessionAppletsAbsentWithoutTheirBackends`,
  `LauncherGating`, `NotificationActionsAndInteractivity`).
- No indicator factory can reach launcher, session-window, tray-menu, pairing,
  or shutdown operations it did not explicitly request.

## P1: complete the theme extraction (residual) — Done

The original target — value model, injection, no globals — has landed
(`e57963f`). The residual palette seam also landed:

1. Path resolution and palette-file loading live in
   [`PaletteSource`](../qypr/src/ui/PaletteSource.hpp) (`resolveColorsPath`,
   `readPaletteFile`).
2. CSS/GTK/JSON decoding and token→theme mapping live in
   [`PaletteReader`](../qypr/src/ui/PaletteReader.hpp) (`parsePalette`,
   `applyPaletteTokens`, `applyColorsFile`), reusing
   [`MatugenTokens`](../common/render/MatugenTokens.hpp).
3. [`loadThemeState()`](../qypr/src/ui/Theme.cpp) is only defaults plus typed
   config overrides (plus the AutoPalette value API).
4. Widget-level coverage: `ThemePropagatesToStatusBarChildren` proves a
   host-owned `State` reaches indicators, the QS panel, and its tiles through
   the `setTheme` cascade; `PaletteReaderParsesMatugenFormats` covers the
   extracted readers without a full Config/State load.

Each new theme field belongs to `State` only. Do not reintroduce a
compatibility shim.

## P1: separate application composition from runtime policy (Done)

[`BarApp.hpp`](../qypr/src/core/BarApp.hpp#L49) (461+195 lines) owns nearly
every bar backend, two bus connections, theme state and palette watchers, the
state cache, the display, the status bar, and all input callbacks, plus private
`startBackends()`, `syncOverlay()`, `syncKeyboard()`, `reloadConfig()`, and
`refreshSolarTimes()`. [`App`](../qypr/src/core/App.hpp#L34) (277+101 lines) has
the same shape for the lock path, and [`Shell`](../qypr/src/ui/Shell.hpp#L27)
(181 lines) coordinates the two UI peers plus idle/DND policy.

The classes are valid composition roots, but too much operational policy is
embedded in them. Existing seams to build on:
[`ConfigWatcher`](../qypr/src/core/ConfigWatcher.hpp#L16) (inotify → callback),
`App::applyTheme()`, and the solar refresh logic that is duplicated between
`BarApp` and `App`.

### Proposed components

- `BarRuntime`: startup/shutdown sequence and event-loop ownership.
- `BarBackendLifecycle`: delayed backend startup and teardown.
- `ConfigRuntime`: config reload, module reload, geometry reload, and watcher
  coordination.
- `ThemeRuntime`: palette parsing, solar refresh, and theme publication
  (absorbing the duplicated `refreshSolarTimes()` and palette-watcher logic).
- `BarSurfaceController`: overlay height, keyboard interactivity, and display
  surface synchronization.
- `LockRuntime`: authentication, display lock, notification startup, and safe
  teardown.
- `ShellInputRouter`: explicit input priority between lock UI and bar UI
  (formalizing what `Shell` already routes).

`BarApp` and `App` should remain thin composition roots that assemble these
objects. This also makes preview and test modes less dependent on full daemon
startup.

## P1: decompose `LockScreen` — Done

Before step 9, [`LockScreen.cpp`](../qypr/src/ui/LockScreen.cpp) (576+134
lines) combined authentication callbacks, reveal/collapse state, hide timers,
pointer hit testing, password submission, power-menu expansion and
confirmation, layout, and complete Cairo rendering. Several widgets were
already extracted and injected: `Clock`, `PasswordField`, `StatusMessage`,
`NotificationView`, `ActionButton`, `ConfirmPopover`, and an optional
`AudioController`. The former pure geometry helpers
`powerRowRect()`, `powerButtonRect()`, and `powerAnchorRect()` showed that a
`LockLayout` extraction was feasible; they now live in that component.

### Proposed components

- `LockController`: state machine for locked, revealed, authenticating,
  authenticated, and error states.
- `LockInput`: maps keyboard and pointer events to controller commands.
- `LockLayout`: computes clock, password, status, and power-menu rectangles.
- `LockRenderer`: draws the lock surface from a state and layout snapshot.
- `PowerMenuController`: safe action selection and confirmation policy
  (extending what `ConfirmPopover` already does).

The controller must own authentication and lock policy; the renderer must not
be able to invoke `SystemActions` or unlock directly. The `SecureBuffer`
password discipline (QL-3 in the lock security review) must survive the split
unchanged.

What landed:

- [`LockController`](../qypr/src/ui/LockController.hpp) owns reveal state,
  hide/tick timers, PAM submission, auth-result handling, and the only unlock
  request path.
- [`LockInput`](../qypr/src/ui/LockInput.hpp) preserves modal priority and
  routes keyboard, pointer, notification, audio, and power events.
- [`LockLayout`](../qypr/src/ui/LockLayout.hpp) owns output-size and power-pill
  geometry; its tests run without Wayland or D-Bus.
- [`LockRenderer`](../qypr/src/ui/LockRenderer.hpp) owns lock widgets and
  Cairo composition, while [`PowerMenuController`](../qypr/src/ui/PowerMenuController.hpp)
  owns the explicit fixed-action allow-list and fail-closed confirmation.
- `LockScreen` remains the Shell-facing composition host, and all new sources
  stay in `QYPR_LOCK_ONLY_SOURCES`.

## P1: split D-Bus backends into protocol, model, and operations (Open)

### Wi-Fi

[`WifiBackend.hpp`](../qypr/src/system/WifiBackend.hpp#L51) and its 923-line
implementation combine NetworkManager D-Bus parsing, adapter discovery,
active-connection state, two serialized asynchronous fetch chains, network list
construction, saved-connection matching, scanning, connect/disconnect
operations, and snapshot publication. The chains already carry explicit state
(`fetchInFlight_`/`pendingRefresh_`, `netInFlight_`/`pendingNetRefresh_`,
`NetPhase`), so the "do not merge into a generic state machine unless phases
are explicit types" condition is already partly satisfied.

Suggested seams:

- `NetworkManagerClient`: object paths, method calls, signals, and reply
  decoding.
- `WifiSnapshotReducer`: pure state transitions from decoded events.
- `WifiNetworkEnumerator`: AP and saved-connection enumeration chain.
- `WifiOperations`: enable, scan, connect, forget, and disconnect commands.
- `WifiBackend`: lifecycle and publication facade.

Preserve the serialized-chain behavior and cover it with tests before moving
code; note that `seed()` currently couples cache seeding to the network-chain
staging (`netResults_`), which must move with the reducer.

### Bluetooth

[`BluetoothBackend.hpp`](../qypr/src/system/BluetoothBackend.hpp#L54) (644+207
lines) combines BlueZ object parsing, adapter discovery, signal subscription,
power control, device operations, discovery lifecycle, pairing state, and agent
ownership. [`BluetoothAgent`](../qypr/src/system/BluetoothAgent.hpp) (334+141
lines) is already extracted, and `setPairingAgentEnabled()` is already the
lock-safety switch (QL-1).

Suggested seams:

- `BluezClient`: object-manager and signal protocol adapter.
- `BluetoothSnapshotReducer`: pure adapter/device state model.
- `BluetoothOperations`: power, connect, disconnect, pair, trust, and forget.
- `DiscoveryController`: picker-driven scan lifetime.
- `PairingController`: agent enablement and prompt response policy.

Keep the lock-screen rule explicit: `PairingController` must be absent or
disabled in the lock runtime, not merely hidden by the UI, with a test to match.

### Notifications

[`NotificationMonitor.cpp`](../qypr/src/notifications/NotificationMonitor.cpp)
(474+89 lines) contains monitor connection setup, D-Bus message draining,
notification parsing, replacement/close correlation, privacy filtering
(sensitive-app list at :42-77, transient skip), backlog seeding, pending-call
tracking, and teardown. The send-side `NotificationActions` and the
`NotificationLog` recorder are already separate services.

Suggested seams:

- `NotificationTransport`: dedicated monitor connection and event-loop FD.
- `NotificationParser`: converts D-Bus messages into typed events.
- `NotificationPolicy`: sensitive-app loading and transient/privacy decisions.
- `NotificationStore`: replacement, close, ordering, and daemon-ID
  reconciliation.
- `NotificationMonitor`: coordinates transport and publishes store changes.

`NotificationLog` can remain a separate transport service, but its wire record
format should be shared with the parser/store types rather than reconstructed
inside the monitor.

## P2: separate SNI protocol from tray presentation (Open)

[`SNIBackend`](../qypr/src/system/SNIBackend.hpp#L48) (537+140 lines) supports
two modes in one class: external watcher host mode and watcher-plus-host mode.
Both modes are now implemented — `qypr/docs/SNI_WATCHER_PLAN.md` is a
historical design record — but the class still owns item discovery, bus
ownership tracking, icon pixmaps, item actions, and protocol vtables.

[`SNITrayHost`](../qypr/src/ui/indicators/SNITrayHost.hpp#L20) (407+69 lines)
then handles icon resolution, monochrome detection, overflow policy, hit
testing, and context-menu routing.

Suggested seams:

- `SniProtocol`: D-Bus vtables, watcher detection, and registration.
- `SniItemStore`: item ownership, snapshots, and change notifications.
- `SniActions`: activation, secondary activation, and scrolling.
- `TrayLayout`: active/passive grouping and overflow geometry.
- `TrayView`: icon rendering and input mapping.

The dual-mode behavior is legitimate, but it should be represented by two
protocol strategies behind one item store rather than by mode conditionals
throughout one backend.

## P2: share Wayland connection plumbing, preserve surface policy (Open)

`WaylandDisplay` and `BarDisplay` intentionally differ: one owns session-lock
surfaces and the other owns layer-shell surfaces. They should not be collapsed
into one policy-heavy display class.

They do, however, both own registry discovery, compositor/SHM binding, seat
creation, event-loop integration, invalidation, and output lifecycle. See
[`WaylandDisplay.hpp`](../qypr/src/wayland/WaylandDisplay.hpp#L26) (125 lines),
[`BarDisplay.hpp`](../qypr/src/wayland/BarDisplay.hpp#L30) (157 lines), with
`BarWindow` (111+234) and `Output` (83+137). [`ShmBuffer`](../common/wayland/ShmBuffer.hpp)
is already shared, as are `BackdropBlur` and `IconResolver` in
`common/render`.

Suggested shared pieces:

- `WaylandConnection`: `wl_display`, registry, dispatch, and flush.
- `WaylandGlobals`: compositor, SHM, seat, and optional protocol bindings.
- `OutputRegistry`: output discovery and removal.
- `InputSeat`: keyboard/pointer translation, with host-provided surface sizing.
- Separate `LockSurfaceHost` and `LayerSurfaceHost` for security and surface
  policy.

[`Seat`](../qypr/src/wayland/Seat.hpp#L26) (292+118 lines) itself currently
combines keyboard/xkb state, pointer state, cursor creation, surface-size
resolution, layout reporting, and key repeat. It is a candidate for a follow-up
split into `KeyboardInput`, `PointerInput`, and `KeyRepeat` after the
connection seam is stable.

## P2: split state-cache persistence (Open)

[`StateCache`](../qypr/src/system/StateCache.hpp#L42) (195+93 lines) loads an
INI file, deserializes five backend snapshots, seeds live backends, observes
changes, serializes current state, debounces writes, creates directories, and
performs atomic replacement (`serialize()` is already public for tests).

Suggested seams:

- `StateCacheCodec`: typed snapshot ↔ text conversion.
- `StateCacheStore`: path, directory, temporary-file, and atomic-rename logic.
- `StateCacheCoordinator`: event-loop debounce and backend sampling.

The current atomic write behavior should remain in `StateCacheStore`; it is a
correct reliability boundary and should not be duplicated by individual
backends.

## Modules not currently needing decomposition

Avoid extracting code merely to reduce line count in these areas:

- [`SystemBus`](../qypr/src/system/SystemBus.hpp#L21) (78+117 lines): it has a
  focused connection, event-loop, timeout, and teardown responsibility.
- [`Config`](../qypr/src/core/Config.hpp#L32) (71+186 lines): the hand-rolled
  parser is small and intentionally dependency-free. Improve diagnostics or
  tests before splitting it.
- Small individual indicator implementations: most are already bounded to one
  backend snapshot plus one compact/detail view.
- `NotificationView` (`Notification.cpp`, 290 lines): it is primarily a view
  and reconciliation object; the monitor, not the view, is the larger target.
- `BarWindow` (111+234 lines) and `Output` (83+137 lines): keep
  surface-specific Wayland policy local while extracting only shared
  connection/registry plumbing.

## Migration order

1. ~~Finish the `theme::State` migration and remove global synchronization~~ —
   done (`2e88b39`, `e57963f`).
2. ~~Introduce stable quick-settings tile roles~~ — done (`b0657f6`).
3. ~~Split `StatusIndicator` capability interfaces~~ — done (`fcc0dea`);
   the capability *bundles* remain and fold into step 7.
4. ~~Extract the palette source/readers out of `Theme.cpp`, reusing
   `MatugenTokens`, and add the widget-level theme-propagation test~~ — done
   (`PaletteSource`/`PaletteReader`; `ThemePropagatesToStatusBarChildren`,
   `PaletteReaderParsesMatugenFormats`).
5. ~~Extract `IndicatorHost`, `StatusBarLayout`, `StatusBarInput`, and
   `TooltipController`; extend `PopoverManager` with anchoring and dismissal~~ — done.
6. ~~Split the quick-settings factory/model/layout/input/renderer and replace
   `findTileBounds(title)` with a role-based lookup~~ — done.
7. ~~Replace `SystemBackends` with capability bundles and bundle-based factories
   on both hosts~~ — done.
8. ~~Move bar and lock startup/reload/theme logic into runtime controllers~~ — done.
9. ~~Split `LockScreen` state/input/layout/rendering while preserving lock policy~~ — done.
10. Decompose Wi-Fi, Bluetooth, and notification protocol adapters.
11. Separate SNI protocol state from tray presentation.
12. Extract shared Wayland connection primitives.
13. Isolate state-cache codec and persistence.

Each step should leave the executable boundary unchanged and should be
validated before the next one begins.

## Verification requirements

Every extraction should add or preserve tests at the lowest practical level:

- pure reducers and parsers: unit tests with malformed and partial input
- layout and hit testing: geometry tests at multiple scales and output sizes
- controllers: fake backend ports and explicit state-transition tests
- lock policy: tests proving unsafe capabilities are unavailable, not merely
  invisible
- rendering: existing qypr preview/golden tests
- integration: `ctest --test-dir qypr/build` (the `qypr-test` target, 137
  tests), `ctest --test-dir waylaunch/build`, `ctest --test-dir common/build`,
  `./scripts/check-invariants.sh` (I4, Q5, B1), and
  `./scripts/check-doc-paths.sh`.

Existing precedents for the "testable without the full runtime" criterion:
`IndicatorCapabilitySeam` and `QuickSettingsPanelRoleDedup` (narrow seams),
`ThemeMatugen*` and `ThemePaletteAutoResolve` (pure parsing),
`PopoverManagerLifecycle` (extracted component).

Do not use a successful build as evidence that a decomposition is complete.
The useful completion criterion is that the extracted component can be tested
without constructing the full Wayland, D-Bus, or lock-screen runtime.
