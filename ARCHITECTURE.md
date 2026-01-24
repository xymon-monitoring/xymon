
No reverse dependency is allowed.


xymon_common Library
--------------------

### Role

`xymon_common` contains:
- generic utilities,
- shared client/server code,
- functionality independent of server mode.

It must **never** depend on server-specific code.

### Current Scope

- memory, string, time, and hash utilities
- generic data structures
- shared IPC and buffer handling
- non-server-specific logging code

### Files (B6.x)

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
- reportlog.c  

### Constraints

- no server network access
- no server business logic
- stable and reusable API


xymon_server_core Library
------------------------

### Role

`xymon_server_core` contains **only**:
- Xymon server-specific code,
- modules requiring server mode,
- server business logic.

It may depend on `xymon_common`.

### Current Scope (B6.x)

- server stubs
- server logging logic
- HTML and notification handling

### Files (B6.x)

- server_stub.c  
- eventlog.c  
- notifylog.c  
- htmllog.c  

### Evolution Rules

- any migration from `xymon_common` must be:
  - explicit,
  - documented,
  - done in small, controlled batches (B6.5).
- no new server code may be introduced elsewhere.


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
- no functional extension
- internal use only (test / CI)


Global Architecture Rules
-------------------------

- strictly unidirectional dependencies
- no server code in `xymon_common`
- any new separation must:
  - keep the build green,
  - be introduced via atomic commits,
  - comply with this architecture


Status
------

- Architecture frozen at **B6.x**
- Next step: **B6.5 – controlled migration of non-log server modules**
- This document must be updated **only** when a validated structural change occurs.

