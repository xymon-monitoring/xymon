# Contributing to Xymon

Patches are welcome. This file covers the things that are easy to get wrong
because they are not visible from the source tree.

## Where a change gets written down

Two files, two different jobs. A change usually belongs in one, sometimes in
both, and the distinction is not cosmetic.

**`Changes`** is the full list — one line per merged pull request, so that
`grep` finds it later. Entries start with a verb and say what the change does
for someone running Xymon:

```
* Fix single-service graphs matching unrelated RRD files (#139) (Thanks, Mark Felder)
```

Keep the entry on one line. Searching is what this file is for, and a folded
entry returns half a sentence to `grep`. Tighten the wording rather than
breaking the line.

**`RELEASENOTES`** is for what someone needs to know *before upgrading*: a
default that changed, a setting that stops working, a dependency that is now
required. Prose, not bullets, and no pull-request numbers — the reader is an
administrator planning an upgrade, not someone tracing a commit. Most changes
do not belong here. If yours cannot break a working installation, it does not
need an entry.

## Credit

Contributors are credited in `Changes` as `(Thanks, Name)`. Credit follows the
work rather than the pull request, in this order:

1. whoever wrote the change;
2. whoever reported the problem, if a maintainer wrote the fix;
3. whoever reviewed it, when there is nobody else to name.

Use the person's name, not their login. If you do not know it, ask rather than
guess.

## Version sections

Do not open a new `Changes from X -> Y` section, and do not bump the version in
`RELEASENOTES`. Add your entry to the section already at the top of the file;
maintainers open the next one at release time.

Release documentation for the next version is prepared on the **`Changes`**
branch, not on `main`. If your pull request needs a `Changes` or `RELEASENOTES`
entry and you are not sure where it should go, say so in the pull request and a
maintainer will place it — it is easier than untangling two entries for the same
release afterwards.

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
