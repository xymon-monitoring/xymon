/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char sendmail_rcsid[] = "$Id: do_sendmail.c,v 1.2 2004-12-28 16:15:47 henrik Exp $";

static char *sendmail_params[] = { "rrdcreate", rrdfn, 
				   "DS:msgsfr:DERIVE:600:0:U",
				   "DS:bytes_from:DERIVE:600:0:U",
				   "DS:msgsto:DERIVE:600:0:U",
				   "DS:bytes_to:DERIVE:600:0:U",
				   "DS:msgsrej:DERIVE:600:0:U",
				   "DS:msgsdis:DERIVE:600:0:U",
				   rra1, rra2, rra3, rra4, NULL };

int do_sendmail_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	/*
	 * The data we process is the output from the "mailstats" command.
	 *
	 * Statistics from Wed Jun 19 16:29:41 2002
	 *  M   msgsfr  bytes_from   msgsto    bytes_to  msgsrej msgsdis  Mailer
	 *  3   183435     215701K        0          0K        0       0  local
	 *  5        0          0K   183435     215544K        0       0  esmtp
	 * =============================================================
	 *  T   183435     215701K   183435     215544K        0       0
	 *  C   183435               183435                    0
	 *
	 * We pick up those lines that come before the "============" line, and
	 * create one RRD per "Mailer", with the counters.
	 */

	char *bofdata, *eofdata, *eoln;
	int done, found;
	unsigned int msgsfr, bytesfr, msgsto, bytesto, msgsrej, msgsdis;
	char mailer[1024];

	/* Find the line that begins with "=====" and NULL the message there */
	eofdata = strstr(msg, "\n=="); if (eofdata) *(eofdata+1) = '\0'; else return -1;

	/* Find the start of the Statistics part. */
	bofdata = strstr(msg, "\nStatistics ");

	/* Skip the "Statistics from.... " line */
	if (bofdata) bofdata = strchr(bofdata+1, '\n');

	/* Skip the header line */
	if (bofdata) bofdata = strchr(bofdata+1, '\n');
	if (bofdata) bofdata++;

	done = (bofdata == NULL);
	while (!done) {
		*rrdvalues = '\0';

		eoln = strchr(bofdata, '\n');
		if (eoln) {
			*eoln = '\0';
			found = sscanf(bofdata, "%u %uK %u %uK %u %u %s", 
					&msgsfr, &bytesfr, &msgsto, &bytesto, &msgsrej, &msgsdis, mailer);
			if (found == 7) {
				sprintf(rrdvalues, "%d:%u:%u:%u:%u:%u:%u", 
					(int)tstamp, msgsfr, bytesfr, msgsto, bytesto, msgsrej, msgsdis);
			}
			else {
				msgsrej = msgsdis = 0;
				found = sscanf(bofdata, "%u %uK %u %uK %s", 
					&msgsfr, &bytesfr, &msgsto, &bytesto, mailer);

				if (found == 5) {
					sprintf(rrdvalues, "%d:%u:%u:%u:%u:U:U", 
						(int)tstamp, msgsfr, bytesfr, msgsto, bytesto);
				}
			}

			if (*rrdvalues) {
				sprintf(rrdfn, "sendmail.%s.rrd", mailer);
				create_and_update_rrd(hostname, rrdfn, sendmail_params, update_params);
			}

			*eoln = '\n';
			bofdata = eoln+1;
			done = (*bofdata == '\0');
		}
		else done=1;
	}

	if (eofdata) *(eofdata+1) = '=';

	return 0;
}

