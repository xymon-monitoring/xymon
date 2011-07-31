/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char xymonproxy_rcsid[] = "$Id$";

int do_xymonproxy_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{ 
	static char *xymonproxy_params[]       = { "DS:runtime:GAUGE:600:0:U", NULL };
	static void *xymonproxy_tpl            = NULL;

	char	*p;
	float	runtime;

	if (xymonproxy_tpl == NULL) xymonproxy_tpl = setup_template(xymonproxy_params);

	p = strstr(msg, "Average queue time");
	if (p && (sscanf(p, "Average queue time : %f", &runtime) == 1)) {
		if (strcmp("xymonproxy", testname) != 0) {
			setupfn2("%s.%s.rrd", "xymonproxy", testname);
		}
		else {
			setupfn("%s.rrd", "xymonproxy");
		}
		sprintf(rrdvalues, "%d:%.2f", (int) tstamp, runtime);
		return create_and_update_rrd(hostname, testname, classname, pagepaths, xymonproxy_params, xymonproxy_tpl);
	}

	return 0;
}

