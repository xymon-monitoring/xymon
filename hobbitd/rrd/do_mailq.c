/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char mailq_rcsid[] = "$Id: do_mailq.c,v 1.7 2004-12-06 13:10:33 henrik Exp $";

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
		/* Looking for "... N requests ... " */
		p = strstr(msg, "requests");
		if (p) {
			while ((p > msg) && (isspace((int) *(p-1)) || isdigit((int) *(p-1)))) p--;
			mailq = atoi(p);

			sprintf(rrdfn, "mailq.rrd");
			sprintf(rrdvalues, "%d:%d", (int)tstamp, mailq);
			return create_and_update_rrd(hostname, rrdfn, mailq_params, update_params);
		}
	}

	return 0;
}

