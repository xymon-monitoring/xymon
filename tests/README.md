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

### `XYMON_VARIANT` — telling the suite which build this is

Most tests only execute against a build, so run one first. The suite then needs
to know *which* build, because the same program lives at different paths
depending on the variant:

    ./configure --server && make -j$(nproc)
    XYMON_VARIANT=server ./tests/testsuite

| variant | configured with | `xymongrep` | `xymond_client` |
| ------- | --------------- | ----------- | --------------- |
| `server` | `./configure --server` | `common/xymongrep` | `xymond/xymond_client` |
| `localclient` | `CONFTYPE=client ./configure --client` | `client/xymongrep` | `client/xymond_client` |
| `client` | `./configure --client` | `client/xymongrep` | not built |

The full table is `variant_products()` in `lib/assert.sh`; it also covers
`xymond_rrd` and `svcstatus.cgi`, both server-only.

A configured tree vouches for the caller's label: `configure.client` writes
`CLIENTONLY` and `LOCALCLIENT` into the toplevel `Makefile`, and their
reachable combinations are exactly the three variants. On a disagreement the
runner refuses to run — the variable describes a build, it does not select
one, and a label that contradicts the tree is a mistake rather than a request.
This is not theoretical: `CONFTYPE=client` is the *localclient* build, and a
caller declaring `client` for it would drop
`tests/analysis/analysis-file-ifexist.sh` from the only build that produces
what it drives. Without a label, the tree answers for itself.

The variable still answers where the tree cannot: an unconfigured tree, and a
CMake build, write no such `Makefile`.

Leaving it unset is the default and stays supported: each test falls back to its
own in-tree path, which is what a developer run, a release tarball and the
build-free `tests.yml` lane all do.

## What lives here, what doesn't

- **Here:** shell-level integration scenarios that exercise binaries,
  shipped config files, packaging artefacts, or documented invariants.
- **Not here:** pure-C unit tests. Those stay where they are
  (`xymonnet/test-evheap.c`, `lib/test-endianness.c`, etc.) and are
  built/run by the existing per-directory Makefiles.

## Directory layout

**A test's folder is decided by which builds contain the thing it drives.**
The folder name labels a set of variants, not a subject.

1. What does the test execute or compile — a binary, a source file, a shipped
   config, or nothing (a pure scan)?
2. Which variants produce that thing?
3. File it in the area below whose row lists exactly those variants.

A test needing several things takes the most restrictive — it needs all of
them. Among areas covering the same variants the choice is readability, not
correctness, so settle the variant set first and the name second.

Not by topic: a test *about* server behaviour that only compiles `lib/` code
runs in every build, and filing it under `server` would skip it in the client
legs. Not by source path either — those shift (the CMake migration is in
flight) and one test often spans several.

| Area              | What lives here                                        |
| ----------------- | ------------------------------------------------------ |
| `tests/common/`   | tools every variant ships (xymon, xymoncmd, xymongrep, xymoncfg, xymondigest) |
| `tests/client/`   | xymon client tools and behaviours                      |
| `tests/analysis/`  | the local data analyser (`xymond_client`, wherever the variant builds it) |
| `tests/server/`   | xymond-side tools (xymoncgimsg, alert routing, config parsing) |
| `tests/xymonnet/` | xymonnet probes (xymonping, network checks)            |
| `tests/libxymon/` | harnesses compiling only `lib/` sources                |
| `tests/web/`      | CGIs, HTML rendering paths                             |
| `tests/packaging/`| cross-cutting: shipped files, paths, generated configs |
| `tests/buildsystem/` | parallel make, configure probes, CMake feature detection |
| `tests/integration/` | end-to-end scenarios spanning multiple components   |
| `tests/lib/`      | sourced helpers (`assert.sh`, future `net.sh` etc.)    |
| `tests/fixtures/` | shared data files (config snippets, expected outputs)  |

Worked through: `xymongrep-filter.sh` reads as server-side work, but the binary
it drives is one every variant builds -- `common/xymongrep` in a server tree,
`client/xymongrep` in a client one -- so it goes under `common/`, and a client
build gets the coverage it exists for. `analysis-file-ifexist.sh` drives
`xymond_client`, which only a localclient or server build produces, so it goes
under `localclient/`.

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
- **One success line, through `pass`.** End with `pass "<what held>"`
  — a claim, not a label: `pass "namematch() compares the plain name
  list case-insensitively"`, not `pass "namematch test"`. The runner
  prints the path and counts the verdict; only the test can say what
  it verified. A compiled harness reports through its exit status and
  prints no success line of its own; keep its failure output, which is
  what makes a red run readable.
- **Say so when only half of it ran.** A test that could not verify
  everything it covers — no sanitizer for the half that needs one,
  nothing built to drive — ends with `pass_partial "<what held>"
  "<what could not be checked>"`. It still exits 0, so the runner
  cannot tell it from a full pass by exit status; the summary counts
  the two apart, and a run that checked half of what it claims stops
  reading as a complete one.
- **A skip inside an area the build provides is a regression.** With
  `XYMON_TESTS_STRICT=1` and `XYMON_VARIANT` set — which is what CI
  does after installing the dependencies and building — the runner
  fails on any test that skipped in an area this variant produces:
  the build has that subject, so the test had no business standing
  down. Outside those areas the filter has already removed the test.
  Strict also refuses what it cannot hold to that floor: a test in
  an area `area_in_variant` has never heard of, a test sitting
  directly under `tests/`, or an executable `.sh` under `tests/lib/`
  or `tests/fixtures/` (invisible to discovery) each fail the run as
  a filing error. A developer box declares nothing and is never held
  to any of this.
- **Rejected: classifying skips by cause.** A `skip_env` marker ("a
  host condition, so the floor ignores it") classifies by cause, and
  cause does not decide whether coverage was lost: a missing tool is
  a host condition *and*, where CI installs that tool so the test can
  run, a real regression. Likewise rejected: exempting `require_bin`
  callers from the area filter — two answers to "should this run
  here" is one too many.
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
- **Test another OS's code here; keep its primitives behind a named
  helper.** A test may extract a block from any client and run it under
  stubs, whatever OS the lane happens to be -- that is how one lane
  covers all five clients, and why `tests/client/fs-filter-*.sh` are not
  `uname`-guarded. The price is paid on both sides. The extracted region
  must be POSIX shell with portable tool flags (no bashism, no GNU-only
  flag, no `/proc`), and whatever is genuinely per-OS lives in a named
  helper -- `fs_mounts`, `fs_filesystems`, `fs_procname` -- which the
  test replaces *by name* with `fsf_stub_helper`, never by rewriting the
  path the helper reads. `tests/buildsystem/test-suite-portability.sh`
  enforces both halves.
  That leaves the helpers' own bodies covered by nothing, since every
  stubbed test throws them away. Close that with a **native test**: one
  that exercises the real primitive, guarded by `uname` and skipping
  where it doesn't apply (`tests/client/fs-procname.sh`,
  `tests/client/fs-mounts.sh`). These are the only tests allowed to
  guard on `uname`, and they should stay few -- one per primitive, not
  one per behaviour that happens to use it.
- **A fixture that has to be a real binary is built, not copied.** Use
  `fsf_wedge_binary` (tests/client/fs-filter-common.sh): it compiles one, falls
  back to copying, and checks the result still runs before handing it back.
  Copying a system binary looks portable and is not -- BusyBox picks its applet
  from `argv[0]`, so `cp $(command -v sleep) df` gives you `df`; macOS kills a
  copy of a signed system binary outright. Both were found the expensive way,
  on lanes, and neither shows on a developer's Linux box.
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
  default to the in-tree path -- `require_bin` and `require_cfg`
  (lib/assert.sh) do this for binaries and config files:
  ```bash
  require_bin XYMONGREP common/xymongrep          # binaries
  require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg  # config files
  SCRIPT="${XYMONCLIENT_LINUX:-$ROOT/client/xymonclient-linux.sh}"  # scripts
  ```
  `require_bin` resolves in three steps, first match wins: an explicitly
  exported `$VAR`; then, if `XYMON_VARIANT` is set and the table knows this
  role, the path that variant builds it at; then the caller's default. A role
  the table names but this variant's row omits is a `skip` -- that build
  genuinely does not produce it. A role the table does not mention at all
  falls through to the default, so an undeclared product degrades to a probe
  rather than a false claim.
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
  it. `require_bin` and `require_cfg` implement both halves;
  installed-script tests guard `$XYMONCLIENT_LINUX` the same way.
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
              XYMONSERVER_CFG=/etc/xymon/xymonserver.cfg \
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
