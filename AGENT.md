# AGENT.md — instructions for AI coding agents working in waypoint (monorepo root)

Read this file before any code task. It encodes the monorepo layout,
quality gates, and hard rules. Violating it wastes a review cycle.
`waylaunch/AGENT.md` remains authoritative for waylaunch internals;
this file governs the repo as a whole and wins on conflicts of scope.

## 1. What this repo is

Keyboard-first Wayland desktop suite. One repo, three parts, separate
binaries, integrated only by exec boundary — never by linking.

| Directory | Contents | Binaries |
|---|---|---|
| `qypr/` | bar, lock screen, notification recorder | `qypr-bar`, `qypr-lock`, `qypr-record` |
| `waylaunch/` | launcher, Alt+Tab switcher, power overlay, dropdown host, content indexer | `waylaunch`, `waylaunchd`, `waylaunchctl` |
| `common/` | shared core: protocols, `core/`, `render/`, `wayland/`, `system/`, `toplevel/` seam | libraries only (plus its own unit tests) |

Rules:

1. The bar spawns `waylaunch --power` / `waylaunch` / `waylaunch --switch`.
   The switcher stays a separate process by design (a bar crash must never
   take Alt+Tab with it).
2. Shared `common/` objects are explicitly allowed on both sides of every
   link gate. Anything else crossing `qypr/` ↔ `waylaunch/` is a violation.
3. Style is per directory, no mass renames: `qypr/` + `common/` use
   `namespace qypr`, `PascalCase.hpp`, `camelCase()`; `waylaunch/` uses
   `namespace waylaunch`, `snake_case.h`, `snake_case()`. New shared code
   follows the qypr style.

## 2. Essential commands (Ninja required)

Each component configures and builds in its own directory. Unix Makefiles
race the shared protocol codegen rules — always `-G Ninja`.

```bash
cmake -S qypr -B qypr/build -G Ninja && cmake --build qypr/build --parallel
cmake -S waylaunch -B waylaunch/build -G Ninja -DBUILD_TESTING=ON && cmake --build waylaunch/build --parallel
cmake -S common -B common/build -G Ninja && cmake --build common/build --parallel

ctest --test-dir qypr/build --output-on-failure      # 135 unit tests (incl. I1–I4 lock invariants)
ctest --test-dir waylaunch/build --output-on-failure   # 26 suites
ctest --test-dir common/build --output-on-failure      # blur_test, icon_test, desktop_test
./scripts/check-invariants.sh                   # I4 + Q5 + B1 structural gates (needs built trees)
./scripts/check-invariants.sh qypr/build waylaunch/build   # explicit dirs
```

A configured `build/` directory must exist before any clang-tidy work — it
provides `compile_commands.json` (plus the wayland-scanner-generated
protocol headers configure alone does not emit).

## 3. Quality gates (non-negotiable)

CI (`.github/workflows/ci.yml`) builds all three components, runs all
three test suites, and enforces `./scripts/check-invariants.sh`.
Per-component lint configs are the authority for style even where CI does
not (yet) gate on them. Local must match CI exactly, plus lint clean.
Never declare work done with a red gate.

| Gate | Command |
|---|---|
| qypr format check | `cd qypr && ./scripts/lint.sh --format-only` |
| qypr tidy | `cd qypr && ./scripts/lint.sh --tidy-only` |
| qypr lint (both) | `cd qypr && ./scripts/lint.sh` |
| qypr lint fix | `cd qypr && ./scripts/lint.sh --fix` (safe fixits only; identifier-renames, ranges erase-remove, internal-linkage moves still only report — see `scripts/test-lint-fix.sh`) |
| waylaunch format check | `cmake --build waylaunch/build --target format-check` (equiv: `clang-format --dry-run --Werror` over `waylaunch/src|include|tests` `*.cpp|*.h`, excluding `*/build/*`) |
| waylaunch format fix | `cmake --build waylaunch/build --target format` or `clang-format -i <files>` (same file set) |
| waylaunch tidy, single file (fast) | `clang-tidy -p waylaunch/build <file> --warnings-as-errors='*'` |
| waylaunch tidy, full (slow, serial) | `run-clang-tidy -p waylaunch/build -j$(nproc) -warnings-as-errors='*'` |
| waylaunch pre-commit | `cd waylaunch && pre-commit run --all-files` |
| Structural I4 | `qypr-lock` links no `waylaunch/` content — enforced by `scripts/check-invariants.sh` |
| Structural Q5 | `waylaunch` links no `qypr/` bar sources — enforced by `scripts/check-invariants.sh` |
| Structural B1 | `qypr-bar` links no lock-only stack (mpv/PAM) — enforced by `scripts/check-invariants.sh` |
| Doc paths | no dangling source references in tracked docs — enforced by `scripts/check-doc-paths.sh` (intentional history in `scripts/doc-paths-allow.txt`) |
| Tests | all three suites above, green, with asserts live (see §3.1) |

Rules:

1. Run format + tidy on every file you touch, before finishing.
2. `clang-tidy` must pass with `--warnings-as-errors='*'` — the default
   invocation hides failures. Always append the flag when verifying.
3. If a check fires project-wide on idiomatic code, disable that check in
   the component's `.clang-tidy` with a comment-worthy reason — do not
   sprinkle `NOLINT` or rewrite the codebase around a pedantic check.
4. **Never run `clang-format` on `CMakeLists.txt` or any non-C/C++ file.**
   It mangles CMake syntax and breaks `cmake -S . -B build`. Lint/format
   hooks stay scoped to C/C++ under `src|include|tests` (+ `common/`
   `core|render|wayland|system|toplevel`) — do not widen them.
5. New assert-based suites must carry `-UNDEBUG` — no exceptions. A
   Release-family build defines `NDEBUG` and silently turns every `assert`
   into a no-op (this once hid a real `Subprocess::run` stdin bug behind a
   21/21 "green" run while CI failed). Distrust any green run you did not
   verify: check `CMAKE_BUILD_TYPE` in `build/CMakeCache.txt`.
6. New compositor coupling goes behind a seam interface
   (`IToplevelBackend`, `IPlacementBackend`, `toplevel/ToplevelBackend.hpp`)
   so the core stays testable without a compositor. New shared code lives
   in `common/` and is rebuilt + retested in **all** consumers before push.
7. Structural violations (I4/Q5/B1) are fixed by unlinking, never by
   allow-listing. `common/` is the only shared link surface.

### 3.1 Test-before-done checklist

- [ ] `cmake --build` clean for every touched component (`-Wall -Wextra`,
      plus `-Wpedantic` on several waylaunch targets) — warning-free.
- [ ] `ctest --test-dir qypr/build` / `ctest --test-dir waylaunch/build` /
      `ctest --test-dir common/build` green as applicable.
- [ ] `./scripts/check-invariants.sh` prints `INVARIANTS HOLD`.
- [ ] format + tidy clean on every touched file (§3 table).

## 4. Pre-commit (mandatory)

1. Hooks must be installed and must run on every commit:
   `pre-commit install` (hook lives at `.git/hooks/pre-commit`;
   waylaunch hooks are defined in `waylaunch/.pre-commit-config.yaml`,
   qypr staged checks via `qypr/scripts/lint.sh --staged`).
2. Verify with `pre-commit run --all-files` (from the component dir) or
   `qypr/scripts/lint.sh` for qypr-wide changes before finishing.
   The tidy hook is serial/slow — per-file runs are the norm during work,
   full runs before done.
3. If a hook fails, fix the finding and amend/retry. A hook failure is a
   finding, not an obstacle: e.g. a fresh tidy hit on a header means
   fixing the header.
4. Never commit with `--no-verify` unless the user explicitly approves,
   and say so in the final summary if you did.

## 5. Commit discipline — always commit after every fix or new feature

Every completed fix or new feature (including a feature slice, bug fix,
gate rollout, or doc update) MUST end in a commit on the current branch.
Do not stockpile unrelated changes in the working tree, and do not wait
for an explicit commit request — committing is the default, not an option.

1. Inspect before staging: `git status --short`, `git diff`, and
   `git log --oneline -10` for message style. Stage only intended files;
   never commit secrets, tokens, or local-only paths.
2. Message style is conventional commits, lowercase scope with component
   prefix: `fix(qypr): ...`, `feat(waylaunch): ...`, `fix(common): ...`,
   `chore(quality): ...`, `docs: ...`, `test: ...`, `config: ...`,
   `ci: ...`. One line subject, blank line, short body explaining why
   (not what — the diff shows what).
3. Let the pre-commit hooks run on `git commit` (§4). Do not bypass.
4. Branch naming: `feat/<topic>`, `fix/<topic>`.
5. Always commit, without asking, once §3.1 is green and hooks pass.
   Amending the just-created commit for an immediate hook-fix retry is
   part of the same unit of work. Pushing, opening PRs, or amending older
   commits still requires an explicit user request.

## 6. Privilege — pkexec for root, nothing else

Polkit via `pkexec` is the only sanctioned elevation path for agent and
developer commands. `sudo`, `su`, `doas`, `runas`, setuid helpers, and
`echo <password> | sudo -S` are forbidden in repo code, scripts, docs
examples, and agent shell invocations.

1. Shipped C++ never elevates: no `setuid`/`seteuid`, no `system("sudo…")`,
   no `popen("sudo…")`, no `posix_spawn` of a `sudo`/`pkexec` wrapper.
   Power actions stay unprivileged through logind: `systemctl
   reboot|poweroff|suspend|hibernate` and `loginctl lock-session`
   (`qypr/src/power/SystemActions.*`,
   `waylaunch/src/power/power_action_backend.cpp`). A missing binary is a
   startup diagnostic, never a reason to reach for elevation.
2. Agent shell commands that need root (install to `/usr`, system config)
   use `pkexec`, e.g. `pkexec cmake --install qypr/build`,
   `pkexec install -m644 …`. Never `sudo …`, never run the agent session
   itself as root, and never run builds or tests as root.
3. The `sudo apt-get …` lines in `.github/workflows/ci.yml` and the
   `sudo pacman …` lines in install docs are ephemeral-runner / human-admin
   exceptions. Do not copy them into scripts or agent commands.
4. New daemons/services must declare `needs no elevated privileges`
   (cf. `waylaunch/dist/waylaunchd.service.in` hardening) unless the user
   explicitly approves otherwise.

## 7. Gotchas ledger (append new entries here, don't re-learn them)

- `link_tokens` in `scripts/check-invariants.sh` needs a built tree: an
  unbuilt target prints `SKIP … (build <target> first)`, not `PASS`.
- `common/` protocol XMLs are vendored upstream — bump only to a newer
  upstream revision, never with local edits (except the `namespace` →
  `wl_namespace` codegen firewall, which lives in each consumer's build
  rules, not in the XML).
- Shell sessions reap background jobs: a daemon started with `&` under the
  tool session dies with its command. Detach smoke-test daemons with
  `setsid -f … >/dev/null 2>&1` and clean up afterwards.
- Never `pkill -f` with a pattern that appears in your own command line —
  it kills your shell session. Prefer compositor IPC for stray test windows.
- See `waylaunch/AGENT.md` §6 for the waylaunch-specific ledger
  (non-blocking fd drains, shutdown grace, Hyprland IPC quirks,
  `hyprctl repl` stdin, click-test button mapping).

## 8. Docs map

- `README.md` — monorepo layout, build, test, structural gates, conventions.
- `waylaunch/AGENT.md` — waylaunch commands, gates, conventions, ledger.
- `waylaunch/docs/INTEGRATION.md` §9 — recorded decisions (I1 allow-list,
  one-repo, `waypoint` brand, switcher decoupling).
- `common/README.md` — shared-core contents, consuming, versioning.
- `qypr/docs/LOCK_SECURITY_REVIEW.md` — lock threat model (why I4 exists).
