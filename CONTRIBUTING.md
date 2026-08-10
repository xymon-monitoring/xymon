# Contributing to Xymon

Patches are welcome. This file covers the things that are easy to get wrong
because they are not visible from the source tree.

## Where a change gets written down

The changelog (`Changes`) and the upgrade notes (`RELEASENOTES`) are handled
by the release manager on this branch — the **`Changes`** branch — with
one entry per merged pull request: one line, starting with a verb, with a
`(Thanks, Name)` credit for the contributor. The full construction rules —
entry format, credit order, what belongs in which file — are in
`.github/RELEASING.md` with the rest of the release process.

Do not open a new `Changes from X -> Y` section or bump the version in
`RELEASENOTES`; maintainers do that at release time. If your pull request
needs an entry and you are not sure what it should say, note it in the pull
request and the release manager will place it — easier than untangling two
entries for the same release afterwards.

## Manual pages

Every manual page exists twice: the man(7) source next to the code it
documents (`common/hosts.cfg.5`, `xymond/analysis.cfg.5`, ...) and a generated
HTML copy under `docs/manpages/`. The source is the original. Never edit the
HTML by hand — it is overwritten at the next regeneration, and the diff of a
hand-edited page tells a reviewer nothing about what actually changed.

No tooling is needed to keep the pair in step: edit the source and push. If
the HTML is out of date, the "manual pages match their sources" check fails
and its log says exactly how to recover — it uploads the pages it generated
and prints the download command, run id filled in.

With `mandoc` and `python3` installed you can instead run
`build/makehtml.sh common/hosts.cfg.5` and commit both halves together. The
converter's output differs between mandoc versions and the check pins one,
so if it still disagrees with your local run, its artifact wins.

Either way, do not bump the version or date in the `.TH` line — that happens
at release time — and keep `git diff --check` quiet: the sources carry no
trailing whitespace.

## Pull requests

- One change per pull request. A fix and the cleanup you noticed next to it are
  two pull requests.
- Say what you verified, and how. "Built and ran the test suite" is useful;
  "should work" is not. If you could not test something — another platform, a
  configuration you do not run — say that too. It is not held against you, and
  it tells a reviewer where to look.
- `tests/` holds the regression suite. If your change fixes something a test
  could have caught, adding one is worth more than the fix; see the existing
  tests for the shape.
- Keep the pull request description accurate as it evolves. A reviewer reading
  it after three force-pushes should not be reading the original plan.

## Style

Match the file you are editing. The tree spans two decades and several hands;
consistency within a file beats consistency across the project.
