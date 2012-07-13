/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char xymonnet_rcsid[] = "$Id$";

int do_xymonnet_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{ 
	static char *xymonnet_params[] = { "DS:runtime:GAUGE:600:0:U", NULL };
	static void *xymonnet_tpl      = NULL;

	char	*p;
	float	runtime;

	if (xymonnet_tpl == NULL) xymonnet_tpl = setup_template(xymonnet_params);

	p = strstr(msg, "TIME TOTAL");
	if (p && (sscanf(p, "TIME TOTAL %f", &runtime) == 1)) {
		if (strcmp("xymonnet", testname) != 0) {
			setupfn2("%s.%s.rrd", "xymonnet", testname);
		}
		else {
			setupfn("%s.rrd", "xymonnet");
		}
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%.2f", (int) tstamp, runtime);
		return create_and_update_rrd(hostname, testname, classname, pagepaths, xymonnet_params, xymonnet_tpl);
	}

	return 0;
}
