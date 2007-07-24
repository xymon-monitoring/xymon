/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbtest_rcsid[] = "$Id: do_bbtest.c,v 1.15 2007-07-24 08:45:01 henrik Exp $";

int do_bbtest_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{ 
	static char *bbtest_params[] = { "DS:runtime:GAUGE:600:0:U", NULL };
	static char *bbtest_tpl      = NULL;

	char	*p;
	float	runtime;

	if (bbtest_tpl == NULL) bbtest_tpl = setup_template(bbtest_params);

	p = strstr(msg, "TIME TOTAL");
	if (p && (sscanf(p, "TIME TOTAL %f", &runtime) == 1)) {
		if (strcmp("bbtest", testname) != 0) {
			setupfn("bbtest.%s.rrd", testname);
		}
		else {
			setupfn("%s", "bbtest.rrd");
		}
		sprintf(rrdvalues, "%d:%.2f", (int) tstamp, runtime);
		return create_and_update_rrd(hostname, testname, bbtest_params, bbtest_tpl);
	}

	return 0;
}
