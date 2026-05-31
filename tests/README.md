# tests/ — regression scenarios

A place to put runnable, reproducible regression scenarios for behaviour
the project has consciously changed. The bar is intentionally low: when
a PR changes user-visible behaviour, drop a test here so the next person
can re-run the check without re-reading the PR.

Designed as the implementation of RFC [#97](https://github.com/xymon-monitoring/xymon/issues/97).

## Running the tests

From a fresh checkout, no build required:

    ./tests/testsuite

It discovers every executable `tests/**/*.sh`, runs each, and prints a
pass/skip/fail summary (exit `0` = pass, `77` = skip, anything else = fail).
Output adapts on its own: plain text on a terminal, GitHub Actions annotations
under CI — the workflow and a developer run the exact same runner.

Once the tree is configured, `make test` runs the same thing.

## What lives here, what doesn't

- **Here:** shell-level integration scenarios that exercise binaries,
  shipped config files, packaging artefacts, or documented invariants.
- **Not here:** pure-C unit tests. Those stay where they are
  (`xymonnet/test-evheap.c`, `lib/test-endianness.c`, etc.) and are
  built/run by the existing per-directory Makefiles.

## Directory layout

Tests are organised by **domain area**, not by source path. Source
paths shift over time (CMake migration is in flight); domains don't.
Cross-cutting scenarios that don't map to a single source dir (e.g.
shipped-file invariants) get their own area.

| Area              | What lives here                                        |
| ----------------- | ------------------------------------------------------ |
| `tests/client/`   | xymon client tools and behaviours                      |
| `tests/server/`   | xymond-side tools (xymongrep, xymoncgimsg, alert routing) |
| `tests/network/`  | xymonnet probes (xymonping, network checks)            |
| `tests/web/`      | CGIs, HTML rendering paths                             |
| `tests/packaging/`| cross-cutting: shipped files, paths, generated configs |
| `tests/buildsystem/` | parallel make, configure probes, CMake feature detection |
| `tests/integration/` | end-to-end scenarios spanning multiple components   |
| `tests/lib/`      | sourced helpers (`assert.sh`, future `net.sh` etc.)    |
| `tests/fixtures/` | shared data files (config snippets, expected outputs)  |

Add a new area by PR when an existing one doesn't fit. Don't bend a
test to fit the wrong area just to avoid creating a new directory.

### Runnable vs sourced/data files

Only the **test entry point** has the executable bit set. Helpers
under `lib/` are sourced; files under `fixtures/` are read. Neither
is `+x`. The CI discovery rule (`find tests -type f -name '*.sh'
-executable`) relies on this — there is no path-based exclude list to
maintain.

## Conventions

- **One file per scenario set.** Filename describes the area, not the
  PR: `tests/client/fs-filter.sh`, `tests/packaging/fhs-paths.sh`.
  Split when a file passes ~200 lines.
- **Executable. Bash. Strict mode.** First line:
  `#!/usr/bin/env bash`. Second line: `set -euo pipefail`.
  POSIX-sh compatibility is a non-goal.
- **Quiet on success, verbose on failure.** Don't print per-step
  progress on the happy path; CI logs are noisy enough. On failure
  the `fail` helper prints to stderr and exits, which is usually
  enough context.
- **Exit codes:**
  - `0` — pass
  - `77` — skip (matches the autotools / autopkgtest convention; CI
    treats this as "not run", not as a failure)
  - anything else — fail
- **Deterministic.** Tests must produce the same result every run, on
  any contributor's box and in CI. Flaky tests are removed, not
  retried. If a test needs to wait for something, wait for the
  condition, not for a wall-clock duration.
- **No persistent side effects.** Use `mktemp -d` for scratch space and
  register cleanup via `register_cleanup` (see `lib/assert.sh`).
- **No git assumption.** Tests must run against an extracted release
  tarball or a Debian source package, where `.git` is absent. Use
  `find_root` (script-location-based, not `git rev-parse`). When you
  need to isolate from the source tree before mutating files, copy the
  paths you actually need with `cp -r` into a `mktempdir`; do not use
  `git worktree`, `git stash`, or any other git invocation.
- **Path discovery via env var with default.** When a test needs a
  built binary, read it from an env var and default to the in-tree
  build path:
  ```bash
  XYMONPING="${XYMONPING:-./xymonnet/xymonping}"
  ```
  This keeps tests usable in CMake out-of-source builds (the build
  system passes the real path), in the in-tree Makefile build (default
  matches), and in autopkgtest (which exports installed paths).
- **License.** GPL-2.0+, matching the rest of the repo. A short
  SPDX-style header at the top of each test is sufficient:
  ```bash
  # SPDX-License-Identifier: GPL-2.0-or-later
  ```

## How to run

Standalone — what reviewers do:
```sh
./tests/client/fs-filter.sh
```

All of them at once — what CI does:
```sh
find tests -type f -executable -name '*.sh' -print0 \
  | xargs -0 -n1 -I{} bash -c '{}'
```

## How to add a regression scenario

1. Pick the area: `tests/<area>/<scenario>.sh`. Create the subdirectory
   if needed.
2. Copy the SPDX header and the strict-mode preamble from any existing
   test as a starting point.
3. Source the helpers: `. "$(dirname "$0")/../lib/assert.sh"`.
4. Drive the scenario: set up fixtures in a temp dir, invoke the
   binary or script under test, assert on its output / exit code /
   side effects.
5. Run it standalone. If it passes locally and is deterministic, open
   the PR. CI will run it on every push.

## Why no framework

Day-1 deliberate choice. Adopting bats/shunit2/pytest forces every
contributor (and every distro packager downstream) to learn that
framework before they can read a test. `set -euo pipefail` plus a tiny
assertion helper covers the surface we have today. If the directory
grows past ~30 tests and the lack of structure starts to bite, we
revisit.

## Downstream consumers

Debian's [autopkgtest](https://wiki.debian.org/autopkgtest) is an
intended consumer (see [#97](https://github.com/xymon-monitoring/xymon/issues/97)
discussion). Tests should be portable to a minimal Debian chroot:
declare any non-trivial host dependency at the top of the file with a
`skip` if it's missing, rather than failing.
