/* Harness for tests/web/reportlog-event-count.sh.
 *
 * Calls the real generate_replog() (lib/reportlog.c) with a report whose
 * six colors each carry a different event count, and writes the HTML
 * availability report to stdout for the test to read.
 */
#include <stdio.h>
#include <string.h>

#include "libxymon.h"
#include "reportlog.h"

int main(void)
{
	reportinfo_t repinfo;

	memset(&repinfo, 0, sizeof(repinfo));
	repinfo.fstate = "OK";
	repinfo.fullavailability = 99.0;
	repinfo.count[COL_GREEN]  = 7;
	repinfo.count[COL_YELLOW] = 3;
	repinfo.count[COL_RED]    = 5;
	repinfo.count[COL_PURPLE] = 2;
	repinfo.count[COL_CLEAR]  = 4;
	repinfo.count[COL_BLUE]   = 6;
	reploghead = NULL;

	generate_replog(stdout, NULL, "text.txt", "h1", "http", COL_GREEN, 0,
			"127.0.0.1", "h1", 0, 86400, 97.0, 99.995, -1, &repinfo);
	return 0;
}
