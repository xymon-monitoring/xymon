/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbtest_rcsid[] = "$Id$";

int do_bbtest_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{ 
	static char *bbtest_params[] = { "DS:runtime:GAUGE:600:0:U", NULL };
	static void *bbtest_tpl      = NULL;

	char	*p;
	float	runtime;

	if (bbtest_tpl == NULL) bbtest_tpl = setup_template(bbtest_params);

	p = strstr(msg, "TIME TOTAL");
	if (p && (sscanf(p, "TIME TOTAL %f", &runtime) == 1)) {
		if (strcmp("bbtest", testname) != 0) {
			setupfn2("%s.%s.rrd", "bbtest", testname);
		}
		else {
			setupfn("%s.rrd", "bbtest");
		}
		sprintf(rrdvalues, "%d:%.2f", (int) tstamp, runtime);
		return create_and_update_rrd(hostname, testname, classname, pagepaths, bbtest_params, bbtest_tpl);
	}

	return 0;
}
