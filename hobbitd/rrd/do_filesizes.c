/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles "filesizes" messages.                                  */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char filesize_rcsid[] = "$Id: do_filesizes.c,v 1.7 2007-11-26 21:41:31 henrik Exp $";

static char *filesize_params[] = { "DS:size:GAUGE:600:0:U", NULL };
static char *filesize_tpl      = NULL;

int do_filesizes_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	char *boln, *eoln;

	if (filesize_tpl == NULL) filesize_tpl = setup_template(filesize_params);

	boln = strchr(msg, '\n'); if (boln) boln++;
	while (boln && *boln) {
		char *fn, *szstr = NULL;

		eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';

		fn = strtok(boln, ":"); if (fn) szstr = strtok(NULL, ":");
		if (fn && szstr) {
			char *p;

			for (p=strchr(fn, '/'); (p); p = strchr(p, '/')) *p = ',';
			setupfn2("%s.%s.rrd", "filesizes", fn);

			sprintf(rrdvalues, "%d:%s", (int)tstamp, szstr);
			create_and_update_rrd(hostname, testname, filesize_params, filesize_tpl);
		}

		boln = (eoln ? eoln+1 : NULL);
	}

	return 0;
}

