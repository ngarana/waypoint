# Architecture Review

Reviewed: 2026-09-21

This repository combines the `qypr`, `waylaunch`, and `common` projects. The
merge has preserved useful executable boundaries, but several parallel
abstractions and stale project remnants remain. This document records the
current review findings and suggested refactoring order.

This is an architectural review, not a recommendation to merge the
applications into one binary. Separate `qypr` and `waylaunch` executables are
reasonable; the main goal is to ensure that shared responsibilities have one
clear implementation and contract.

## Summary

The most significant maintenance risks are:

1. Two competing build and test definitions for `waylaunch`.
2. Fragile qypr source selection using a recursive glob and denylist.
3. Duplicate process, toplevel, desktop-entry, rendering, and theme models.
4. Split ownership of status-bar and quick-settings tiles.
5. Large interfaces and coordinator classes with too many responsibilities.
6. Documentation and test-registration drift after the merge.

## Findings

### 1. Fragile qypr target source boundaries

**Severity:** High  
**Principles:** KISS, DRY, separation of concerns

[`qypr/CMakeLists.txt`](../qypr/CMakeLists.txt#L131) gathers almost every
source file with `GLOB_RECURSE`, then removes a manually maintained list of
lock-only files before constructing `qypr-bar`.

This means a newly added lock-only source can silently be linked into the bar
if the exclusion list is not updated. The source boundary is therefore
implicit and easy to violate.

**Recommendation:** Use explicit source lists for `qypr-lock` and `qypr-bar`,
or organize target-specific sources into target-specific directories. Add a
structural check that prevents lock-only dependencies such as PAM or video
support from entering the bar target.

### 2. Stale parallel waylaunch test/build graph

**Severity:** High  
**Principles:** KISS, DRY, reliability

The active tests are defined in the top-level
[`waylaunch/CMakeLists.txt`](../waylaunch/CMakeLists.txt#L259), while
[`waylaunch/tests/CMakeLists.txt`](../waylaunch/tests/CMakeLists.txt#L1)
contains an older, independent test project. The older project references the
removed `src/search/search_manager.cpp` and defines different targets and
dependencies.

This creates two incompatible build truths: the normal build passes while a
developer trying to build the nested test project hits the stale reference
at build time (configuring the nested project succeeds; nothing references
the subproject, so the normal build is unaffected).

**Recommendation:** Remove the obsolete test CMake file, or turn it into a
thin wrapper around the active test configuration. Keep one authoritative test
graph.

### 3. Competing process abstractions

**Severity:** High  
**Principles:** DRY, DIP, consistency

The shared [`qypr::Process`](../common/core/Process.hpp#L1) API and
[`waylaunch::Subprocess`](../waylaunch/include/waylaunch/subprocess.h#L18)
both provide process spawning, reaping, detached execution, and PATH
resolution. `waylaunch` additionally contains its own capture loop, double-fork
implementation, tracked-child implementation, and fallback behavior.

The implementations have different signal, reaping, and failure semantics.
`Subprocess::spawn_reaped` can also fall back to a legacy implementation when
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

### 4. Duplicate toplevel-management models

**Severity:** High  
**Principles:** DRY, domain-model consistency

qypr has its own toplevel model and backend in
[`qypr/src/system/ToplevelBackend.hpp`](../qypr/src/system/ToplevelBackend.hpp#L31),
while waylaunch uses a separate model and interface in
[`common/toplevel/ToplevelBackend.hpp`](../common/toplevel/ToplevelBackend.hpp#L1)
plus its own WLR listener/cache.

The two models represent the same Wayland protocol concepts but use different
field names, handle types, state flags, and observer contracts. Bug fixes and
behavioral changes can therefore diverge.

**Recommendation:** Share a low-level foreign-toplevel client and normalized
window state model. Keep qypr and waylaunch-specific adapters for activation,
presentation, and policy.

### 5. Split ownership of quick-settings tiles

**Severity:** High  
**Principles:** DRY, SRP, KISS

[`StatusBar.cpp`](../qypr/src/ui/statusbar/StatusBar.cpp#L53) creates tiles in
repeated left, center, and right loops. Later,
[`QuickSettingsPanel.cpp`](../qypr/src/ui/statusbar/QuickSettingsPanel.cpp#L86)
removes and recreates some of those tiles by comparing display strings such as
`"Bluetooth"`, `"Brightness"`, and `"Keep awake"`.

This makes user-visible labels function as internal identifiers. A label
change can cause duplicate tiles, failed removal, or removal of the wrong
tile. Ownership is also split between indicators and the quick-settings
panel.

**Recommendation:** Define stable tile identifiers or roles and assign one
owner/factory to each tile. Use explicit replacement or composition rather
than title-based deduplication.

### 6. Duplicate desktop-entry models

**Severity:** High  
**Principles:** DRY, avoiding unnecessary translation layers

[`common/system/DesktopIndex.hpp`](../common/system/DesktopIndex.hpp#L16)
defines `qypr::DesktopEntry`. Waylaunch defines another `DesktopEntry` in
[`app_launcher.h`](../waylaunch/include/waylaunch/app_launcher.h#L9) and
copies fields across in
[`app_launcher.cpp`](../waylaunch/src/modes/app_launcher.cpp#L36).

The conversion is mostly mechanical and already risks drift: fields such as
`no_display` and `hidden` are not populated from the common model.

**Recommendation:** Use the common model directly where possible. If a
waylaunch-specific model is required, make the conversion a single explicit
provider-boundary adapter and test every field mapping.

### 7. Overly broad `StatusIndicator` interface

**Severity:** Medium  
**Principles:** ISP, SRP

[`StatusIndicator.hpp`](../qypr/src/ui/statusbar/StatusIndicator.hpp#L87)
combines compact rendering, quick-settings tiles, detail popovers, polling,
backend updates, animation, mouse input, scroll input, lock behavior, and
sensitivity handling. Most indicators need only a subset and inherit default
no-op methods for the rest.

**Recommendation:** Split the interface into small capabilities such as
`CompactIndicator`, `TileProvider`, `DetailView`, `BackendObserver`, and
`InputHandler`. Alternatively, compose these capabilities instead of using a
single large base class.

### 8. `LauncherUI` has too many responsibilities

**Severity:** Medium  
**Principles:** SRP, DIP

[`launcher_ui.h`](../waylaunch/include/waylaunch/launcher_ui.h#L57) and its
implementation coordinate Wayland, search, providers, worker threads,
rendering, input, themes, switcher mode, power mode, and process lifecycle.

The class is simultaneously the application composition root, event reactor,
search controller, view coordinator, and mode manager. This makes lifecycle
and threading behavior difficult to test independently.

**Recommendation:** Extract a `SearchController`, an overlay/event-loop host,
and complete the existing switcher and power controllers. Keep `LauncherUI`
focused on composition and high-level view coordination.

### 9. Rendering and value types are duplicated

**Severity:** Medium  
**Principles:** DRY, consistency

`common` provides `Color`, `Rect`, animation, and `Painter` through
[`common/core/Types.hpp`](../common/core/Types.hpp#L25) and
[`common/render/Painter.hpp`](../common/render/Painter.hpp#L31). Waylaunch
defines another color/theme model and reimplements Cairo/Pango helpers and
rounded-rectangle drawing in its renderer.

Some waylaunch rendering is intentionally specialized, but neutral primitives
should not have multiple implementations.

**Recommendation:** Move stable Cairo/Pango primitives and neutral geometry
operations into common. Keep high-level mode-specific rendering local.

### 10. Theme and palette state can drift

**Severity:** Medium  
**Principles:** DRY, DIP, testability

qypr has mutable theme globals and its own matugen handling in
[`qypr/src/ui/Theme.hpp`](../qypr/src/ui/Theme.hpp#L26). Waylaunch has an
independent theme/configuration model in
[`waylaunch/include/waylaunch/config.h`](../waylaunch/include/waylaunch/config.h#L13)
and separate matugen parsing.

The configuration formats do not need to be merged, but shared semantic
palette values currently have multiple sources of truth. Mutable global theme
state also makes tests and multiple application instances order-dependent.

**Recommendation:** Define a shared semantic palette schema or generated
token file. Inject a `ThemeState` value into widgets rather than relying on
mutable global state.

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

### 12. CMake configuration is repetitive

**Severity:** Medium  
**Principles:** DRY

Both build systems repeat include paths, compiler flags, link libraries, and
test setup. The waylaunch test definitions in
[`waylaunch/CMakeLists.txt`](../waylaunch/CMakeLists.txt#L275) are especially
repetitive.

**Recommendation:** Introduce interface targets and small CMake helper
functions for shared compile and link properties while preserving explicit
target source lists.

## Documentation and test drift

The merge left several stale references:

- The root README describes shared `Spawn` code although the implementation is
  now `Process`.
- qypr documentation still describes files that moved into `common`.
- waylaunch's nested `tests/CMakeLists.txt` references the removed
  `search_manager.cpp`. (`docs/DESIGN.md` mentions the file only in
  historical bug-table rows marked Fixed — intentional history, not drift.)
- The root README reports 122 qypr tests, while the current test binary reports
  124 tests (the +2 are the recent solar-palette tests; the README count is
  updated alongside that feature).
- qypr tests are run manually and are not registered with CTest.

These should be corrected after the build graph is simplified. A lightweight
documentation check that verifies referenced source paths would prevent this
kind of drift.

## Verification snapshot

The review was performed against the current repository tree.

- `waylaunch`: 26 tests passed.
- `common`: 3 tests passed.
- Repository executable-boundary invariants passed.
- qypr built successfully, but its manual test binary reported 123 passed and
  1 failed in a bus-less environment. With a system bus available the same
  binary reports 124/124; pointing the bus at nothing reproduces the
  123-plus-1 signature exactly, so the failure is environment-dependent, not
  a code regression.
- The qypr failure was
  `WifiPopoverToggleSwitchAndScanning`. The backend returns early when the
  system bus is unavailable, while the test expects an optimistic state
  update. See [`WifiBackend.cpp`](../qypr/src/system/WifiBackend.cpp#L531) and
  [`test_indicators.cpp`](../qypr/tests/test_indicators.cpp#L221).

## Recommended implementation order

1. Remove or repair the stale waylaunch test/build graph.
2. Replace qypr's glob-plus-denylist source selection with explicit lists.
3. Define one canonical process API.
4. Fix quick-settings tile ownership and stable identifiers.
5. Consolidate desktop-entry and toplevel models.
6. Split `StatusIndicator` and `LauncherUI` responsibilities.
7. Centralize shared palette tokens and low-level rendering primitives.
8. Update README and design documentation after the new boundaries are stable.

