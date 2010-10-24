/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* This module handles various "counts" messages.                             */
/*                                                                            */
/* Copyright (C) 2005-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char counts_rcsid[] = "$Id$";

static int do_one_counts_rrd(char *counttype, char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp, char *params[], char *tpl) 
{ 
	char *boln, *eoln;

	boln = strchr(msg, '\n'); if (boln) boln++;
	while (boln && *boln) {
		char *fn, *countstr = NULL;

		eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';

		fn = strtok(boln, ":"); if (fn) countstr = strtok(NULL, ":");
		if (fn && countstr) {
			char *p;

			for (p=strchr(fn, '/'); (p); p = strchr(p, '/')) *p = ',';
			setupfn2("%s.%s.rrd", counttype, fn);

			sprintf(rrdvalues, "%d:%s", (int)tstamp, countstr);
			create_and_update_rrd(hostname, testname, classname, pagepaths, params, tpl);
		}

		boln = (eoln ? eoln+1 : NULL);
	}

	return 0;
}

static char *counts_params[] = { "DS:count:GAUGE:600:0:U", NULL };
static void *counts_tpl      = NULL;
static char *derive_params[] = { "DS:count:DERIVE:600:0:U", NULL };
static void *derive_tpl      = NULL;

int do_counts_rrd(char *counttype, char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{
	if (counts_tpl == NULL) counts_tpl = setup_template(counts_params);

	return do_one_counts_rrd(counttype, hostname, testname, classname, pagepaths, msg, tstamp, counts_params, counts_tpl);
}

int do_derives_rrd(char *counttype, char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{
	if (derive_tpl == NULL) derive_tpl = setup_template(derive_params);

	return do_one_counts_rrd(counttype, hostname, testname, classname, pagepaths, msg, tstamp, derive_params, derive_tpl);
}

