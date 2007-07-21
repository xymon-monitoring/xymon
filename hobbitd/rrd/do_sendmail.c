/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char sendmail_rcsid[] = "$Id: do_sendmail.c,v 1.15 2007-07-21 10:19:16 henrik Exp $";

int do_sendmail_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	static char *sendmail_params_1[] = { "rrdcreate", rrdfn, 
					     "DS:msgsfr:DERIVE:600:0:U",
					     "DS:bytes_from:DERIVE:600:0:U",
					     "DS:msgsto:DERIVE:600:0:U",
					     "DS:bytes_to:DERIVE:600:0:U",
					     "DS:msgsrej:DERIVE:600:0:U",
					     "DS:msgsdis:DERIVE:600:0:U",
					     NULL };
	static char *sendmail_tpl_1      = NULL;

	static char *sendmail_params_2[] = { "rrdcreate", rrdfn, 
					     "DS:msgsfr:DERIVE:600:0:U",
					     "DS:bytes_from:DERIVE:600:0:U",
					     "DS:msgsto:DERIVE:600:0:U",
					     "DS:bytes_to:DERIVE:600:0:U",
					     "DS:msgsrej:DERIVE:600:0:U",
					     "DS:msgsdis:DERIVE:600:0:U",
					     "DS:msgsqur:DERIVE:600:0:U",
					     NULL };
	static char *sendmail_tpl_2      = NULL;

	/*
	 * The data we process is the output from the "mailstats" command.
	 *
	 * Statistics from Mon Apr 25 16:29:41 2005
	 *  M   msgsfr  bytes_from   msgsto    bytes_to  msgsrej msgsdis msgsqur  Mailer
	 *  3   183435     215701K        0          0K        0       0       0  local
	 *  5        0          0K   183435     215544K        0       0       0  esmtp
	 * =====================================================================
	 *  T   183435     215701K   183435     215544K        0       0       0
	 *  C   183435               183435                    0
	 *
	 * We pick up those lines that come before the "============" line, and
	 * create one RRD per "Mailer", with the counters.
	 *
	 * The output of the  mailstats command will depend on the version of sendmail
	 * used. This example is from sendmail 8.13.x which added the  msgsqur column.
	 * Sendmail versions prior to 8.10.0 did not have the mgsdis and msgsrej
	 * columns.
	 * 
	 */

	char *bofdata, *eofdata, *eoln = NULL;
	int done, found;
	unsigned long msgsfr, bytesfr, msgsto, bytesto, msgsrej, msgsdis, msgsqur;

	if (sendmail_tpl_1 == NULL) sendmail_tpl_1 = setup_template(sendmail_params_1);
	if (sendmail_tpl_2 == NULL) sendmail_tpl_2 = setup_template(sendmail_params_2);

	/* Find the line that begins with "=====" and NULL the message there */
	eofdata = strstr(msg, "\n=="); if (eofdata) *(eofdata+1) = '\0'; else return -1;

	/* Find the start of the Statistics part. */
	bofdata = strstr(msg, "\nStatistics "); if (!bofdata) return -1;

	/* Skip the "Statistics from.... " line */
	bofdata = strchr(bofdata+1, '\n'); if (!bofdata) return -1;

	/* Skip the header line */
	bofdata = strchr(bofdata+1, '\n'); if (bofdata) bofdata++; else return -1;

	done = (bofdata == NULL);
	while (!done) {
		char mailer[1024];

		MEMDEFINE(mailer);
		*rrdvalues = '\0';

		eoln = strchr(bofdata, '\n');
		if (eoln) {
			*eoln = '\0';

			/* First try for sendmail 8.13.x format */
			found = sscanf(bofdata, "%*s %lu %luK %lu %luK %lu %lu %lu %s",
					&msgsfr, &bytesfr, &msgsto, &bytesto, &msgsrej, &msgsdis, &msgsqur, mailer);
			if (found == 8) {
				sprintf(rrdvalues, "%d:%lu:%lu:%lu:%lu:%lu:%lu:%lu",
					(int)tstamp, msgsfr, bytesfr*1024, msgsto, bytesto*1024, 
					msgsrej, msgsdis, msgsqur);
				goto gotdata;
			}

			/* Next sendmail 8.10.x - without msgsqur */
			found = sscanf(bofdata, "%*s %lu %luK %lu %luK %lu %lu %s",
					&msgsfr, &bytesfr, &msgsto, &bytesto, &msgsrej, &msgsdis, mailer);
			if (found == 7) {
				sprintf(rrdvalues, "%d:%lu:%lu:%lu:%lu:%lu:%lu:U",
					(int)tstamp, msgsfr, bytesfr*1024, msgsto, bytesto*1024, msgsrej, msgsdis);
				goto gotdata;
			}

			/* Last resort: Sendmail prior to 8.10 - without msgsrej, msgsdis, msgsqur */
			found = sscanf(bofdata, "%*s %lu %luK %lu %luK %s",
					&msgsfr, &bytesfr, &msgsto, &bytesto, mailer);
			if (found == 5) {
				sprintf(rrdvalues, "%d:%lu:%lu:%lu:%lu:U:U:U",
					(int)tstamp, msgsfr, bytesfr*1024, msgsto, bytesto*1024);
				goto gotdata;
			}

gotdata:
			if (*rrdvalues) {
				int dscount, i;
				char **dsnames = NULL;

				setupfn("sendmail.%s.rrd", mailer);

				/* Get the RRD-file dataset count, so we can decide what to do */
				dscount = rrddatasets(hostname, rrdfn, &dsnames);

				if ((dscount > 0) && dsnames) {
					/* Free the dsnames list */
					for (i=0; (i<dscount); i++) xfree(dsnames[i]);
					xfree(dsnames);
				}

				if (dscount == 6) {
					char *p;

					/* We have an existing RRD without the msgsqur DS. */
					/* Chop off the msgsqur item in rrdvalues */
					p = strrchr(rrdvalues, ':'); if (p) *p = '\0';
					create_and_update_rrd(hostname, testname, rrdfn, sendmail_params_1, sendmail_tpl_1);
				}
				else {
					/* New format, or it does not exist: Use latest format */
					create_and_update_rrd(hostname, testname, rrdfn, sendmail_params_2, sendmail_tpl_2);
				}
			}

			*eoln = '\n';
			bofdata = eoln+1;
			done = (*bofdata == '\0');
		}
		else done=1;

		MEMUNDEFINE(mailer);
	}

	if (eofdata) *(eofdata+1) = '=';


	return 0;
}

