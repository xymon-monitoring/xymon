/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char iishealth_rcsid[] = "$Id: do_iishealth.c,v 1.2 2005-01-15 22:27:28 henrik Exp $";

static char *iishealth_params[] = { "rrdcreate", rrdfn, "DS:realmempct:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_iishealth_larrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	char *bol, *eoln, *tok;

	bol = strchr(msg, '\n'); if (bol) bol++; else return 0;

	while (bol && *bol) {
		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
		*rrdfn = '\0';

		tok = strtok(bol, " \t\r\n");	/* Get color marker */
		if (tok == NULL) continue;

		tok = strtok(NULL, " \t\r\n");	/* Get keyword */
		if (tok == NULL) continue;

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

		if (*rrdfn) create_and_update_rrd(hostname, rrdfn, iishealth_params, update_params);

		bol = (eoln ? eoln+1 : NULL);
	}

	return 0;
}

