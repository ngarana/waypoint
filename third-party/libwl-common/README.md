# libwl-common — shared core for the qypr desktop suite

Canonical, vendored Wayland protocol XML consumed by both
[`waylaunch`](https://github.com/ngarana/waylaunch) and
[`qypr`](https://github.com/ngarana/lockscreen) (`lockscreen` repo) as a
**git subtree** at `protocols/` — Stage 2, item 1 of the waylaunch
`docs/INTEGRATION.md` consolidation plan. One copy eliminates the drift bug
class (waylaunch once shipped a hand-reduced 151-line layer-shell next to
qypr's full 407-line upstream).

## Contents

| Path | Source of truth | Notes |
|---|---|---|
| `protocols/wlr-layer-shell-unstable-v1.xml` | upstream v5 (identical in both repos) | `get_layer_surface` has a `namespace` arg — a C++ keyword; both repos firewall it at codegen (qypr `qypr_gen_protocol`, waylaunch `cmake/fix_scanner_keywords.cmake`) |
| `protocols/wlr-foreign-toplevel-management-unstable-v1.xml` | qypr's copy (newer: `finished` destructor type, `fullscreen since="2"`) | waylaunch's older copy had comment drift and no destructor type |
| `protocols/wlr-screencopy-unstable-v1.xml` | waylaunch's copy (backdrop blur) | unused by qypr (vendored for the union) |
| `protocols/wlr-gamma-control-unstable-v1.xml` | qypr's copy (night light) | unused by waylaunch (vendored for the union) |
| `core/EventLoop.hpp` / `core/EventLoop.cpp` | qypr's epoll reactor | Single-threaded fd/timer/post multiplexing. Shared code follows qypr conventions (`namespace qypr`, `PascalCase`, `camelBack`) per the integration plan — including in waylaunch, which consumes it as-is |

## Consuming (both repos already do this)

One subtree per repo at `third-party/libwl-common`, root on the include
path so `#include "core/EventLoop.hpp"` (and the `protocols/` dir for
scanner rules) resolve identically everywhere:

```sh
# first time:
git subtree add --prefix=third-party/libwl-common <libwl-common-remote> main --squash

# pull updates:
git subtree pull --prefix=third-party/libwl-common <libwl-common-remote> main --squash
```

`<libwl-common-remote>` is `https://github.com/ngarana/libwl-common`
(registered as the `libwl-common` git remote in both consumers).

Build paths are unchanged: each repo's CMake still globs its own
`protocols/` dir and runs its own `wayland-scanner` rules (including the
`namespace` firewall) — only the *data* is shared. Each repo generates
only the protocols it uses.

## Versioning

Bump protocol files only to a newer upstream revision, never with local
edits (except the two files' already-absorbed history). After any bump,
rebuild + test **both** consumers before pushing.
