/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: acklog.c,v 1.4 2004-10-29 10:21:57 henrik Exp $";

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

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "acklog.h"

int havedoneacklog = 0;

void do_acklog(FILE *output, int maxcount, int maxminutes)
{
	FILE *acklog;
	char acklogfilename[MAX_PATH];
	time_t cutoff;
	struct stat st;
	char l[MAX_LINE_LEN];
	char title[200];
	ack_t *acks;
	int num, ackintime_count;

	havedoneacklog = 1;

	cutoff = ( (maxminutes) ? (time(NULL) - maxminutes*60) : 0);
	if ((!maxcount) || (maxcount > 100)) maxcount = 100;

	sprintf(acklogfilename, "%s/acklog", getenv("BBACKS"));
	acklog = fopen(acklogfilename, "r");
	if (!acklog) {
		/* If no acklog, that is OK - some people dont use acks */
		dprintf("Cannot open acklog");
		return;
	}

	/* HACK ALERT! */
	if (stat(acklogfilename, &st) == 0) {
		/* Assume a log entry is max 150 bytes */
		if (150*maxcount < st.st_size)
			fseek(acklog, -150*maxcount, SEEK_END);
		fgets(l, sizeof(l), acklog);
		if (strchr(l, '\n') == NULL) {
			errprintf("Oops - couldnt find a newline in acklog\n");
		}
	}

	acks = (ack_t *) malloc(maxcount*sizeof(ack_t));
	ackintime_count = num = 0;

	while (fgets(l, sizeof(l), acklog)) {
		char ackedby[MAX_LINE_LEN], hosttest[MAX_LINE_LEN], color[10], ackmsg[MAX_LINE_LEN];
		char ackfn[MAX_PATH];
		char *testname;
		int ok;

		if (atol(l) >= cutoff) {
			int c_used;
			char *p, *p1;

			sscanf(l, "%u\t%d\t%d\t%d\t%s\t%s\t%s\t%n",
				(unsigned int *)&acks[num].acktime, &acks[num].acknum,
				&acks[num].duration, &acks[num].acknum2,
				ackedby, hosttest, color, &c_used);

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
			/* Show only the first 30 characters in message */
			ackmsg[30] = '\0';

			sprintf(ackfn, "%s/ack.%s", getenv("BBACKS"), hosttest);

			testname = strrchr(hosttest, '.');
			if (testname) {
				*testname = '\0'; testname++; 
			}
			else testname = "unknown";

			ok = 1;

			/* Ack occurred within wanted timerange ? */
			if (ok && (acks[num].acktime < cutoff)) ok = 0;

			/* Unknown host ? */
			if (ok && (find_host(hosttest) == NULL)) ok = 0;

			if (ok) {
				char *ackerp;

				/* If ack has expired or tag file is gone, the ack is no longer valid */
				acks[num].ackvalid = 1;
				if ((acks[num].acktime + 60*acks[num].duration) < time(NULL)) acks[num].ackvalid = 0;
				if (acks[num].ackvalid && (stat(ackfn, &st) != 0)) acks[num].ackvalid = 0;

				ackerp = ackedby;
				if (strncmp(ackerp, "np_", 3) == 0) ackerp += 3;
				p = strrchr(ackerp, '_');
				if (p > ackerp) *p = '\0';
				acks[num].ackedby = strdup(ackerp);

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
			period = ((time(NULL)-acks[firstack].acktime) / 60);
		}

		sprintf(title, "%d events acknowledged in the past %u minutes", ackintime_count, period);

		fprintf(output, "<BR><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"%s\" BORDER=0>\n", title);
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD></TR>\n", title);

		for (num = lastack; (ackintime_count); ackintime_count--, num = ((num == 0) ? (maxcount-1) : (num - 1)) ) {
			fprintf(output, "<TR BGCOLOR=#000000>\n");

			fprintf(output, "<TD ALIGN=CENTER><FONT COLOR=white>%s</FONT></TD>\n", ctime(&acks[num].acktime));
			fprintf(output, "<TD ALIGN=CENTER BGCOLOR=%s><FONT COLOR=black>%s</FONT></TD>\n", colorname(acks[num].color), acks[num].hostname);
			fprintf(output, "<TD ALIGN=CENTER><FONT COLOR=white>%s</FONT></TD>\n", acks[num].testname);

			if (acks[num].color != -1) {
   				fprintf(output, "<TD ALIGN=CENTER><IMG SRC=\"%s/%s\"></TD>\n", 
					getenv("BBSKIN"), 
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
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD></TR>\n", title);
	}

	fprintf(output, "</TABLE>\n");

	fclose(acklog);
}

