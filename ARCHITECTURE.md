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

`xymon_server_core` contains **only**:
- Xymon server-specific code,
- modules requiring server execution context,
- server-owned behavior.

It may depend on `xymon_common`.

### Current Scope (B6.5 baseline)

- server bootstrap / stub
- server logging logic only

### Files (B6.5)

- server_stub.c

**Logs**
- eventlog.c
- notifylog.c
- htmllog.c
- reportlog.c

### Explicit Exclusions (current state)

- configuration loaders (`loadhosts*`, `loadalerts*`, etc.)
- host/page/tree ownership
- network protocol handling

These modules remain **outside the server core boundary**
until their dependency surfaces are fully isolated.

### Evolution Rules

- any migration into `xymon_server_core` must be:
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
-------------------------

- strictly unidirectional dependencies
- `xymon_common` must never include server semantics
- `xymon_server_core` owns server-only behavior
- loaders must not move without full dependency resolution
- all changes must:
  - keep the build green,
  - be atomic,
  - be architecture-compliant


Status
------

- Architecture frozen at **B6.5**
- Baseline validated by CI
- Next phase: **B6.6 – dependency analysis of loader modules**
- This document is updated **only after validated structural changes**

