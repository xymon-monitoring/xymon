/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char mailq_rcsid[] = "$Id: do_mailq.c,v 1.5 2004-11-13 22:01:47 henrik Exp $";

static char *mailq_params[]       = { "rrdcreate", rrdfn, "DS:mailq:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_mailq_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char	*p;
	int	mailq;

	/* Looking for "... N requests ... " */
	p = strstr(msg, "requests");
	if (p) {
		while ((p > msg) && (isspace((int) *(p-1)) || isdigit((int) *(p-1)))) p--;
		mailq = atoi(p);

		sprintf(rrdfn, "mailq.rrd");
		sprintf(rrdvalues, "%d:%df", (int)tstamp, mailq);
		return create_and_update_rrd(hostname, rrdfn, bbgen_params, update_params);
	}

	return 0;
}

