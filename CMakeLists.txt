# ---- xymon_common: shared utilities + loaders (non isolables) ----
add_library(xymon_common STATIC
    lib/errormsg.c
    lib/tree.c
    lib/memory.c
    lib/md5.c
    lib/strfunc.c
    lib/timefunc.c
    lib/digest.c
    lib/encoding.c
    lib/calc.c
    lib/misc.c
    lib/msort.c
    lib/files.c
    lib/stackio.c
    lib/sig.c
    lib/suid.c
    lib/xymond_buffer.c
    lib/xymond_ipc.c
    lib/matching.c
    lib/timing.c
    lib/crondate.c

    # loaders (B6.5: not yet isolable)
    lib/loadhosts.c
    lib/loadhosts_file.c
    lib/loadalerts.c
    lib/loadcriticalconf.c
)

# ---- xymon_server_core: server-only, clean boundary ----
add_library(xymon_server_core STATIC
    lib/server_stub.c
    lib/eventlog.c
    lib/notifylog.c
    lib/htmllog.c
    lib/reportlog.c
)

