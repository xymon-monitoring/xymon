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

static char rcsid[] = "$Id: hobbitsvc-info.c,v 1.2 2003-01-28 06:47:51 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "bbgen.h"
#include "util.h"
#include "infogen.h"

char infocol[20] = "info";
int enable_infogen = 0;
int info_update_interval = 300; /* Update INFO pages every N seconds */

void generate_info(char *infocolumn)
{
	hostlist_t *hostwalk;
	struct utimbuf logfiletime;
	char infobuf[8192];
	char l[512];
	int ping, testip;

	if (!run_columngen("info", info_update_interval, enable_infogen))
		return;

	logfiletime.actime = logfiletime.modtime = (time(NULL) + atoi(getenv("PURPLEDELAY"))*60);

	for (hostwalk=hosthead; (hostwalk); hostwalk = hostwalk->next) {
		char logfn[256], htmlfn[256];
		FILE *fd;
		char *p, *hostname, *alertspec, *url, *slaspec, *noprop;

		sprintf(logfn, "%s/%s.%s", getenv("BBLOGS"), hostwalk->hostentry->hostname, infocolumn);
		if (getenv("BBHTML")) {
			sprintf(htmlfn,"%s/%s.%s.html", getenv("BBHTML"), hostwalk->hostentry->hostname, infocolumn);
		}
		else {
			sprintf(htmlfn,"%s/www/html/%s.%s.html", getenv("BBHOME"), hostwalk->hostentry->hostname, infocolumn);
		}

		infobuf[0] = '\0';
		hostname = strstr(hostwalk->hostentry->rawentry, "NAME:");
		if (!hostname) {
			sprintf(l, "Hostname : %s<br>\n", hostwalk->hostentry->hostname);
		}
		else {
			hostname += 5;
			p = strchr(hostname, ' ');
			if (p) *p = '\0';
			sprintf(l, "Hostname : %s<br>\n", hostname);
			if (p) *p = ' ';
		}
		strcat(infobuf, l);

		sprintf(l, "IP : %s<br>\n", hostwalk->hostentry->ip); strcat(infobuf, l);
		strcat(infobuf, "<br>\n");

		p = hostwalk->hostentry->alerts;
		if (p) {
			alertspec = (p+1); /* Skip leading comma */
			p = alertspec + strlen(alertspec) - 1; /* Point to trailing comma */
			if (*p == ',') *p = '\0'; else p = NULL;

			sprintf(l, "NK Alerts : %s<br>\n", alertspec); strcat(infobuf, l);
			if (p) *p = ',';
		}
		else {
			strcat(infobuf, "NK alerts : None<br>\n");
		}
		slaspec = strstr(hostwalk->hostentry->rawentry, "SLA=");
		if (slaspec) {
			slaspec +=4;
			p = strchr(slaspec, ' ');
			if (p) *p = '\0';
			sprintf(l, "Alert times : %s<br>\n", slaspec); strcat(infobuf, l);
			if (p) *p = ' ';
		}
		if (hostwalk->hostentry->nopropyellowtests) {
			noprop = (hostwalk->hostentry->nopropyellowtests+1);
			p = noprop + strlen(noprop) - 1; /* Point to trailing comma */
			if (*p == ',') *p = '\0'; else p = NULL;

			sprintf(l, "Suppressed warnings (yellow) : %s<br>\n", noprop);
			strcat(infobuf, l);
			if (p) *p = ',';
		}
		if (hostwalk->hostentry->nopropredtests) {
			noprop = (hostwalk->hostentry->nopropredtests+1);
			p = noprop + strlen(noprop) - 1; /* Point to trailing comma */
			if (*p == ',') *p = '\0'; else p = NULL;

			sprintf(l, "Suppressed alarms (red) : %s<br>\n", noprop);
			strcat(infobuf, l);
			if (p) *p = ',';
		}
		strcat(infobuf, "<br>\n");

		p = strstr(hostwalk->hostentry->rawentry, "NET:");
		if (p) {
			char *location;
			p += 4; location = p;
			p = strchr(location, ' ');
			if (p) *p = '\0';
			sprintf(l, "Tested from network : %s<br>\n", location); strcat(infobuf, l);
			if (p) *p = ' ';
		}

		testip = 0;
		if (strstr(hostwalk->hostentry->rawentry, "testip")) testip = 1;
		sprintf(l, "Network tests use : %s<br>\n", (testip ? "IP-address" : "hostname")); strcat(infobuf, l);

		ping = 1;
		if (strstr(hostwalk->hostentry->rawentry, "noping")) ping = 0;
		if (strstr(hostwalk->hostentry->rawentry, "noconn")) ping = 0;
		sprintf(l, "Checked with ping : %s<br>\n", (ping ? "Yes" : "No")); strcat(infobuf, l);
		strcat(infobuf, "<br>\n");

		url = strstr(hostwalk->hostentry->rawentry, " http");
		if (url) {
			strcat(infobuf, "URL checks:<br>\n");

			while (url) {
				url++;  /* Skip space */
				p = strchr(url, ' ');
				if (p) *p = '\0';
				sprintf(l, "&nbsp;&nbsp;<a href=\"%s\">%s</a><br>\n", realurl(url), url); 
				strcat(infobuf, l);
				if (p) {
					*p = ' ';
					url = strstr(p, " http");
				}
				else {
					url = NULL;
				}
			}
			strcat(infobuf, "<br>\n");
		}

		url = strstr(hostwalk->hostentry->rawentry, " content=");
		if (url) {
			strcat(infobuf, "Content checks:<br>\n");

			while (url) {
				url+=9;  /* Skip " content=" */
				p = strchr(url, ' ');
				if (p) *p = '\0';
				sprintf(l, "&nbsp;&nbsp;<a href=\"%s\">%s</a><br>\n", realurl(url), url); 
				strcat(infobuf, l);
				if (p) {
					*p = ' ';
					url = strstr(p, " content=");
				}
				else {
					url = NULL;
				}
			}
			strcat(infobuf, "<br>\n");
		}

		strcat(infobuf, "Other tags : ");
		p = strtok(hostwalk->hostentry->rawentry, " \t");
		while (p) {
			if (
					(strncmp(p, "#", 1) != 0)
				&&	(strncmp(p, "NK:", 3) != 0)
				&&	(strncmp(p, "NET:", 4) != 0)
				&&	(strncmp(p, "NOPROP:", 7) != 0)
				&&	(strncmp(p, "NOPROPRED:", 10) != 0)
				&&	(strncmp(p, "NOPROPYELLOW:", 13) != 0)
				&&	(strncmp(p, "SLA=", 4) != 0)
				&&	(strncmp(p, "http", 4) != 0)
				&&	(strncmp(p, "content=", 8) != 0)
				&&	(strncmp(p, "testip", 6) != 0)
				&&	(strncmp(p, "noconn", 6) != 0)
				&&	(strncmp(p, "noping", 6) != 0)
			   )  {
				sprintf(l, "%s ", p);
				strcat(infobuf, l);
			}

			p = strtok(NULL, " \t");
		}

		fd = fopen(logfn, "w");
		if (!fd) {
			perror("Cannot open logfile");
			exit(1);
		}

		fprintf(fd, "green %s info\n\n", timestamp);
		fprintf(fd, "%s", infobuf);
		fclose(fd);
		utime(logfn, &logfiletime);


		sethostenv(hostwalk->hostentry->hostname, hostwalk->hostentry->ip, infocolumn, "green");
		fd = fopen(htmlfn, "w");
 		headfoot(fd, "hostsvc", "", "", "header", COL_GREEN);

		fprintf(fd, "<!-- Start of code generated by Big Brother Systems and Network Monitor -->\n");
		fprintf(fd, "\n");
		fprintf(fd, "<A NAME=begindata>&nbsp;</A>\n");
		fprintf(fd, "\n");
		fprintf(fd, "<CENTER><TABLE ALIGN=CENTER BORDER=0>\n");
		fprintf(fd, "<TR><TH ALIGN=CENTER><FONT %s>\n", getenv("MKBBROWFONT"));
		fprintf(fd, "%s - %s<BR><HR WIDTH=60%%></FONT></TH>\n", hostwalk->hostentry->hostname, infocolumn);

		fprintf(fd, "<TR><TD ALIGN=LEFT>\n<br>%s\n</TD></TR>\n", infobuf);
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

