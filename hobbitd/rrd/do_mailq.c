/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char mailq_rcsid[] = "$Id: do_mailq.c,v 1.10 2005-04-24 20:52:40 henrik Exp $";

static char *mailq_params[]       = { "rrdcreate", rrdfn, "DS:mailq:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_mailq_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char	*p;
	char    *inqueue, *outqueue;
	int	mailq, inq, outq;

	/* 
	 * The normail "mailq" report only has a "... N requests" line and a single graph.
	 * Erik's enhanced script has both an incoming and an outgoing mail queue, with 
	 * two different RRD's. We'll try to handle both setups.
	 */

	outqueue = strstr(msg, "\nMail queue out:");
	inqueue = strstr(msg, "\nMail queue in:");
	if (inqueue && outqueue) {
		/* Dual queue message */

		/* Skip the "Mail queue X" line */
		outqueue = strchr(outqueue+1, '\n');
		inqueue = strchr(inqueue+1, '\n');
		if ((outqueue == NULL) || (inqueue == NULL)) return 0;
		outqueue++; inqueue++;

		/* Next line has "&green          Mail Queue (26 requests)". Cut string at end-of-line */
		p = strchr(outqueue, '\n'); if (p) *p = '\0';
		p = strchr(inqueue, '\n'); if (p) *p = '\0';

		/* Skip until we find a number, and get the digit. */
		p = outqueue + strcspn(outqueue, "0123456789"); outq = atoi(p);
		p = inqueue + strcspn(inqueue, "0123456789"); inq = atoi(p);

		/* Update RRD's */
		sprintf(rrdfn, "mailqin.rrd");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, inq);
		create_and_update_rrd(hostname, rrdfn, mailq_params, update_params);

		sprintf(rrdfn, "mailqout.rrd");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, outq);
		create_and_update_rrd(hostname, rrdfn, mailq_params, update_params);
		return 0;

	}
	else {
		char *bol, *eol;

		/* Looking for "... N requests ... " */
		bol = strstr(msg, "requests");
		if (bol) {
			while ((bol > msg) && (*bol != '\n')) bol--;
			eol = strchr(bol, '\n'); if (eol) *eol = '\0';

			bol += strcspn(bol, "0123456789");
			mailq = atoi(bol);

			sprintf(rrdfn, "mailq.rrd");
			sprintf(rrdvalues, "%d:%d", (int)tstamp, mailq);
			return create_and_update_rrd(hostname, rrdfn, mailq_params, update_params);
		}
	}

	return 0;
}

