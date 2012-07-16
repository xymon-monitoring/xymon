/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This file contains code to build the acknowledgement log shown on the      */
/* "all non-green" page.                                                      */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "libxymon.h"

int havedoneacklog = 0;

void do_acklog(FILE *output, int maxcount, int maxminutes)
{
	FILE *acklog;
	char acklogfilename[PATH_MAX];
	time_t cutoff;
	struct stat st;
	char l[MAX_LINE_LEN];
	char title[200];
	ack_t *acks;
	int num, ackintime_count;

	havedoneacklog = 1;

	cutoff = ( (maxminutes) ? (getcurrenttime(NULL) - maxminutes*60) : 0);
	if ((!maxcount) || (maxcount > 100)) maxcount = 100;

	sprintf(acklogfilename, "%s/acknowledge.log", xgetenv("XYMONSERVERLOGS"));
	acklog = fopen(acklogfilename, "r");
	if (!acklog) {
		/* BB compatible naming */
		sprintf(acklogfilename, "%s/acklog", xgetenv("XYMONACKDIR"));
		acklog = fopen(acklogfilename, "r");
	}
	if (!acklog) {
		/* If no acklog, that is OK - some people dont use acks */
		dbgprintf("Cannot open acklog\n");
		return;
	}

	/* HACK ALERT! */
	if (stat(acklogfilename, &st) == 0) {
		if (st.st_size != 0) {
			/* Assume a log entry is max 150 bytes */
			if (150*maxcount < st.st_size) {
				fseeko(acklog, -150*maxcount, SEEK_END);
				if ((fgets(l, sizeof(l), acklog) == NULL) || (strchr(l, '\n') == NULL)) {
					errprintf("Oops - couldnt find a newline in acklog\n");
				}
			}
		}
	}

	acks = (ack_t *) calloc(maxcount, sizeof(ack_t));
	ackintime_count = num = 0;

	while (fgets(l, sizeof(l), acklog)) {
		char ackedby[MAX_LINE_LEN], hosttest[MAX_LINE_LEN], color[10], ackmsg[MAX_LINE_LEN];
		char ackfn[PATH_MAX];
		char *testname;
		void *hinfo;
		int ok;

		if (atol(l) >= cutoff) {
			int c_used;
			char *p, *p1, *xymondacker = NULL;
			unsigned int atim;

			sscanf(l, "%u\t%d\t%d\t%d\t%s\t%s\t%s\t%n",
				&atim, &acks[num].acknum,
				&acks[num].duration, &acks[num].acknum2,
				ackedby, hosttest, color, &c_used);
			acks[num].acktime = atim;

			p1 = ackmsg;
			for (p=l+c_used, p1=ackmsg; (*p); ) {
				/*
				 * Need to de-code the ackmsg - it may have been entered
				 * via a web page that did "%asciival" encoding.
				 */
				if ((*p == '%') && (strlen(p) >= 3) && isxdigit((int)*(p+1)) && isxdigit((int)*(p+2))) {
					char hexnum[3];

					hexnum[0] = *(p+1);
					hexnum[1] = *(p+2);
					hexnum[2] = '\0';
					*p1 = (char) strtol(hexnum, NULL, 16);
					p1++;
					p += 3;
				}
				else {
					*p1 = *p;
					p1++;
					p++;
				}
			}
			*p1 = '\0';

			/* Xymon uses \n in the ack message, for the "acked by" data. Cut it off. */
			nldecode(ackmsg); p = strchr(ackmsg, '\n'); 
			if (p) {
				if (strncmp(p, "\nAcked by:", 10) == 0) xymondacker = p+10;
				*p = '\0';
			}

			/* Show only the first 30 characters in message */
			if (strlen(ackmsg) > 30) ackmsg[30] = '\0';

			sprintf(ackfn, "%s/ack.%s", xgetenv("XYMONACKDIR"), hosttest);

			testname = strrchr(hosttest, '.');
			if (testname) {
				*testname = '\0'; testname++; 
			}
			else testname = "unknown";

			ok = 1;

			/* Ack occurred within wanted timerange ? */
			if (ok && (acks[num].acktime < cutoff)) ok = 0;

			/* Unknown host ? */
			hinfo = hostinfo(hosttest);
			if (!hinfo) ok = 0;
			if (hinfo && xmh_item(hinfo, XMH_FLAG_NONONGREEN)) ok = 0;

			if (ok) {
				char *ackerp;

				/* If ack has expired or tag file is gone, the ack is no longer valid */
				acks[num].ackvalid = 1;
				if ((acks[num].acktime + 60*acks[num].duration) < getcurrenttime(NULL)) acks[num].ackvalid = 0;
				if (acks[num].ackvalid && (stat(ackfn, &st) != 0)) acks[num].ackvalid = 0;

				if (strcmp(ackedby, "np_filename_not_used") != 0) {
					ackerp = ackedby;
					if (strncmp(ackerp, "np_", 3) == 0) ackerp += 3;
					p = strrchr(ackerp, '_');
					if (p > ackerp) *p = '\0';
					acks[num].ackedby = strdup(ackerp);
				}
				else if (xymondacker) {
					acks[num].ackedby = strdup(xymondacker);
				}
				else {
					acks[num].ackedby = "";
				}

				acks[num].hostname = strdup(hosttest);
				acks[num].testname = strdup(testname);
				strcat(color, " "); acks[num].color = parse_color(color);
				acks[num].ackmsg = strdup(ackmsg);
				ackintime_count++;

				num = (num + 1) % maxcount;
			}
		}
	}

	if (ackintime_count > 0) {
		int firstack, lastack;
		int period = maxminutes;

		if (ackintime_count <= maxcount) {
			firstack = 0;
			lastack = ackintime_count-1;
			period = maxminutes;
		}
		else {
			firstack = num;
			lastack = ( (num == 0) ? maxcount : (num-1));
			ackintime_count = maxcount;
			period = ((getcurrenttime(NULL)-acks[firstack].acktime) / 60);
		}

		sprintf(title, "%d events acknowledged in the past %u minutes", ackintime_count, period);

		fprintf(output, "<BR><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"%s\" BORDER=0>\n", title);
		fprintf(output, "<TR BGCOLOR=\"#333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"#33ebf4\">%s</FONT></TD></TR>\n", title);

		for (num = lastack; (ackintime_count); ackintime_count--, num = ((num == 0) ? (maxcount-1) : (num - 1)) ) {
			fprintf(output, "<TR BGCOLOR=#000000>\n");

			fprintf(output, "<TD ALIGN=CENTER><FONT COLOR=white>%s</FONT></TD>\n", ctime(&acks[num].acktime));
			fprintf(output, "<TD ALIGN=CENTER BGCOLOR=%s><FONT COLOR=black>%s</FONT></TD>\n", colorname(acks[num].color), acks[num].hostname);
			fprintf(output, "<TD ALIGN=CENTER><FONT COLOR=white>%s</FONT></TD>\n", acks[num].testname);

			if (acks[num].color != -1) {
   				fprintf(output, "<TD ALIGN=CENTER><IMG SRC=\"%s/%s\"></TD>\n", 
					xgetenv("XYMONSKIN"), 
					dotgiffilename(acks[num].color, acks[num].ackvalid, 1));
			}
			else
   				fprintf(output, "<TD ALIGN=CENTER><FONT COLOR=white>&nbsp;</FONT></TD>\n");

			fprintf(output, "<TD ALIGN=LEFT BGCOLOR=#000033>%s</TD>\n", acks[num].ackedby);
			fprintf(output, "<TD ALIGN=LEFT>%s</TD></TR>\n", acks[num].ackmsg);
		}

	}
	else {
		sprintf(title, "No events acknowledged in the last %u minutes", maxminutes);

		fprintf(output, "<BR><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"%s\" BORDER=0>\n", title);
		fprintf(output, "<TR BGCOLOR=\"#333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"#33ebf4\">%s</FONT></TD></TR>\n", title);
	}

	fprintf(output, "</TABLE>\n");

	fclose(acklog);
}

