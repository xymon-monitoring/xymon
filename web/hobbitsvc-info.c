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

static char rcsid[] = "$Id: hobbitsvc-info.c,v 1.46 2004-08-02 13:20:49 henrik Exp $";

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
#include "pagegen.h"		/* for documentationurl variable */
#include "infogen.h"
#include "alert.h"

char *infocol = "info";
int enable_infogen = 0;
int info_update_interval = 300; /* Update INFO pages every N seconds */

static char *service_text(char *svc)
{
	if (strlen(svc) == 0) return "&nbsp;";

	if (strcmp(svc, "*") == 0) return "All"; else return svc;
}

static void timespec_text(char *spec, char **infobuf, int *infobuflen)
{
	char l[MAX_LINE_LEN];
	char *sCopy;
	char *sItem;

	sCopy = malcop(spec);
	sCopy[strcspn(sCopy, " \t\r\n")] = '\0';
	sItem = strtok(sCopy, ",");
	while (sItem) {
		l[0] = '\0';

		switch (*sItem) {
			case '*': sprintf(l, "All days%s", (sItem+1));
				  break;
			case 'W': sprintf(l, "Weekdays%s", (sItem+1));
				  break;
			case '0': sprintf(l, "Sunday%s", (sItem+1));
				  break;
			case '1': sprintf(l, "Monday%s", (sItem+1));
				  break;
			case '2': sprintf(l, "Tuesday%s", (sItem+1));
				  break;
			case '3': sprintf(l, "Wednesday%s", (sItem+1));
				  break;
			case '4': sprintf(l, "Thursday%s", (sItem+1));
				  break;
			case '5': sprintf(l, "Friday%s", (sItem+1));
				  break;
			case '6': sprintf(l, "Saturday%s", (sItem+1));
				  break;
			default:
				  break;
		}

		sItem = strtok(NULL, ",");
		if (sItem) strcat(l, ", ");
		addtobuffer(infobuf, infobuflen, l);
	}
	free(sCopy);
}

int generate_info(char *infocolumn)
{
	hostlist_t *hostwalk, *clonewalk;
	struct utimbuf logfiletime;
	int infobuflen = 0;
	char *infobuf = NULL;
	char l[MAX_LINE_LEN];
	int ping, testip, dialup;
	alertrec_t *alerts;

	if (!run_columngen("info", info_update_interval, enable_infogen))
		return 1;

	/* Load the alert setup */
	load_alerts();

	logfiletime.actime = logfiletime.modtime = (time(NULL) + atoi(getenv("PURPLEDELAY"))*60);
	infobuflen = 4096; infobuf = (char *)malloc(infobuflen); *infobuf = '\0';

	for (hostwalk=hosthead; (hostwalk); hostwalk = hostwalk->next) {
		char logfn[MAX_PATH], htmlfn[MAX_PATH];
		FILE *fd;
		char *p, *alertspec, *slaspec, *noprop, *rawcopy;
		int firstcontent;

		if (hostwalk->hostentry->banksize > 0) continue; /* No info for modem-banks */

		sprintf(logfn, "%s/%s.%s", getenv("BBLOGS"), 
			commafy(hostwalk->hostentry->hostname), infocolumn);
		if (getenv("BBHTML")) {
			sprintf(htmlfn,"%s/%s.%s.html", getenv("BBHTML"), 
				hostwalk->hostentry->hostname, infocolumn);
		}
		else {
			sprintf(htmlfn,"%s/www/html/%s.%s.html", getenv("BBHOME"), 
				hostwalk->hostentry->hostname, infocolumn);
		}

		*infobuf = '\0';
		addtobuffer(&infobuf, &infobuflen, "<table width=\"100%%\">\n");

		if (hostwalk->hostentry->displayname && (strcmp(hostwalk->hostentry->displayname, hostwalk->hostentry->hostname) != 0)) {
			sprintf(l, "<tr><th align=left>Hostname:</th><td align=left>%s (%s)</td></tr>\n", 
				hostwalk->hostentry->displayname, hostwalk->hostentry->hostname);
		}
		else {
			sprintf(l, "<tr><th align=left>Hostname:</th><td align=left>%s</td></tr>\n", hostwalk->hostentry->hostname);
		}
		addtobuffer(&infobuf, &infobuflen, l);

		if (hostwalk->hostentry->clientalias) {
			sprintf(l, "<tr><th align=left>Client alias:</th><td align=left>%s</td></tr>\n", hostwalk->hostentry->clientalias);
			addtobuffer(&infobuf, &infobuflen, l);
		}

		sprintf(l, "<tr><th align=left>IP:</th><td align=left>%s</td></tr>\n", hostwalk->hostentry->ip);
		addtobuffer(&infobuf, &infobuflen, l);
		if (documentationurl) {
			sprintf(l, "<tr><th align=left>Documentation:</th><td align=left><a href=\"%s\">%s</a>\n", 
				urldoclink(documentationurl, hostwalk->hostentry->hostname),
				urldoclink(documentationurl, hostwalk->hostentry->hostname));
			addtobuffer(&infobuf, &infobuflen, l);
		}
		if (hostwalk->hostentry->link != &null_link) {
			sprintf(l, "<tr><th align=left>Notes:</th><td align=left><a href=\"%s\">%s%s</a>\n", 
				hostlink(hostwalk->hostentry->link),
				getenv("BBWEBHOST"),
				hostlink(hostwalk->hostentry->link));
			addtobuffer(&infobuf, &infobuflen, l);
		}
		sprintf(l, "<tr><th align=left>Page/subpage:</th><td align=left><a href=\"%s/%s\">%s</a>\n", 
			getenv("BBWEB"), hostpage_link(hostwalk->hostentry), hostpage_name(hostwalk->hostentry));
		addtobuffer(&infobuf, &infobuflen, l);
		for (clonewalk = hostwalk->clones; (clonewalk); clonewalk = clonewalk->next) {
			sprintf(l, "<br><a href=\"%s/%s\">%s</a>\n", 
				getenv("BBWEB"), hostpage_link(clonewalk->hostentry), hostpage_name(clonewalk->hostentry));
			addtobuffer(&infobuf, &infobuflen, l);
		}
		addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		if (hostwalk->hostentry->description) {
			char *delim = strchr(hostwalk->hostentry->description, ':');

			if (delim) *delim = '\0';
			sprintf(l, "<tr><th align=left>Host type:</th><td align=left>%s</td></tr>\n",
				hostwalk->hostentry->description);
			addtobuffer(&infobuf, &infobuflen, l);
			if (delim) { 
				*delim = ':'; 
				delim++;
				sprintf(l, "<tr><th align=left>Description:</th><td align=left>%s</td></tr>\n", delim);
				addtobuffer(&infobuf, &infobuflen, l);
			}
		}
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		p = hostwalk->hostentry->alerts;
		if (p) {
			alertspec = (p+1); /* Skip leading comma */
			p = alertspec + strlen(alertspec) - 1; /* Point to trailing comma */
			if (*p == ',') *p = '\0'; else p = NULL;

			sprintf(l, "<tr><th align=left>NK Alerts:</th><td align=left>%s", alertspec); 
			addtobuffer(&infobuf, &infobuflen, l);
			if (p) *p = ',';

			slaspec = strstr(hostwalk->hostentry->rawentry, "NKTIME=");
			if (slaspec) {
				slaspec +=7;
				p = strchr(slaspec, ' ');
				if (p) *p = '\0';
				sprintf(l, " (%s)", slaspec);
				addtobuffer(&infobuf, &infobuflen, l);
				if (p) *p = ' ';
			}
			else addtobuffer(&infobuf, &infobuflen, " (24x7)");

			addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		}
		else {
			addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>NK alerts:</th><td align=left>None</td></tr>\n");
		}
		slaspec = strstr(hostwalk->hostentry->rawentry, "NKTIME=");
		if (slaspec) {
			slaspec +=7;

			addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>NK alerts shown:</th><td align=left>");
			timespec_text(slaspec, &infobuf, &infobuflen);
			addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		}
		slaspec = strstr(hostwalk->hostentry->rawentry, "SLA=");
		if (slaspec) {
			slaspec +=4;

			addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>Alert times:</th><td align=left>");
			timespec_text(slaspec, &infobuf, &infobuflen);
			addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		}
		slaspec = strstr(hostwalk->hostentry->rawentry, "DOWNTIME=");
		if (slaspec) {
			slaspec +=9;

			addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>Planned downtime:</th><td align=left>");
			timespec_text(slaspec, &infobuf, &infobuflen);
			addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		}
		slaspec = strstr(hostwalk->hostentry->rawentry, "REPORTTIME=");
		if (slaspec) {
			slaspec +=11;

			addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>SLA report period:</th><td align=left>");
			timespec_text(slaspec, &infobuf, &infobuflen);
			addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");

			sprintf(l, "<tr><th align=left>SLA Availability:</th><td align=left>%.2f</td></tr>\n", hostwalk->hostentry->reportwarnlevel); 
			addtobuffer(&infobuf, &infobuflen, l);
		}
		if (hostwalk->hostentry->nopropyellowtests) {
			noprop = (hostwalk->hostentry->nopropyellowtests+1);
			p = noprop + strlen(noprop) - 1; /* Point to trailing comma */
			if (*p == ',') *p = '\0'; else p = NULL;

			sprintf(l, "<tr><th align=left>Suppressed warnings (yellow):</th><td align=left>%s</td></tr>\n", noprop);
			addtobuffer(&infobuf, &infobuflen, l);
			if (p) *p = ',';
		}
		if (hostwalk->hostentry->nopropredtests) {
			noprop = (hostwalk->hostentry->nopropredtests+1);
			p = noprop + strlen(noprop) - 1; /* Point to trailing comma */
			if (*p == ',') *p = '\0'; else p = NULL;

			sprintf(l, "<tr><th align=left>Suppressed alarms (red):</th><td align=left>%s</td></tr>\n", noprop);
			addtobuffer(&infobuf, &infobuflen, l);
			if (p) *p = ',';
		}
		if (hostwalk->hostentry->noproppurpletests) {
			noprop = (hostwalk->hostentry->noproppurpletests+1);
			p = noprop + strlen(noprop) - 1; /* Point to trailing comma */
			if (*p == ',') *p = '\0'; else p = NULL;

			sprintf(l, "<tr><th align=left>Suppressed alarms (purple):</th><td align=left>%s</td></tr>\n", noprop);
			addtobuffer(&infobuf, &infobuflen, l);
			if (p) *p = ',';
		}
		if (hostwalk->hostentry->nopropacktests) {
			noprop = (hostwalk->hostentry->nopropacktests+1);
			p = noprop + strlen(noprop) - 1; /* Point to trailing comma */
			if (*p == ',') *p = '\0'; else p = NULL;

			sprintf(l, "<tr><th align=left>Suppressed alarms (acked):</th><td align=left>%s</td></tr>\n", noprop);
			addtobuffer(&infobuf, &infobuflen, l);
			if (p) *p = ',';
		}
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		p = strstr(hostwalk->hostentry->rawentry, "NET:");
		if (p) {
			char *location;
			p += 4; location = p;
			p = strchr(location, ' ');
			if (p) *p = '\0';
			sprintf(l, "<tr><th align=left>Tested from network:</th><td align=left>%s</td></tr>\n", location);
			addtobuffer(&infobuf, &infobuflen, l);
			if (p) *p = ' ';
		}

		dialup = 0;
		if (strstr(hostwalk->hostentry->rawentry, "dialup")) dialup = 1;
		if (dialup) addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2 align=left>Host downtime does not trigger alarms (dialup host)</td></tr>\n");

		testip = 0;
		if (strstr(hostwalk->hostentry->rawentry, "testip")) testip = 1;
		sprintf(l, "<tr><th align=left>Network tests use:</th><td align=left>%s</td></tr>\n", 
			(testip ? "IP-address" : "Hostname"));
		addtobuffer(&infobuf, &infobuflen, l);

		ping = 1;
		if (strstr(hostwalk->hostentry->rawentry, "noping")) ping = 0;
		if (strstr(hostwalk->hostentry->rawentry, "noconn")) ping = 0;
		sprintf(l, "<tr><th align=left>Checked with ping:</th><td align=left>%s</td></tr>\n", (ping ? "Yes" : "No"));
		addtobuffer(&infobuf, &infobuflen, l);

		p = strstr(hostwalk->hostentry->rawentry, "TIMEOUT:");
		if (p) {
			char *tspec = malcop(p);
			int t1, t2;

			if (sscanf(tspec, "TIMEOUT:%d:%d", &t1, &t2) == 2) {
				sprintf(l, "<tr><th align=left>Network timeout:</th><<td align=left>%d seconds (connect), %d seconds (full request)</td></tr>\n",
					t1, t2);
				addtobuffer(&infobuf, &infobuflen, l);
			}
			free(tspec);
		}

		/* Space */
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		rawcopy = malcop(hostwalk->hostentry->rawentry);
		firstcontent = 1;
		p = strtok(rawcopy, " \t");
		while (p) {
			if (*p == '~') p++;

			if (strncmp(p, "http", 4) == 0) {
				if (firstcontent) {
					addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>URL checks:</th><td align=left>\n");
					firstcontent = 0;
				}

				sprintf(l, "<a href=\"%s\">%s</a><br>\n", 
					realurl(p, NULL, NULL, NULL, NULL), 
					realurl(p, NULL, NULL, NULL, NULL)); 
				addtobuffer(&infobuf, &infobuflen, l);
			}
			p = strtok(NULL, " \t");
		}
		if (!firstcontent) addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");

		strcpy(rawcopy, hostwalk->hostentry->rawentry);
		firstcontent = 1;
		p = strtok(rawcopy, " \t");
		while (p) {
			if (*p == '~') p++;

			if ( (strncmp(p, "content=", 8) == 0) ||
			     (strncmp(p, "cont;", 5) == 0)    ||
			     (strncmp(p, "nocont;", 7) == 0)  ||
			     (strncmp(p, "type;", 5) == 0)    ||
			     (strncmp(p, "post;", 5) == 0)    ||
			     (strncmp(p, "nopost;", 7) == 0) ) {

				if (firstcontent) {
					addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>Content checks:</th><td align=left>\n");
					firstcontent = 0;
				}

				sprintf(l, "<a href=\"%s\">%s</a>", 
					realurl(p, NULL, NULL, NULL, NULL), 
					realurl(p, NULL, NULL, NULL, NULL)); 
				addtobuffer(&infobuf, &infobuflen, l);
				if ((strncmp(p, "cont;", 5) == 0) || (strncmp(p, "nocont;", 7) == 0) || 
				    (strncmp(p, "type;", 5) == 0) ||
				    (strncmp(p, "post;", 5) == 0) || (strncmp(p, "nopost;", 7) == 0)) {
					char *wanted = strrchr(p, ';');

					if (wanted) {
						wanted++;
						sprintf(l, "&nbsp; %s return %s'%s'", 
							((strncmp(p, "no", 2) == 0) ? "cannot" : "must"), 
							((strncmp(p, "type;", 5) == 0) ? "content-type " : ""),
							wanted);
						addtobuffer(&infobuf, &infobuflen, l);
					}
				}
				addtobuffer(&infobuf, &infobuflen, "<br>\n");
			}
			p = strtok(NULL, " \t");
		}
		if (!firstcontent) addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		alerts = find_alert(hostwalk->hostentry->hostname, 0, 0);
		if (!dialup) {
			if (alerts) {
				addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>E-mail/SMS alerting:</th><td align=left>\n");
				addtobuffer(&infobuf, &infobuflen, "<table width=\"100%%\" border=1>\n");
				addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>Services</th><th align=left>Ex.Services</th><th align=left>Weekdays</th><th align=left>Time</th><th align=left>Recipients</th></tr>\n");
				while (alerts) {
					char *recips = malcop(alerts->items[6]);
					char *onercpt;

					addtobuffer(&infobuf, &infobuflen, "<tr>\n");

					sprintf(l, "<td align=left>%s</td>\n", service_text(alerts->items[2]));
					addtobuffer(&infobuf, &infobuflen, l);
					sprintf(l, "<td align=left>%s</td>\n", service_text(alerts->items[3]));
					addtobuffer(&infobuf, &infobuflen, l);
					sprintf(l, "<td align=left>%s</td>\n", weekday_text(alerts->items[4]));
					addtobuffer(&infobuf, &infobuflen, l);
					sprintf(l, "<td align=left>%s</td>\n", time_text(alerts->items[5]));
					addtobuffer(&infobuf, &infobuflen, l);

					addtobuffer(&infobuf, &infobuflen, "<td align=left>");
					onercpt = strtok(recips, " \t");
					while (onercpt) {
						addtobuffer(&infobuf, &infobuflen, onercpt);
						onercpt = strtok(NULL, " \t");
						if (onercpt) addtobuffer(&infobuf, &infobuflen, "<br>");
					}
					addtobuffer(&infobuf, &infobuflen, "</td>\n");

					addtobuffer(&infobuf, &infobuflen, "</tr>\n");

					alerts = find_alert(hostwalk->hostentry->hostname, 0, 1);
				}
				addtobuffer(&infobuf, &infobuflen, "</table>\n");

				addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");

				sprintf(l, "<tr><th align=left>Default time between each alert:</th><td align=left>%d minutes</td></tr>\n", 
					pagedelay);
				addtobuffer(&infobuf, &infobuflen, l);
			}
			else {
				addtobuffer(&infobuf, &infobuflen, "<tr><th colspan=2 align=left>No e-mail/SMS alerting defined</th></tr>\n");
			}
		}
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>Other tags:</th><td align=left>");
		strcpy(rawcopy, hostwalk->hostentry->rawentry); /* Already allocated */
		p = strtok(rawcopy, " \t");
		while (p) {
			if ((strncmp(p, "NAME:", 5) == 0) || 
			    (strncmp(p, "COMMENT:", 8) == 0) ||
			    (strncmp(p, "DESCR:", 8) == 0)) {
				char *p2 = strchr(p, ':');

				p2++;
				if (*p2 == '"') {
					/* See if the end '"' is already in the token */
					p2++;
					if (strchr(p2, '"') == NULL) {
						/* Skip to the next '"' or end-of-line */
						p = strtok(NULL, "\"\r\n");
					}
				}
			}
			else if (
					(strncmp(p, "#", 1) != 0)
				&&	(strncmp(p, "NK:", 3) != 0)
				&&	(strncmp(p, "NET:", 4) != 0)
				&&	(strncmp(p, "CLIENT:", 4) != 0)
				&&	(strncmp(p, "NOPROP:", 7) != 0)
				&&	(strncmp(p, "NOPROPRED:", 10) != 0)
				&&	(strncmp(p, "NOPROPYELLOW:", 13) != 0)
				&&	(strncmp(p, "SLA=", 4) != 0)
				&&	(strncmp(p, "NKTIME=", 7) != 0)
				&&	(strncmp(p, "DOWNTIME=", 9) != 0)
				&&	(strncmp(p, "REPORTTIME=", 11) != 0)
				&&	(strncmp(p, "WARNPCT:", 8) != 0)
				&&	(strncmp(p, "TIMEOUT:", 8) != 0)
				&&	(strncmp(p, "http", 4) != 0)
				&&	(strncmp(p, "content=", 8) != 0)
				&&	(strncmp(p, "cont;", 5)  != 0)
				&&	(strncmp(p, "nocont;", 7)  != 0)
				&&	(strncmp(p, "post;", 5)  != 0)
				&&	(strncmp(p, "nopost;", 7)  != 0)
				&&	(strncmp(p, "type;", 5)  != 0)
				&&	(strncmp(p, "testip", 6) != 0)
				&&	(strncmp(p, "dialup", 6) != 0)
				&&	(strncmp(p, "noconn", 6) != 0)
				&&	(strncmp(p, "noping", 6) != 0)
			   )  {
				sprintf(l, "%s ", p);
				addtobuffer(&infobuf, &infobuflen, l);
			}

			p = strtok(NULL, " \t");
		}
		free(rawcopy);
		addtobuffer(&infobuf, &infobuflen, "</td></tr>\n</table>\n");

		fd = fopen(logfn, "w");
		if (!fd) {
			errprintf("Cannot open info logfile %s\n", logfn);
			return 1;
		}

		fprintf(fd, "green %s info\n\n", timestamp);
		fprintf(fd, "%s", infobuf);
		fclose(fd);
		utime(logfn, &logfiletime);

		/* HTML files generated only if we use BBLOGSTATUS=STATIC */
		if (generate_static()) {
			sethostenv(hostwalk->hostentry->hostname, hostwalk->hostentry->ip, infocolumn, "green");
			fd = fopen(htmlfn, "w");
			if (!fd) {
				errprintf("Cannot open info html logfile %s\n", htmlfn);
				return 1;
			}
 			headfoot(fd, "hostsvc", "", "header", COL_GREEN);

			fprintf(fd, "<!-- Start of code generated by Big Brother Systems and Network Monitor -->\n");
			fprintf(fd, "\n");
			fprintf(fd, "<A NAME=begindata>&nbsp;</A>\n");
			fprintf(fd, "\n");
			fprintf(fd, "<CENTER><TABLE ALIGN=CENTER BORDER=0 WIDTH=\"80%%\">\n");
			fprintf(fd, "<TR><TH ALIGN=CENTER><FONT %s>", getenv("MKBBROWFONT"));
			fprintf(fd, "%s - %s</FONT><BR><HR WIDTH=\"100%%\"></TH></TR>\n", hostwalk->hostentry->hostname, infocolumn);
			fprintf(fd, "<TR><TD>\n%s\n</TD></TR>\n", infobuf);
			fprintf(fd, "</TABLE>\n");
			fprintf(fd, "\n");
			fprintf(fd, "</CENTER>\n");
			fprintf(fd, "\n");
			fprintf(fd, "<!-- End of code generated by Big Brother Systems and Network Monitor -->\n");

			headfoot(fd, "hostsvc", "", "footer", COL_GREEN);
			fclose(fd);
		}
	}

	free(infobuf);
	return 0;
}

