Module: loadcriticalconf.c

Includes:
- <sys/types.h>
- <sys/stat.h>
- <string.h>
- <stdlib.h>
- <unistd.h>
- <time.h>
- <limits.h>
- <ctype.h>
- <utime.h>
- libxymon.h

External symbols (selected):
- xgetenv
- errprintf
- dbgprintf
- xfree
- newstrbuffer
- freestrbuffer
- stackfopen
- stackfclose
- stackfgets
- stackfmodified
- stackfclist
- getcurrenttime
- within_sla
- xtreeNew
- xtreeAdd
- xtreeFind
- xtreeDelete
- xtreeFirst
- xtreeNext
- xtreeEnd

Data access:
- Structures: critconf_t
- Builds and mutates critical configuration tree (rbconf)
- Manages clone and alias records using xtree
- Allocates and frees configuration records and keys
- Reads and rewrites critical configuration file

Files:
- Reads critical configuration file (default: etc/critical.cfg)
- Writes updated configuration and backup (.bak)

Runtime access:
- Network: no
- Sockets: no
- Server loop: no
- Daemon runtime: indirect (queried at runtime)

Coupling:
- Strong dependency on server-global critical config state
- Uses shared time and SLA evaluation logic
- Performs persistent configuration writes

Status:
- TRANSITIONAL (B6.5 -> B6.6)

