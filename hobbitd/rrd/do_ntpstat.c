/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char ntpstat_rcsid[] = "$Id: do_ntpstat.c,v 1.2 2004-11-07 18:24:24 henrik Exp $";

static char *ntpstat_params[]     = { "rrdcreate", rrdfn, "DS:offsetms:GAUGE:600:U:U", rra1, rra2, rra3, rra4, NULL };

int do_ntpstat_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char *p;
	float offset;

	p = strstr(msg, "\nOffset:");
	if (p && (sscanf(p+1, "Offset: %f", &offset) == 1)) {
		sprintf(rrdfn, "%s/%s.%s.rrd", rrddir, commafy(hostname), testname);
		sprintf(rrdvalues, "%d:%.6f", (int)tstamp, offset);
		return create_and_update_rrd(rrdfn, ntpstat_params, update_params);
	}

	return 0;
}

