/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbtest_rcsid[] = "$Id: do_bbtest.c,v 1.4 2004-11-13 13:23:47 henrik Exp $";

static char *bbtest_params[] = { "rrdcreate", rrdfn, "DS:runtime:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_bbtest_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{ 
	char	*p;
	float	runtime;

	p = strstr(msg, "TIME TOTAL");
	if (p && (sscanf(p, "TIME TOTAL %f", &runtime) == 1)) {
		sprintf(rrdfn, "%s.rrd", testname);
		sprintf(rrdvalues, "%d:%.2f", (int) tstamp, runtime);
		return create_and_update_rrd(hostname, rrdfn, bbtest_params, update_params);
	}

	return 0;
}
