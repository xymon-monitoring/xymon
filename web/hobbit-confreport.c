/*----------------------------------------------------------------------------*/
/* Hobbit CGI tool to generate a report of the Hobbit configuration           */
/*                                                                            */
/* Copyright (C) 2003-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-confreport.c,v 1.1 2005-06-01 22:05:22 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include "libbbgen.h"
#include "hobbitd_alert.h"

typedef struct hostlist_t {
	char *hostname;
	int testcount;
	htnames_t *tests;
	struct hostlist_t *next;
} hostlist_t;

hostlist_t *hosthead = NULL;
static char *pingcolumn = "conn";
static char *pingplus = "conn=";

void errormsg(char *msg)
{
        printf("Content-type: text/html\n\n");
        printf("<html><head><title>Invalid request</title></head>\n");
        printf("<body>%s</body></html>\n", msg);
        exit(1);
}

static int host_compare(const void *v1, const void *v2)
{
	hostlist_t **h1 = (hostlist_t **)v1;
	hostlist_t **h2 = (hostlist_t **)v2;

	return strcmp((*h1)->hostname, (*h2)->hostname);
}

static int test_compare(const void *v1, const void *v2)
{
	htnames_t **t1 = (htnames_t **)v1;
	htnames_t **t2 = (htnames_t **)v2;

	return strcmp((*t1)->name, (*t2)->name);
}


static int is_net_test(char *tname)
{
	char *miscnet[] = { pingcolumn,  "http", "dns", "dig", "rpc", "ntp", "ldap", "content", "sslcert", NULL };
	int i;

	if (find_tcp_service(tname)) return 1;
	for (i=0; (miscnet[i]); i++) if (strcmp(tname, miscnet[i]) == 0) return 1;

	return 0;
}


typedef struct tag_t {
	char *columnname;
	char *visualdata;	/* The URL or other end-user visible test spec. */
	char *expdata;
	struct tag_t *next;
} tag_t;

static void print_host(char *hostname, htnames_t *testnames[], int testcount)
{
	int testi, rowcount, netcount;
	namelist_t *hinfo = hostinfo(hostname);
	char *dispname = NULL, *clientalias = NULL, *comment = NULL, *description = NULL;
	char *net = NULL, *nkalerts = NULL;
	char *itm;
	tag_t *taghead = NULL;
	int contidx = 0, haveping = 0;
	char contcol[1024];
	htnames_t hname, lname, tname;
	activealerts_t alert;
	char *buf = NULL; 
	int buflen = 0;

	fprintf(stdout, "<p style=\"page-break-before: always\">\n"); 
	fprintf(stdout, "<table width=\"100%%\" border=1 summary=\"%s configuration\">\n", hostname);

	dispname = bbh_item(hinfo, BBH_DISPLAYNAME); 
	if (dispname && (strcmp(dispname, hostname) == 0)) dispname = NULL;
	clientalias = bbh_item(hinfo, BBH_CLIENTALIAS); 
	if (clientalias && (strcmp(clientalias, hostname) == 0)) clientalias = NULL;
	comment = bbh_item(hinfo, BBH_COMMENT);
	description = bbh_item(hinfo, BBH_DESCRIPTION); 
	net = bbh_item(hinfo, BBH_NET);

	rowcount = 1;
	if (dispname || clientalias) rowcount++;
	if (comment) rowcount++;
	if (description) rowcount++;

	fprintf(stdout, "<tr>\n");
	fprintf(stdout, "<th rowspan=%d align=left width=\"25%%\">Basics</th>\n", rowcount);
	fprintf(stdout, "<th align=center>%s (%s)</th>\n", 
		(dispname ? dispname : hostname), bbh_item(hinfo, BBH_IP));
	fprintf(stdout, "</tr>\n");

	if (dispname || clientalias) {
		fprintf(stdout, "<tr><td>Aliases:");
		if (dispname) fprintf(stdout, " %s", dispname);
		if (clientalias) fprintf(stdout, " %s", clientalias);
		fprintf(stdout, "</td></tr>\n");
	}
	if (comment) fprintf(stdout, "<tr><td>Comment: %s</td></tr>\n", comment);
	if (description) fprintf(stdout, "<tr><td>Description: %s</td></tr>\n", description);


	nkalerts = bbh_item(hinfo, BBH_NK);

	/* Build a list of the network tests */
	itm = bbh_item_walk(hinfo);
	while (itm) {
		char *visdata = NULL, *colname = NULL, *expdata = NULL;
		bburl_t bu;
		int dialuptest = 0, reversetest = 0, alwaystruetest = 0, httpextra = 0;

		if (*itm == '?') { dialuptest=1;     itm++; }
		if (*itm == '!') { reversetest=1;    itm++; }
		if (*itm == '~') { alwaystruetest=1; itm++; }

		if ( argnmatch(itm, "http")         ||
		     argnmatch(itm, "content=http") ||
		     argnmatch(itm, "cont;http")    ||
		     argnmatch(itm, "cont=")        ||
		     argnmatch(itm, "nocont;http")  ||
		     argnmatch(itm, "nocont=")      ||
		     argnmatch(itm, "post;http")    ||
		     argnmatch(itm, "post=")        ||
		     argnmatch(itm, "nopost;http")  ||
		     argnmatch(itm, "nopost=")      ||
		     argnmatch(itm, "type;http")    ||
		     argnmatch(itm, "type=")        ) {
			visdata = decode_url(itm, &bu);
			colname = bu.columnname; 
			if (!colname) {
				if (bu.expdata) {
					httpextra = 1;
					if (contidx == 0) {
						colname = "content";
						contidx++;
					}
					else {
						sprintf(contcol, "content%d", contidx);
						colname = contcol;
						contidx++;
					}
				}
				else {
					colname = "http";
				}
			}
			expdata = bu.expdata;
		}
		else if (strncmp(itm, "rpc=", 4) == 0) {
			colname = "rpc";
			visdata = strdup(itm+4);
		}
		else if (strncmp(itm, "dns=", 4) == 0) {
			colname = "dns";
			visdata = strdup(itm+4);
		}
		else if (strncmp(itm, "dig=", 4) == 0) {
			colname = "dns";
			visdata = strdup(itm+4);
		}
		else if (strncmp(itm, pingplus, strlen(pingplus)) == 0) {
			haveping = 1;
			colname = pingcolumn;
			visdata = strdup(itm+strlen(pingplus));
		}
		else if (is_net_test(itm)) {
			colname = strdup(itm);
		}


		if (colname) {
			tag_t *newitem;

addtolist:
			for (newitem = taghead; (newitem && strcmp(newitem->columnname, colname)); newitem = newitem->next);

			if (!newitem) {
				newitem = (tag_t *)malloc(sizeof(tag_t));
				newitem->columnname = strdup(colname);
				newitem->visualdata = (visdata ? strdup(visdata) : NULL);
				newitem->expdata = (expdata ? strdup(expdata) : NULL);
				newitem->next = taghead;
				taghead = newitem;
			}
			else {
				/* Multiple tags for one column - must be http */
				newitem->visualdata = (char *)realloc(newitem->visualdata, strlen(newitem->visualdata) + strlen(visdata) + 5);
				strcat(newitem->visualdata, "<br>");
				strcat(newitem->visualdata, visdata);
			}

			if (httpextra) {
				httpextra = 0;
				colname = "http";
				expdata = NULL;
				goto addtolist;
			}
		}

		itm = bbh_item_walk(NULL);
	}

	if (!haveping) {
		for (testi = 0; (testi < testcount); testi++) {
			if (strcmp(testnames[testi]->name, pingcolumn) == 0) {
				tag_t *newitem = (tag_t *)malloc(sizeof(tag_t));
				newitem->columnname = strdup(pingcolumn);
				newitem->visualdata = NULL;
				newitem->expdata = NULL;
				newitem->next = taghead;
				taghead = newitem;
			}
		}
	}

	if (taghead) {
		fprintf(stdout, "<tr>\n");
		fprintf(stdout, "<th align=left valign=top>Network tests");
		if (net) fprintf(stdout, "<br>(from %s)", net);
		fprintf(stdout, "</th>\n");

		fprintf(stdout, "<td><table border=0 cellpadding=\"3\" cellspacing=\"5\" summary=\"%s network tests\">\n", hostname);
		fprintf(stdout, "   <tr><th align=left>Name</th><th align=left>NK</th><th align=left>Specifics</th></tr>\n");
	}
	for (testi = 0, netcount = 0; (testi < testcount); testi++) {
		tag_t *twalk;

		for (twalk = taghead; (twalk && strcasecmp(twalk->columnname, testnames[testi]->name)); twalk = twalk->next);
		if (!twalk) continue;

		fprintf(stdout, "<tr>");
		fprintf(stdout, "<td valign=top>%s</td>", testnames[testi]->name);
		fprintf(stdout, "<td valign=top>%s</td>", (checkalert(nkalerts, testnames[testi]->name) ? "Yes" : "No"));

		fprintf(stdout, "<td>");
		fprintf(stdout, "<i>%s</i>", (twalk->visualdata ? twalk->visualdata : "&nbsp;"));
		if (twalk->expdata) fprintf(stdout, " must return <i>'%s'</i>", twalk->expdata);
		fprintf(stdout, "</td>");

		fprintf(stdout, "</tr>");
		netcount++;
	}
	if (taghead) {
		fprintf(stdout, "</table></td>\n");
		fprintf(stdout, "</tr>\n");
	}


	if (netcount != testcount) {
		fprintf(stdout, "<tr>\n");
		fprintf(stdout, "<th align=left valign=top>Local tests</th>\n");
		fprintf(stdout, "<td><table border=0 cellpadding=\"3\" cellspacing=\"5\" summary=\"%s local tests\">\n", hostname);
		fprintf(stdout, "   <tr><th align=left>Name</th><th align=left>NK</th></tr>\n");
	}
	for (testi = 0; (testi < testcount); testi++) {
		tag_t *twalk;

		for (twalk = taghead; (twalk && strcasecmp(twalk->columnname, testnames[testi]->name)); twalk = twalk->next);
		if (twalk) continue;

		fprintf(stdout, "<tr>");
		fprintf(stdout, "<td>%s</td>", testnames[testi]->name);
		fprintf(stdout, "<td>%s</td>", (checkalert(nkalerts, testnames[testi]->name) ? "Yes" : "No"));
		fprintf(stdout, "</tr>");
	}
	if (netcount != testcount) {
		fprintf(stdout, "</table></td>\n");
		fprintf(stdout, "</tr>\n");
	}

	/* Do the alerts */
	hname.name = hostname; hname.next = NULL;
	lname.name = hinfo->page->pagepath; lname.next = NULL;
	tname.next = NULL;
	alert.hostname = &hname;
	alert.location = &lname;
	alert.testname = &tname;
	strcpy(alert.ip, "127.0.0.1");
	alert.color = COL_RED;
	alert.pagemessage = "";
	alert.ackmessage = NULL;
	alert.eventstart = 0;
	alert.nextalerttime = 0;
	alert.state = A_PAGING;
	alert.cookie = 12345;
	alert.next = NULL;
	alert_printmode(1);
	for (testi = 0; (testi < testcount); testi++) {
		tname.name = testnames[testi]->name;
		if (have_recipient(&alert)) print_alert_recipients(&alert, &buf, &buflen);
	}

	if (buf) {
		fprintf(stdout, "<tr>\n");
		fprintf(stdout, "<th align=left valign=top>Alerts</th>\n");
		fprintf(stdout, "<td><table border=0 cellpadding=\"3\" cellspacing=\"5\" summary=\"%s alerts\">\n", hostname);
		fprintf(stdout, "<tr><th>Service</th><th>Recipient</th><th>1st Delay</th><th>Stop after</th><th>Repeat</th><th>Time of Day</th><th>Colors</th></tr>\n");

		fprintf(stdout, "%s", buf);

		fprintf(stdout, "</table></td>\n");
		fprintf(stdout, "</tr>\n");
	}

	/* Finish off this host */
	fprintf(stdout, "</table>\n");
}

int main(int argc, char *argv[])
{
	int argi, hosti, testi;
	char *pagepattern = NULL, *hostpattern = NULL;
	char *envarea = NULL, *cookie = NULL, *p, *nexthost;
	char hobbitcmd[1024];
        int alertcolors, alertinterval;
	char configfn[PATH_MAX];
	char *respbuf = NULL;
	hostlist_t *hwalk;
	htnames_t *twalk;
	hostlist_t **allhosts = NULL;
	htnames_t **alltests = NULL;
	int hostcount = 0, maxtests = 0;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
	}

	redirect_cgilog("hobbit-confreport");

	/* Setup the filter we use for the report */
	cookie = getenv("HTTP_COOKIE");
	if (cookie && ((p = strstr(cookie, "pagepath=")) != NULL)) {
		p += strlen("pagepath=");
		pagepattern = strdup(p);
		p = strchr(pagepattern, ';'); if (p) *p = '\0';
		if (strlen(pagepattern) == 0) { xfree(pagepattern); pagepattern = NULL; }
	}
	else if (cookie && ((p = strstr(cookie, "host=")) != NULL)) {
		p += strlen("host=");
		hostpattern = strdup(p);
		p = strchr(hostpattern, ';'); if (p) *p = '\0';
		if (strlen(hostpattern) == 0) { xfree(hostpattern); hostpattern = NULL; }
	}

	/* Fetch the list of host+test statuses we currently know about */
	if (pagepattern) {
		sprintf(hobbitcmd, "hobbitdboard page=%s fields=hostname,testname", pagepattern);
	}
	else if (hostpattern) {
		sprintf(hobbitcmd, "hobbitdboard host=%s fields=hostname,testname", hostpattern);
	}
	else {
		sprintf(hobbitcmd, "hobbitdboard fields=hostname,testname");
	}

	if (sendmessage(hobbitcmd, NULL, NULL, &respbuf, 1, BBTALK_TIMEOUT) != BB_OK) {
		errormsg("Cannot contact the Hobbit server\n");
		return 1;
	}

	/* Parse it into a usable list */
	nexthost = respbuf;
	do {
		char *hname, *tname, *eoln;

		eoln = strchr(nexthost, '\n'); if (eoln) *eoln = '\0';
		hname = nexthost;
		tname = strchr(nexthost, '|'); if (tname) { *tname = '\0'; tname++; }

		if (hname && tname && strcmp(hname, "summary") && strcmp(tname, xgetenv("INFOCOLUMN")) && strcmp(tname, xgetenv("LARRDCOLUMN"))) {
			htnames_t *newitem = (htnames_t *)malloc(sizeof(htnames_t));

			for (hwalk = hosthead; (hwalk && strcmp(hwalk->hostname, hname)); hwalk = hwalk->next);
			if (!hwalk) {
				hwalk = (hostlist_t *)malloc(sizeof(hostlist_t));
				hwalk->hostname = strdup(hname);
				hwalk->tests = NULL;
				hwalk->testcount = 0;
				hwalk->next = hosthead;
				hosthead = hwalk;
				hostcount++;
			}

			newitem->name = strdup(tname);
			newitem->next = hwalk->tests;
			hwalk->tests = newitem;
			hwalk->testcount++;
		}

		if (eoln) {
			nexthost = eoln+1;
			if (*nexthost == '\0') nexthost = NULL;
		}
	} while (nexthost);

	allhosts = (hostlist_t **) malloc(hostcount * sizeof(hostlist_t *));
	alltests = (htnames_t **) malloc(maxtests * sizeof(htnames_t *));
	for (hwalk = hosthead, hosti=0; (hwalk); hwalk = hwalk->next, hosti++) {
		allhosts[hosti] = hwalk;
		if (hwalk->testcount > maxtests) maxtests = hwalk->testcount;
	}
	qsort(&allhosts[0], hostcount, sizeof(hostlist_t **), host_compare);

	/* Get the static info */
	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
	load_all_links();
	init_tcp_services();
	pingcolumn = xgetenv("PINGCOLUMN");
	pingplus = (char *)malloc(strlen(pingcolumn) + 2);
	sprintf(pingplus, "%s=", pingcolumn);

	/* Load alert config */
	alertcolors = colorset(xgetenv("ALERTCOLORS"), ((1 << COL_GREEN) | (1 << COL_BLUE)));
	alertinterval = 60*atoi(xgetenv("ALERTREPEAT"));
	sprintf(configfn, "%s/etc/hobbit-alerts.cfg", xgetenv("BBHOME"));
	load_alertconfig(configfn, alertcolors, alertinterval);


	printf("Content-Type: text/html\n\n");
	sethostenv("", "", "", colorname(COL_BLUE));
	headfoot(stdout, "confreport", "", "header", COL_BLUE);

	for (hosti=0; (hosti < hostcount); hosti++) {
		for (twalk = allhosts[hosti]->tests, testi = 0; (twalk); twalk = twalk->next, testi++) {
			alltests[testi] = twalk;
		}
		qsort(&alltests[0], allhosts[hosti]->testcount, sizeof(htnames_t **), test_compare);

		print_host(allhosts[hosti]->hostname, alltests, allhosts[hosti]->testcount);
	}

	headfoot(stdout, "confreport", "", "footer", COL_BLUE);

	return 0;
}

