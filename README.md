# libwl-common — shared core for the waypoint desktop suite

Canonical sources consumed by the suite's components (`qypr` bar/lock,
`waylaunch` launcher/switcher) as a **git subtree**. One copy eliminates
the drift bug class (waylaunch once shipped a hand-reduced 151-line
layer-shell next to qypr's full 407-line upstream).

Consumers graft this repo (see waypoint root README for the monorepo
layout; standalone consumers use a `third-party/libwl-common` subtree
with its root on the include path). Build paths, codegen rules and loop
wiring stay in each consumer — only the *data and units* below are shared.

## Contents

| Path | Source of truth | Notes |
|---|---|---|
| `protocols/wlr-layer-shell-unstable-v1.xml` | upstream v5 | `get_layer_surface` has a `namespace` arg — a C++ keyword; consumers firewall it at codegen |
| `protocols/wlr-foreign-toplevel-management-unstable-v1.xml` | qypr's copy (newer: `finished` destructor type, `fullscreen since="2"`) | |
| `protocols/wlr-screencopy-unstable-v1.xml` | waylaunch's copy (backdrop blur) | unused by qypr (vendored for the union) |
| `protocols/wlr-gamma-control-unstable-v1.xml` | qypr's copy (night light) | unused by waylaunch (vendored for the union) |
| `core/EventLoop.hpp` / `.cpp` | qypr's epoll reactor | fd/timer/post multiplexing; `addFd`/`addTimer`/`post`/`addPrepare` |
| `core/Process.hpp` / `.cpp` | qypr's I3 spawn primitive | `posix_spawn`, absolute paths, `SIGDEF`, pidfd reaping; `env_add`/`devnull_stdio` options for hook commands |
| `core/Types.hpp` | qypr's UI value types | `Color`, `Rect`, easing, `Animated`; header-only |
| `render/Painter.hpp` / `.cpp` | qypr's cairo/pango helpers | `fillGlass` takes an explicit `solid` flag so the unit stays theme-free |
| `render/BackdropBlur.hpp` / `.cpp` | waylaunch's screencopy blur, ported | downsample + separable box blur; fixed a latent null-surface crash found by `blur_test` |
| `render/IconResolver.*` | merged superset | theme chain + spec metadata ordering, sized SVG rasterization, URIs/heuristics, bounded miss cache |
| `system/DesktopIndex.*` | qypr's index + waylaunch's `searchKey` | extra metadata fields, `setSearchPaths` override, `desktopPath` |
| `wayland/ShmBuffer.*` | qypr's memfd shm wrapper | busy/release tracking included |
| `toplevel/ToplevelBackend.hpp` | waylaunch's observer seam, verbatim | rename waits for the second implementer (bar-hosted switcher) |

Shared code follows qypr conventions (`namespace qypr`, `PascalCase`,
`camelBack`), including in waylaunch, which consumes it as-is.

## Build & test (this repo standalone)

Requires cairo, librsvg, and a C++20 compiler. Painter itself is built
and exercised in the consumers; the units testable on pixels or the
filesystem run here:

```sh
cmake -S . -B build && cmake --build build --parallel
ctest --test-dir build   # blur_test, icon_test (fake theme tree), desktop_test
```

## Consuming

```sh
# graft (monorepo: already present as common/):
git subtree add --prefix=third-party/libwl-common <remote> main --squash
# pull updates:
git subtree pull --prefix=third-party/libwl-common <remote> main --squash
```

`<remote>` is `https://github.com/ngarana/libwl-common`.

## Versioning

Bump protocol files only to a newer upstream revision, never with local
edits. After any change, rebuild + test **all** consumers before pushing
(the waypoint CI does this automatically).
