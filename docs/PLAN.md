# PLAN — self-describing metrics: homogeneous tests, tables, group aggregator

Target branch: **feature/self-describing-metrics** (this file was written from the
solo-dashboard worktree; move/commit it onto that branch). Scope: evolve the
branch so a test is a small **homogeneous** unit, add a **table** display element,
and add a **testgroup** (a display-only aggregator of tests) — reusing existing
machinery, with minimal new surface.

Legend: **[V]** verified in code (file:line). **[P]** proposal. **[O]** open / untraced.

---

## 0. Vocabulary (pinned — decided 2026-07-24)

- **test** = the atomic status object (`host.testname`, one `xymond_log_t`):
  **one color → one alert → one ack.** The homogeneous atom (`smarttemp`, `smartsata`).
  Stock Xymon (core code + user-facing docs/man pages) calls this a **"column"** — kept
  as the legacy synonym there; new branch code/comments say **test**.
- **testgroup** = a **display-only aggregator** of multiple tests → one matrix light +
  one merged detail page. COMPACT-style, emits no status/alert/ack of its own.
  (Earlier drafts called this "column"/"GROUP".) Stock has no status-level equivalent.
- **metric** / **ds** = one DS (a curve). **instance** = one RRD file.
  **graph** = a graphs.cfg definition (gdef). **image** = one rendered slice.
- CAVEAT: where this PLAN quotes stock code/man (`per-column ack`, `AGGDS column`,
  pagegen "column"), "column" = the stock status unit = a **test** here.

## 1. Core model

A test **is a relation**: rows = instances, columns = DS/metrics, cells = values,
predicate = THRESHOLD, key = the instance. The three goals are relational
operators over it — and the branch already ships most, uncomposed:

| View | Goal | Existing machinery | Status |
|---|---|---|---|
| relation (schema+rows) | — | `METRICS` block → per-(instance,DS) RRD; rows re-parseable from `restofmsg` | **[V]** do_devmon.c; htmllog.c:470 |
| catalog | — | fileset index stores `ts,u=,h=,d=,t=` (no values) | **[V]** filesetindex flush |
| PROJECT-over-time → **graph** | — | showgraph + synth gdef; THRESHOLD drawn as HRULE/LINE | **[V]** showgraph.c:1325 |
| SELECT + color → **table** | 2 | value in `restofmsg`; predicate in fsidx `t=` | gap ↓ |
| AGGREGATE → color → **rollup** | 3 | `AGGDS` collapses a fileset to one color, per-metric-scopable | **[V]** client_config.c |
| worst-of rollup **column** | 3 | `COMPACT` synthetic worst-of column (display-only) | **[V]** loaddata.c:448 |
| JOIN members on instance → **virtual test** | 3 | — | gap ↓ |

Element set = the **closed set of relational views** (table=SELECT, graph=PROJECT,
rollup=AGGREGATE, aggregator=JOIN). Fixed on purpose — no fifth operator, no plugin surface.

---

## 2. Wire surface (producer)

One fact-marker only: **`METRICS`** (schema + units + values + `THRESHOLD` lines),
**name-optional** (defaults to the test). `GRAPH` optional. Everything else is
server-side derivation or test.cfg view config. No new markers. No presentation on the wire.

---

## 3. Goal 1 — Homogeneous tests  (cheap; policy + tiny parser change)

- **[V]** Heterogeneity is emergent from allowing >1 `METRICS` block per test
  (marker parser returns a list; `do_devmon` writes per-block files; htmllog renders
  one graph per marker in order). It is NOT a subsystem to delete.
- **[DONE 65aa7d3ba]** Doctrine (design doc): **one homogeneous `METRICS` block per test**
  (the atomic status unit — one color/alert/ack); a **testgroup** then aggregates multiple
  tests (display-only, §5). Also fixed the stale `testcfg.h` "does not yet reroute" comment.
- **[DONE 751dc2f07]** `METRICS` **name optional** — marker is now `<!--XYMON METRICS`
  with optional `: <name>`; unnamed defaults to the test name. Shared helper
  `xymon_metrics_marker()` (display parser + block writer agree); present-but-invalid
  name still falls back to the built-in handler; `xymon_markers_parse(msg, defname)`;
  proving tests added. **`GRAPH` name-optional also DONE (1c, 65aa7d3ba)**: absent name or
  an attribute-only `instances=` token → default to the test; invalid name → ignored.
- **[P]** Leave the multi-marker render loop **dormant** (lower risk than trimming).
- **[V]** Only consumer of the parser is `htmllog.c:470` (passes `service`); no consumer
  assumes marker-name ≠ test.

---

## 4. Goal 2 — Table element  (self-contained; also realizes #218 Phase 1)

**[V] Correction that drives this:** the METRICS-block `THRESHOLD` is *declaration-only*.
Parsed at `do_devmon.c:232` → `fsidx` `t=` field; the ONLY consumer is
`showgraph.c:1325` (draws reference lines). **No per-instance verdict is computed
anywhere.** Per-cell alarm colors do not exist today.

**[DONE 7bd8a627b — 2a] The engine:** `threshold_eval(spec, ds, value, getval)` in
`lib/threshold.{c,h}` returns the worst firing severity (ok/warn/crit); relops
`> < >= <=`, numeric or DS-name operands, sev defaults to crit; self-contained,
18-assertion unit test. It serves **both**:
1. the **table**: rows from the `METRICS` block in `restofmsg`, cols = DS, each cell
   coloured by evaluating its value against `fsidx_thresholds()`.
2. **#218's shared `evaluate()` engine (Phase 1)**: the same function is exactly the
   compare/color helper #218 wants extracted into `lib/`, and it turns the declared
   THRESHOLD into an actual evaluated rule — the "second consumer" the branch's doc
   anticipated but never wired. (Realizes #218 Phase 1; does not close the RFC.)

- **[V]** Values source: re-parse the `METRICS` block out of `restofmsg` (present at
  render time, raw, no color). Fileset index stores predicates + units, **not values**.
  No RRD last-value accessor on the web path.
- **[DONE 055a05006 — 2b]** `render_metrics_table()` in `lib/htmllog.c`: each METRICS
  block is replaced **in place** (Bruno's call — not a separate slot) by an HTML table,
  rows=instances, cols=DS, cells coloured by `threshold_eval`. Parsed entirely from
  `restofmsg` (no fsidx/RRD reads); DS-vs-DS operands resolved per row. Proving test:
  warn/crit thresholds → yellow/red cells. Custom column format = **2c** (test.cfg), deferred.
- **[RESOLVED]** Evaluator runs at **display** (htmllog), reading the block straight from
  `restofmsg` — the message carries schema + THRESHOLDs + values, so no ingest-time state.

---

## 5. Goal 3 — testgroup aggregator  (web-layer only; DISPLAY-ONLY is mandatory)

**[V] Decisive constraint:** the testgroup must be **display-only (COMPACT-style),
never a real status column (combostatus-style):**
- A real `status host.testgroup` would generate its OWN alert on top of members'
  (`xymond.c:1832` posts to pagechn for any alert-color status) → **double-alert**,
  and would need its own ack.
- `COMPACT` lives **entirely in xymongen**, emits no status to xymond
  (`XMH_COMPACT` absent from `xymond/`) → **no own alert, no own ack**; members keep
  owning status/history/alert/ack.
- Ack is keyed per (host,column) cookie (`xymond.c:4341/4355`); there is **no**
  group-ack concept — another reason members, not the group, are the ack unit.

⇒ **The aggregator is a web-layer feature. No xymond / alerting / acking changes.**

**[P] Realization = COMPACT rollup light (unchanged) + retarget its link to a merged page:**
- Matrix light: reuse COMPACT **unchanged** — one worst-of light for the group
  (`loaddata.c:448–470`, inserted via `gen_column_list`, cell at `pagegen.c:546–612`);
  member tests stay hidden from the matrix (`e->compacted=1`, `xymongen.h:127` /
  `pagegen.c:229`). Members do **not** get their own matrix column. No inline
  matrix expand/disclosure (dropped — fiddly, not needed).
- **[P] The one change:** retarget the light's link (`pagegen.c:604`) from the synthetic
  column to the **testgroup detail page** — a SINGLE HTML page that MERGES the member tests
  (the instance-JOIN table + their graphs). Keep the member list (COMPACT discards it
  after `generate_compactitems`) so that page can render them.
- Detail page: one page = the **JOIN of member tables on the instance key** (Goal-2 table
  per member) + their graphs + a rollup band. Join-key stability relies on the branch's
  **reversible instance encoding** (KEEP item) so `sda` == `sda` across members.
- Rollup color: reuse AGGDS / COMPACT worst-of.
- Home: a **`testgroup`** block in test.cfg. **[DONE 6125c262a — 3a]** `group <name>
  { member <test>…; rollup worst }` parses into `tc_group_t`; API `testcfg_groups()` +
  `testcfg_group_of(head, test)`.
- **[DONE — 3b]** Merged group page in `web/svcstatus.c`: `SERVICE=<group>` is matched
  against `testcfg_groups()` (`do_request`), then `generate_group_page()` fetches each
  member live (`xymondlog host=H test=M fields=color`, worst-of rollup), emits the page
  frame + a status band of the members, then stacks each member's full content below via
  the frameless `generate_html_log` (`htmllog_frameless=1`) — so each member renders its
  own METRICS table + graphs. Compile+link verified; the live xymondlog path needs a
  running xymond, not covered by the local suite.
- **[DONE — 3c]** Matrix rollup light in `xymongen/loaddata.c`: `generate_groupitems()`
  (called after `generate_compactitems()`) mirrors the COMPACT machinery but is driven by
  `testcfg_groups()` instead of the per-host `COMPACT` tag. For each host it worst-of's the
  member entries, sets `e->compacted=1` (members drop out of the matrix), and injects one
  synthetic column named `group->name`. No `pagegen` change needed: the synthetic column's
  light already links via `hostsvcurl(host, group->name)` → `svcstatus.cgi?SERVICE=<group>`,
  which `do_request()` now routes to `generate_group_page` (3b). Compile+link verified;
  matrix layout is integration-level (no isolated xymongen board harness), same caveat as 3b.
- **[O]** The merged detail page is net-new rendering — no existing multi-test merged page to reuse.
- **[O]** Dynamic (CGI) host-matrix path, if any, not traced; static `xymongen` authoritative for layout.
- **[DECIDED 2026-07-24]** Model (a): the **test** is the alert/ack atom — one test →
  one status → one alert → one ack. A **testgroup** (display-only aggregator)
  aggregates tests. A test that belongs to a testgroup is **hidden from the matrix**
  (COMPACT `e->compacted=1`); only the testgroup light shows. A standalone test (in no testgroup)
  still shows its own light. Tests stay real status units, individually alertable/ackable;
  the testgroup adds **no** status/alert/ack of its own (hence display-only).

---

## 6. New code — exactly two pieces

1. `evaluate(value, THRESHOLD) → verdict` — powers table cell colors AND is #218's shared evaluate() engine (Phase 1).
2. the **instance-JOIN render** + **COMPACT hide→expand** (member-list field + disclosure).

Everything else = composition + test.cfg view definitions + reuse
(METRICS, fileset index, AGGDS, synth gdef, showgraph, COMPACT, reversible encoding, test.cfg nesting).

---

## 7. Sequencing (by verified cost/risk)

1. **Homogeneous** — doctrine + name-default. Trivial.
2. **Table** — the THRESHOLD evaluator + a table render element. Self-contained,
   high payoff (realizes #218 Phase 1), reads only `restofmsg` + `fsidx_thresholds`.
3. **Aggregator** — web-layer only (verified): COMPACT expand + JOIN + test.cfg testgroup.
   Bounded to xymongen/pagegen/loaddata + test.cfg; no xymond changes.

---

## 8. Do NOT

- Do not make a testgroup a real status object (double-alert + ack ambiguity — **[V]**).
- Do not add a marker for the table or for presentation (decompose by fact, not consumer).
- Do not delete the heterogeneity code (dormant, not a subsystem) or test.cfg (config tool + testgroup home).
- Do not put layout/aggregation scope on the wire — server-side (test.cfg / gdef FNPATTERN / AGGDS).

---

## Appendix — verified anchors (avoid re-tracing)

- Marker parser struct/list: `lib/xymonmarkers.h` (`xymonmarker_t`, `blockinstances`, `instancespec`).
- Block writer per-instance file: `xymond/rrd/do_devmon.c:384` `setupfn2("%s.%s.rrd",...)`.
- THRESHOLD declared-only: parse `do_devmon.c:232`; index `t=`; sole consumer `web/showgraph.c:1325`.
- Fileset index stores no values: `lib/filesetindex.c` flush (`ts,u=,h=,d=,t=,g=`); readers `fsidx_units/fsidx_thresholds/fsidx_count_*`.
- AGGDS one-color-per-column, per-metric-scopable: `xymond/client_config.c` (`check_aggds_thresholds`, `modify <host>.<col> ...`).
- xymond status object is flat (one color, no nesting): `xymond/xymond.c:121` `xymond_log_t`.
- Alert decision per-log: `xymond/xymond.c:1832`; `decide_alertstate` `:545`.
- Combo emits real status (would double-alert): `xymond/combostatus.c:446`.
- COMPACT display-only, hides members: `xymongen/loaddata.c:448,460`; skip `xymongen/pagegen.c:229`; hide flag `xymongen/xymongen.h:127`; cell emit `pagegen.c:546,604,609`.
- Ack per (host,column) cookie; host-wide only via negative cookie: `xymond/xymond.c:4341,4344,4355`.
- braceparse nesting: recursive `bp_parse_body`, `BP_MAXDEPTH 64` (`lib/braceparse.c`). testcfg schema: `lib/testcfg.h` `tc_test_t`/`tc_metric_t`.
