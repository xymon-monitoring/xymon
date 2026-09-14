# Contributing to Xymon

Patches are welcome. This file covers what is easy to get wrong because it is
not visible from the source tree.

Fork, remotes and the pull-request flow are the same for every repository in
the organisation and are documented once, in the wiki: start with
[first-contribution.md](https://github.com/xymon-monitoring/xymon-wiki/blob/main/docs/contributing/git/first-contribution.md);
[git-setup.md](https://github.com/xymon-monitoring/xymon-wiki/blob/main/docs/contributing/git/git-setup.md)
has the remote configuration.

## Where a change gets written down

**Do not edit `Changes` or `RELEASENOTES` in a pull request.** Both are written
by the release manager, on the `Changes` branch, after your pull request
merges. If your change needs an entry, put the wording in the pull request
description and it will be placed for you.

Entries added in a pull request land in the wrong release section and have to
be moved, and two pull requests inserting at the top of the same section
conflict for no reason.

What belongs in each file, and how entries are written, is in
[`RELEASING.md`](RELEASING.md).

## Manual pages

Every manual page exists twice: the man(7) source next to the code it documents
(`common/hosts.cfg.5`, `xymond/analysis.cfg.5`, ...) and a generated HTML copy
under `docs/manpages/`. The source is the original — never edit the HTML by
hand, it is overwritten at the next regeneration.

No tooling is needed: edit the source and push. If the HTML is out of date, the
"manual pages match their sources" check fails and its log says how to recover,
run id filled in. With `mandoc` and `python3` you can instead run
`build/makehtml.sh common/hosts.cfg.5` and commit both halves; the converter's
output differs between mandoc versions and the check pins one, so if it still
disagrees, its artifact wins.

Either way, do not bump the version or date in the `.TH` line — that happens at
release — and keep `git diff --check` quiet.

## Pull requests

Every change to this repository goes through a pull request approved by
someone other than its author, maintainers included. This is `xymon`'s rule,
not the organisation's — the wiki and the other repositories are pushed to
directly.

A ruleset on `main`, `devel` and `release/*` enforces it: one approving review
after the last push, all review threads resolved, no force-push, no deletion,
squash or merge commit only. A rejected push to `main` is the rule working.
The `maintainers` team can bypass the ruleset; for them the rule holds by
agreement, and a bypass is for an emergency, said so in the pull request.

- One change per pull request. A fix and the cleanup you noticed next to it are
  two pull requests.
- Say what you verified, and how. "Built and ran the test suite" is useful;
  "should work" is not. If you could not test something, say that too — it is
  not held against you, and it tells a reviewer where to look.
- `tests/` holds the regression suite. If your change fixes something a test
  could have caught, adding one is worth more than the fix.
- Keep the description accurate as it evolves. A reviewer reading it after
  three force-pushes should not be reading the original plan.

## Style

Match the file you are editing. The tree spans two decades and several hands;
consistency within a file beats consistency across the project.

## C code

The tree sets no `-std=` — not in `configure`, not in any `build/Makefile.*`. The
language level is whatever each platform's compiler defaults to, across AIX,
HP-UX, Solaris, the BSDs, macOS and Linux. That is what makes the conventions
below load-bearing rather than decorative: each of them is the tree's answer to
something the language does not settle for us.

**Free with `xfree()`, allocate with plain `malloc()`.** The asymmetry is
deliberate and it reads like an inconsistency: roughly 1,700 calls to `xfree`
against 160 to `free`, but 760 to `malloc` against 14 to `xmalloc`. `xfree`
(`lib/memory.h:101`) aborts on a NULL free and sets the pointer to NULL
afterwards, so a double free aborts at the second call instead of corrupting the
heap for someone else to find. The `xmalloc` family is a different mechanism: it
is switched in wholesale by defining `XYMON_MEMORY_WRAPPERS`, which is `#undef`'d
at `lib/memory.h:14` and set by no build. Calling it directly opts one site into
something meant to be all or nothing.

**Read the environment with `xgetenv()`.** It is not a wrapper around `getenv()`:
when a variable is unset it falls back to the built-in default table in
`lib/environ.c` and caches the answer back into the environment. `getenv()`
returns NULL where `xgetenv()` returns the compiled-in default, so plain `getenv`
is right only for a variable this project defines no default for.

**Report errors with `errprintf()`.** It timestamps the line to the microsecond,
prefixes the application name, flushes stderr, and — when the caller has asked
for it — accumulates the text into a buffer that can be sent back inside a status
message. `fprintf(stderr, ...)` loses all four.

**A buffer carries its own length.** `SBUF_DEFINE(name)` declares `name` and
`name_buflen` together; `SBUF_MALLOC(name, len)` allocates `len+1` bytes and
records `len`; the buffer is then written with `snprintf(name, name_buflen, ...)`.
The recorded length excludes the terminating NUL and the allocation includes it.
Growing a buffer without updating `name_buflen`, or sizing one without room for
the NUL, is a bug this tree has shipped more than once — keep the two in the same
statement.

**Resolve a platform difference once, not at the call sites.** It belongs behind a
macro the build detects, in `lib/` or `common/`. The tree already reads this way:
93 preprocessor guards across `lib/`'s 54 sources and 39 across `xymonnet/`'s 13,
against none at all in `web/`'s 31 and `xymongen/`'s 10. A change that needs a new
detected macro says in its pull request what is detected, in what order, which
macro that sets, and where the macro is read.
