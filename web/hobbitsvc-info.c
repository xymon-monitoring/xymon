/*----------------------------------------------------------------------------*/
/* Hobbit "info" column generator.                                            */
/*                                                                            */
/* This is a standalone tool for generating data for the "info" column data.  */
/* This extracts all of the static configuration info about a host contained  */
/* in Hobbit, and displays it on one webpage.                                 */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc-info.c,v 1.86 2005-04-03 16:21:18 henrik Exp $";

#include <limits.h>
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

#include "hobbitd_alert.h"

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

static int test_name_compare(const void *v1, const void *v2)
{
	htnames_t *r1 = (htnames_t *)v1;
	htnames_t *r2 = (htnames_t *)v2;

	return strcmp(r1->name, r2->name);
}

static void generate_hobbit_alertinfo(char *hostname, char **buf, int *buflen)
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
		char *hobbitcmd = (char *)malloc(1024 + strlen(hostname));
		gotconfig = 1;

		sprintf(hobbitcmd, "hobbitdboard fields=hostname,testname host=%s", hostname);
		if (sendmessage(hobbitcmd, NULL, NULL, &statuslist, 1, 30) != BB_OK) {
			addtobuffer(buf, buflen, "Alert configuration unavailable");
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

		if ( (strcmp(t, xgetenv("INFOCOLUMN")) != 0) && (strcmp(t, xgetenv("LARRDCOLUMN")) != 0) ) {
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
	addtobuffer(buf, buflen, "<tr><th>Service</th><th>Recipient</th><th>1st Delay</th><th>Stop after</th><th>Repeat</th><th>Time of Day</th><th>Colors</th></tr>\n");

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

char *generate_info(char *hostname)
{
	char *infobuf = NULL;
	int infobuflen = 0;
	char l[MAX_LINE_LEN];
	namelist_t *hostwalk;
	char *val;
	namelist_t *clonewalk;
	int ping, first;
	int alertcolors, alertinterval;

	/* Get host info */
	hostwalk = hostinfo(hostname);
	if (!hostwalk) return NULL;

	/* Load alert config */
	alertcolors = colorset(xgetenv("ALERTCOLORS"), ((1 << COL_GREEN) | (1 << COL_BLUE)));
	alertinterval = 60*atoi(xgetenv("ALERTREPEAT"));
	{
		char configfn[PATH_MAX];

		sprintf(configfn, "%s/etc/hobbit-alerts.cfg", xgetenv("BBHOME"));
		load_alertconfig(configfn, alertcolors, alertinterval);
	}

	/* Load links */
	load_all_links();

	addtobuffer(&infobuf, &infobuflen, "<table width=\"100%\" summary=\"Host Information\">\n");

	val = bbh_item(hostwalk, BBH_DISPLAYNAME);
	if (val && (strcmp(val, hostname) != 0)) {
		sprintf(l, "<tr><th align=left>Hostname:</th><td align=left>%s (%s)</td></tr>\n", 
			val, hostname);
	}
	else {
		sprintf(l, "<tr><th align=left>Hostname:</th><td align=left>%s</td></tr>\n", hostname);
	}
	addtobuffer(&infobuf, &infobuflen, l);

	val = bbh_item(hostwalk, BBH_CLIENTALIAS);
	if (val && (strcmp(val, hostname) != 0)) {
		sprintf(l, "<tr><th align=left>Client alias:</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(&infobuf, &infobuflen, l);
	}

	val = bbh_item(hostwalk, BBH_IP);
	if (strcmp(val, "0.0.0.0") == 0) {
		struct in_addr addr;
		struct hostent *hent;
		static char hostip[30];

		hent = gethostbyname(hostname);
		if (hent) {
			memcpy(&addr, *(hent->h_addr_list), sizeof(struct in_addr));
			strcpy(hostip, inet_ntoa(addr));
			if (inet_aton(hostip, &addr) != 0) {
				strcat(hostip, " (dynamic)");
				val = hostip;
			}
		}
	}
	sprintf(l, "<tr><th align=left>IP:</th><td align=left>%s</td></tr>\n", val);
	addtobuffer(&infobuf, &infobuflen, l);

	val = bbh_item(hostwalk, BBH_DOCURL);
	if (val) {
		sprintf(l, "<tr><th align=left>Documentation:</th><td align=left><a href=\"%s\">%s</a>\n", val, val);
		addtobuffer(&infobuf, &infobuflen, l);
	}

	val = hostlink(hostname);
	if (val) {
		sprintf(l, "<tr><th align=left>Notes:</th><td align=left><a href=\"%s\">%s%s</a>\n", 
			val, xgetenv("BBWEBHOST"), val);
		addtobuffer(&infobuf, &infobuflen, l);
	}

	val = bbh_item(hostwalk, BBH_PAGEPATH);
	sprintf(l, "<tr><th align=left>Page/subpage:</th><td align=left><a href=\"%s/%s\">%s</a>\n", 
		xgetenv("BBWEB"), val, bbh_item(hostwalk, BBH_PAGEPATHTITLE));
	addtobuffer(&infobuf, &infobuflen, l);

	clonewalk = hostwalk->next;
	while (clonewalk && (strcmp(hostname, clonewalk->bbhostname) == 0)) {
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
		addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>NK alerts:</th><td align=left>None</td></tr>\n");
	}

	val = bbh_item(hostwalk, BBH_NKTIME);
	if (val) {
		addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>NK alerts shown:</th><td align=left>");
		timespec_text(val, &infobuf, &infobuflen);
		addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
	}

	val = bbh_item(hostwalk, BBH_DOWNTIME);
	if (val) {
		addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>Planned downtime:</th><td align=left>");
		timespec_text(val, &infobuf, &infobuflen);
		addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
	}

	val = bbh_item(hostwalk, BBH_REPORTTIME);
	if (val) {
		addtobuffer(&infobuf, &infobuflen, "<tr><th align=left>SLA report period:</th><td align=left>");
		timespec_text(val, &infobuf, &infobuflen);
		addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");

		val = bbh_item(hostwalk, BBH_WARNPCT);
		if (val == NULL) val = xgetenv("BBREPWARN");
		if (val == NULL) val = "(not set)";

		sprintf(l, "<tr><th align=left>SLA Availability:</th><td align=left>%s</td></tr>\n", val); 
		addtobuffer(&infobuf, &infobuflen, l);
	}

	val = bbh_item(hostwalk, BBH_NOPROPYELLOW);
	if (val) {
		sprintf(l, "<tr><th align=left>Suppressed warnings (yellow):</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(&infobuf, &infobuflen, l);
	}

	val = bbh_item(hostwalk, BBH_NOPROPRED);
	if (val) {
		sprintf(l, "<tr><th align=left>Suppressed alarms (red):</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(&infobuf, &infobuflen, l);
	}

	val = bbh_item(hostwalk, BBH_NOPROPPURPLE);
	if (val) {
		sprintf(l, "<tr><th align=left>Suppressed alarms (purple):</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(&infobuf, &infobuflen, l);
	}

	val = bbh_item(hostwalk, BBH_NOPROPACK);
	if (val) {
		sprintf(l, "<tr><th align=left>Suppressed alarms (acked):</th><td align=left>%s</td></tr>\n", val);
		addtobuffer(&infobuf, &infobuflen, l);
	}
	addtobuffer(&infobuf, &infobuflen, "<tr><td colspan=2>&nbsp;</td></tr>\n");

	val = bbh_item(hostwalk, BBH_NET);
	if (val) {
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
		addtobuffer(&infobuf, &infobuflen, "<tr><th align=left valign=top>Alerting:</th><td align=left>\n");
		generate_hobbit_alertinfo(hostname, &infobuf, &infobuflen);
		addtobuffer(&infobuf, &infobuflen, "</td></tr>\n");
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
			sprintf(l, "%s ", val);
			addtobuffer(&infobuf, &infobuflen, l);
		}

		val = bbh_item_walk(NULL);
	}
	addtobuffer(&infobuf, &infobuflen, "</td></tr>\n</table>\n");

	return infobuf;
}

