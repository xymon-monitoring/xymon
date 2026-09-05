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

### Titles

The title is the one line a reader gets in the pull request list and, after a
squash merge, in `git log --oneline`. Write it so that line is enough.

- Start with the component that changes, then a colon: a program or directory
  name as it appears in the tree (`xymond:`, `xymonnet:`, `xymond_rrd:`,
  `loadhosts:`) or one of a few fixed areas (`build:`, `ci:`, `docs:`,
  `tests:`, `client:`, `tools:`). Not a topic, and not a status such as
  `DRAFT:` or `follow-up:` — GitHub has a draft flag for that.
- After the colon, a complete sentence with a verb, in lower case. `send the
  tested hostname as the TLS server name` says what happens;
  `TLS servername support` does not.
- Say what the change makes true, not only what it removes. When one
  behaviour replaces another, name both: `bind the address --listen names, not
  every interface`.
- Keep the reason in the title when it fits in a clause: `size the grown
  testflags buffer for its terminating NUL` explains itself.
- Anything that belongs in the description stays out of the title: commit
  hashes, tracker numbers, "backport of", "informational". One issue reference
  in parentheses at the end is fine.
- Stay under about 70 characters so nothing is truncated in the list or the
  log.

## Style

Match the file you are editing. The tree spans two decades and several hands;
consistency within a file beats consistency across the project.
