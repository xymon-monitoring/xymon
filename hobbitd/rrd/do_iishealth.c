/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char iishealth_rcsid[] = "$Id: do_iishealth.c,v 1.10 2007-07-21 10:19:16 henrik Exp $";

int do_iishealth_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	static char *iishealth_params[] = { "rrdcreate", rrdfn, "DS:realmempct:GAUGE:600:0:U", NULL };
	static char *iishealth_tpl      = NULL;

	char *bol, *eoln, *tok;

	if (iishealth_tpl == NULL) iishealth_tpl = setup_template(iishealth_params);

	bol = strchr(msg, '\n'); if (bol) bol++; else return 0;

	while (bol && *bol) {
		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
		*rrdfn = '\0';

		tok = strtok(bol, " \t\r\n");	/* Get color marker */
		if (tok) tok = strtok(NULL, " \t\r\n");	/* Get keyword */
		if (tok) {
			if (strcmp(tok, "Connections:") == 0) {
				tok = strtok(NULL, " \t\r\n");
				if (tok == NULL) continue;

				strcpy(rrdfn, "iishealth.connections.rrd");
				sprintf(rrdvalues, "%d:%lu", (int)tstamp, atol(tok));
			}
			else if (strcmp(tok, "RequestsQueued:") == 0) {
				tok = strtok(NULL, " \t\r\n");
				if (tok == NULL) continue;

				strcpy(rrdfn, "iishealth.requestqueued.rrd");
				sprintf(rrdvalues, "%d:%lu", (int)tstamp, atol(tok));
			}
			else if (strcmp(tok, "Sessions:") == 0) {
				tok = strtok(NULL, " \t\r\n");
				if (tok == NULL) continue;

				strcpy(rrdfn, "iishealth.sessions.rrd");
				sprintf(rrdvalues, "%d:%lu", (int)tstamp, atol(tok));
			}

			if (*rrdfn) create_and_update_rrd(hostname, testname, rrdfn, iishealth_params, iishealth_tpl);
		}

		bol = (eoln ? eoln+1 : NULL);
	}

	return 0;
}

