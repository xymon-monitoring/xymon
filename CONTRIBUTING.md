# Contributing to Xymon

Patches are welcome. This file covers what is easy to get wrong because it is
not visible from the source tree.

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
