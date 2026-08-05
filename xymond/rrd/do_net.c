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

#include <math.h>	/* isfinite() - reject nan/inf offset tokens */

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
		 * sntp output: 
		 *    2009 Nov 13 11:29:10.000313 + 0.038766 +/- 0.052900 secs
		 * ntpdate output: 
		 *    server 172.16.10.2, stratum 3, offset -0.040324, delay 0.02568
		 *    13 Nov 11:29:06 ntpdate[7038]: adjust time server 172.16.10.2 offset -0.040324 sec
		 * built-in SNTP probe banner:
		 *    ... stratum 2, offset +0.001234 +/- 0.005678 sec, delay 0.000900 sec
		 */

		static char *ntp_params[] = { "DS:offset:GAUGE:600:U:U",
					      "DS:rootdist:GAUGE:600:0:U", NULL };
		static void *ntp_tpl = NULL;

		char *offsetval = NULL;
		char offsetbuf[40];
		char rdbuf[24];
		double rootdist_ms = -1.0;
		char *msgcopy = strdup(msg);

		if (ntp_tpl == NULL) ntp_tpl = setup_template(ntp_params);

		if ((p = strstr(msgcopy, "offset ")) != NULL) {
			/* ntpdate and the built-in SNTP probe both report "offset <seconds>". */
			offsetval = strtok(p + 7, " \r\n\t");
		}
		else if (strstr(msgcopy, " secs") != NULL) {
			/* Probably new "sntp" output */
			char *year, *tm, *offsetdirection, *offset, *plusminus, *errorbound, *secs;

			tm = offsetdirection = plusminus = errorbound = secs = NULL;
			year = strtok(msgcopy, " ");
			tm = year ? strtok(NULL, " ") : NULL;
			offsetdirection = tm ? strtok(NULL, " ") : NULL;
			offset = offsetdirection ? strtok(NULL, " ") : NULL;
			plusminus = offset ? strtok(NULL, " ") : NULL;
			errorbound = plusminus ? strtok(NULL, " ") : NULL;
			secs = errorbound ? strtok(NULL, " ") : NULL;

			if ( offsetdirection && ((strcmp(offsetdirection, "+") == 0) || (strcmp(offsetdirection, "-") == 0)) &&
			     plusminus && (strcmp(plusminus, "+/-") == 0) && 
			     secs && (strcmp(secs, "secs") == 0) ) {
				/* Looks sane */
				snprintf(offsetbuf, sizeof(offsetbuf), "%s%s", offsetdirection, offset);
				offsetval = offsetbuf;
			}
		}
		
		/* The built-in probe banner also carries the "+/-" root distance - the RFC
		 * 5905 accuracy bound (which folds in root dispersion). Record it next to
		 * the offset so the graph can draw offset +/- rootdist. ntpdate omits it, so
		 * it is optional ("U" when absent). Parsed from the untouched message, since
		 * msgcopy is strtok-chewed by the offset parser above. */
		if ((p = strstr(msg, "+/- ")) != NULL) {
			char *rdend;
			double rd = strtod(p + 4, &rdend);
			/* Only record a real, finite number; "+/- " with no parseable
			 * value (or a nan/inf token) stays "U". */
			if (rdend != p + 4 && isfinite(rd)) rootdist_ms = rd * 1000.0;
		}

		if (offsetval) {
			char *offend;
			double offset_sec = strtod(offsetval, &offend);
			/* Fully validate the token rather than just requiring a numeric
			 * prefix: strtod must consume at least one character, the value
			 * must be finite (reject "nan"/"inf"), and the only trailing
			 * character allowed is the comma ntpdate appends ("offset <sec>,").
			 * Anything else - an error word like "unavailable", or junk like
			 * "12junk" - is skipped rather than recorded as a misleading sample. */
			if ((offend != offsetval) && isfinite(offset_sec) &&
			    ((*offend == '\0') || ((*offend == ',') && (offend[1] == '\0')))) {
				/* Both numbers are reported in seconds; the RRD records milliseconds. */
				if (rootdist_ms >= 0.0) snprintf(rdbuf, sizeof(rdbuf), "%.6f", rootdist_ms);
				else                    strcpy(rdbuf, "U");
				setupfn("%s.rrd", "ntp");
				snprintf(rrdvalues, sizeof(rrdvalues), "%d:%.6f:%s",
					 (int)tstamp, offset_sec * 1000.0, rdbuf);
				create_and_update_rrd(hostname, testname, classname, pagepaths, ntp_params, ntp_tpl);
			}
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

