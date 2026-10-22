# Plan

Live work items. An item is deleted when it lands; a refused one keeps one line
with the reason.

## Deduplicate the unix client handlers in xymond

Eleven handlers call `unix_cpu_report` and share a spine: aix, darwin, freebsd,
hpux, irix, linux, netbsd, openbsd, osf, sco_sv, solaris.

Twelve sections are fetched by all eleven -- `clock date df ifstat msgcache
msgs netstat ports ps top uptime who` -- so each handler carries 12
declarations and 12 `getdata()` calls, plus `sprintf(fromline...)`,
`splitmsg()`, and six report calls that are universal (`msgs_report`,
`file_report`, `linecount_report`, `deltacount_report`, `unix_netstat_report`,
`unix_ifstat_report`). About 30 invariant lines each, one copy of which stays:
**~300 redundant lines**.

Not invariant, and staying per-handler: the `unix_disk_report` /
`unix_inode_report` column names, the `unix_procs_report` /
`unix_ports_report` arguments, `unix_vmstat_report` (9 of 11),
`unix_inode_report` (8 of 11), and the memory-parsing block, which runs from 25
to 172 lines of genuinely different code.

Shape: `xymond/xymond_client.c` gains a `unix_sections_t` holding the shared
strings, `unix_getsections()` filling it, and `unix_generic_reports()` making
the six universal calls. Nothing that differs between operating systems moves,
so no per-OS escape hatches -- the property that made the client-script
fragments worth doing.

Sequence, two commits:

1. A golden-output test. `xymond_client --local --no-update` reads
   `@@client|...` on stdin and prints every generated status to stdout, which
   `tests/server/disk-unavailable-honest-message.sh` already drives that way.
   One canned message per OS, stdout captured before the refactor, required
   byte-identical after. Worth having on its own: nothing currently pins these
   handlers' output as a whole, and four of them -- irix, osf, sco_sv, hpux --
   have no coverage and no CI lane at all.
2. The refactor.

Out of scope: `zvm`, `zos`, `zvse`, `bbwin`, `mqcollect` do not share the spine.

## Duplication left after the client-script fragments

Measured with the generated regions excluded, so these are hand-maintained:

| what | redundant | note |
|---|---|---|
| the eleven unix handlers | ~300 | the item above |
| `fs-sentinel-{freebsd,netbsd,openbsd}.sh` | ~310 | refused, see below |
| `parse_query()` web x3, `getname()` lib x3 | 28 each | small, crosses modules |
| `cleandir()` web x2 | 19 | |
| `xymond_launch()` server tests x3 | 18 | could move to `tests/lib/` |
| `sigmisc_handler()` x2, `convert_time()` x2, `main()` web x2 | 35 total | |

## Refused

- **Sharing a body between the three BSD sentinel tests** (~310 lines). Closed
  as PR #426's first form. Test locality is worth more than in production code,
  and the sibling `fs-filter-common.sh` needed three escape hatches within one
  release (`FSF_ARGV_EXCLUDE_PSEUDO=''`, a hand-written darwin mount stub,
  `FSF_INODE_FILTERED=no`). Revisit if the same fix ever has to be made in all
  three.
- **Sharing the client scripts at runtime.** `xymonclient.sh` picks one by
  uname and executes it; a sourced fragment is absent for a moment during
  clientupdate and would abort the client on exactly the hosts the remote-df
  sentinel protects. Hence `build/mkclientshared.sh` stamping committed copies
  instead.
