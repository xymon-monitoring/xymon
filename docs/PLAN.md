# PLAN

## Run the regression suite in every build leg (#166, PR #167)

`build.yml` gates the test step on `matrix.variant == 'server'`, so the two
client legs compile their binaries and run nothing against them. Ungating it
is one line; what makes it work is the two questions that follow.

**Which binary does this test drive?** `require_bin` hardcodes one path per
test, so a test looking for `xymond/xymond_client` skips in a localclient
build that produces the same program as `client/xymond_client`.

**Should this test run here at all?** Once every leg runs the suite, a client
leg reaches the server web-CGI tests, compiles them, and writes
`lib/libxymon*.a` into its own tree as a side effect — the suite stops being
idempotent.

### Items

1. **One table: what each variant builds, and where** — `lib/assert.sh`.
   A row per variant, naming each build product a test may ask for and the
   path that variant puts it at:

       server       XYMONGREP=common/xymongrep  XYMOND_CLIENT=xymond/xymond_client  ...
       localclient  XYMONGREP=client/xymongrep  XYMOND_CLIENT=client/xymond_client
       client       XYMONGREP=client/xymongrep

   `require_bin XYMONGREP` looks the role up in this variant's row instead of
   taking a hardcoded path, so one test finds its program under whichever
   variant is built. Today `analysis-file-ifexist.sh` looks for
   `xymond/xymond_client` and skips in a localclient build that produces the
   same program as `client/xymond_client`.

   **Library archives need no table at all** -- which took building all three
   variants to establish, after two guesses that did not survive contact.

   | | server | client | localclient |
   | --- | --- | --- | --- |
   | `libxymon.a` `libxymoncomm.a` | yes | no | no |
   | `libxymonclient.a` `libxymonclientcomm.a` `libxymontime.a` | yes | yes | yes |

   The seven lib-only tests link `lib/libxymoncomm.a`, which is server-only --
   so they cannot build in a client tree today. But the three archives in the
   second row are produced by *every* variant, and within a build the shared
   objects are byte-identical across the archives carrying them:

       digest.o   libxymoncomm.a = libxymonclient.a = libxymon.a  (85a8da60)
       tree.o     libxymoncomm.a = libxymonclient.a = libxymon.a  (70b858f0)
       stackio.o  libxymoncomm.a = libxymonclient.a = libxymon.a  (af434956)
       misc.o     libxymoncomm.a = libxymonclient.a = libxymon.a  (a597fe28)
       loadhosts.o  libxymoncomm.a = libxymonclientcomm.a         (d57ef805)

   The archives differ in membership, not in the objects they share. A harness
   linking `libxymonclientcomm.a libxymonclient.a` resolves everywhere:
   byte-identical to today on a server build, and the client-compiled build of
   the same sources in the client legs -- which is what those builds ship, so
   testing it there is right rather than a compromise.

   **Order matters**, contrary to what this item said before it was built:
   `libxymonclientcomm.a` references `errprintf`, `debug`, `md5hash` and the
   strbuffer helpers from `libxymonclient.a`, so comm comes first. The reverse
   order fails to link with about twenty undefined symbols -- the same reason
   `history-reportlog-reject.sh` already repeats `libxymon.a` on either side of
   `libxymoncomm.a`.

   Four harnesses link an archive, not the seven named below:
   `xmh-item-names`, `notbefore-notafter`, `config-include-dir` and
   `netrc-password`. `digest-md5hash`, `xtree-destroy`, `xtree-iterate-deleted`
   and `sbuf-define-c90` compile the sources directly and need no change.

   Measured on two fresh trees, `./configure --server` and `./configure
   --client`, each built and then run:

   | build | before | after |
   | ----- | ------ | ----- |
   | server | 73 pass / 0 skip / 0 fail | 73 / 0 / 0 |
   | client | 47 pass / 26 skip / 0 fail | **49 / 24 / 0** |

   The two gained are `xmh-item-names` and `notbefore-notafter`, which used to
   skip on a missing `libxymoncomm.a`.

   That is a one-line change per harness, not a second table. Two earlier
   drafts of this item proposed one: first assuming `libxymoncomm.a` was
   universal (false), then a per-variant archive list (unnecessary). Both were
   written before building a client tree. Per-test link checks still belong in
   the work -- a test needing an object outside those two archives stays
   server-only -- but the mechanism does not.

   Shell only — no `awk`, `sed` or other optional host tool. Working out where
   a binary lives must not need more of the host than running the test does.

2. **What "missing" means, decided by the table** — `lib/assert.sh`.
   The lookup answers a question a bare path cannot:

   | declared variant | role in its row | file on disk | result |
   | ---------------- | --------------- | ------------ | ------ |
   | yes | yes | yes | run against it |
   | yes | yes | **no** | **fail** — the variant is defined to build it |
   | yes | no  | –   | skip — this variant never builds it |
   | no  | –   | –   | probe the known paths, skip if none (developer run) |

   The second row is the point. Without it every absence is a skip, so a
   build that stopped producing `xymond_rrd` reports green. An unrecognised
   variant fails rather than skipping every test quietly.

   Deliberately one flat table, not a capability graph. An earlier draft
   described variants as nesting (`server` provides `client`, and so on) and
   cross-checked a role's capability against the variants providing it —
   two tables kept in agreement by a runtime check. The flat form states the
   same thing directly, and there is no second table to disagree with.

3. **File each test under the builds that ship its subject** — moves only.
   `xymongrep-filter.sh` and `loadhosts-canonical-charset.sh` drive a binary
   every variant ships, `analysis-file-ifexist.sh` one that only a localclient
   or server build has. Filed by source path they would be filtered out of the
   legs they exist to cover; filed by what they need, the filter in item 4 is
   correct without any exemption.

4. **Skip tests whose subject this build does not contain** — `tests/testsuite`.
   One table, area → the variants containing it:

   | area | contained in |
   | ---- | ------------ |
   | `buildsystem`, `packaging`, `libxymon`, `common`, `client` | every variant |
   | `localclient` | server, localclient |
   | `server`, `web`, `xymond`, `rrd`, `xymonnet` | server |

   Two edge cases, both already found the hard way: a test file directly under
   `tests/` has no area at all (do not treat its filename as one), and an area
   the table has never heard of cannot be judged (run it, print a `NOTE:`, do
   not abort the run).

   **How a test's folder is decided.** The table is only as good as the filing,
   and `tests/README.md` currently says tests are organised by "domain area" —
   a topic. A topic cannot answer "does this build contain it?", which is how
   `tests/server/` came to hold seven tests that touch only `lib/`. The rule
   the filter needs is:

   > A test's folder is decided by which builds contain the thing it drives.
   > The folder name labels a set of variants, not a subject.

   1. What does the test execute or compile — a binary, a source file, a
      shipped config, or nothing (a pure scan)?
   2. Which variants produce that thing?
   3. File it in the area whose row lists exactly those variants.

   Two tie-breakers. A test needing several things takes the **intersection**,
   the most restrictive, because it needs all of them. Among areas with the
   same variant set the choice is readability, not correctness — the filter
   cannot tell `packaging` from `client` — so settle the variant set first and
   the name second. That ordering is what the old rule inverted.

   **Filing corrections this step must make first**, found by checking what
   each test actually compiles:

   - seven tests move out of `tests/server/` into a new `libxymon` area —
     `digest-md5hash`, `xtree-destroy`, `xtree-iterate-deleted`,
     `xmh-item-names`, `notbefore-notafter`, `config-include-dir`,
     `sbuf-define-c90`. Each compiles against `lib/` objects only, and every
     variant compiles those objects -- under different archive names, which is
     what item 1 resolves. Filed under `server` the client legs skip seven
     tests they could run.
   - `netrc-password.sh` moves from `network/` to `libxymon` for the same
     reason: it drives `lib/url.c`.
   - `tests/network/` becomes `tests/xymonnet/`. With netrc gone, all four
     remaining tests drive `xymonnet/*`, and the area table's axis is what a
     build produces — `xymonnet` is a binary a server build makes and a client
     build does not. "network" and "probe" both name an activity instead, and
     clients do network probing too.

   `libxymon` rather than `lib`, because `tests/lib/` holds the sourced
   helpers; freeing that path would mean editing the source line in all 53
   tests. `libxymon` matches the archives the tests link (`libxymon.a`,
   `libxymoncomm.a`) and costs no churn.

   **This step depends on the harness link change in item 1, and cannot land
   before it.** The assumption it was originally written on -- that a client build
   produces `lib/libxymoncomm.a` -- was checked against a real client build and
   is false: that archive is server-only. Moving the eight tests into an area
   marked "every variant" without item 1 would send them into the client legs
   to skip on a missing archive, which is the green skip this whole feature
   exists to remove, relocated rather than fixed.

   With the harnesses linking the universal archive pair, the area is correct:
   every variant produces them, so every leg can run the tests. Sequence is
   1 -> 2 -> 4, and a client-leg run is what proves it.

5. **Run it in every leg** — `.github/workflows/build.yml`.
   Drop the `variant == 'server'` gate, export `XYMON_VARIANT` for the variant
   just built, and map the runner's all-skip `77` to a hard failure: with the
   full source built, "nothing verified" can only mean broken discovery.

6. **Write the contract down once** — `tests/README.md`.
   One statement of what `XYMON_VARIANT` does. Not three copies across the
   README, the runner and the workflow, which is how the previous attempt
   ended up with a workflow comment asserting the opposite of the code.

7. **Document the variable where people look for how to run the suite** —
   `tests/README.md`, the running section.
   `./tests/testsuite` and `make test` already work from a plain checkout, and
   stay the default: no variant declared, nothing filtered. But everything
   items 1-5 add is inert unless `XYMON_VARIANT` is set, and nothing in the
   tree sets it except the three CI legs — so in practice the only place it is
   ever exercised is a pushed branch. One line next to `make test` showing

       XYMON_VARIANT=server ./tests/testsuite

   is enough to make it reachable before pushing. (Meaningful against a built
   tree: on an unbuilt one the promised binaries are legitimately absent and
   every one of them reports a failure, which is correct but not useful.)

### Verification

Every item must hold in all four modes — `server`, `client`, `localclient`,
and unset (the developer run and the build-free `tests.yml` lane) — with the
counts recorded before and after. Plus, per item: an unknown variant, a
top-level `tests/*.sh`, no `awk` on `PATH`, and a promised binary removed.

Both builds are cheap to make locally and the client one is the check that
matters, since it is the leg every assumption here has been wrong about:

    ./configure --server && make -j$(nproc) && ./tests/testsuite
    ./configure --client && make -j$(nproc) && ./tests/testsuite

### Decided against

- **A coverage floor** (fail when a test in a provided area skips). Across
  three CI legs it never evaluated a single skip — every skip in every leg is
  a filtered one, which it cannot see — while it turned a busy runner's
  timeout, a sandbox without loopback UDP and a compiler without ASan into
  red legs blaming lost coverage. Item 2 guards the case it was reaching for,
  and says which binary is missing.
- **`XYMON_TESTS_STRICT`.** Had no consumer other than the floor.
- **`skip_env`** (marking a skip as a host condition so the floor ignores it).
  Classifies by cause, and cause does not decide whether coverage was lost: a
  missing tool is a host condition *and*, where CI installs that tool so the
  test can run, a real regression.
- **Exempting `require_bin` callers from the area filter.** Item 3 removes the
  need. Two answers to "should this run here" is one too many.
- **Uniform result reporting** (`pass()` everywhere, one skip wording, partial
  runs counted apart). Real work, and a different feature — separate PR on top
  of this one.
