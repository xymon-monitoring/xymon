/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char do_net_rcsid[] = "$Id$";

int do_net_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
	static char *xymonnet_params[]       = { "DS:sec:GAUGE:600:0:U", NULL };
	static void *xymonnet_tpl            = NULL;

	char *p;
	float seconds = 0.0;
	int do_default = 1;

	if (xymonnet_tpl == NULL) xymonnet_tpl = setup_template(xymonnet_params);

	if (strcmp(testname, "http") == 0) {
		char *line1, *url = NULL, *eoln;

		do_default = 0;

		line1 = msg;
		while ((line1 = strchr(line1, '\n')) != NULL) {
			line1++; /* Skip the newline */
			eoln = strchr(line1, '\n'); if (eoln) *eoln = '\0';

			if ( (strncmp(line1, "&green", 6) == 0) || 
			     (strncmp(line1, "&yellow", 7) == 0) ||
			     (strncmp(line1, "&red", 4) == 0) ) {
				p = strstr(line1, "http");
				if (p) {
					url = xstrdup(p);
					p = strchr(url, ' ');
					if (p) *p = '\0';
				}
			}
			else if (url && ((p = strstr(line1, "Seconds:")) != NULL) && (sscanf(p, "Seconds: %f", &seconds) == 1)) {
				char *urlfn = url;

				if (strncmp(urlfn, "http://", 7) == 0) urlfn += 7;
				p = urlfn; while ((p = strchr(p, '/')) != NULL) *p = ',';
				setupfn3("%s.%s.%s.rrd", "tcp", "http", urlfn);
				snprintf(rrdvalues, sizeof(rrdvalues), "%d:%.2f", (int)tstamp, seconds);
				create_and_update_rrd(hostname, testname, classname, pagepaths, xymonnet_params, xymonnet_tpl);
				xfree(url); url = NULL;
			}

			if (eoln) *eoln = '\n';
		}

		if (url) xfree(url);
	}
	else if (strcmp(testname, xgetenv("PINGCOLUMN")) == 0) {
		/*
		 * Ping-tests, possibly using fping.
		 */
		char *tmod = "ms";

		do_default = 0;

		if ((p = strstr(msg, "time=")) != NULL) {
			/* Standard ping, reports ".... time=0.2 ms" */
			seconds = atof(p+5);
			tmod = p + 5; tmod += strspn(tmod, "0123456789. ");
		}
		else if ((p = strstr(msg, "alive")) != NULL) {
			/* fping, reports ".... alive (0.43 ms)" */
			seconds = atof(p+7);
			tmod = p + 7; tmod += strspn(tmod, "0123456789. ");
		}

		if (strncmp(tmod, "ms", 2) == 0) seconds = seconds / 1000.0;
		else if (strncmp(tmod, "usec", 4) == 0) seconds = seconds / 1000000.0;

		setupfn2("%s.%s.rrd", "tcp", testname);
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%.6f", (int)tstamp, seconds);
		return create_and_update_rrd(hostname, testname, classname, pagepaths, xymonnet_params, xymonnet_tpl);
	}
	else if (strcmp(testname, "ntp") == 0) {
		/*
		 * sntp (4.2.6 and older) output - note the sign is a separate token:
		 *    2009 Nov 13 11:29:10.000313 + 0.038766 +/- 0.052900 secs
		 * ntpdate output:
		 *    server 172.16.10.2, stratum 3, offset -0.040324, delay 0.02568
		 *    13 Nov 11:29:06 ntpdate[7038]: adjust time server 172.16.10.2 offset -0.040324 sec
		 * ntpdig (ntpsec) and sntp 4.2.7+ output:
		 *    2015-10-14 13:46:04.534916 (+0500) -0.000007 +/- 0.084075 localhost 127.0.0.1 s2 no-leap
		 * macOS sntp output (no timestamp prefix):
		 *    +0.009083 +/- 0.013184 pool.ntp.org 193.134.29.12
		 * chronyd -Q output:
		 *    2026-06-11T22:28:28Z System clock wrong by 0.368564 seconds (ignored)
		 *
		 * All of these report the offset in seconds. The ntpstat RRD stores
		 * milliseconds (its other feed is the client-side "ntpstat" test
		 * with "ntpq -c rv" output, which is in ms), so convert at the end.
		 */

		char dataforntpstat[100];
		double offsetsecs = 0.0;
		int gotoffset = 0;
		char *msgcopy = strdup(msg);

		if ((strstr(msgcopy, "ntpdate") != NULL) && ((p = strstr(msgcopy, "offset ")) != NULL)) {
			/* Old-style "ntpdate" output. The ntpsec ntpdate wrapper outputs the ntpdig format instead, handled below. */
			char *endptr;

			p += 7;
			offsetsecs = strtod(p, &endptr);
			gotoffset = (endptr != p);
		}
		else if ((p = strstr(msgcopy, " +/- ")) != NULL) {
			/* ntpdig / sntp : the clock offset is the field just before the "+/-" error bound */
			char *tokstart, *endptr;

			*p = '\0';
			tokstart = p;
			while ((tokstart > msgcopy) &&
			       (*(tokstart-1) != ' ') && (*(tokstart-1) != '\t') &&
			       (*(tokstart-1) != '\n') && (*(tokstart-1) != '\r')) tokstart--;
			offsetsecs = strtod(tokstart, &endptr);
			if ((endptr != tokstart) && (*endptr == '\0')) {
				/* The whole token parses as a number - looks sane */
				gotoffset = 1;

				if ((*tokstart != '+') && (*tokstart != '-') && (tokstart >= msgcopy+2) &&
				    (*(tokstart-2) == '-') &&
				    ((tokstart-2 == msgcopy) || (*(tokstart-3) == ' ') || (*(tokstart-3) == '\t') ||
				     (*(tokstart-3) == '\n') || (*(tokstart-3) == '\r'))) {
					/* sntp 4.2.6 and older print an unsigned offset with the sign as a separate token */
					offsetsecs = -offsetsecs;
				}
			}
		}
		else if ((p = strstr(msgcopy, " wrong by ")) != NULL) {
			/* chronyd -Q : same sign convention as the ntp offset (positive = local clock behind) */
			char *endptr;

			p += 10;
			offsetsecs = strtod(p, &endptr);
			gotoffset = (endptr != p);
		}

		if (gotoffset) {
			snprintf(dataforntpstat, sizeof(dataforntpstat), "offset=%.6f", offsetsecs * 1000.0);
			do_ntpstat_rrd(hostname, testname, classname, pagepaths, dataforntpstat, tstamp);
		}

		xfree(msgcopy);
	}


	if (do_default) {
		/*
		 * Normal network tests - pick up the "Seconds:" value
		 */
		p = strstr(msg, "\nSeconds:");
		if (p && (sscanf(p+1, "Seconds: %f", &seconds) == 1)) {
			setupfn2("%s.%s.rrd", "tcp", testname);
			snprintf(rrdvalues, sizeof(rrdvalues), "%d:%f", (int)tstamp, seconds);
			return create_and_update_rrd(hostname, testname, classname, pagepaths, xymonnet_params, xymonnet_tpl);
		}
	}

	return 0;
}

