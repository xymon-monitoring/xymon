/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbproxy_rcsid[] = "$Id: do_bbproxy.c,v 1.12 2006-06-09 22:23:49 henrik Exp $";

int do_bbproxy_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{ 
	static char *bbproxy_params[]       = { "rrdcreate", rrdfn, "DS:runtime:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };
	static char *bbproxy_tpl            = NULL;

	char	*p;
	float	runtime;

	if (bbproxy_tpl == NULL) bbproxy_tpl = setup_template(bbproxy_params);

	p = strstr(msg, "Average queue time");
	if (p && (sscanf(p, "Average queue time : %f", &runtime) == 1)) {
		if (strcmp("bbproxy", testname) != 0) {
			setupfn("bbproxy.%s.rrd", testname);
		}
		else {
			strcpy(rrdfn, "bbproxy.rrd");
		}
		sprintf(rrdvalues, "%d:%.2f", (int) tstamp, runtime);
		return create_and_update_rrd(hostname, rrdfn, bbproxy_params, bbproxy_tpl);
	}

	return 0;
}

