/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbproxy_rcsid[] = "$Id$";

int do_bbproxy_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{ 
	static char *bbproxy_params[]       = { "DS:runtime:GAUGE:600:0:U", NULL };
	static void *bbproxy_tpl            = NULL;

	char	*p;
	float	runtime;

	if (bbproxy_tpl == NULL) bbproxy_tpl = setup_template(bbproxy_params);

	p = strstr(msg, "Average queue time");
	if (p && (sscanf(p, "Average queue time : %f", &runtime) == 1)) {
		if (strcmp("bbproxy", testname) != 0) {
			setupfn2("%s.%s.rrd", "bbproxy", testname);
		}
		else {
			setupfn("%s.rrd", "bbproxy");
		}
		sprintf(rrdvalues, "%d:%.2f", (int) tstamp, runtime);
		return create_and_update_rrd(hostname, testname, classname, pagepaths, bbproxy_params, bbproxy_tpl);
	}

	return 0;
}

