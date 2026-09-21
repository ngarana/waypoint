# waypoint — keyboard-first Wayland desktop suite

One repository, three parts, separate binaries: the bar you depart from and
return to, and the launcher that takes you places.

| Directory | Contents | Binaries |
|---|---|---|
| `qypr/` | bar, lock screen, notification recorder (`lockscreen` repo) | `qypr-bar`, `qypr-lock`, `qypr-record` |
| `waylaunch/` | spotlight launcher, Alt+Tab switcher, power overlay, dropdown host, content indexer | `waylaunch`, `waylaunchd`, `waylaunchctl` |
| `common/` | shared core: protocols, `core/` (EventLoop, Spawn, Types, DesktopIndex), `render/` (Painter, BackdropBlur, IconResolver), `wayland/` (ShmBuffer), `system/`, `toplevel/` seam | libraries only (plus its own unit tests) |

Runtime integration is by exec boundary, never by linking: the bar spawns
`waylaunch --power` / `waylaunch` / `waylaunch --switch`; the switcher stays
a separate process by design (a bar crash must never take Alt+Tab with it).

## Build

Each component configures and builds in its own directory (Ninja required —
Unix Makefiles race the protocol codegen rules that several targets share):

```sh
cmake -S qypr -B qypr/build -G Ninja && cmake --build qypr/build --parallel
cmake -S waylaunch -B waylaunch/build -G Ninja -DBUILD_TESTING=ON && cmake --build waylaunch/build --parallel
cmake -S common -B common/build -G Ninja && cmake --build common/build --parallel
```

## Test

```sh
./qypr/build/qypr-test            # 123 unit tests (incl. I1–I4 lock invariants)
ctest --test-dir waylaunch/build  # 27 suites
ctest --test-dir common/build     # shared units
./scripts/check-invariants.sh     # I4 + Q5 structural gates (see below)
```

## Structural gates

`scripts/check-invariants.sh` verifies the link lines CMake wrote:

- **I4** — `qypr-lock` links no waylaunch content (indexer, extractors, providers).
- **Q5** — the `waylaunch` binary links no qypr bar sources (Stage 3 rejection).

Shared `common/` objects are explicitly allowed on both sides.

## Conventions (Stage 4 decision)

Both styles live on, per directory — no mass rename, no `git blame` churn.
`qypr/` and `common/` follow qypr conventions (`namespace qypr`,
`PascalCase.hpp`, `camelCase()`); `waylaunch/` keeps its own
(`namespace waylaunch`, `snake_case.h`, `snake_case()`). New shared code
follows the qypr style.

## History

Assembled 2026-09-19 via `git subtree add` (full histories preserved — `git
log --follow` works across the grafts). The standalone `ngarana/waylaunch`,
`ngarana/lockscreen` and `ngarana/libwl-common` remotes stay for reference;
new work lands here. Decisions (I1 allow-list, one-repo, `waypoint` brand,
switcher decoupling) are recorded in `waylaunch/docs/INTEGRATION.md` §9.
