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

Once the tree is configured, `make test` runs the same thing. A single
test also runs standalone — what reviewers do:

    ./tests/client/fs-filter-linux.sh

`bash` is a hard prerequisite of the suite (every test uses it; see
Conventions). The runner itself is POSIX sh, and on a host without bash it
skips the whole suite with exit `77` rather than reporting interpreter
failures as test failures.

### `XYMON_VARIANT` — scoping tests to a build

Some tests exercise a component absent from certain builds (the server
configure, RRD, xymonnet, the web CGIs). They gate on `XYMON_VARIANT` via
`need_variant` (lib/assert.sh):

| `XYMON_VARIANT`          | Behaviour                                          |
| ------------------------ | -------------------------------------------------- |
| unset / empty            | no restriction — every test runs (developer run, release tarball, build-free `tests.yml` lane) |
| `server` / `client` / `localclient` | scoped tests run only in matching lanes; others skip with `77` |
| anything else            | `fail` — an unknown value is a CI-matrix typo, not a reason to skip silently |

The build legs of `build.yml` export the variant they just built; the
build-free `tests.yml` lane leaves it unset. Leave it unset for a normal
developer run.

> **"No build required" is not "build-independent."** Running `./tests/testsuite`
> in a tree you have already compiled will exercise those built binaries via
> `require_bin` rather than skipping them. To reproduce the truly
> build-independent lane, run from a clean checkout or a fresh
> `git worktree add --detach`, with `XYMON_VARIANT` unset.

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
-perm -u+x`) relies on this — the exec bit, not the path, decides what
runs. The runner additionally excludes `lib/` and `fixtures/` as a
backstop for checkouts where every file reads as executable (FAT/NTFS
mounts, `core.fileMode=false` trees), so a sourced helper is never
mistaken for a test there; new test directories need no exclude-list
maintenance.

## Conventions

- **One file per scenario set.** Filename describes the area, not the
  PR: `tests/client/fs-filter-linux.sh`, `tests/packaging/fhs-paths.sh`.
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
- **Skip only for a missing environment, never for missing project
  code.** A test ships in the same tree as the behaviour it guards, so
  the *feature being absent* is a regression to fail on, not a reason to
  skip. Reserve `skip` for things the host can't provide: an absent host
  tool (`awk`, `df`), an OS the test doesn't apply to, a binary that
  wasn't built, or source files genuinely not present in this checkout.
  "The wiring this test guards is gone" must `fail`. The one exception is
  a test deliberately staged ahead of an unmerged feature — call that out
  in the test and remove the staging skip the moment the feature lands.
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
  built binary or an installed artefact, read it from an env var and
  default to the in-tree path -- `require_bin` (lib/assert.sh) does this
  for binaries:
  ```bash
  require_bin XYMONGREP common/xymongrep          # binaries
  SCRIPT="${XYMONCLIENT_LINUX:-$ROOT/client/xymonclient-linux.sh}"  # scripts
  ```
  This keeps tests usable in CMake out-of-source builds (the build
  system passes the real path), in the in-tree Makefile build (default
  matches), and in autopkgtest (the control file exports installed
  paths). The override contract assumes test and artefact come from the
  **same version**: pointing a newer test at an older installed artefact
  will fail on features that artefact predates -- that is the
  skip-only-for-environment policy above working as designed, not a bug.
  (In Debian CI the contract holds automatically: tests and debs are
  built from the same source package.) The override is also an
  **existence assertion**: a test may `skip` when the in-tree default is
  absent (binary not built in this configuration), but an explicitly
  exported path that points at nothing must `fail` -- a broken build or
  package layout is precisely what the exporting caller (CMake,
  autopkgtest) runs the suite to catch, and skipping would green-light
  it. `require_bin` implements both halves; installed-script tests
  guard `$XYMONCLIENT_LINUX` the same way.
- **License.** GPL-2.0+, matching the rest of the repo. A short
  SPDX-style header at the top of each test is sufficient:
  ```bash
  # SPDX-License-Identifier: GPL-2.0-or-later
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
discussion). autopkgtest runs test commands from the root of the
**unpacked, unbuilt source package** (read-only -- another reason for
the copy-into-`mktempdir` convention) against the **installed binary
packages**. The suite maps onto that as a single test entry, roughly:

```
Test-Command: XYMONGREP=/usr/lib/xymon/client/bin/xymongrep \
              XYMONCLIENT_LINUX=/usr/lib/xymon/client/bin/xymonclient-linux.sh \
              ./tests/testsuite
Depends: xymon, xymon-client, gcc, make
Restrictions: skippable
```

`Restrictions: skippable` maps the runner's all-skip exit `77` to SKIP
instead of FAIL; `gcc`/`make` in `Depends` keep the compile-probe tests
alive (autopkgtest does not provide build-dependencies by default). The
env overrides point binary-driving tests (`require_bin`) and
installed-artefact tests at the package's files -- those are the tests
that give Debian's library-transition CI something that can break at
runtime. Source-reading tests run against the unpacked (patched) source
tree; for the installed-package use case they are *supplementary*
signal, which is why new tests that can drive a built binary should.

Tests should be portable to a minimal Debian chroot: declare any
non-trivial host dependency at the top of the file with a `skip` if
it's missing, rather than failing.
