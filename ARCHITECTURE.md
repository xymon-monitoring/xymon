ARCHITECTURE.md
===============

No reverse dependency is allowed.


xymon_common Library
-------------------

### Role

`xymon_common` contains:
- generic utilities,
- shared client/server code,
- functionality independent of server mode.

It must **never** depend on server-specific code.

### Current Scope (B6.5 baseline)

- memory, string, time, and hash utilities
- generic data structures
- shared IPC and buffer handling
- low-level helpers reusable by client and server

### Files (B6.5)

- errormsg.c
- tree.c
- memory.c
- md5.c
- strfunc.c
- timefunc.c
- digest.c
- encoding.c
- calc.c
- misc.c
- msort.c
- files.c
- stackio.c
- sig.c
- suid.c
- xymond_buffer.c
- xymond_ipc.c
- matching.c
- timing.c
- crondate.c

### Explicit Exclusions

- no server business logic
- no server networking
- no configuration loaders
- no ownership of server data structures

### Constraints

- stable and reusable API
- suitable for client and server builds
- must remain dependency-clean


xymon_server_core Library
------------------------

### Role

`xymon_server_core` contains:
- Xymon server-specific code,
- modules requiring server execution context,
- server-owned behavior.

It may depend on `xymon_common`.

### Current Scope (B6.5 baseline – adjusted)

- server bootstrap / stub
- server logging logic
- server-side configuration loaders required at startup

### Files (B6.5)

- server_stub.c

**Logs**
- eventlog.c
- notifylog.c
- htmllog.c
- reportlog.c

**Configuration loaders (server-scoped, TRANSITIONAL)**
- loadalerts.c
- loadcriticalconf.c
- loadhosts.c

These loaders are included **temporarily** and were subject to
explicit dependency analysis (B6.6).

They are included **only as long as**:
- they depend exclusively on `xymon_common`,
- they do not introduce runtime network protocol handling,
- they do not transfer ownership of core server data structures.

### Loader analysis references

- loadhosts.c  
  → docs/architecture/loaders/loadhosts.md
- loadalerts.c  
  → docs/architecture/loaders/loadalerts.md
- loadcriticalconf.c  
  → docs/architecture/loaders/loadcriticalconf.md

### Loader Migration Status (B6.6)

- loadhosts.c  
  Classification: NON-EXTRACTABLE  
  Reasons:
  - direct inclusion of .c submodules
  - reliance on server-global mutable state
  - indirect network access via loader components

- loadalerts.c  
  Classification: CANDIDAT EXTRACTABLE  
  Notes:
  - depends on xymon_common and libpcre
  - no network access
  - no direct runtime I/O
  - eligible for future isolation in xymon_server_loaders

- loadcriticalconf.c  
  Classification: NON-EXTRACTABLE  
  Reasons:
  - persistent configuration writes
  - clone and alias management
  - shared global configuration state

### Explicit Exclusions (B6.5)

- network protocol handling
- runtime daemon logic
- client-side behavior
- cross-mode shared ownership of server data

### Constraints

- server-only semantics
- no reverse dependency into `xymon_common`
- loader code must remain dependency-auditable

### Evolution Rules

- any migration into or out of `xymon_server_core` must be:
  - explicit,
  - documented,
  - dependency-complete,
  - reversible.
- partial or speculative moves are forbidden.
- failed migrations must be reverted immediately.


xymond_channel Binary
--------------------

### Role

`xymond_channel` is a **minimal validation binary**.

It is used to:
- verify correct linkage,
- validate dependency consistency,
- serve as a CI anchor.

### Constraints

- no business logic
- no functional behavior
- internal use only (test / CI)


Global Architecture Rules
------------------------

- strictly unidirectional dependencies
- `xymon_common` must never include server semantics
- `xymon_server_core` owns server-only behavior
- loaders may reside in server core **only if dependency-clean**
- all changes must:
  - keep the build green,
  - be atomic,
  - be architecture-compliant


Status
------

- Architecture baseline: **B6.5**
- Baseline validated by CI
- Loader analysis completed (B6.6)
- Loader classification frozen
- Next phase (optional): **B6.7 – isolate loadalerts.c**
- This document is updated **only after validated structural changes**

