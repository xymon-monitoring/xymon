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

static char rcsid[] = "$Id: hobbitsvc-trends.c,v 1.3 2002-12-20 08:48:08 hstoerne Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>

#include "bbgen.h"
#include "util.h"
#include "loaddata.h"
#include "larrdgen.h"

int enable_larrdgen = 0;

static int run_larrdgen(void)
{
	/* Check if $BBTMP/.larrdgen exists */

	FILE	*fd;
	char	fn[256];

	if (!enable_larrdgen)
		return 0;

	sprintf(fn, "%s/.larrdgen", getenv("BBTMP"));
	fd = fopen(fn, "r");
	if (fd != NULL) {
		fclose(fd);
		unlink(fn);
		return 1;
	}
	else {
		return 0;
	}
}

void generate_larrd(char *rrddirname, char *larrdcolumn)
{
	DIR *rrddir;
	struct dirent *d;
	char fn[256];
	hostlist_t *hostwalk;
	rrd_t *rwalk;
	int i;
	char *allrrdlinks;
	char timestamp[100];
	time_t now;
	struct utimbuf logfiletime;

	if (!run_larrdgen())
		return;


	for (i=0; rrdnames[i]; i++) ;
	allrrdlinks = malloc(256*i);

	now = time(NULL);
	strcpy(timestamp, ctime(&now));
	timestamp[strlen(timestamp)-1] = '\0';

	i = atoi(getenv("PURPLEDELAY"));
	logfiletime.actime = logfiletime.modtime = now + i*60;


	chdir(rrddirname);
	rrddir = opendir(rrddirname);
	if (!rrddir) {
		perror("Cannot access RRD directory");
		exit(1);
	}

	while ((d = readdir(rrddir))) {
		strcpy(fn, d->d_name);

		if ((strlen(fn) > 4) && (strcmp(fn+strlen(fn)-4, ".rrd") == 0)) {
			char *p, *rrdname;
			char *r;
			int found;
			int i;

			for (p=fn; *p; p++) {
				if (*p == ',') *p = '.';
			}

			hostwalk = hosthead; found = 0;
			while (hostwalk && (!found)) {
				if (strncmp(hostwalk->hostentry->hostname, fn, strlen(hostwalk->hostentry->hostname)) == 0) {
					/* First part of filename matches.
					   Now check that there is a valid RRD id next -
					   if not, then we may have hit a partial hostname */

					rrdname = fn + strlen(hostwalk->hostentry->hostname) + 1;
					p = strchr(rrdname, '.');
					*p = '\0';

					for (i=0; (rrdnames[i] && (strcmp(rrdnames[i], rrdname) != 0)); i++) ;
					if (rrdnames[i]) {
						found = 1;
						r = rrdnames[i];
					}
				}

				if (!found) {
					hostwalk = hostwalk->next;
				}
			}

			if (found) {
				for (rwalk = hostwalk->hostentry->rrds; (rwalk && (rwalk->rrdname != r)); rwalk = rwalk->next) ;
				if (rwalk == NULL) {
					rrd_t *newrrd = malloc(sizeof(rrd_t));

					newrrd->rrdname = r;
					newrrd->next = hostwalk->hostentry->rrds;
					hostwalk->hostentry->rrds = newrrd;
				}
				/* printf("Host\t%-40s\t\trrd %s\n", hostwalk->hostentry->hostname, r); */
			}
		}
	}

	chdir(getenv("BBLOGS"));

	for (hostwalk=hosthead; (hostwalk); hostwalk = hostwalk->next) {
		char logfn[256], htmlfn[256], rrdlink[512];
		FILE *fd;
		int i;

		sprintf(logfn, "%s/%s.%s", getenv("BBLOGS"), hostwalk->hostentry->hostname, larrdcolumn);
		if (getenv("BBHTML")) {
			sprintf(htmlfn,"%s/%s.%s.html", getenv("BBHTML"), hostwalk->hostentry->hostname, larrdcolumn);
		}
		else {
			sprintf(htmlfn,"%s/www/html/%s.%s.html", getenv("BBHOME"), hostwalk->hostentry->hostname, larrdcolumn);
		}


		strcpy(allrrdlinks, "");

		for (i=0; rrdnames[i]; i++) {
			for (rwalk = hostwalk->hostentry->rrds; (rwalk && (rwalk->rrdname != rrdnames[i])); rwalk = rwalk->next) ;
			if (rwalk) {
				sprintf(rrdlink, "<p><A HREF=\"%s/larrd-grapher.cgi?host=%s&service=%s\"><IMG SRC=\"%s/larrd-grapher.cgi?host=%s&service=%s&graph=hourly\" ALT=\"larrd is accumulating %s\" BORDER=0></A>\n\n",
					getenv("CGIBINURL"), hostwalk->hostentry->hostname, rwalk->rrdname,
					getenv("CGIBINURL"), hostwalk->hostentry->hostname, rwalk->rrdname,
					rwalk->rrdname);

				strcat(allrrdlinks, rrdlink);
			}
		}


		fd = fopen(logfn, "w");
		if (!fd) {
			perror("Cannot open logfile");
			exit(1);
		}

		fprintf(fd, "green %s - larrd is accumulating <center><BR>\n", timestamp);
		fprintf(fd, "%s\n", allrrdlinks);
		fprintf(fd, "</center>\n");
		fclose(fd);
		utime(logfn, &logfiletime);

		sethostenv(hostwalk->hostentry->hostname, hostwalk->hostentry->ip, larrdcolumn, "green");
		fd = fopen(htmlfn, "w");
 		headfoot(fd, "hostsvc", "", "", "header", COL_GREEN);

		fprintf(fd, "<!-- Start of code generated by Big Brother Systems and Network Monitor -->

<A NAME=begindata>&nbsp;</A>

<CENTER><TABLE ALIGN=CENTER BORDER=0>
<TR><TH><FONT SIZE=+1 COLOR=\"#FFFFCC\" FACE=\"Tahoma, Arial, Helvetica\">
%s - %s<BR><HR WIDTH=60%%></TH>
<TR><TD><H3>
green %s - larrd is accumulating <center><BR>
</H3><PRE>",
		hostwalk->hostentry->hostname, larrdcolumn, timestamp);

		fprintf(fd, "%s\n", allrrdlinks);
		fprintf(fd, "
</PRE>
</TD></TR></TABLE>

<TABLE ALIGN=CENTER BORDER=0>
<TR><TD ALIGN=CENTER>
<FONT COLOR=teal SIZE=-1>
<BR></center></FONT></TD></TR>
</TABLE>

</CENTER>

<!-- End of code generated by Big Brother Systems and Network Monitor -->");

 		headfoot(fd, "hostsvc", "", "", "footer", COL_GREEN);
		fclose(fd);
	}

	closedir(rrddir);
	free(allrrdlinks);
}

