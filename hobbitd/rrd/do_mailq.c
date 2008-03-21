/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char mailq_rcsid[] = "$Id: do_mailq.c,v 1.21 2008-03-21 11:53:55 henrik Exp $";

int do_mailq_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	static char *mailq_params[]       = { "DS:mailq:GAUGE:600:0:U", NULL };
	static void *mailq_tpl            = NULL;

	char	*p;
	char    *inqueue, *outqueue;
	int	mailq, inq, outq;

	if (mailq_tpl == NULL) mailq_tpl = setup_template(mailq_params);

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
		setupfn("%s.rrd", "mailqin");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, inq);
		create_and_update_rrd(hostname, testname, mailq_params, mailq_tpl);

		setupfn("%s.rrd", "mailqout");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, outq);
		create_and_update_rrd(hostname, testname, mailq_params, mailq_tpl);
		return 0;

	}
	else {
		char *valptr;

		/* Looking for "... N requests ... " */
		valptr = strstr(msg, "requests");
		if (valptr) {
			/* Go back past any whitespace before "requests" */
			do { valptr--; } while ((valptr > msg) && (*valptr != '\n') && isspace((int)*valptr));

			/* Go back to the beginning of the number */
			while ((valptr > msg) && isdigit((int) *(valptr-1))) valptr--;

			mailq = atoi(valptr);

			setupfn("%s.rrd", "mailq");
			sprintf(rrdvalues, "%d:%d", (int)tstamp, mailq);
			return create_and_update_rrd(hostname, testname, mailq_params, mailq_tpl);
		}
	}

	return 0;
}

