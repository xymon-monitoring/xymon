/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbproxy_rcsid[] = "$Id: do_bbproxy.c,v 1.5 2004-11-13 17:37:36 henrik Exp $";

static char *bbproxy_params[]       = { "rrdcreate", rrdfn, "DS:runtime:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_bbproxy_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{ 
	char	*p;
	float	runtime;

	p = strstr(msg, "Average queue time");
	if (p && (sscanf(p, "Average queue time : %f", &runtime) == 1)) {
		sprintf(rrdfn, "bbproxy.%s.rrd", testname);
		sprintf(rrdvalues, "%d:%.2f", (int) tstamp, runtime);
		return create_and_update_rrd(hostname, rrdfn, bbproxy_params, update_params);
	}

	return 0;
}

