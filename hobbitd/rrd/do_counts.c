/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles various "counts" messages.                             */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char counts_rcsid[] = "$Id: do_counts.c,v 1.2 2006-05-02 12:05:42 henrik Exp $";

static char *counts_params[] = { "rrdcreate", rrdfn, "DS:count:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };
static char *counts_tpl      = NULL;

int do_counts_rrd(char *counttype, char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	char *boln, *eoln;

	if (counts_tpl == NULL) counts_tpl = setup_template(counts_params);

	boln = strchr(msg, '\n'); if (boln) boln++;
	while (boln && *boln) {
		char *fn, *countstr = NULL;

		eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';

		fn = strtok(boln, ":"); if (fn) countstr = strtok(NULL, ":");
		if (fn && countstr) {
			char *p;

			for (p=strchr(fn, '/'); (p); p = strchr(p, '/')) *p = ',';
			sprintf(rrdfn, "%s.%s.rrd", counttype, fn);

			sprintf(rrdvalues, "%d:%s", (int)tstamp, countstr);
			create_and_update_rrd(hostname, rrdfn, counts_params, counts_tpl);
		}

		boln = (eoln ? eoln+1 : NULL);
	}

	return 0;
}

