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

static char rcsid[] = "$Id: hobbitsvc-trends.c,v 1.17 2003-02-22 08:29:18 henrik Exp $";

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

char    larrdcol[20] = "larrd";
int 	enable_larrdgen = 0;
int 	larrd_update_interval = 300; /* Update LARRD pages every N seconds */
int     log_nohost_rrds = 0;

char	*rrdnames[] = { 
        "la",
        "disk",
        "memory",
        "tcp",
        "citrix",
        "users",
        "vmstat",
        "netstat",
        "iostat",
	"ntpstat",
        NULL
};

int generate_larrd(char *rrddirname, char *larrdcolumn)
{
	DIR *rrddir;
	struct dirent *d;
	char fn[256];
	hostlist_t *hostwalk;
	rrd_t *rwalk;
	int i;
	char *allrrdlinks;
	time_t now;
	struct utimbuf logfiletime;

	if (!run_columngen("larrd", larrd_update_interval, enable_larrdgen))
		return 1;


	for (i=0; rrdnames[i]; i++) ;
	allrrdlinks = malloc(256*i);

	now = time(NULL);
	i = atoi(getenv("PURPLEDELAY"));
	logfiletime.actime = logfiletime.modtime = now + i*60;

	/*
	 * General idea: Scan the RRD directory for all RRD files, and 
	 * pick up which RRD's are present for each host.
	 * Since there are only a limited set of possible RRD links to
	 * generate, this does not take up a huge hunk of memory.
	 * Then, loop over the list of hosts, and generate a log
	 * file and an html file for the larrd column.
	 */

	chdir(rrddirname);
	rrddir = opendir(rrddirname);
	if (!rrddir) {
		printf("Cannot access RRD directory\n");
		return 1;
	}

	while ((d = readdir(rrddir))) {
		strcpy(fn, d->d_name);

		if ((strlen(fn) > 4) && (strcmp(fn+strlen(fn)-4, ".rrd") == 0)) {
			char *p, *rrdname;
			char *r = NULL;
			int found, hostfound;
			int i;

			/* Logfiles use ',' instead of '.' in FQDN hostnames */
			for (p=fn; *p; p++) {
				if (*p == ',') *p = '.';
			}

			/* Is this a known host? */
			hostwalk = hosthead; found = hostfound = 0;
			while (hostwalk && (!found)) {
				if (strncmp(hostwalk->hostentry->hostname, fn, strlen(hostwalk->hostentry->hostname)) == 0) {

					p = fn + strlen(hostwalk->hostentry->hostname);
					hostfound = ( (*p == '.') || (*p = ',') );

					/* First part of filename matches.
					 * Now check that there is a valid RRD id next -
					 * if not, then we may have hit a partial hostname 
					 */

					rrdname = fn + strlen(hostwalk->hostentry->hostname) + 1;
					p = strchr(rrdname, '.');
					if (p) *p = '\0';

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
				/* hostwalk now points to the host owning this RRD */
				for (rwalk = hostwalk->hostentry->rrds; (rwalk && (rwalk->rrdname != r)); rwalk = rwalk->next) ;
				if (rwalk == NULL) {
					rrd_t *newrrd = malloc(sizeof(rrd_t));

					newrrd->rrdname = r;
					newrrd->next = hostwalk->hostentry->rrds;
					hostwalk->hostentry->rrds = newrrd;
				}
				/* printf("Host\t%-40s\t\trrd %s\n", hostwalk->hostentry->hostname, r); */
			}

			if (!hostfound && log_nohost_rrds) {
				/* This rrd file has no matching host. */
				printf("No host record for rrd %s\n", d->d_name);
			}
		}
	}

	chdir(getenv("BBLOGS"));

	for (hostwalk=hosthead; (hostwalk); hostwalk = hostwalk->next) {
		char logfn[256], htmlfn[256], rrdlink[512];
		FILE *fd;
		int i;

		sprintf(logfn, "%s/%s.%s", getenv("BBLOGS"), 
			commafy(hostwalk->hostentry->hostname), larrdcolumn);
		if (getenv("BBHTML")) {
			sprintf(htmlfn,"%s/%s.%s.html", getenv("BBHTML"), 
				hostwalk->hostentry->hostname, larrdcolumn);
		}
		else {
			sprintf(htmlfn,"%s/www/html/%s.%s.html", getenv("BBHOME"), 
				hostwalk->hostentry->hostname, larrdcolumn);
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


		if (strlen(allrrdlinks) > 0) {
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

			/* HTML files generated only if we use BBLOGSTATUS=STATIC */
			if (generate_static()) {
				sethostenv(hostwalk->hostentry->hostname, hostwalk->hostentry->ip, larrdcolumn, "green");
				fd = fopen(htmlfn, "w");
 				headfoot(fd, "hostsvc", "", "", "header", COL_GREEN);

				fprintf(fd, "<!-- Start of code generated by Big Brother Systems and Network Monitor -->\n");
				fprintf(fd, "\n");
				fprintf(fd, "<A NAME=begindata>&nbsp;</A>\n");
				fprintf(fd, "\n");
				fprintf(fd, "<CENTER><TABLE ALIGN=CENTER BORDER=0>\n");
				fprintf(fd, "<TR><TH><FONT %s>\n", getenv("MKBBROWFONT"));
				fprintf(fd, "%s - %s<BR><HR WIDTH=60%%></TH>\n", hostwalk->hostentry->hostname, larrdcolumn);
				fprintf(fd, "<TR><TD><H3>\n");
				fprintf(fd, "green %s - larrd is accumulating <center><BR>\n", timestamp);
				fprintf(fd, "</H3><PRE>\n");

				fprintf(fd, "%s\n", allrrdlinks);
				fprintf(fd, "\n");

				fprintf(fd, "</PRE>\n");
				fprintf(fd, "</TD></TR></TABLE>\n");
				fprintf(fd, "\n");
				fprintf(fd, "<TABLE ALIGN=CENTER BORDER=0>\n");
				fprintf(fd, "<TR><TD ALIGN=CENTER>\n");
				fprintf(fd, "<FONT COLOR=teal SIZE=-1>\n");
				fprintf(fd, "<BR></center></FONT></TD></TR>\n");
				fprintf(fd, "</TABLE>\n");
				fprintf(fd, "\n");
				fprintf(fd, "</CENTER>\n");
				fprintf(fd, "\n");
				fprintf(fd, "<!-- End of code generated by Big Brother Systems and Network Monitor -->\n");

 				headfoot(fd, "hostsvc", "", "", "footer", COL_GREEN);
				fclose(fd);
			}
		}
	}

	closedir(rrddir);
	free(allrrdlinks);
	return 0;
}

