/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a standalone tool for generating data for the "info" column data.  */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc-info.c,v 1.76 2005-02-20 09:08:12 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

/* The following is for the DNS lookup we perform on DHCP adresses */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "libbbgen.h"

#ifdef HOBBIT
#include "hobbitd_alert.h"
#endif

static namelist_t *hosthead = NULL;

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

	sCopy = strdup(spec);
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
	xfree(sCopy);
}

#ifdef HOBBIT
int test_name_compare(const void *v1, const void *v2)
{
	htnames_t *r1 = (htnames_t *)v1;
	htnames_t *r2 = (htnames_t *)v2;

	return strcmp(r1->name, r2->name);
}

void generate_hobbit_alertinfo(char *hostname, char **buf, int *buflen, char *columnname)
{
	static int gotconfig = 0;
	static char *statuslist = NULL;

	namelist_t *hi = hostinfo(hostname);
	htnames_t hname, lname;
	activealerts_t alert;
	char *key, *walk;
	htnames_t *tnames = NULL;
	int testsz, testcount, i, rcount;
	char l[1024];

	if (!gotconfig) {
		char configfn[PATH_MAX];
		int alertcolors = ( (1 << COL_RED) | (1 << COL_YELLOW) | (1 << COL_PURPLE) );
		int alertinterval = 60*30;

		sprintf(configfn, "%s/etc/hobbit-alerts.cfg", xgetenv("BBHOME"));
		load_alertconfig(configfn, alertcolors, alertinterval);
		gotconfig = 1;

		if (sendmessage("hobbitdlist", NULL, NULL, &statuslist, 1, 30) != BB_OK) {
			errprintf("Could not get the Hobbit statuslog-list\n");
			statuslist = NULL;
			return;
		}

		alert_printmode(1);
	}

	if (statuslist == NULL) return;

	testsz = 10;
	tnames = (htnames_t *)malloc(testsz * sizeof(htnames_t));

	key = (char *)malloc(1 + strlen(hostname) + 1);
	sprintf(key, "%s|", hostname);
	walk = statuslist;
	testcount = 0;
	while ((walk = strstr(walk, key)) != NULL) {
		char *eol, *t;

		eol = strchr(walk+strlen(key), '\n'); if (eol) *eol = '\0';
		t = (walk+strlen(key));

		if ( (strcmp(t, columnname) != 0) &&
		     (strcmp(t, "trends") != 0)   &&
		     (strcmp(t, "graphs") != 0)   &&
		     (strcmp(t, "larrd") != 0) ) {
			tnames[testcount].name = strdup(t);
			tnames[testcount].next = NULL;
			testcount++;
			if (testcount == testsz) {
				testsz += 10;
				tnames = (htnames_t *)realloc(tnames, (testsz * sizeof(htnames_t)));
			}
		}

		if (eol) *eol = '\n';
		walk += strlen(key);
	}
	free(key);

	/* Sort them so the display looks prettier */
	qsort(&tnames[0], testcount, sizeof(htnames_t), test_name_compare);

	sprintf(l, "<table summary=\"%s Alerts\" border=1>\n", hostname);
	addtobuffer(buf, buflen, l);
	addtobuffer(buf, buflen, "<tr><th>Service</th><th>Recipient</th><th>Delay before 1st</th><th>Stop after</th><th>Repeat</th><th>Time of Day</th><th>Colors</th></tr>\n");

	hname.name = hostname; hname.next = NULL;
	lname.name = (hi ? hi->page->pagepath : ""); lname.next = NULL;
	alert.hostname = &hname;
	alert.location = &lname;
	strcpy(alert.ip, "127.0.0.1");
	alert.color = COL_RED;
	alert.pagemessage = "";
	alert.ackmessage = NULL;
	alert.eventstart = 0;
	alert.nextalerttime = 0;
	alert.state = A_PAGING;
	alert.cookie = 12345;
	alert.next = NULL;
	rcount = 0;

	for (i = 0; (i < testcount); i++) {
		alert.testname = &tnames[i];
		if (have_recipient(&alert)) { rcount++; print_alert_recipients(&alert, buf, buflen); }
		free(tnames[i].name);
	}
	free(tnames);

	if (rcount == 0) {
		/* No alerts defined. */
		addtobuffer(buf, buflen, "<tr><td colspan=9 align=center><b><i>No alerts defined</i></b></td></tr>\n");
	}
	addtobuffer(buf, buflen, "</table>\n");
}
#endif

int generate_info(char *infocolumn, char *documentationurl, int hobbitd, int sendmetainfo)
{
	char *infobuf = NULL;
	int infobuflen = 0;
	char *metabuf = NULL;
	int metabuflen = 0;
	char l[MAX_LINE_LEN];
	namelist_t *hostwalk;
	char *val;
	namelist_t *clonewalk;
	alertrec_t *alerts;
	int ping, first;

	/* Send the info columns as combo messages */
	if (hobbitd) {
		combo_start();
		if (sendmetainfo) meta_start();
	}

	/* Load the alert setup */
	if (!hobbitd) bbload_alerts();

	hostwalk = hosthead;
	while (hostwalk) {
		val = bbh_item(hostwalk, BBH_FLAG_NOINFO);
		if (val) {
			hostwalk = hostwalk->next; 
			continue;	/* Skip this host */
		}

		addtobuffer(&infobuf, &infobuflen, "<table width=\"100%\" summary=\"Host Information\">\n");

		sprintf(l, "<Hostname>%s</Hostname>\n", hostwalk->bbhostname);
		addtobuffer(&metabuf, &metabuflen, l);

		val = bbh_item(hostwalk, BBH_DISPLAYNAME);
		if (val && (strcmp(val, hostwalk->bbhostname) != 0)) {
			sprintf(l, "<Displayname>%s</Displayname>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);
			sprintf(l, "<tr><th align=left>Hostname:</th><td align=left>%s (%s)</td></tr>\n", 
				val, hostwalk->bbhostname);
		}
		else {
			sprintf(l, "<tr><th align=left>Hostname:</th><td align=left>%s</td></tr>\n", hostwalk->bbhostname);
		}
		addtobuffer(&infobuf, &infobuflen, l);

		val = bbh_item(hostwalk, BBH_CLIENTALIAS);
		if (val && (strcmp(val, hostwalk->bbhostname) != 0)) {
			sprintf(l, "<Clientname>%s</Clientname>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);
			sprintf(l, "<tr><th align=left>Client alias:</th><td align=left>%s</td></tr>\n", val);
			addtobuffer(&infobuf, &infobuflen, l);
		}

		val = bbh_item(hostwalk, BBH_IP);
		if (strcmp(val, "0.0.0.0") == 0) {
			struct in_addr addr;
			struct hostent *hent;
			static char hostip[30];

			hent = gethostbyname(hostwalk->bbhostname);
			if (hent) {
				memcpy(&addr, *(hent->h_addr_list), sizeof(struct in_addr));
				strcpy(hostip, inet_ntoa(addr));
				if (inet_aton(hostip, &addr) != 0) {
					strcat(hostip, " (dynamic)");
					val = hostip;
				}
			}
		}
		sprintf(l, "<IP>%s</IP>\n", val);
		addtobuffer(&metabuf, &metabuflen, l);
		sprintf(l, "<tr><th align=left>IP:</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(&infobuf, &infobuflen, l);

		val = bbh_item(hostwalk, BBH_DOCURL);
		if (val) {
			sprintf(l, "<HostDocumentationURL>%s</HostDocumentationURL>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);
			sprintf(l, "<tr><th align=left>Documentation:</th><td align=left><a href=\"%s\">%s</a>\n", val, val);
			addtobuffer(&infobuf, &infobuflen, l);
		}

		val = hostlink(hostwalk->bbhostname);
		if (val) {
			sprintf(l, "<HostNotesURL>%s</HostNotesURL>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);
			sprintf(l, "<tr><th align=left>Notes:</th><td align=left><a href=\"%s\">%s%s</a>\n", 
				val, xgetenv("BBWEBHOST"), val);
			addtobuffer(&infobuf, &infobuflen, l);
		}

		val = bbh_item(hostwalk, BBH_PAGEPATH);
		sprintf(l, "<tr><th align=left>Page/subpage:</th><td align=left><a href=\"%s/%s\">%s</a>\n", 
			xgetenv("BBWEB"), val, bbh_item(hostwalk, BBH_PAGEPATHTITLE));
		addtobuffer(&infobuf, &infobuflen, l);

		clonewalk = hostwalk->next;
		while (clonewalk && (strcmp(hostwalk->bbhostname, clonewalk->bbhostname) == 0)) {
			val = bbh_item(clonewalk, BBH_PAGEPATH);
			sprintf(l, "<br><a href=\"%s/%s/\">%s</a>\n", 
				xgetenv("BBWEB"), val, bbh_item(clonewalk, BBH_PAGEPATHTITLE));
			addtobuffer(&infobuf, &infobuflen, l);
			clonewalk = clonewalk->next;
		}
		addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		val = bbh_item(hostwalk, BBH_DESCRIPTION);
		if (val) {
			char *delim;

			sprintf(l, "<Description>%s</Description>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			delim = strchr(val, ':'); if (delim) *delim = '\0';
			sprintf(l, "<tr><th align=left>Host type:</th><td align=left>%s</td></tr>\n", val);
			addtobuffer(&infobuf, &infobuflen, l);
			if (delim) { 
				*delim = ':'; 
				delim++;
				sprintf(l, "<tr><th align=left>Description:</th><td align=left>%s</td></tr>\n", delim);
				addtobuffer(&infobuf, &infobuflen, l);
			}
			addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");
		}

		val = bbh_item(hostwalk, BBH_NK);
		if (val) {
			sprintf(l, "<NKAlerts>%s</NKAlerts>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			sprintf(l, "<tr><th align=left>NK Alerts:</th><td align=left>%s", val); 
			addtobuffer(&infobuf, &infobuflen, l);

			val = bbh_item(hostwalk, BBH_NKTIME);
			if (val) {
				sprintf(l, " (%s)", val);
				addtobuffer(&infobuf, &infobuflen, l);
			}
			else addtobuffer(&infobuf, &infobuflen, " (24x7)");

			addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		}
		else {
			sprintf(l, "<NKAlerts>N/A</NKAlerts>\n");
			addtobuffer(&metabuf, &metabuflen, l);
			addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>NK alerts:</th><td align=left>None</td></tr>\n");
		}

		val = bbh_item(hostwalk, BBH_NKTIME);
		if (val) {
			sprintf(l, "<NKAlertTimes>%s</NKAlertTimes>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>NK alerts shown:</th><td align=left>");
			timespec_text(val, &infobuf, &infobuflen);
			addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		}

		val = bbh_item(hostwalk, BBH_DOWNTIME);
		if (val) {
			sprintf(l, "<DownTimes>%s</DownTimes>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>Planned downtime:</th><td align=left>");
			timespec_text(val, &infobuf, &infobuflen);
			addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		}

		val = bbh_item(hostwalk, BBH_REPORTTIME);
		if (val) {
			sprintf(l, "<ReportTimes>%s</ReportTimes>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>SLA report period:</th><td align=left>");
			timespec_text(val, &infobuf, &infobuflen);
			addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");

			val = bbh_item(hostwalk, BBH_WARNPCT);
			if (val == NULL) val = xgetenv("BBREPWARN");
			if (val == NULL) val = "(not set)";

			sprintf(l, "<MinimumAvailabilityPCT>%s</MinimumAvailabilityPCT>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			sprintf(l, "<tr><th align=left>SLA Availability:</th><td align=left>%s</td></tr>\n", val); 
			addtobuffer(&infobuf, &infobuflen, l);
		}

		val = bbh_item(hostwalk, BBH_NOPROPYELLOW);
		if (val) {
			sprintf(l, "<SuppressedYellow>%s</SuppressedYellow>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			sprintf(l, "<tr><th align=left>Suppressed warnings (yellow):</th><td align=left>%s</td></tr>\n", val);
			addtobuffer(&infobuf, &infobuflen, l);
		}

		val = bbh_item(hostwalk, BBH_NOPROPRED);
		if (val) {
			sprintf(l, "<SuppressedRed>%s</SuppressedRed>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			sprintf(l, "<tr><th align=left>Suppressed alarms (red):</th><td align=left>%s</td></tr>\n", val);
			addtobuffer(&infobuf, &infobuflen, l);
		}

		val = bbh_item(hostwalk, BBH_NOPROPPURPLE);
		if (val) {
			sprintf(l, "<SuppressedPurple>%s</SuppressedPurple>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			sprintf(l, "<tr><th align=left>Suppressed alarms (purple):</th><td align=left>%s</td></tr>\n", val);
			addtobuffer(&infobuf, &infobuflen, l);
		}

		val = bbh_item(hostwalk, BBH_NOPROPACK);
		if (val) {
			sprintf(l, "<SuppressedAcked>%s</SuppressedAcked>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			sprintf(l, "<tr><th align=left>Suppressed alarms (acked):</th><td align=left>%s</td></tr>\n", val);
			addtobuffer(&infobuf, &infobuflen, l);
		}
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		val = bbh_item(hostwalk, BBH_NET);
		if (val) {
			sprintf(l, "<NetLocation>%s</NetLocation>\n", val);
			addtobuffer(&metabuf, &metabuflen, l);

			sprintf(l, "<tr><th align=left>Tested from network:</th><td align=left>%s</td></tr>\n", val);
			addtobuffer(&infobuf, &infobuflen, l);
		}

		if (bbh_item(hostwalk, BBH_FLAG_DIALUP)) {
			addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2 align=left>Host downtime does not trigger alarms (dialup host)</td></tr>\n");
		}

		sprintf(l, "<tr><th align=left>Network tests use:</th><td align=left>%s</td></tr>\n", 
			(bbh_item(hostwalk, BBH_FLAG_TESTIP) ? "IP-address" : "Hostname"));
		addtobuffer(&infobuf, &infobuflen, l);

		ping = 1;
		if (bbh_item(hostwalk, BBH_FLAG_NOPING)) ping = 0;
		if (bbh_item(hostwalk, BBH_FLAG_NOCONN)) ping = 0;
		sprintf(l, "<tr><th align=left>Checked with ping:</th><td align=left>%s</td></tr>\n", (ping ? "Yes" : "No"));
		addtobuffer(&infobuf, &infobuflen, l);

		/* Space */
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		first = 1;
		val = bbh_item_walk(hostwalk);
		while (val) {
			if (*val == '~') val++;

			if (strncmp(val, "http", 4) == 0) {
				char *urlstring = decode_url(val, NULL);

				sprintf(l, "<HttpTest><![CDATA[%s]]></HttpTest>\n", val);
				addtobuffer(&metabuf, &metabuflen, l);

				if (first) {
					addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>URL checks:</th><td align=left>\n");
					first = 0;
				}

				sprintf(l, "<a href=\"%s\">%s</a><br>\n", urlstring, urlstring);
				addtobuffer(&infobuf, &infobuflen, l);
			}
			val = bbh_item_walk(NULL);
		}
		if (!first) addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");

		first = 1;
		val = bbh_item_walk(hostwalk);
		while (val) {
			if (*val == '~') val++;

			if ( (strncmp(val, "cont;", 5) == 0)    ||
			     (strncmp(val, "cont=", 5) == 0)    ||
			     (strncmp(val, "nocont;", 7) == 0)  ||
			     (strncmp(val, "nocont=", 7) == 0)  ||
			     (strncmp(val, "type;", 5) == 0)    ||
			     (strncmp(val, "type=", 5) == 0)    ||
			     (strncmp(val, "post;", 5) == 0)    ||
			     (strncmp(val, "post=", 5) == 0)    ||
			     (strncmp(val, "nopost=", 7) == 0)  ||
			     (strncmp(val, "nopost;", 7) == 0) ) {

				bburl_t bburl;
				char *urlstring = decode_url(val, &bburl);

				sprintf(l, "<HttpTest><![CDATA[%s]]></HttpTest>\n", val);
				addtobuffer(&metabuf, &metabuflen, l);

				if (first) {
					addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>Content checks:</th><td align=left>\n");
					first = 0;
				}

				sprintf(l, "<a href=\"%s\">%s</a>", urlstring, urlstring);
				addtobuffer(&infobuf, &infobuflen, l);

				sprintf(l, "&nbsp; %s return %s'%s'", 
						((strncmp(val, "no", 2) == 0) ? "cannot" : "must"), 
						((strncmp(val, "type;", 5) == 0) ? "content-type " : ""),
						bburl.expdata);
				addtobuffer(&infobuf, &infobuflen, l);
				addtobuffer(&infobuf, &infobuflen, "<br>\n");
			}

			val = bbh_item_walk(NULL);
		}
		if (!first) addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		if (!bbh_item(hostwalk, BBH_FLAG_DIALUP)) {
			if (hobbitd) {
#ifdef HOBBIT
				addtobuffer(&infobuf, &infobuflen, "<tr><th align=left valign=top>Alerting:</th><td align=left>\n");
				generate_hobbit_alertinfo(hostwalk->bbhostname, &infobuf, &infobuflen, infocolumn);
				addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
#endif
			}
			else if ( (alerts = bbfind_alert(hostwalk->bbhostname, 0, 0)) != NULL) {
				int wantedstate = 0;  /* Start with the normal rules */
				int firstinverse = 1;

				addtobuffer(&infobuf, &infobuflen, "<tr><th align=left valign=top>Alerting:</th><td align=left>\n");
				addtobuffer(&infobuf, &infobuflen, "<table width=\"100%%\" border=1>\n");
				addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>Services</th><th align=left>Ex.Services</th><th align=left>Weekdays</th><th align=left>Time</th><th align=left>Recipients</th></tr>\n");
				while (alerts) {
					char *recips;
					char *onercpt;

					if (alerts->inverse == wantedstate) {

						if ((wantedstate == 1) && firstinverse) {
							firstinverse = 0;
							addtobuffer(&infobuf, &infobuflen, "<tr><th colspan=5 align=center><i>Exceptions</i></th></tr>\n");
						}

						recips = strdup(alerts->items[6]);
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
					}

					alerts = bbfind_alert(hostwalk->bbhostname, 0, 1);
					if ((wantedstate == 0) && (alerts == NULL)) {
						/* No more normal rules - see if any inverted rules */
						wantedstate = 1;
						alerts = bbfind_alert(hostwalk->bbhostname, 0, 0);
					}
				}
				addtobuffer(&infobuf, &infobuflen, "</table>\n");

				addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");

				sprintf(l, "<tr><th align=left>Default time between each alert:</th><td align=left>%d minutes</td></tr>\n", 
					bbpagedelay);
				addtobuffer(&infobuf, &infobuflen, l);
			}
			else {
				addtobuffer(&infobuf, &infobuflen, "<tr><th colspan=2 align=left>No alerts defined</th></tr>\n");
			}
		}
		addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

		addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>Other tags:</th><td align=left>");
		val = bbh_item_walk(hostwalk);
		while (val) {
			if (*val == '~') val++;

			if ( (bbh_item_idx(val) == -1)          &&
			     (strncmp(val, "http", 4)    != 0)  &&
			     (strncmp(val, "cont;", 5)   != 0)  &&
			     (strncmp(val, "cont=", 5)   != 0)  &&
			     (strncmp(val, "nocont;", 7) != 0)  &&
			     (strncmp(val, "nocont=", 7) != 0)  &&
			     (strncmp(val, "type;", 5)   != 0)  &&
			     (strncmp(val, "type=", 5)   != 0)  &&
			     (strncmp(val, "post;", 5)   != 0)  &&
			     (strncmp(val, "post=", 5)   != 0)  &&
			     (strncmp(val, "nopost=", 7) != 0)  &&
			     (strncmp(val, "nopost;", 7) != 0) ) {
				sprintf(l, "<OtherTest><![CDATA[%s]]></OtherTest>\n", val);
				addtobuffer(&metabuf, &metabuflen, l);

				sprintf(l, "%s ", val);
				addtobuffer(&infobuf, &infobuflen, l);
			}

			val = bbh_item_walk(NULL);
		}
		addtobuffer(&infobuf, &infobuflen, "</td></tr>\n</table>\n");

		do_savelog(hostwalk->bbhostname, hostwalk->ip, infocolumn, infobuf, hobbitd);
		if (sendmetainfo) do_savemeta(hostwalk->bbhostname, infocolumn, "Info", metabuf);
		*infobuf = '\0';
		*metabuf = '\0';

		clonewalk = hostwalk;
		do {
			hostwalk = hostwalk->next;
		} while (hostwalk && (strcmp(hostwalk->bbhostname, clonewalk->bbhostname) == 0));

	}

	if (hobbitd) {
		combo_end();
		if (sendmetainfo) meta_end();
	}

	if (infobuf) xfree(infobuf);
	if (metabuf) xfree(metabuf);

	return 0;
}

int main(int argc, char *argv[])
{
	int argi;
	char *bbhostsfn = NULL;
	char *infocol = "info";
	char *docurl = NULL;
	int usehobbitd = 0;
	int sendmeta = 0;

	getenv_default("USEHOBBITD", "FALSE", NULL);
	usehobbitd = (strcmp(xgetenv("USEHOBBITD"), "TRUE") == 0);

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = dontsendmessages = 1;
		}
		else if (argnmatch(argv[argi], "--bbhosts=")) {
			char *p = strchr(argv[argi], '=');
			bbhostsfn = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--column=")) {
			char *p = strchr(argv[argi], '=');
			infocol = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--docurl=")) {
			char *p = strchr(argv[argi], '=');
			docurl = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--hobbitd") == 0) {
			usehobbitd = 1;
		}
		else if (strcmp(argv[argi], "--meta") == 0) {
			sendmeta = 1;
		}
		else if (strcmp(argv[argi], "--no-update") == 0) {
			dontsendmessages = 1;
		}
	}

	if (bbhostsfn == NULL) bbhostsfn = xgetenv("BBHOSTS");

	hosthead = load_hostnames(bbhostsfn, NULL, get_fqdn(), docurl);
	load_all_links();

	generate_info(infocol, docurl, usehobbitd, sendmeta);

	return 0;
}

