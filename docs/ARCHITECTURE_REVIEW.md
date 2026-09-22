# Architecture Review

Reviewed: 2026-09-21; status re-checked 2026-09-22 against the code baseline at
`main` `9954082`.

This repository combines the `qypr`, `waylaunch`, and `common` projects. The
merge has preserved useful executable boundaries, but at review time several
parallel abstractions and stale project remnants remained. This document
records the review findings and their resolution status.

This is an architectural review, not a recommendation to merge the
applications into one binary. Separate `qypr` and `waylaunch` executables are
reasonable; the main goal is to ensure that shared responsibilities have one
clear implementation and contract.

Findings are recorded as written on the review date. Each one carries a
**Status** line from the re-check; qypr-specific follow-up sequencing lives in
[`QYPR_DECOMPOSITION_PLAN.md`](QYPR_DECOMPOSITION_PLAN.md).

Status legend: **Resolved** — the risk is closed in code and covered by a gate
or test; **Partial** — the main extraction landed, a named piece remains;
**Open** — not started.

## Summary

Most of the merge risks identified in this review have since been resolved:

- qypr now uses explicit per-target source lists, with the B1 structural gate;
- waylaunch has one test graph again;
- both projects share one process contract, one toplevel state decoder, one
  desktop-entry model, shared rendering primitives, and a shared matugen token
  lookup;
- quick-settings tiles have stable role identities instead of display-string
  identifiers;
- qypr's theme is an injected value (no mutable globals) and waylaunch's
  `[theme]` ownership moved into `ThemeManager`;
- the documentation and test drift listed at the end is fixed, and the
  doc-path gate now prevents it from returning.

What remains is concentrated in a few places:

1. Large coordinator classes and broad interfaces: `StatusIndicator` capability
   bundles and `StatusBar`/`BarApp`/`App`/`Shell` seams in qypr (tracked in the
   decomposition plan); `LauncherUI`'s inline search pipeline in waylaunch.
2. Wayland infrastructure is still only partially shared (connection,
   registry, seat/input, and surface lifecycle code).
3. CMake still repeats shared-source and test configuration; there is no
   interface target for the `common/` sources.

## Findings

### 1. Fragile qypr target source boundaries

**Severity:** High  
**Principles:** KISS, DRY, separation of concerns

[`qypr/CMakeLists.txt`](../qypr/CMakeLists.txt#L131) used to gather almost
every source file with `GLOB_RECURSE`, then remove a manually maintained list
of lock-only files before constructing `qypr-bar`. A newly added lock-only
source could silently be linked into the bar if the exclusion list was not
updated: the source boundary was implicit and easy to violate.

**Recommendation:** Use explicit source lists for `qypr-lock` and `qypr-bar`,
or organize target-specific sources into target-specific directories. Add a
structural check that prevents lock-only dependencies such as PAM or video
support from entering the bar target.

**Status:** Resolved (`a25653c`). The build now uses explicit
`QYPR_SHARED_SOURCES`, `QYPR_RECORDER_SOURCES`, and `QYPR_LOCK_ONLY_SOURCES`
sets, and `scripts/check-invariants.sh` adds the B1 gate that fails when
`qypr-bar` links lock-only objects (libmpv/libpam).

### 2. Stale parallel waylaunch test/build graph

**Severity:** High  
**Principles:** KISS, DRY, reliability

The active tests are defined in the top-level
[`waylaunch/CMakeLists.txt`](../waylaunch/CMakeLists.txt#L272), while
`waylaunch/tests/CMakeLists.txt` used to contain an older, independent test
project that referenced the removed `src/search/search_manager.cpp` and
defined different targets and dependencies. That created two incompatible
build truths.

**Recommendation:** Remove the obsolete test CMake file, or turn it into a
thin wrapper around the active test configuration. Keep one authoritative test
graph.

**Status:** Resolved (`60c60a9`). The nested CMake file is gone;
`waylaunch/tests/` now holds sources only, registered by the top-level test
block. `ctest --test-dir waylaunch/build` runs 28 suites.

### 3. Competing process abstractions

**Severity:** High  
**Principles:** DRY, DIP, consistency

The shared [`qypr::Process`](../common/core/Process.hpp#L1) API and
[`waylaunch::Subprocess`](../waylaunch/include/waylaunch/subprocess.h#L24)
both provided process spawning, reaping, detached execution, and PATH
resolution. `waylaunch` additionally contained its own capture loop,
double-fork implementation, tracked-child implementation, and fallback
behavior.

The implementations had different signal, reaping, and failure semantics.
`Subprocess::spawn_reaped` could also fall back to a legacy implementation when
the shared event loop is absent, which hides an important lifecycle choice.

**Recommendation:** Establish one shared process contract with explicit
policies for:

- output capture
- detached execution
- tracked children
- sandboxing and resource limits
- event-loop integration

The specialized sandbox supervisor may remain local to waylaunch, but ordinary
process execution should use one implementation.

**Status:** Resolved (`a3443ba`). `Subprocess` is now a thin facade over the
shared `qypr::runCapture` / `commandExists` / `spawnDaemon` / `spawnReaped`
implementation ([`subprocess.cpp`](../waylaunch/src/search/subprocess.cpp));
the silent null-loop fallback is gone — `spawn_reaped` needs a loop by
signature, and the loop-less policy is the separate, documented `launch()`.
`spawn_tracked` remains the single documented supervisor-local exception
(vfork-clone avoidance for the dropdown session supervisor).

### 4. Duplicate toplevel-management models

**Severity:** High  
**Principles:** DRY, domain-model consistency

qypr has its own toplevel backend in
[`qypr/src/system/ToplevelBackend.hpp`](../qypr/src/system/ToplevelBackend.hpp#L35),
while waylaunch uses a separate interface in
[`common/toplevel/ToplevelBackend.hpp`](../common/toplevel/ToplevelBackend.hpp#L1)
plus its own WLR listener/cache.

The two models represent the same Wayland protocol concepts but used different
field names, handle types, state flags, and observer contracts. Bug fixes and
behavioral changes could therefore diverge.

**Recommendation:** Share a low-level foreign-toplevel client and normalized
window state model. Keep qypr and waylaunch-specific adapters for activation,
presentation, and policy.

**Status:** Resolved (`f179bfc`). Both clients decode the protocol state array
through the shared `qypr::ToplevelStates` / `decodeToplevelStates()`
([`ToplevelStates.hpp`](../common/toplevel/ToplevelStates.hpp#L22), tested once
in [`toplevel_state_test.cpp`](../common/tests/toplevel_state_test.cpp)) and
map the decoded state onto their own snapshot/observer model at the boundary.
A shared low-level client was not extracted; the per-client adapters are the
boundary this recommendation explicitly allows.

### 5. Split ownership of quick-settings tiles

**Severity:** High  
**Principles:** DRY, SRP, KISS

[`StatusBar.cpp`](../qypr/src/ui/statusbar/StatusBar.cpp#L42) creates tiles in
repeated left, center, and right loops. Later,
[`QuickSettingsPanel.cpp`](../qypr/src/ui/statusbar/QuickSettingsPanel.cpp#L86)
removed and recreated some of those tiles by comparing display strings such as
`"Bluetooth"`, `"Brightness"`, and `"Keep awake"`, making user-visible labels
function as internal identifiers. Ownership was also split between indicators
and the quick-settings panel.

**Recommendation:** Define stable tile identifiers or roles and assign one
owner/factory to each tile. Use explicit replacement or composition rather
than title-based deduplication.

**Status:** Resolved for tile identity (`b0657f6`). Tiles carry a
`QSTile::Role` ([`QSTile.hpp`](../qypr/src/ui/statusbar/QSTile.hpp#L31)) and
the panel dedupes/replaces by role. Two residual pieces are tracked in the
decomposition plan: `findTileBounds()` still looks tiles up by display title
(the production caller is the preview path,
[`BarApp.cpp`](../qypr/src/core/BarApp.cpp#L260)), and a single factory/owner
for the configured tile set (`QSTileFactory`/`QuickSettingsModel`) does not
exist yet.

### 6. Duplicate desktop-entry models

**Severity:** High  
**Principles:** DRY, avoiding unnecessary translation layers

[`common/system/DesktopIndex.hpp`](../common/system/DesktopIndex.hpp)
defines `qypr::DesktopEntry`. Waylaunch used to define another `DesktopEntry`
in `app_launcher.h` and copy fields across in `app_launcher.cpp`. The
conversion was mostly mechanical and already risked drift: fields such as
`no_display` and `hidden` were not populated from the common model.

**Recommendation:** Use the common model directly where possible. If a
waylaunch-specific model is required, make the conversion a single explicit
provider-boundary adapter and test every field mapping.

**Status:** Resolved (`f179bfc`). `AppLauncher::search()` returns
`const qypr::DesktopEntry*` directly
([`app_launcher.h`](../waylaunch/include/waylaunch/app_launcher.h#L21)); the
local model and the field-copying conversion are gone.

### 7. Overly broad `StatusIndicator` interface

**Severity:** Medium  
**Principles:** ISP, SRP

[`StatusIndicator.hpp`](../qypr/src/ui/statusbar/StatusIndicator.hpp#L88)
combines compact rendering, quick-settings tiles, detail popovers, polling,
backend updates, animation, mouse input, scroll input, lock behavior, and
sensitivity handling. Most indicators need only a subset and inherit default
no-op methods for the rest.

**Recommendation:** Split the interface into small capabilities such as
`CompactIndicator`, `TileProvider`, `DetailView`, `BackendObserver`, and
`InputHandler`. Alternatively, compose these capabilities instead of using a
single large base class.

**Status:** Partial (`fcc0dea`). The compose-the-capabilities alternative
landed: [`IndicatorCapabilities.hpp`](../qypr/src/ui/statusbar/IndicatorCapabilities.hpp#L25)
defines `ICompactView`, `ITileProvider`, `IDetailProvider`,
`IIndicatorLifecycle`, `IIndicatorInput`, and `IIndicatorPolicy`, and
`StatusIndicator` composes all six. What remains is ownership of the services
themselves: the `SystemBackends` aggregate and the registry factory signature
are unchanged, so every factory can still reach every capability. Bundle
extraction is tracked in the decomposition plan.

### 8. `LauncherUI` has too many responsibilities

**Severity:** Medium  
**Principles:** SRP, DIP

[`launcher_ui.h`](../waylaunch/include/waylaunch/launcher_ui.h#L58) and its
implementation coordinate Wayland, search, providers, worker threads,
rendering, input, themes, switcher mode, power mode, and process lifecycle.

The class is simultaneously the application composition root, event reactor,
search controller, view coordinator, and mode manager. This makes lifecycle
and threading behavior difficult to test independently.

**Recommendation:** Extract a `SearchController`, an overlay/event-loop host,
and complete the existing switcher and power controllers. Keep `LauncherUI`
focused on composition and high-level view coordination.

**Status:** Partial (`fcc0dea`). `ThemeManager` was extracted with its own
test ([`theme_manager.h`](../waylaunch/include/waylaunch/theme_manager.h#L24));
`WaylandCore`, the switcher controllers, and the power controllers already
exist as separate classes, and `run()` is driven by the shared `EventLoop`.
`LauncherUI` still owns the inline search pipeline (`update_search`,
`rebuild_*`, the file-search worker thread) and rendering; its own comment
notes the provider migration is incremental, so `SearchController` and the
overlay-host extraction remain open.

### 9. Rendering and value types are duplicated

**Severity:** Medium  
**Principles:** DRY, consistency

`common` provides `Color`, `Rect`, animation, and `Painter` through
[`common/core/Types.hpp`](../common/core/Types.hpp#L25) and
[`common/render/Painter.hpp`](../common/render/Painter.hpp#L47). Waylaunch
defines another color/theme model and used to reimplement Cairo/Pango helpers
and rounded-rectangle drawing in its renderer.

Some waylaunch rendering is intentionally specialized, but neutral primitives
should not have multiple implementations.

**Recommendation:** Move stable Cairo/Pango primitives and neutral geometry
operations into common. Keep high-level mode-specific rendering local.

**Status:** Partial (`b3793ce`). The rounded-rect path is shared:
`Renderer::round_rect_path` forwards to `qypr::roundedRectPath`
([`renderer.cpp`](../waylaunch/src/ui/renderer.cpp#L190)), and
`common/render/Painter` builds on the same primitive. Waylaunch still carries
its own `Color` value type ([`renderer.h`](../waylaunch/include/waylaunch/renderer.h#L14))
for its config-facing theme model; that split is within this review's accepted
scope (the `[theme]` formats stay per-app), and the duplicated geometry it
objected to is gone.

### 10. Theme and palette state can drift

**Severity:** Medium  
**Principles:** DRY, DIP, testability

qypr had mutable theme globals and its own matugen handling in
[`qypr/src/ui/Theme.hpp`](../qypr/src/ui/Theme.hpp#L24). Waylaunch has an
independent theme/configuration model in
[`waylaunch/include/waylaunch/config.h`](../waylaunch/include/waylaunch/config.h#L13)
and separate matugen parsing.

The configuration formats do not need to be merged, but shared semantic
palette values had multiple sources of truth. Mutable global theme state also
made tests and multiple application instances order-dependent.

**Recommendation:** Define a shared semantic palette schema or generated
token file. Inject a `ThemeState` value into widgets rather than relying on
mutable global state.

**Status:** Resolved in scope (`2e88b39`, `e57963f`, `b3793ce`, `fcc0dea`,
palette extraction). qypr's `theme::State` is injected through
`ThemeAware::setTheme()/theme()`; the globals, `toGlobals()`, and
`loadTheme()` are deleted, and tests construct states directly. Waylaunch owns
`[theme]` through `ThemeManager`, and both projects resolve semantic colours
through the shared `pickMatugenToken`
([`MatugenTokens.hpp`](../common/render/MatugenTokens.hpp#L19)). The two
configuration formats remain separate, as this review intended. Palette-file
I/O and format decoding now live in
[`PaletteSource.hpp`](../qypr/src/ui/PaletteSource.hpp) /
[`PaletteReader.hpp`](../qypr/src/ui/PaletteReader.hpp); `loadThemeState()`
owns only defaults plus typed overrides.

### 11. Wayland infrastructure is only partially shared

**Severity:** Medium  
**Principles:** DRY, appropriate abstraction boundaries

Both projects use shared SHM support, but layer-shell, seat, input, output,
registry, and protocol lifecycle code remains duplicated. This is partly
justified because qypr supports bars and lock/session-lock behavior while
waylaunch manages overlays and mode-specific surfaces.

**Recommendation:** Share small, stable primitives such as connection,
registry, SHM, and protocol-adapter utilities. Do not force application
policy or surface lifecycle into one large cross-application abstraction.

**Status:** Open. [`ShmBuffer`](../common/wayland/ShmBuffer.hpp) is shared,
but qypr's `WaylandDisplay`/`BarDisplay`/`Seat` and waylaunch's own connection
code still duplicate registry, seat/input, and output lifecycle. The
extraction (shared connection primitives, per-host surface policy) is step 12
of the decomposition plan.

### 12. CMake configuration is repetitive

**Severity:** Medium  
**Principles:** DRY

Both build systems repeat include paths, compiler flags, link libraries, and
test setup. The waylaunch test definitions in
[`waylaunch/CMakeLists.txt`](../waylaunch/CMakeLists.txt#L272) are especially
repetitive.

**Recommendation:** Introduce interface targets and small CMake helper
functions for shared compile and link properties while preserving explicit
target source lists.

**Status:** Partial. qypr gained explicit per-target source sets and a
`qypr_gen_protocol` helper ([`qypr/CMakeLists.txt`](../qypr/CMakeLists.txt#L60));
waylaunch groups its four content tests through a `foreach`. There is still no
interface target for the `common/` sources — each consumer lists them
explicitly (`WL_COMMON_SOURCES` in qypr, `${LIBWL_COMMON}` source paths in
waylaunch) — and the remaining waylaunch suites are defined one by one.

## Documentation and test drift

All of the drift recorded at review time is now fixed, and a gate keeps it
from returning:

- The root README describes `Process` (not `Spawn`) and reports the current
  suite counts (135 qypr tests, 28 waylaunch suites, 8 shared suites).
- qypr documentation no longer references files that moved into `common`.
- The removed `waylaunch/tests/CMakeLists.txt` and its `search_manager.cpp`
  reference are gone; `docs/DESIGN.md` keeps the file only in historical
  bug-table rows.
- qypr's suite is registered with CTest
  ([`qypr/CMakeLists.txt`](../qypr/CMakeLists.txt#L369)) and runs in CI.
- The recommended lightweight path check exists:
  [`check-doc-paths.sh`](../scripts/check-doc-paths.sh) fails CI on dangling
  source references in tracked markdown, with intentional history listed in
  [`doc-paths-allow.txt`](../scripts/doc-paths-allow.txt).

## Verification snapshot

Status re-checked 2026-09-22 against the same tree (code baseline `9954082`),
with session and system buses available:

- `ctest --test-dir qypr/build`: `qypr-test` passed — the single registered
  target wraps 135 test cases (5.6 s).
- `ctest --test-dir waylaunch/build`: 28/28 suites passed.
- `ctest --test-dir common/build`: 8/8 suites passed (blur, desktop, icon,
  matugen tokens, painter, process, solar, toplevel state).
- Structural gates: `./scripts/check-invariants.sh qypr/build waylaunch/build`
  prints `PASS I4`, `PASS Q5`, `PASS B1`; `./scripts/check-doc-paths.sh`
  holds.
- The review-time bus-less observation for
  [`WifiPopoverToggleSwitchAndScanning`](../qypr/tests/test_indicators.cpp#L221)
  remains correctly diagnosed as environment-dependent, not a code regression:
  [`WifiBackend::setEnabled()`](../qypr/src/system/WifiBackend.cpp#L531)
  returns early when the system bus is unavailable, so the optimistic-update
  path the test asserts needs a bus. The full suite passes with buses present.

## Recommended implementation order

1. ~~Remove or repair the stale waylaunch test/build graph.~~ Done (`60c60a9`).
2. ~~Replace qypr's glob-plus-denylist source selection with explicit lists.~~
   Done (`a25653c`), with the B1 gate.
3. ~~Define one canonical process API.~~ Done (`a3443ba`).
4. ~~Fix quick-settings tile ownership and stable identifiers.~~ Done
   (`b0657f6`); consolidating tile construction under one factory is step 6 of
   the decomposition plan.
5. ~~Consolidate desktop-entry and toplevel models.~~ Done (`f179bfc`).
6. Split `StatusIndicator` and `LauncherUI` responsibilities. Partial:
   capability interfaces (`fcc0dea`) and `ThemeManager` landed; the capability
   bundles and `LauncherUI`'s search/overlay split remain.
7. ~~Centralize shared palette tokens and low-level rendering primitives.~~
   Done (`b3793ce`).
8. ~~Update README and design documentation after the new boundaries are
   stable.~~ Done (`f662b89`); the doc-path gate now enforces it.
9. Remaining, in rough order: `LauncherUI`'s search/overlay extraction; shared
   Wayland connection primitives; a `common/` interface target to end the
   repeated source lists. qypr-specific sequencing is in the decomposition
   plan.
