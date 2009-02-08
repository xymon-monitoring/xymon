/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char iishealth_rcsid[] = "$Id: do_iishealth.c 5819 2008-09-30 16:37:31Z storner $";

int do_iishealth_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{ 
	static char *iishealth_params[] = { "DS:realmempct:GAUGE:600:0:U", NULL };
	static void *iishealth_tpl      = NULL;

	char *bol, *eoln, *tok;

	if (iishealth_tpl == NULL) iishealth_tpl = setup_template(iishealth_params);

	bol = strchr(msg, '\n'); if (bol) bol++; else return 0;

	while (bol && *bol) {
		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';

		tok = strtok(bol, " \t\r\n");	/* Get color marker */
		if (tok) tok = strtok(NULL, " \t\r\n");	/* Get keyword */
		if (tok) {
			int havedata = 0;

			if (strcmp(tok, "Connections:") == 0) {
				tok = strtok(NULL, " \t\r\n");
				if (tok == NULL) continue;

				setupfn2("%s.%s.rrd", "iishealth", "connections");
				sprintf(rrdvalues, "%d:%lu", (int)tstamp, atol(tok));
				havedata = 1;
			}
			else if (strcmp(tok, "RequestsQueued:") == 0) {
				tok = strtok(NULL, " \t\r\n");
				if (tok == NULL) continue;

				setupfn2("%s.%s.rrd", "iishealth", "requestqueued");
				sprintf(rrdvalues, "%d:%lu", (int)tstamp, atol(tok));
				havedata = 1;
			}
			else if (strcmp(tok, "Sessions:") == 0) {
				tok = strtok(NULL, " \t\r\n");
				if (tok == NULL) continue;

				setupfn2("%s.%s.rrd", "iishealth", "sessions");
				sprintf(rrdvalues, "%d:%lu", (int)tstamp, atol(tok));
				havedata = 1;
			}

			if (havedata) create_and_update_rrd(hostname, testname, classname, pagepaths, iishealth_params, iishealth_tpl);
		}

		bol = (eoln ? eoln+1 : NULL);
	}

	return 0;
}

