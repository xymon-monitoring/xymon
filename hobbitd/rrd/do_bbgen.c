/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbgen_rcsid[] = "$Id: do_bbgen.c,v 1.15 2007-07-21 10:19:16 henrik Exp $";

int do_bbgen_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	static char *bbgen_params[] = { "rrdcreate", rrdfn, "DS:runtime:GAUGE:600:0:U", NULL };
	static char *bbgen_tpl      = NULL;
	static char *bbgen2_params[] = { "rrdcreate", rrdfn, 
					"DS:hostcount:GAUGE:600:0:U", "DS:statuscount:GAUGE:600:0:U", 
					NULL };
	static char *bbgen2_tpl      = NULL;

	char	*p;
	float	runtime;
	int	hostcount, statuscount;

	if (bbgen_tpl == NULL) bbgen_tpl = setup_template(bbgen_params);
	if (bbgen2_tpl == NULL) bbgen2_tpl = setup_template(bbgen2_params);

	p = strstr(msg, "TIME TOTAL");
	if (p && (sscanf(p, "TIME TOTAL %f", &runtime) == 1)) {
		if (strcmp("bbgen", testname) != 0) {
			setupfn("bbgen.%s.rrd", testname);
		}
		else {
			strcpy(rrdfn, "bbgen.rrd");
		}
		sprintf(rrdvalues, "%d:%.2f", (int)tstamp, runtime);
		create_and_update_rrd(hostname, testname, rrdfn, bbgen_params, bbgen_tpl);
	}

	hostcount = statuscount = -1;
	p = strstr(msg, "\n Hosts");
	if (!p || (sscanf(p+1, " Hosts : %d", &hostcount) != 1)) hostcount = -1;
	p = strstr(msg, "\n Status messages");
	if (!p || (sscanf(p+1, " Status messages : %d", &statuscount) != 1)) statuscount = -1;

	if ((hostcount != -1) && (statuscount != -1)) {
		if (strcmp("bbgen", testname) != 0) {
			setupfn("hobbit.%s.rrd", testname);
		}
		else {
			strcpy(rrdfn, "hobbit.rrd");
		}
		sprintf(rrdvalues, "%d:%d:%d", (int)tstamp, hostcount, statuscount);
		create_and_update_rrd(hostname, testname, rrdfn, bbgen2_params, bbgen2_tpl);
	}

	return 0;
}

