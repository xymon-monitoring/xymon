# Self-describing metrics: dialect design

Design doctrine and decided/candidate surface for the XYMON METRICS/GRAPH
marker dialect (lib/xymonmarkers.h) and everything derived from it. Nothing
here is specific to one column: disk, temperature, response time or any
custom collector all speak this dialect. The disk migration itself lives in
self-describing-disk-plan.md and references this document.

## Vocabulary (binding for all new surface)

Four nouns, used with one meaning everywhere: **metric** (a DS - the curves
of an image), **instance** (a measured object - one RRD file; the unit of
filters, counts and paging), **graph** (a graphs.cfg definition), **image**
(one rendered slice). Rules: every new name states its unit (instances=,
MAXINSTANCESPERIMAGE as instances-per-image); legacy names
(maxgraphs, linecount, GRAPHS ::N, FNPATTERN) are frozen aliases documented
against the glossary, never removed and never duplicated with a second new
spelling; new code speaks the glossary (instancespec, instancecount - not
countspec, itemcount). The marker attribute is instances=N / instances=all
(renamed from the earlier count= while unshipped, history rewritten).

## Homogeneous test doctrine (settled)

One homogeneous METRICS block per test. The block name is OPTIONAL and defaults
to the test (implemented: the marker is `<!--XYMON METRICS` with an optional
`: <name>`; likewise `<!--XYMON GRAPH`). A test measures one homogeneous thing -
one unit, one instance-set; heterogeneous data is modelled as several small
homogeneous tests. Grouping them for display is a **testgroup** - a server-side,
display-only aggregator (COMPACT-style, emits no status/alert/ack), never a wire
concern. See docs/PLAN.md.

## Marker design doctrine (settled)

Two markers, and the axes on which that decision was made - so it is not
relitigated:

- XYMON METRICS declares a FACT (schema + instance values); XYMON GRAPH is a
  display INSTRUCTION (gdef name + instances=). Decomposition is by fact
  declared, never by consumer: METRICS already serves three readers (writer
  stores, AGGDS aggregates, paging counts) as projections of one block - one
  source of truth, and a fourth consumer costs zero wire change. One marker
  per consumer would triple the data and allow the copies to disagree.
- No verbs in names (STORE/SHOW): METRICS is multi-verb by design; the verbs
  live in the contract documentation (xymonmarkers.h). No backend in names
  (RRD): the block is a neutral contract - naming the backend is the
  DEVMON RRD mistake this vocabulary supersedes.
- Schema and values always travel together: a SCHEMA/VALUES split would make
  the server remember state between messages - statelessness outranks the
  few DS: lines saved.
- Markers declare facts, never policy: no thresholds in the wire; policy
  belongs to the server rule engine (RFC #218, one rule engine).
- Extension is a new XYMON <WORD>: marker, granted only for a distinct fact
  with a distinct lifecycle; unknown markers are ignored comments, so the
  namespace is forward- and backward-compatible for free. The DS: line
  dialect is documented as a generic schema mini-language (type, heartbeat
  as validity window, min/max), not an RRDtool allegiance.
- The only standalone projection of a block is the count, and only where no
  block exists: the legacy linecount hint, and instances= when display
  diverges from the block.

## Counting / display doctrine (amended)

The branch currently answers "how many graphs?" with the fileset-unknown
predicate: when a store filter (STOREPATTERN/EXSTOREPATTERN) makes the file
set diverge from the message, the count is set to 0 and the graph renders
UNSLICED. That is a stopgap, not a design: a host with 150 filesystems and
one filter gets a single page of 150 graph images - the exact problem paging
exists to solve. Replace it with a hierarchy where every level knows what it
counts:

1. An explicit instances= on a XYMON GRAPH marker wins - the producer of the
   MESSAGE knows its own graphs.
2. Otherwise, a per-host fileset index maintained by the WRITER: xymond_rrd
   is the single creator of RRD files, so it keeps "instance -> last write"
   up to date as it writes (bookkeeping at event time, not recounting at
   render time). The renderer reads that one small file and applies the
   staleness rule to its entries - no readdir, no per-file stat. Two
   obligations: freshness is time-based, so the index stores last-write
   timestamps (not a bare counter); external deletions (trimhistory, manual
   rm) bypass the writer, so a missing/inconsistent index triggers a one-off
   rebuild scan and drift is tolerated between rebuilds.
3. Unsliced rendering remains ONLY as the last resort when neither message
   nor files are reachable (locator-based remote RRD storage).

This reinstates the sound half of PR #246 (count what will actually render)
while keeping its env-var config surface retired.

IMPLEMENTED (lib/filesetindex.c): the writer bookkeeps RRD updates in an
in-memory tree and publishes <host>/.fileset-index ("<rrdfn> <ts> [k=v ...]"
lines, readers ignore trailing fields - units/thresholds extend the
record). Write economics: a file's freshness is its own mtime (touched
only for ACCEPTED updates), so plain data updates never write the index
- only index-only state does. Schema declarations flush immediately, a
plain new-file entry only joins an index that already exists, and a
host with no self-describing state never materializes an index file at
all. Readers (fsidx_count_*, the seed load) take file freshness from
mtime. Flushes are
atomic (tmp+rename) and merge under flock because the status- and
data-channel writers share the file; a missing index reseeds from a
one-off directory scan; drophost/renamehost hooks parallel the
AGGDS ones.
Consumers: ALL of htmllog's graph paths - marker graphs, GRAPHS_<service>
entries and the legacy default-graph link - count store-filtered
filesets from the index (staleness cutoff 86400, matching showgraph).
Counting is FNPATTERN-aware: gdef metadata captures the pattern (INCLUDE
inherits it) and xymon_gdef_fileset_count() matches index entries by it,
falling back to the "<name>.<instance>.rrd" prefix rule when the gdef has
no pattern. Unsliced rendering now happens ONLY without an index - the
doctrine's level 3, as designed.

## Lazy: RETIRED (2026-07) - design preserved for a future scale case

Decision: the lazy/flat-instance mechanism was removed from this branch.
The measured ledger did not justify it: its best case (an SNMP-scale
park, 10k idle instances) saves ~400MB of storage, ~50x inodes and a
few disk writes per second - cheap resources on modern hardware, with
file-churn (backups) the only tangible operational gain - while its
complexity concentrated the branch's audit findings (baseline
resurrection across the two writers, freshness persistence economics,
splice seeding, warm-up gates). A conditional gain does not buy an
unconditional cost. The feature never shipped, so no compatibility
surface remains: the banner attributes and the graphs.cfg keyword are
gone (unknown banner attributes and unknown index fields are ignored by
the dialect's generic forward compatibility, which is a property of the
format, not a lazy remnant).

The as-built mechanism, preserved: a flat instance = one index record
"b=<since>,<values>" (no RRD file), rendered as one HRULE per value
component with a "flat since <date>" legend; file materializes at the
first deviating sample, created with an early start and seeded with the
baseline value one step before the change (a true step edge, no RRA
backfill); the record tombstones on materialization; positional d=
names map flat values into AGGDS; showgraph enumerates virtual
instances from the index so an entirely-flat fileset still renders;
freshness rode the graph's staleness window.

Lessons paid (bind any revival):
1. Never persist what the filesystem already records - a real file's
   freshness is its mtime; duplicating it in the index cost a
   rewrite+2 fsyncs per host per cycle for every install.
2. Cross-writer retraction must be durable or evidence-based: the
   in-memory bl_cleared tombstone let the peer channel resurrect a
   retired baseline from disk forever (fix direction: the file's
   existence IS the retraction proof, as showgraph's access() check
   and note_commit already treated it).
3. A state machine (flat vs materialized) taxes every future feature;
   prefer stateless filters.

Reopen criterion: a real park where the FILE COUNT itself is the
operational problem (~10^5 idle instances), measured, with the
write-thinning successor below already deployed and insufficient.

## Write-thinning: the successor (IMPLEMENTED)

The observation that dissolves most of lazy's case: RRD's own heartbeat
mechanism already supports sparse updates. Verified with rrdtool 1.7.2:
- A GAUGE with heartbeat 48h updated once a day with a constant value
  yields a continuous line (574/575 known points) - RRD interpolates.
- Gaps beyond the heartbeat go UNKNOWN (verified) - the declared
  heartbeat bounds how much continuity may be invented.
- A rejected update does not advance mtime; an accepted one does
  (verified) - mtime stays the commit-gated freshness carrier.
- CRITICAL: a value change after a gap back-fills the whole gap with
  the NEW value (verified) - and for COUNTER the delta smears over the
  gap as a low rate instead of a spike (verified). Both are fixed by
  ONE mechanism: on change, pre-write (t-step, old_values) then write
  (t, new_values) - verified to pin the step edge and the counter
  spike in the right bucket.

The design: a stateless write filter in the RRD writer, uniform over
the whole stream - "write only new information".
- Skip the update when ALL of the file's values equal the last written
  ones AND the last write is younger than R (keepalive interval).
- Keepalive: one ordinary update with the unchanged values every R -
  it keeps the line continuous within the heartbeat and advances the
  mtime as a side effect. R derives from the declaration: R = h/2 (no
  new config knob).
- On change: pre-write (t-step, old) + write (t, new).
- Exclusions: ABSOLUTE DSes (equal readings are new information - a
  factor-N rate error if thinned) and 'U' values (thinning unknowns
  would invent continuity over genuinely unknown periods). Files
  mixing hot and cold DSes never thin (all-DS condition) - the gain
  concentrates on fully-idle files, which is the SNMP profile.
- Gating: only DSes whose METRICS block declares a heartbeat >= 2R
  (the h= plumbing exists); rrdreconcile tunes existing files'
  heartbeats. Legacy paths without declarations: untouched.
- Invariant: R << XYMON_STALE_WINDOW (the update-cache lag already
  floors any meaningful window at ~2h; the fixed 86400 absorbs both).
- Composition: sits before the update cache (which holds the last
  values already - the comparator is nearly free); orthogonal to
  rrdcached; NO cross-writer shared state (each writer thins its own
  files) - the whole merge/resurrection bug class cannot exist.
- Economics vs lazy: ~1 write/hour/idle instance instead of 0 (and no
  storage/inode saving), degrades continuously (a metric changing
  once a day saves ~90%, where lazy saved nothing after
  materialization), at ~5% of lazy's complexity.
- What it does NOT give: storage, inodes, creation burst - the cheap
  axes.
- Restart: comparator empty -> one eager cycle, then thinning resumes.
  No persistence (lesson 1).

Status: parked as opt-in-by-declaration; implement when a real
deployment asks for the I/O reduction.

## Archive consolidations derived from graph DEFs (creation half IMPLEMENTED)

Implementation status: gdef metadata collects the CF set its DEF lines
read; at file creation the writer unions the CFs of every matching gdef
(FNPATTERN or name-prefix) and clones each AVERAGE archive per extra CF,
skipping CFs the definition set already carries. Safe by default: a file
no gdef reads beyond AVERAGE gets a byte-identical stock archive set.
The reconcile half for late gdef changes is the rrdreconcile tune-pass
(forward-only, see "File schema evolution" below).

An RRA line bundles two decisions that belong to different owners:
WHICH consolidations exist (AVERAGE/MAX/...) is the consumer's need -
the graph knows it reads DEF:...:MAX; the retention ladder (resolutions,
depth, disk budget) is the admin's policy. Split them accordingly:

- rrddefinitions.cfg keeps ONLY the ladder (steps x rows per resolution).
- The needed consolidations are DERIVED, no new keyword: at file creation
  the writer collects the gdefs whose FNPATTERN match the file (the gdef
  meta scanner already parses graphs.cfg) and unions the consolidation
  functions their DEF lines read. Ladder x union = the RRA set. Adding
  DEF:...:MAX to a graph is what causes MAX archives for future files -
  one source of truth per decision, in its owner's file.
- Motivation: AVERAGE-only archives flatten peaks on long ranges (a
  20-minute 98% disk spike averages invisible on the yearly view), and
  legend GPRINT:...:MAX only shows the max of the averaged points. A MAX
  archive read by DEF:...:MAX preserves true peaks - today unused and
  unreachable without hand-syncing two config files.

DECIDED: no back-migration - new archives start today. Old files keep
AVERAGE only, forever (no rrdtool create --source pass). Consequence the
renderer must absorb: DEF:...:MAX against a file lacking the archive
fails the whole rrdtool graph, so showgraph must probe each file's
available consolidations (rrd_info) and omit DEFs referencing an archive
that file does not have. Same family as the heartbeat watch-item:
declarations changed after creation only affect new files, and the
reader tolerates the mix.

## Units and gdef scaffolding (decided; IMPLEMENTED except mixed-unit grouping)

Implementation status: the writer records declared units into the fileset
index ("u=ds:unit,..." on the file's entry, live declarations replacing
stale ones); the synthetic gdef derives YAXIS when every dataset shares
one unit after alias normalization, applying the renderer hint table
(canonical spelling, "-b 1024" for byte quantities, "--units-exponent 0"
for percentages) - an unknown unit labels the axis verbatim with default
rendering, and --emit-gdef scaffolds the derived YAXIS and options.
Mixed units RESOLVED (2026-07, verified against rrdtool 1.10.3): the
renderer supports exactly ONE unit per image automatically - and that is
an engine fact, not a design choice. rrdtool has a single value axis;
--right-axis is only a fixed linear relabeling of it (scale:shift), so
independent per-series axes are impossible at any dialect ambition.
DECIDED: one unit per image, full stop.
- 1 unit: automatic (YAXIS + hints derived) - implemented.
- 2+ units: SPLIT THE BLOCK, one unit per block (producer guidance,
  SHOULD) - each gets its own image/axis/hints, and stacked same-window
  images preserve the correlation reading (aligned small multiples).
The two-unit --right-axis overlay (CDEF-scale the second series,
relabel with a fixed scale:shift) is deliberately NOT a design tier:
its linear ratio is a presentation judgment (data-derived ratios flap
per render; fixed ones squash one curve). Hand-written gdefs remain
free-form as always, so an admin can still build one - unsanctioned,
unautomated, undocumented as doctrine.
The wire stays unrestricted: units are facts, never policed. The earlier
per-unit image-grouping candidate is retired - it would add a second
image-splitting dimension (instances x unit-groups) through the whole
paging stack to automate what block-splitting gives for free.

- Unit as a declared fact (DS dialect extension, DECIDED): the unit is
  metric semantics - producer knowledge, like type and bounds - and the one
  fact whose absence caps auto-generated graphs at YAXIS "Value". Declare it
  PER DS, as an optional 7th colon-separated field
  ("DS:read_ms:GAUGE:600:0:U:ms"); the parser consumes field 7, the writer
  reconstructs the 6-field spec for rrdtool, an unsuffixed line stays
  legal. DECIDED: colon, one separator for the whole line - simplicity and
  coherence outrank the alternative (a space suffix would have kept the
  left part a verbatim rrdtool spec for third-party parsers/copy-paste).
  Positionally unambiguous because the arity is fixed (see COMPUTE below).
  Accepted cost: a suffixed line is no longer a paste-able rrdtool DS
  spec, and strict 6-field parsers must tolerate a 7th field - the writer
  is unaffected since it revalidates and rebuilds the spec anyway.
  Persistence: an RRD file cannot carry a unit (the format has no metadata
  slot) and showgraph renders from files without the message in hand, so a
  declared unit must survive server-side: it rides the writer-kept fileset
  index (same writer, same cadence - entries extend to instance ->
  last-write + per-DS units), where the renderer already looks to count.
  The synthetic gdef reads it for its YAXIS (grouping by unit); a
  hand-written gdef still wins. Unit absent = exactly today's behaviour:
  hand-written YAXIS or the generic "Value".
  COMPUTE is excluded from the wire dialect on three grounds, none of them
  taste: it is structurally redundant (the producer computes its instance
  values, so any derived DS is expressible as a plain DS with computed
  values, at identical storage cost); it would bake RRD-specific RPN
  semantics into the backend-neutral contract (a non-RRD writer would need
  an RPN evaluator); and it is measured-unused in twenty years of xymon
  code and config. Its exclusion is what makes the DS arity fixed. Never a per-block
  unit declaration: a block may legitimately mix units (bytes/s + packets/s
  + errors), and a second declaration surface invites contradiction. The
  graph axis is DERIVED, not declared: all DSes of an image share a unit ->
  that is the YAXIS; mixed units -> the synthetic gdef groups DSes by unit
  (one image per unit); hand-written gdefs decide for themselves. New wire
  surface -> high bar; goes with the markers slice review, not before.
- Unit namespace (DECIDED): free text on the wire, never rejected -
  syntactic constraints only (no colon: it is the separator; no whitespace;
  printable ASCII; short cap). The reason to know a unit is to RENDER it,
  not to police it: scaling is knowledge only the renderer can use. So the
  knowledge lives in a compact built-in table next to the synthesizer
  (a dozen entries, not a config file - exotic wants a hand-written gdef,
  which already wins), mapping well-known units to rendering hints:
  base ("B", "B/s" -> --base 1024, everything else 1000), SI autoscale
  on/off (on for scalable quantities, off for "%"/counts/ratios where
  rrdtool would print "0.9 k"), and spelling aliases ("msec"->"ms",
  "bytes"->"B") applied at render time for grouping and axes - the wire
  keeps what the producer said. An unknown unit is fully legal: verbatim
  axis label, byte-exact grouping, default rendering. So nobody has to fix
  a "bad" unit: a known unit renders smartly, an unknown one renders
  plainly, and both work. Grouping is byte-exact AFTER alias
  normalization. Our own emitters and docs use the canonical spellings
  (SHOULD, not MUST). Table entries are addable without ever touching the
  wire contract.
- Gdef scaffold mode (DECIDED, ~20 lines): the runtime synthesizer
  (synthetic_gdef/synthetic_defs) IS the generator - add a print mode
  (showgraph --emit-gdef <name>) that writes the synthesized block for the
  admin to capture into graphs.d/ and customize. One-shot scaffold, never a
  sync: once edited the file is the admin's (hand-written already wins).

## Threshold rendering (rendering half IMPLEMENTED; alert half gated on #218)

Implementation status: the writer parses THRESHOLD: declarations (grammar
below, validated against the block's DSes, invalid lines ignored with a
debug note) and records the relations on the fileset-index entry
("t=base:relop-operand:sev,..."). The renderer derives: threshold-DS
operands never plot as peer metrics; on a single-instance image they
co-plot threshold-styled (warn yellow, crit red) and literal operands
become HRULEs; THRESHOLDS ON|OFF in graphs.cfg (meta-only section works,
INCLUDE inherits) is the admin's say; --emit-gdef scaffolds it all.
NOT yet implemented: the alert derivation (a generic DS-vs-DS rule in the
#218 engine, under analysis.cfg precedence) - blocked on #218 itself.

- A metric's thresholds have two origins with two owners, and each gets its
  own mechanism - never mixed:
  - Producer-emitted threshold metrics (the usual case: the producer emits
    read_ms AND read_ms_warn): the VALUE is the producer's policy, but the
    RELATION - "this DS is the warn level of that DS" - is a fact only the
    producer knows, so it is declared in the METRICS block on its own line:
    "THRESHOLD:<base-ds>:<relop><threshold-ds>[:<severity>]" - severity
    warn|crit, default crit. The line speaks the BLOCK's dialect, not
    analysis.cfg's: colon fields with a keyword prefix and an optional
    trailing field, the same shape as a DS line (one separator for the
    whole line - the unit decision's principle). Severity is the generic
    warn|crit, NOT a xymon color: the wire is backend-neutral (the COMPUTE
    exclusion's reason), and yellow/red is one consumer's vocabulary - the
    xymon consumer maps warn->yellow, crit->red, exactly as it derives
    YAXIS from the unit; analysis.cfg keeps speaking colors on its own
    layer. The comparison operator IS kept, glued to its operand as one
    token (">resp_ms_warn") - comparison is universal, not xymon-specific,
    and #218 ingests it directly. One severity may appear in multiple
    relations per base metric - a temperature with low+high warn and
    low+high crit is four lines ("<temp_lo_crit", "<temp_lo_warn:warn",
    ">temp_hi_warn:warn", ">temp_hi_crit"), rendering as an operating band.
    The literal form is INCLUDED (promoted from held-back): the operand
    slot holds either a declared DS name or a number - one grammar, not a
    second mechanism. Resolution: a token naming a DS declared in the same
    block is a threshold curve; else a number is a literal; else the line
    is ignored (the parser's silent-ignore convention). The two forms
    differ semantically, and that difference IS the producer guidance:
    a literal is BLOCK-WIDE (one value for every instance - per-instance
    levels must be DSes) and has NO history (a changed literal moves the
    flat line for all time; a constant DS shows the step); in exchange it
    costs nothing - no DS in every file, no value on every instance line,
    no U case. Per-instance or evolving -> threshold DS; universal and
    static -> literal. Mixing forms on one base metric is normal (dynamic
    warn curve + fixed crit literal). A literal renders as a flat HRULE;
    persistence is uniform - the fileset index carries the full relation
    (relop, operand, severity) either way, a literal is just a relation
    with no DS behind it.
    Example: "THRESHOLD:read_ms:>read_ms_warn:warn". The line declares ONLY the
    relation - never a display instruction (display belongs to the graph
    side, same reasoning that put MAXINSTANCESPERIMAGE/TRENDS/STALE in
    graphs.cfg, not in the block). Rendering is DERIVED from the fact,
    exactly as YAXIS is derived from the unit: the synthetic gdef's default
    is to plot the threshold DS on its base metric's image, threshold-styled
    (LINE from its DEF - a curve with history, better than any flat rule),
    excluded from instance counting/aggregation; a hand-written gdef wins
    and may show it separately, differently, or not at all. A renderer that
    ignores the line entirely still stores and shows the threshold DS as an
    ordinary curve - storage is unconditional, co-plotting is derivation.
    New wire surface -> high bar; same review gate as the rest of the
    dialect.
    NOT a naming convention ("*_warn" suffix magic): names cannot carry the
    relation reliably - false positives (log_warn = a count of warning
    lines, silently demoted to a threshold line with no producer opt-out),
    false negatives (rrdtool caps DS names at 19 chars, so exactly the
    longer names truncate and the pairing breaks silently), it grows into a
    name-encoded mini-language (crit, multiple levels, direction, ambiguous
    base-name stripping), and it retroactively reinterprets every existing
    file that happens to match. A fact is stated, not inferred. The
    convention survives as a SPELLING habit: our emitters name the DS
    read_ms_warn AND declare it - the name for humans, the line for
    machines.
  - analysis.cfg thresholds: policy, stays server-side, never on the wire.
    The renderer asks the rule engine what applies to (host, metric) and
    draws HRULEs (flat lines; a TIME-conditional rule renders its currently
    effective level). Gated on RFC #218's unified rule engine - re-parsing
    analysis.cfg inside showgraph would be a second, drift-prone matcher.
  Deep-pass amendments (candidate, with the rest):
  - Precedence - the honest answer to "is this policy on the wire?": relop +
    severity IS a rule, so the declared form is the producer's DEFAULT, never
    the last word. In the #218 engine, analysis.cfg matches first (first-match,
    as always); the declared rule fires only when server policy says nothing
    about that metric. The exact alerting mirror of "hand-written gdef wins".
    Without this, two rule sources fire independently - the contradiction
    trap this doc warns about elsewhere.
  - Multi-instance images: co-plot thresholds ONLY when the image shows a
    single instance. With MAXINSTANCESPERIMAGE > 1, per-instance threshold
    curves belong to different instances and drown the image - such images
    behave as THRESHOLDS OFF; a hand-written gdef can still do anything.
  - Unknown values: a threshold DS at U makes its rule SILENT (no alert,
    gap in the curve). No path may compare U as 0 - that would fire every
    "<" rule the moment a producer misses a baseline cycle.
  - Severities: warn|crit only, nothing else - a crossing that means
    "fine" is not a threshold. The color mapping (warn->yellow, crit->red)
    lives in the xymon consumer, never on the wire.
  - Scope: both operands name DSes declared in the SAME block. Cross-file
    references stay out (that is analysis.cfg/#218 territory). In-block
    scope buys timestamp coherence: metric and thresholds land in the same
    RRD write, so evaluation always compares same-cycle values - a DS rule
    across two files can race a collection cycle, the declared form
    structurally cannot.
  - Precision: threshold DSes are not "excluded from instance counting"
    (instances are lines; counting never saw DSes) - the real exclusions
    are: the synthetic gdef must not plot them as peer metrics, and
    aggregates over "all DSes of a fileset" must skip them.
  The GRAPH marker is not involved: it answers "which images belong to this
  status, how many instances" - graph CONTENT is always derived server-side
  from facts + gdefs, so thresholds never touch it. A producer with a FIXED
  level needs no separate mechanism either: a per-instance or evolving
  fixed level is a constant-valued threshold DS (the step stays visible
  when it changes); a universal static one is a literal operand
  ("THRESHOLD:read_ms:>200:warn") in the same grammar slot. So the entire
  wire surface for thresholds is ONE line in ONE marker: THRESHOLD in the
  METRICS block.
  Whether the threshold is PLOTTED is the admin's say, not the producer's -
  the declaration never forces a pixel. Control points, coarse to fine:
  the synthetic gdef co-plots by default (most people want to see what
  would alert); a per-graph display keyword in graphs.cfg - THRESHOLDS
  ON|OFF, default ON, next to MAXINSTANCESPERIMAGE/TRENDS/EXSTALEPATTERN -
  suppresses the threshold curves without writing a full gdef; a
  hand-written gdef has the last word (pick, style, or split them onto
  their own image). Possible later: a &nothresholds URL toggle in
  showgraph (per-view, same family as &nostale) - not needed for the
  model to be complete.
  Both compose on one image. Bonus: the declared THRESHOLD relation is
  exactly what #218 wants too - "alert when a metric crosses its declared
  threshold metric" becomes a generic DS-vs-DS rule instead of per-handler
  hardcoding. One declaration, two consumers (graph and alert), neither
  owns it - the marker doctrine working as intended.

## Display-window keywords (IMPLEMENTED)

- The staleness window is ONE fixed number: XYMON_STALE_WINDOW = 86400,
  main's historic hardcoded value, governing BOTH the showgraph
  stale-file filter and the fileset-index paging counts - one window,
  so the count always equals what renders. It is deliberately not
  configurable: twenty years of main validate the value, and the
  update-cache write lag (~1h) floors any meaningful window anyway.
  (An earlier per-graph STALEAFTER keyword was replaced before ever
  shipping: a per-graph window extended the lifetime of a graph's DEAD
  instances along with its periodic ones.)
- EXSTALEPATTERN <regex>, per graphs.cfg block, INCLUDE inherits
  own-wins: instances the pattern matches are exempt from staleness -
  never filtered, always counted. Per-instance precision for
  legitimately periodic instances (weekly backup mounts), named in the
  storage-pattern family grammar (EX = excluded from the action).
  An exempted instance that is truly gone stays until its file is
  deleted (the file is findable: its mtime stops moving).
- Meta-only graphs.cfg sections (THRESHOLDS, EXSTALEPATTERN and
  friends with no definition lines) keep the synthesized graph: the
  renderer adopts the synthetic scaffold for whatever the section did
  not write. This was documented before it worked - the original
  meta-only test greppped the Content-type header, which an errored
  render also prints; the assertion is now strengthened.
## File schema evolution (IMPLEMENTED: the rrdreconcile tool)

Implementation status: xymond/rrdreconcile walks $XYMONRRDS and compares
every RRD file against the current declarations - archive CFs from the
gdef meta scanner (xymon_gdef_cfs_forfile, the same derivation the
writer applies at creation) and heartbeats from the fileset index's h=
records (the writer captures colon-field 4 of every declared DS spec as
a complete, strong-replace "h=ds:hb,..." record). Divergence is repaired
with "rrdtool tune": --heartbeat per mismatched DS, and per missing CF a
clone of every AVERAGE archive's geometry (the shape the writer would
have created). Dry-run by default, --apply executes; adding archives via
tune needs rrdtool >= 1.5. Run it manually or from cron after changing
graphs.cfg DEFs or producer DS specs - the daemon never mutates existing
files on its own.

The archive-consolidation derivation (above) and the heartbeat re-tune
watch-item (below) are the same mechanism: a declaration changed
AFTER file creation, and the file must be reconciled with it - a
tune-pass, not two ad-hoc patches.
DECIDED semantics for late-added archives: consolidation functions are
mutually unrecoverable (PDPs are folded and discarded - stored AVERAGE
cannot yield MAX, and seeding MAX from AVERAGE data is averages wearing
a MAX label), so an RRA added by the tune-pass fills FORWARD ONLY: no
backfill, no approximate seeding, a documented history gap on a rare
event. Both CFs in one file is standard and useful - identical at the
finest resolution, divergent where buckets aggregate (a daily AVERAGE
erases a 30-minute spike; MAX preserves the worst moment), which is
exactly what a gdef reading DEF:...:MAX declares it needs.
Heartbeat, same decided shape, even milder: it stores nothing (a
validity rule applied at update time), rrdtool tune changes it
instantly and losslessly for the FUTURE, and only the past
interpretation is fixed - gaps already marked UNKNOWN under the old
heartbeat stay UNKNOWN (the raw samples are gone). Forward-only, rare,
accepted. No dual-value concept exists for heartbeat.

## Risks / watch-items

- Second consolidation review: deferred findings, each with its reason.
  (a) RESOLVED - schema fields now carry a declaration timestamp (g=):
  a live declaration stamps now(), the flush merge adopts a NEWER
  on-disk bundle outright and ignores an older one (g-less legacy
  entries keep the weak fill), so a stale writer converges in one flush
  instead of ping-ponging its old spec back. (b) RESOLVED - event time
  and commit time are split: fsidx_note_schema records the entry and
  its declarations when the sample is processed, fsidx_note_commit
  advances freshness only after rrdtool ACCEPTS the batch (every flush
  path funnels through flush_cached_updates), so a chronically rejected
  producer goes stale on schedule. (c) RESOLVED - the tsearch-variant
  xtreeDestroy now collects every record via twalk and tdelete-frees the
  internal nodes and wrappers (it used to free only the handle); keys
  and userdata stay caller-owned. ASan-verified for both tree variants.
  (d) RESOLVED - the writer keeps a 300s drop barrier: drophost (and a
  renamed-away old name) discards straggler status/data messages for
  the host, and the host's cached updates are purged before the forked
  deletion starts (renames flush them into the old-named files first,
  preserving the data). This closes the whole recreate-inside-the-dying-
  dir race family, index included. (e) Scan-seeded entries use file
  mtime, which is stamped at FLUSH time - so a rebuilt index can show an
  entry FRESHER than its newest data by up to a cache-flush delay.
  Bounded, and self-corrects at the first committed update; accepted.
  (f) RESOLVED - the derived axis now requires every selected file of
  the fileset to declare the same canonical unit; any disagreement (or
  an undeclared file) falls back to the generic axis, which never lies
  about a curve. --emit-gdef still scaffolds from the first file: it is
  a one-shot template for the admin to edit, not a rendering.
  (g) --emit-gdef picks the first host readdir yields for its index
  lookup - documented, nondeterministic across hosts by design.
- PARTLY RESOLVED - update-cache churn: entries idle beyond 6h are
  evicted hourly (pending values flushed first - the cache is pure
  batching, so eviction loses nothing). Remaining growth is only the index entries themselves (one
  small line per instance ever seen), plus xtree tombstones per
  eviction. The original note, for the record:
- (historical) Unbounded state under instance churn (accepted for now; revisit before
  SNMP-scale collectors land): the RRD update cache and the fileset-index
  tree keep one entry per instance name EVER seen - upstream updcache
  behaviour, now reachable by any sender via content routing. Steady
  fleets are bounded; ephemeral names (container overlay mounts, rotating
  ids) grow both trees monotonically for the process lifetime. The AGGDS
  store is gated by rule-relevance (only dataset names some rule
  aggregates are stored); a comparable eviction policy for
  updcache/index entries needs a design decision (TTL? LRU cap? drop on
  fileset-index expiry?), not a quick patch - do not bolt one on without
  deciding what a "forgotten" instance means.
- Declared heartbeats only act at file creation: the DS heartbeat lives in
  the RRD file once created, so a producer changing its DS:<hb> declaration
  affects new files only - existing files need an rrdtool tune pass. Either
  the writer detects the mismatch and tunes, or the limitation is documented;
  silently ignoring the new declaration is the one wrong option.
- IMPLEMENTED - instance sort order: rrd_name_compare is now ONE
  version-aware total order (web/namecompare.inc.c, unit-tested for the
  measured counterexamples below): digit runs compare numerically with a
  leading-zero tie-break, everything else byte-wise. Plain integers,
  OID/version keys and names all sort stably; the historical notes stay
  for the record:
- (historical) showgraph's rrd_name_compare knew only two regimes -
  pure-integer keys (numeric sort) and everything else (case-sensitive strcmp).
  Multi-component numeric keys sort wrongly: strcmp puts "1.10.1" before
  "1.2.1", the reverse of OID/version order. Harmless today (stock instances
  are names or plain integers), but once instances are arbitrary keys announced
  by a METRICS block (SNMP collectors -> OIDs, composed indexes), display order
  and first/count paging stability depend on this comparator. Before that
  lands, replace it with ONE version-aware compare (split on separators,
  compare digit runs numerically, strcmp fallback per component - strverscmp
  semantics): it subsumes all three cases (plain integers unchanged, OIDs
  fixed, names unchanged), so it is a drop-in replacement, not a new special
  case. Two hard requirements, both violated by the current comparator: it
  must be a TOTAL ORDER - (a) a digit run always compares numerically
  regardless of the partner key; the current per-pair numeric-or-strcmp
  choice is intransitive (9 < 10 < "1a" but 9 > "1a" directly), so qsort's
  result depends on readdir order - measured: the three permutations of
  {9, 10, 1a} produce three different "sorted" outputs; (b) distinct keys
  must never compare equal ("007" vs "7" returns 0 today) - numerically
  equal components need a strcmp tie-break, otherwise their order, and the
  first/count slice containing them, is unspecified.
