Module: loadalerts.c

Includes:
- <sys/types.h>
- <sys/stat.h>
- <ctype.h>
- <stdio.h>
- <string.h>
- <unistd.h>
- <stdlib.h>
- <time.h>
- <limits.h>
- <errno.h>
- <pcre.h>
- libxymon.h

External symbols (selected):
- errprintf
- dbgprintf
- traceprintf
- logprintf
- xfree
- newstrbuffer
- freestrbuffer
- addtobuffer
- clearstrbuffer
- sanitize_input
- stackfopen
- stackfclose
- stackfgets
- stackfmodified
- stackfclist
- compileregex
- namematch
- timematch
- durationvalue
- durationstring
- parse_color
- colorname
- getcurrenttime
- xgetenv
- hostinfo
- localhostinfo

Data access:
- Structures: rule_t, recip_t, criteria_t, activealerts_t
- Builds and mutates alert rule chains (rulehead, recipients)
- Allocates and frees nested criteria and recipient structures
- Manages token tables and rule state machines
- Uses PCRE compiled regex objects (pcre*)

Files:
- Reads configuration file alerts.cfg (via stackf*)
- No direct file writes

Runtime access:
- Network: no
- Sockets: no
- Server loop: no
- Daemon runtime: indirect (alert matching invoked at runtime)

Coupling:
- Strong dependency on server alert model and host metadata
- Uses host resolution and metadata via hostinfo()
- Heavy use of global server state (rules, tokens, modes)
- Depends on libpcre

Status:
- TRANSITIONAL (B6.5 -> B6.6)

