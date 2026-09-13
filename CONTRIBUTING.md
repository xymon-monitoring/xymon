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

That is one instance of a question the tree asks constantly, and the answer
generalises: **a fact belongs where its reader is, and every other place points
at it rather than repeating it.** Copy a fact only when the second reader cannot
reach the first copy — a test asserting a log message is a legitimate copy,
because it fails when the string changes.

| surface | who reads it | what it carries |
|---|---|---|
| pull request title | everyone scanning the list, and `git log --oneline` after a squash | the component, then what the change makes true |
| pull request description | the reviewer deciding | the defect, the mechanism, what a reviewer cannot infer, the evidence |
| pull request comments | the reviewer, in sequence | answers to findings, and what a revision changed |
| commit message | whoever runs `git blame` years later, offline | what changed and why, in terms that stand alone |
| code comment | whoever edits that line, without the pull request | why this and not the obvious alternative |
| test header and assertion text | whoever the test fails on | what it pins, and what was expected against what happened |
| manual page | an admin on an installed system, with no source | the behaviour and its configuration surface |
| in-tree `README` | a packager or builder, with the tree open | the interface to the build and to the shipped samples |
| log and error strings | an operator at three in the morning | what happened, to which object, and what to do |
| config sample comments | whoever edits that file | what the setting does, and the consequence of each value |
| issue body | whoever decides | the problem with its evidence, the proposal, what is out of scope |
| release note | an admin upgrading | the visible consequence, and the action if one is needed |

A specification is the case that produces most of the duplication, and it follows
the same rule: its reader is an admin on an installed system, so it belongs in a
manual page, changed in the same pull request as the behaviour, with the description
pointing at it instead of restating it — which is also why the manual pages are not
optional (below).

Which page is decided by what changes, and the tree already names them: a
configuration file's format is its section 5 page (`hosts.cfg.5`,
`analysis.cfg.5`, `graphs.cfg.5`, one per file); a program's behaviour and options
are its section 1 or 8 page; the message protocol is `xymon(1)`, which documents
`status`, `notify`, `disable`, `query` and the rest. A syntax used *inside* several
files has no page today — that is a gap to close when one is proposed, not something
to settle by documenting it in one of its consumers.

Structure is documentation and has a home: section 7, where `xymon(7)` is the tree's
only page today — how the parts fit, what talks to what, which component owns which
state. A decision is not structure, even in a design section. *"xymond consolidates
member results as they arrive"* is architecture and belongs there; *"xymond rather
than xymonnet, because `NET:` splits testing across hosts"* exists only relative to a
rejected alternative, and belongs in the issue that rejected it. The test: would the
sentence still be true if the alternative had never been considered?

A decision splits across the surfaces, and asking which one it belongs to is the
wrong question:

- **its observable consequence** — the manual page, as specification, with no trace
  of the deliberation. If a user can hit it without reading the source, the page says
  so; if they cannot, the page says nothing;
- **why, in terms that stand alone** — the commit message, for whoever runs
  `git blame` offline;
- **why not the obvious alternative** — the code comment at the line someone would
  otherwise change back;
- **what was weighed and rejected** — the issue that decided it, which is the only
  place that record belongs and the only one nobody needs in order to use the
  software.

A pointer says which side is authoritative, in its wording rather than by
implication: *for X, see Y* where Y holds the fact, *this is the only copy* where
nothing else does. Two files that split one subject point at each other, because a
reader landing on either needs to know it is a half — `CONTRIBUTING.md` and
[`RELEASING.md`](RELEASING.md) are that pair. A file cited from many places does the
opposite and states its own scope instead of listing its citers: ten back-links are
ten things to keep current, and the routing rule exists to avoid exactly that.

Put a pointer where it applies — in the section whose subject is divided, or in the
opening line when it qualifies the whole file. Not in a footer. A "see also" at the
bottom is detached from the fact it qualifies, which makes it both the least read
part of a page and the first to go stale.

A pointer that is missing its other half is not a reason to open a change by itself.
Add it while you are already editing that document for something else, or fix them
together in one deliberate pass. Scattered across unrelated pull requests, each one
puts a reviewer in front of a diff whose subject is not the subject of the change.

One kind of pointer is not a link at all and must stay one-way: the `@file` imports
that pull a file's text into another. `CLAUDE.md` imports `AGENTS.md`, which imports
this file, so a tool reading the first reads all three. That chain has a direction by
construction. A back-pointer is always written as a reference and never as an import,
or the chain closes into a cycle.

A precise measurement belongs in the text that is read once — a pull request
description, an issue, a commit message — where it proves a claim at the moment
someone decides and is then archived. A document that is maintained carries an
approximation instead, one that stays true over years: *nearly every*, *almost all*,
*none*. Nobody ever recounts a figure in a rules file, so an exact one dates the file
silently, and the more precise it looks the longer it goes unquestioned. Keep a number
exact only where it is the rule holding rather than a census of it — "none at all in
`web/`" is a sentence that should break if someone scatters a guard into the CGIs.

The cost of ignoring any of this is not length, it is drift: when one fact sits in
two places, one copy goes stale and nothing says which.

## Shortening

The rule above says where a fact belongs; this says what may come out when the text
holding it has grown. It applies to any prose the project carries — a pull request
description, a review comment, a commit message, a code comment, a manual page, an
issue.

**Shorten losslessly or not at all.** Before each cut, name what a reader could no
longer do:

| the answer | verdict |
|---|---|
| "read it a second time" | cut it |
| "check the claim" | keep it — that is a citation |
| "understand why" | keep it — that is what stops someone undoing the change |
| "know whether it affects them" | keep it — that is the specifics |

Lossless, because the information survives:

- **Deduplicate** — the same fact stated twice in one document.
- **Relocate** — move a block to where its reader is rather than delete it.
- **Merge overlapping sections** — two sections making one argument become one.
- **Delete superseded text** — prose arguing for a design that was replaced.
- **Cut the audience-less** — notes to self, questions already answered, expired status.
- **State the rule instead of the enumeration** — unless the examples differ in kind
  rather than in detail.
- **Compress prose, not facts** — drop connectives and hedges, and turn a paragraph
  into a table when the table carries the same facts.
- **Let each fact live at its natural level** — a fact about what the code does belongs
  in the code comment, with the prose pointing at it.
- **Reference instead of restate** — only when the reader can reach the target.
  Pointing an admin at a `README` that is not installed removes the information.

Lossy, and each one looks like editing:

- **A citation or a `file:line`** — without it a claim becomes an assertion, and the
  next reader has to rediscover the evidence.
- **The reason behind a decision** — it is what stops someone undoing it.
- **The failure mode or the severity** — it is what justifies the priority, the label
  or the block.
- **One of two proofs that look alike** — check what each proves before calling either
  redundant.
- **The specifics** — "anything keyed on mtimes" loses who is affected; "pre-3.5 Linux
  kernels, and permanently on macOS" lets a reader decide whether it is about them.
- **An answer pulled out of the thread that asked the question** — the same measurement
  is context in a description and a rebuttal in the reply that raised the concern.

**Shorten last.** While a change is still moving, editing its text for length
desynchronises it rather than compressing it: what survives the cut describes the
previous revision. Shorten when a re-read turns up nothing new — that, not a length
target, is the signal.

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

Every change goes through a pull request. Whether it also needs a review by
someone other than its author depends on what the change is. This is `xymon`'s
rule, not the organisation's — the wiki and the other repositories are pushed
to directly.

**What GitHub enforces.** A ruleset on `main`, `devel` and `release/*` requires
a pull request, one approving review after the last push, every thread
resolved, squash or merge commits only, and no force-push or deletion. The
`maintainers` team is on its bypass list unconditionally, so for them it
enforces nothing.

**What we ask of each other.** The bypass is a mechanism, not a permission. A
second reader is always the better outcome, and the rows below say when a change
may go in without one — not when to stop asking:

| change | may merge without a review |
|---|---|
| bug fix, new feature, architectural change | no |
| build, CI, test framework, minor manual refactoring | yes, when tests cover it or the pull request carries the evidence |
| typo, comment, documentation | yes |

GitHub cannot express that, so off the bypass list it asks for a review
whatever the row; the lighter rows say when a maintainer may bypass, and one
who does says so in the pull request.

How a change was written does not enter into it. Whoever opens the pull request
is its author, whatever helped them write it: they read every line, can say
what each part does and how it was checked, and answer for it. Responsibility
does not move to a tool. Work that no person supervised is a different
situation, and this file gets revisited if it arrives.

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

- Start with the component that changes, then a colon: a name the tree already
  uses for it — a program, a directory, a module, a function or a config file
  (`xymond:`, `xymonnet:`, `xymond_rrd:`, `loadhosts:`, `xtree:`,
  `dropdirectory:`, `xymonserver.cfg:`) — or one of a few fixed areas
  (`build:`, `ci:`, `docs:`, `tests:`, `client:`, `tools:`). A module or a
  function counts when the tree names it, with or without a file of its own:
  `xtree:` is the `xtree*` API in `lib/`. A change that introduces the
  component names it too — the tree uses the name once the change lands. What
  is excluded follows from that: a topic, a branch name, and a status such as
  `DRAFT:` or `follow-up:` — GitHub has a draft flag for that. One plain name,
  unscoped: `tests:`, not `test(portability):`.
- After the colon, a complete sentence with a verb, in lower case. `send the
  tested hostname as the TLS server name` says what happens;
  `TLS servername support` does not.
- Say what the change makes true, not only what it removes. When one
  behaviour replaces another, name both: `bind the address --listen names, not
  every interface`. The verb names the behaviour, not the kind of change —
  which is why `fix` and `add` so rarely fit: the diff already shows the kind.
- Keep the reason in the title when it fits in a clause: `size the grown
  testflags buffer for its terminating NUL` explains itself.
- Anything that belongs in the description stays out of the title: "backport
  of", "informational", the plan, the review history. A single parenthesis at
  the end may carry a reference, and it carries only what a later reader
  cannot reconstruct: where the change came from, and what it supersedes.
  `Fixes #N` and `Closes #N` go in the description, which is where GitHub acts
  on them. One pair of brackets, not two.
- Keep the sentence — everything before the trailing reference — to 80
  characters or fewer, so it reads whole in the pull request list and in
  `git log --oneline`. A reference may take the line past that: its length is
  set by what is cited, not by the author, so capping the whole title would
  shorten the wrong half.

### Descriptions

The description is what a reviewer reads to decide, and what a later reader reaches for
when the diff does not explain itself. Write it for both.

- Say what is wrong before what changes: the defect in a sentence, then the mechanism
  that fixes it. A reader who stops after two sentences should still know why the pull
  request exists.
- Carry what a reviewer cannot infer from the diff — a platform floor, an ordering
  against another pull request, a behaviour deliberately left alone, something you
  could not test. Everything else in the description is convenience; this part is the
  reason it exists.
- Show the evidence, compactly. Suite counts, a measurement, a before-and-after table.
  A table of three rows says what three paragraphs say, and survives being skimmed.
- Cite what can be checked. A claim about the tree carries `file:line`; a claim about
  another change carries its number. A citation is what lets a reviewer confirm a
  sentence in one command instead of taking it on trust.
- A comment is not a second copy of the description. It answers a finding, records what
  a revision changed, or states a decision taken since. When the same fact sits in both,
  one of them goes stale — and it is the description, because that is the copy nobody
  re-reads.
- Shortening a description follows the same rule as shortening anything else, and it
  comes last: see [Shortening](#shortening).

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
deliberate and it reads like an inconsistency: nearly every free in the tree is an
`xfree`, while nearly every allocation is a plain `malloc`. `xfree`
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
almost every preprocessor guard in the tree sits in `lib/` or `xymonnet/`, and there
are none at all in `web/` or `xymongen/`. A change that needs a new
detected macro says in its pull request what is detected, in what order, which
macro that sets, and where the macro is read.

None of that is general C practice, and this file does not try to be: portable C,
undefined behaviour, and how other projects handle patch submission and review are
maintained by other people and collected on the wiki, in
[external-references.md](https://github.com/xymon-monitoring/xymon-wiki/blob/main/docs/contributing/external-references.md).
That page holds links and nothing else — the rules are here, and it points back at
this file rather than restating any of them.
