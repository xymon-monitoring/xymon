/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbgen_rcsid[] = "$Id: do_bbgen.c,v 1.12 2006-06-09 22:23:49 henrik Exp $";

int do_bbgen_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	static char *bbgen_params[] = { "rrdcreate", rrdfn, "DS:runtime:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };
	static char *bbgen_tpl      = NULL;

	char	*p;
	float	runtime;

	if (bbgen_tpl == NULL) bbgen_tpl = setup_template(bbgen_params);

	p = strstr(msg, "TIME TOTAL");
	if (p && (sscanf(p, "TIME TOTAL %f", &runtime) == 1)) {
		if (strcmp("bbgen", testname) != 0) {
			setupfn("bbgen.%s.rrd", testname);
		}
		else {
			strcpy(rrdfn, "bbgen.rrd");
		}
		sprintf(rrdvalues, "%d:%.2f", (int)tstamp, runtime);
		return create_and_update_rrd(hostname, rrdfn, bbgen_params, bbgen_tpl);
	}

	return 0;
}
