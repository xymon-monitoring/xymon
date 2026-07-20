# Implementation plan: self-describing disk (metric block replaces do_disk)

Goal: let the stock `disk` (and `inode`) column be produced as a self-describing
METRICS block instead of the built-in `do_disk` handler, without breaking
filenames, history, graphs, or old clients. This is the endpoint the
linecount-hint fix (feat/disk-linecount-hint) was the pragmatic first step of.

The marker dialect itself - vocabulary, marker doctrine, counting doctrine,
units, thresholds, archive consolidations, display keywords - is defined in
`self-describing-metrics.md`; nothing there is disk-specific. This plan covers
only the migration of the disk/inode columns onto that dialect.

## Base: self-describing-metrics only

This branch (`feat/self-describing-disk`) is based on `feat/self-describing-metrics`
and needs nothing else. That branch supplies both genuinely-required pieces:

1. the marker **writer** (`do_devmon_rrd`) that creates the block's RRDs, and
2. **content routing** (`xymon_markers_have_store`) that detects a block and
   dispatches to that writer.

`feat/test-cfg` is NOT required (an earlier draft of this plan wrongly said it
was). The one thing it was invoked for - rerouting disk off the built-in
handler - has a smaller solution that lives entirely on this branch (see
Feature 2). test-cfg's `HANDLER markers` is an optional, cleaner, config-driven
way to express that reroute, and can be adopted later if the two branches merge;
it is not a prerequisite for self-describing disk.

(For reference, if the branches ever do merge, the conflict is four files, all
additive - `include/libxymon.h`, `lib/Makefile`, `lib/xymonrrd.c`,
`lib/htmllog.c` - both sides add to the same regions with no logic clash.)

## The two missing capabilities

### Feature 1 - a collision-free instance encoding, with a one-time rename

Why an encoding at all: a mount point cannot be a filename verbatim - `/` is the
directory separator (`disk,/var/log.rrd` would be read as nested directories).
So `/` must be substituted. `do_disk` uses `/`->`,` (`/`->`,root` for the root),
the marker/devmon writer applies its own `/`->`,` and joins with `.`. Two
issues:

1. The two conventions differ slightly (`disk,root.rrd` vs `disk.,root.rrd`), so
   a naive block would orphan history - the original reason this feature existed.
2. More important, `/`->`,` is **ambiguous and always has been**: `,` is a legal
   filename character, so `/a/b` and `/a,b` both encode to `disk,a,b.rrd` and
   collide into one RRD. Only `/` and NUL are illegal in filenames, so **no
   single-character substitution is safe** - a collision-free scheme must be
   reversible.

Decision (option 2): do NOT add a per-column filename template (`fnfmt`) to the
writer - that bakes a legacy quirk in permanently and keeps the collision.
Instead adopt one reversible, collision-free instance encoding for the whole
writer, and migrate existing files into it once. This fixes the 20-year latent
collision as a side effect and keeps the writer uniform (disk is not special).

Three coordinated pieces (this is writer AND showgraph, not writer alone):

- **Encode** (`xymond/rrd/do_devmon.c` / the shared filename builder): `/` (and
  the escape char) percent-encoded, e.g. `/var` -> `%2Fvar`, `%` -> `%25`.
  Reversible, collision-free.
- **Capture** (graphs.cfg gdef `FNPATTERN`): unchanged mechanism, matches the
  encoded name.
- **Decode** (`web/showgraph.c` `@RRDPARAM@`): decode the captured instance back
  to the mount point for the legend, so graphs read `/var`, not `%2Fvar`.
- **Migrate** (one-time script): rename existing `disk,*.rrd` into the new
  encoding, idempotent and safe (skip already-migrated, never lose a file).
  Run once at cutover; not in the hot write path.

Separability: fixing the ancient collision is independent of self-describing
disk. If the encode/decode/migrate chain proves fiddly, self-describing disk can
ship first on the existing `,` encoding (inheriting the old collision - no worse
than today), and the collision fix lands as its own follow-up. So option 2 is
the target, but it does not block the disk block.

Acceptance: `/a/b` and `/a,b` produce distinct RRD files; the graph legend shows
the real mount point; a migrated tree's graphs are continuous with pre-cutover
history.

### Feature 2 - a block-bearing status wins over the built-in handler

Problem: the dispatcher in `xymond/do_rrd.c` (`update_rrd`) checks built-in
handler ids first (`if (strcmp(id,"disk")==0) do_disk_rrd(...)`), and content
routing is only the fallback. So a `disk` status carrying a METRICS block still
goes to `do_disk_rrd` -> double write.

Fix (on this branch, no test-cfg): move the content-routing check ahead of the
built-in chain - when a status carries a store block
(`xymon_markers_have_store(msg)`), dispatch to `do_devmon_rrd` and do NOT fall
into the built-in `disk` branch. This is the "self-describing beats built-in"
rule, and it is safe because emitting a block is deliberate: a column only
reroutes if its producer chose to add one, so default columns hit exactly the
same built-in branch as today.

Optional refinement (needs test-cfg, later): `HANDLER markers` on a `TEST`
block expresses the same reroute per-column and config-driven, rather than
automatically-on-block-present. Cleaner control, but not required - the
dispatch-precedence rule above is enough to retire `do_disk` for a
block-bearing disk status.

Scope: self-contained on `feat/self-describing-metrics`.

## The disk metric block itself

With feature 2 present (and optionally feature 1), express disk as a block. No
test.cfg is needed - the block itself drives everything:

1. The status body carries a METRICS block whose instance lines are the
   filesystems, with the DS the `[disk]` gdef already expects (percent-used,
   etc. - collision 3, mechanical: match do_disk's dataset names/values). The
   block's presence is what reroutes disk off `do_disk` (feature 2), so no
   `HANDLER`/`TEST2RRD` config is required to make it the sole writer.
   (test.cfg `TEST disk { HANDLER markers ... }` is the optional config-driven
   way to force the same reroute explicitly - not needed here.)
2. Who emits the block: the server-side `unix_disk_report` in
   `xymond/xymond_client.c` (same place the linecount hint lives) is the natural
   producer - it already iterates the filesystems, knows the post-IGNORE set,
   and computes the values. It appends the METRICS block to the status it builds.
   The shell client is unchanged (still ships raw df); the block is synthesised
   server-side from the parsed df. This keeps old clients working and avoids a
   wire-format change on the client.
   - Consequence: the `<!-- linecount -->` hint becomes redundant for disk (the
     block's instance count is exact and derived) and can be dropped for the
     columns that carry a block, kept for those that do not.
3. do_disk retired for the column via feature 2; `do_disk_rrd` still exists for
   any column/client not carrying a block (fallback forever).

## History / migration

Option 2 (chosen) does not reproduce the old names - it replaces the ambiguous
`,` encoding with a reversible one and renames existing files once. History is
preserved by the migration, not by matching names. Acceptance: after the
one-time rename, a column's graphs are continuous across the cutover, and the
previously-colliding `/a/b` vs `/a,b` now have separate RRDs.

If the collision fix is deferred (see Feature 1 "separability"), self-describing
disk ships on the existing `,` encoding with no rename and no display change,
inheriting the old collision unchanged.

## What this buys beyond paging

- Exact paging (already had it via the hint) - now derived, not counted.
- Alerting on the stored RRD values via DS/AGGDS rules (from self-describing) -
  disk % used becomes a first-class metric, not just a status colour.
- disk stops being a special-cased built-in handler and becomes an ordinary
  declared metric - one less bespoke code path, the general model absorbing a
  legacy one.

## Phasing (commits on this branch)

1. DONE - Feature 2 first (smallest): a block-bearing status routes to the
   marker writer ahead of the built-in handler; tested both ways (block
   writes once, no do_disk double-write; block-less disk unchanged).
2. DONE - Feature 1: reversible instance encoding (writer encode + showgraph
   decode) + one-time rename migration; `/a/b` and `/a,b` get distinct RRDs
   and legends show the real mount point.
3. DONE - HANDLER markers config route (via the test-cfg merge).
4. DONE (disk AND inode): `unix_disk_report`/`unix_inode_report` emit the
   METRICS blocks, end-to-end tested via xymond_client --no-update
   (tests/server/disk-metrics-block.sh). Decisions taken:
   - DS line identical to do_disk's params: "DS:pct:GAUGE:600:0:100
     DS:used:GAUGE:600:0:U" - same files, same schema, continuous history.
   - The "used" value mirrors do_disk exactly: absolute df column 2
     (do_disk treats every unix df as DT_UNIX and hardcodes columns[2]);
     non-numeric -> "U". Faithful-to-do_disk IS the spec, not per-OS
     cleverness.
   - RRDDISKS/NORRDDISKS filtering is replicated at synthesis time (same
     exclude-then-include semantics, compiled once): a filtered filesystem
     stays in the df text but gets no block line - matching what do_disk
     stores today, and fixing the hint's overcount under these filters.
   - The linecount hint is KEPT alongside the block, not dropped: the disk
     column renders through the legacy TEST2RRD/GRAPHS path, which reads
     the hint, not block counts - dropping it would regress paging back to
     df-line counting. The block is a STORAGE cutover only; display is
     unchanged until the fileset index lands. (Amends the earlier "drop
     the hint" note, which presumed display derives from the block.)
   - End-to-end test route: xymond_client --local/--test mode feeding a
     synthetic linux client message, grep the emitted status for the block
     (verify the local-mode output path first); then the existing marker
     writer tests cover storage.
   - unix_inode_report gets the same treatment as a follow-up commit
     (block name "inode").
5. DONE: xymond_rrd.8 notes that server-generated disk/inode statuses carry
   blocks (disk is a declared metric); the test.cfg disk example waits for
   test.cfg's own documentation surface (it has no man page yet - by
   design, while it is experimental).

Review decision (FreeBSD/Darwin inode): the raw 9-column df -i shape made
do_disk store GARBAGE for these OSes (the iused count as the instance
name - a new junk RRD every poll). The block emitter is header-driven and
stores instance=mount, pct=%iused - a deliberate divergence-as-improvement,
not a mirror bug. The 'used' DS still carries disk-blocks for that shape
(do_disk's wart, half-kept); acceptable, the pct DS is the graphed one.

The endpoint is reached: disk and inode are declared metrics. do_disk_rrd
remains as the fallback for producers that do not carry a block (netapp/
dbcheck data messages, NT clients not routed through unix_disk_report) -
"fallback forever", as designed.

## Risks / watch-items

- The rename migration is load-bearing and destructive: it must be idempotent,
  skip already-migrated files, and never lose one. Test on a copy first.
- The encode/decode must round-trip: any instance the writer encodes,
  showgraph must decode to the identical mount point, or legends break.
- The dispatcher change (feature 2) touches the hot RRD path for every column;
  guard it so only an explicit HANDLER/marker route diverts - default columns
  must hit the exact same built-in branch as today.
- `unix_disk_report` emitting a block enlarges the status message; check the
  status buffer bounds (it uses `msgline[4096]` per line and strbuffer for the
  body - the block is many short lines, fine, but confirm).
- Content routing already lets any sender create RRDs; server-synthesising the
  block (not trusting a client block) keeps disk's trust model unchanged.
- Project phase: this is rethink-tier (retiring a built-in handler). It rides on
  ONE feature branch (self-describing-metrics), not yet merged upstream.
  Sequence after that lands, or keep as a proving branch. test-cfg is optional
  and only for the config-driven HANDLER refinement.
- Dialect-level watch-items the disk migration depends on (declared
  heartbeats acting only at file creation; the rrd_name_compare total-order
  rewrite): see "Risks / watch-items" in `self-describing-metrics.md`.
