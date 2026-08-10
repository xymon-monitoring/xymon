# Contributing to Xymon

Patches are welcome. This file covers the things that are easy to get wrong
because they are not visible from the source tree.

## Where a change gets written down

The changelog (`Changes`) and the upgrade notes (`RELEASENOTES`) are
maintained by the maintainers on this branch — the **`Changes`** branch — with
one entry per merged pull request: one line, starting with a verb, with a
`(Thanks, Name)` credit for the contributor. The full construction rules —
entry format, credit order, what belongs in which file — are in
`.github/RELEASING.md` with the rest of the release process.

Do not open a new `Changes from X -> Y` section or bump the version in
`RELEASENOTES`; maintainers do that at release time. If your pull request
needs an entry and you are not sure what it should say, note it in the pull
request and a maintainer will place it — easier than untangling two entries
for the same release afterwards.

## Manual pages

Every manual page exists twice: the man(7) source next to the code it
documents (`common/hosts.cfg.5`, `xymond/analysis.cfg.5`, ...) and a generated
HTML copy under `docs/manpages/`. The source is the original. Never edit the
HTML by hand — it is overwritten at the next regeneration, and the diff of a
hand-edited page tells a reviewer nothing about what actually changed.

After editing a source, regenerate its HTML copy and commit the two together:

```
build/makehtml.sh common/hosts.cfg.5
```

The script needs `mandoc` and `python3`. Run with no argument it regenerates
all pages; that is for changes to the generation itself (the stylesheet, the
post-processor), not for a one-page edit — regenerating 69 files to change one
buries the real change in the diff. The `--version` flag is for the release
scripts only: between releases the `.TH` line says what the source says, so do
not bump the version or date in a pull request.

Two properties are worth checking before you push, and both amount to running
the script again:

- Regeneration is idempotent. A second run after yours must leave the tree
  clean; if it does not, source and HTML were committed out of step.
- The sources carry no trailing whitespace. `git diff --check` on your change
  must be quiet.

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
