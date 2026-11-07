/* Harness for tests/web/reportlog-text-*.sh.
 *
 * Calls the real generate_replog() (lib/reportlog.c) for a report with an
 * SLA and two events, the first without a cause, and writes the text report
 * to the file named on the command line. The HTML report goes to stdout.
 */
#include <stdio.h>
#include <string.h>

#include "libxymon.h"
#include "reportlog.h"

int main(int argc, char *argv[])
{
	reportinfo_t repinfo;
	replog_t ev1, ev2;
	FILE *textrep;

	if (argc != 2) { fprintf(stderr, "usage: %s TEXTREPORT\n", argv[0]); return 2; }
	textrep = fopen(argv[1], "w");
	if (!textrep) { fprintf(stderr, "cannot write %s\n", argv[1]); return 2; }

	memset(&repinfo, 0, sizeof(repinfo));
	repinfo.fstate = "OK";
	repinfo.withreport = 1;
	repinfo.fullavailability = 99.0;
	repinfo.reportavailability = 99.5;
	/* Non-critical (yellow) time differs between 24x7 and the SLA hours. */
	repinfo.fullduration[COL_YELLOW] = 7200;
	repinfo.reportduration[COL_YELLOW] = 3600;

	memset(&ev1, 0, sizeof(ev1));
	memset(&ev2, 0, sizeof(ev2));
	ev1.starttime = 1793941200; ev1.duration = 60; ev1.color = COL_YELLOW;
	ev1.cause = NULL; ev1.timespec = "ev1"; ev1.next = &ev2;
	ev2.starttime = 1793944800; ev2.duration = 120; ev2.color = COL_RED;
	ev2.cause = "Server timeout"; ev2.timespec = "ev2"; ev2.next = NULL;
	reploghead = &ev1;

	generate_replog(stdout, textrep, "text.txt", "h1", "http", COL_YELLOW, STYLE_OTHER,
			"127.0.0.1", "h1", 1793923200, 1794009599, 97.0, 99.995, -1, &repinfo);
	fclose(textrep);
	return 0;
}
