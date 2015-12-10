/*----------------------------------------------------------------------------*/
/* Xymon CGI tool to generate a report of the Xymon configuration             */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

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
#include <dirent.h>

#include "libxymon.h"

typedef struct hostlist_t {
	char *hostname;
	int testcount;
	htnames_t *tests;
	htnames_t *disks, *svcs, *procs;
	struct hostlist_t *next;
} hostlist_t;

typedef struct coltext_t {
	char *colname;
	char *coldescr;
	int used;
	struct coltext_t *next;
} coltext_t;

hostlist_t *hosthead = NULL;
static char *pingcolumn = "conn";
static char *pingplus = "conn=";
static char *coldelim = ";";
static coltext_t *chead = NULL;
static int ccount = 0;
static int criticalonly = 0;
static int newcritconfig = 1;

void errormsg(char *msg)
{
        printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
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
	char *miscnet[] = { NULL,  "http", "dns", "dig", "rpc", "ntp", "ldap", "content", "sslcert", NULL };
	int i;

	miscnet[0] = pingcolumn; /* Cannot be computed in advance */
	if (find_tcp_service(tname)) return 1;
	for (i=0; (miscnet[i]); i++) if (strcmp(tname, miscnet[i]) == 0) return 1;

	return 0;
}


void use_columndoc(char *column)
{
	coltext_t *cwalk;

	for (cwalk = chead; (cwalk && strcasecmp(cwalk->colname, column)); cwalk = cwalk->next);
	if (cwalk) cwalk->used = 1;
}

typedef struct tag_t {
	char *columnname;
	char *visualdata;	/* The URL or other end-user visible test spec. */
	char *expdata;
	int b1, b2, b3;		/* "badFOO" values, if any */
	struct tag_t *next;
} tag_t;

static void print_disklist(char *hostname)
{
	/*
	 * We get the list of monitored disks/filesystems by looking at the
	 * set of disk RRD files for this host. That way we do not have to
	 * parse the disk status reports that come in many different flavours.
	 */

	char dirname[PATH_MAX];
	char fn[PATH_MAX];
	DIR *d;
	struct dirent *de;
	char *p;

	sprintf(dirname, "%s/%s", xgetenv("XYMONRRDS"), hostname);
	d = opendir(dirname);
	if (!d) return;

	while ((de = readdir(d)) != NULL) {
		if (strncmp(de->d_name, "disk,", 5) == 0) {
			strcpy(fn, de->d_name + 4);
			p = strstr(fn, ".rrd"); if (!p) continue;
			*p = '\0';
			p = fn; while ((p = strchr(p, ',')) != NULL) *p = '/';
			fprintf(stdout, "%s<br>\n", fn);
		}
	}

	closedir(d);
}

char *criticalval(char *hname, char *tname, char *alerts)
{
	static char *result = NULL;

	if (result) xfree(result);

	if (newcritconfig) {
		char *key;
		critconf_t *critrec;

		key = (char *)malloc(strlen(hname) + strlen(tname) + 2);
		sprintf(key, "%s|%s", hname, tname);
		critrec = get_critconfig(key, CRITCONF_FIRSTMATCH, NULL);
		if (!critrec) {
			result = strdup("No");
		}
		else {
			char *tspec;

			tspec = (critrec->crittime ? timespec_text(critrec->crittime) : "24x7");
			result = (char *)malloc(strlen(tspec) + 30);
			sprintf(result, "%s&nbsp;prio&nbsp;%d", tspec, critrec->priority);
		}
		xfree(key);
	}
	else {
		result = strdup((checkalert(alerts, tname) ? "Yes" : "No"));
	}

	return result;
}


static void print_host(hostlist_t *host, htnames_t *testnames[], int testcount)
{
	int testi, rowcount, netcount;
	void *hinfo = hostinfo(host->hostname);
	char *dispname = NULL, *clientalias = NULL, *comment = NULL, *description = NULL, *pagepathtitle = NULL;
	char *net = NULL, *alerts = NULL;
	char *crittime = NULL, *downtime = NULL, *reporttime = NULL;
	char *itm;
	tag_t *taghead = NULL;
	int contidx = 0, haveping = 0;
	char contcol[1024];
	activealerts_t *alert;
	strbuffer_t *buf = newstrbuffer(0); 

	fprintf(stdout, "<p style=\"page-break-before: always\">\n"); 
	fprintf(stdout, "<table width=\"100%%\" border=1 summary=\"%s configuration\">\n", host->hostname);

	pagepathtitle = xmh_item(hinfo, XMH_PAGEPATHTITLE);
	if (!pagepathtitle || (strlen(pagepathtitle) == 0)) pagepathtitle = "Top page";
	dispname = xmh_item(hinfo, XMH_DISPLAYNAME); 
	if (dispname && (strcmp(dispname, host->hostname) == 0)) dispname = NULL;
	clientalias = xmh_item(hinfo, XMH_CLIENTALIAS); 
	if (clientalias && (strcmp(clientalias, host->hostname) == 0)) clientalias = NULL;
	comment = xmh_item(hinfo, XMH_COMMENT);
	description = xmh_item(hinfo, XMH_DESCRIPTION); 
	net = xmh_item(hinfo, XMH_NET);
	alerts = xmh_item(hinfo, XMH_NK);
	crittime = xmh_item(hinfo, XMH_NKTIME); if (!crittime) crittime = "24x7"; else crittime = strdup(timespec_text(crittime));
	downtime = xmh_item(hinfo, XMH_DOWNTIME); if (downtime) downtime = strdup(timespec_text(downtime));
	reporttime = xmh_item(hinfo, XMH_REPORTTIME); if (!reporttime) reporttime = "24x7"; else reporttime = strdup(timespec_text(reporttime));

	rowcount = 1;
	if (pagepathtitle) rowcount++;
	if (dispname || clientalias) rowcount++;
	if (comment) rowcount++;
	if (description) rowcount++;
	if (!newcritconfig && crittime) rowcount++;
	if (downtime) rowcount++;
	if (reporttime) rowcount++;

	fprintf(stdout, "<tr>\n");
	fprintf(stdout, "<th rowspan=%d align=left width=\"25%%\" valign=top>Basics</th>\n", rowcount);
	fprintf(stdout, "<th align=center>%s (%s)</th>\n", 
		(dispname ? dispname : host->hostname), xmh_item(hinfo, XMH_IP));
	fprintf(stdout, "</tr>\n");

	if (dispname || clientalias) {
		fprintf(stdout, "<tr><td>Aliases:");
		if (dispname) fprintf(stdout, " %s", dispname);
		if (clientalias) fprintf(stdout, " %s", clientalias);
		fprintf(stdout, "</td></tr>\n");
	}
	if (pagepathtitle) fprintf(stdout, "<tr><td>Monitoring location: %s</td></tr>\n", pagepathtitle);
	if (comment) fprintf(stdout, "<tr><td>Comment: %s</td></tr>\n", comment);
	if (description) fprintf(stdout, "<tr><td>Description: %s</td></tr>\n", description);
	if (!newcritconfig && crittime) fprintf(stdout, "<tr><td>Critical monitoring period: %s</td></tr>\n", crittime);
	if (downtime) fprintf(stdout, "<tr><td>Planned downtime: %s</td></tr>\n", downtime);
	if (reporttime) fprintf(stdout, "<tr><td>SLA Reporting Period: %s</td></tr>\n", reporttime);


	/* Build a list of the network tests */
	itm = xmh_item_walk(hinfo);
	while (itm) {
		char *visdata = NULL, *colname = NULL, *expdata = NULL;
		weburl_t bu;
		int httpextra = 0;

		/* Skip modifiers */
		itm += strspn(itm, "?!~");

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
			visdata = strdup("");
		}

		if (!visdata) visdata = strdup("");
		if (colname) {
			tag_t *newitem;

addtolist:
			for (newitem = taghead; (newitem && strcmp(newitem->columnname, colname)); newitem = newitem->next);

			if (!newitem) {
				newitem = (tag_t *)calloc(1, sizeof(tag_t));
				newitem->columnname = strdup(colname);
				newitem->visualdata = (visdata ? strdup(visdata) : NULL);
				newitem->expdata = (expdata ? strdup(expdata) : NULL);
				newitem->next = taghead;
				taghead = newitem;
			}
			else {
				/* Multiple tags for one column - must be http */
				newitem->visualdata = newitem->visualdata ?
					(char *)realloc(newitem->visualdata, strlen(newitem->visualdata) + strlen(visdata) + 5) :
					(char *)malloc(strlen(visdata) + 5);
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

		itm = xmh_item_walk(NULL);
	}

	if (!haveping && !xmh_item(hinfo, XMH_FLAG_NOCONN)) {
		for (testi = 0; (testi < testcount); testi++) {
			if (strcmp(testnames[testi]->name, pingcolumn) == 0) {
				tag_t *newitem = (tag_t *)calloc(1, sizeof(tag_t));
				newitem->columnname = strdup(pingcolumn);
				newitem->next = taghead;
				taghead = newitem;
			}
		}
	}

	/* Add the "badFOO" settings */
	itm = xmh_item_walk(hinfo);
	while (itm) {
		if (strncmp(itm, "bad", 3) == 0) {
			char *tname, *p;
			int b1, b2, b3, n = -1;
			tag_t *tag = NULL;

			tname = itm+3; 
			p = strchr(tname, ':'); 
			if (p) {
				*p = '\0';
				n = sscanf(p+1, "%d:%d:%d", &b1, &b2, &b3);
				for (tag = taghead; (tag && strcmp(tag->columnname, tname)); tag = tag->next);
				*p = ':';
			}

			if (tag && (n == 3)) {
				tag->b1 = b1; tag->b2 = b2; tag->b3 = b3;
			}
		}

		itm = xmh_item_walk(NULL);
	}

	if (taghead) {
		fprintf(stdout, "<tr>\n");
		fprintf(stdout, "<th align=left valign=top>Network tests");
		if (net) fprintf(stdout, "<br>(from %s)", net);
		fprintf(stdout, "</th>\n");

		fprintf(stdout, "<td><table border=0 cellpadding=\"3\" cellspacing=\"5\" summary=\"%s network tests\">\n", host->hostname);
		fprintf(stdout, "<tr><th align=left valign=top>Service</th><th align=left valign=top>Critical</th><th align=left valign=top>C/Y/R limits</th><th align=left valign=top>Specifics</th></tr>\n");
	}
	for (testi = 0, netcount = 0; (testi < testcount); testi++) {
		tag_t *twalk;

		for (twalk = taghead; (twalk && strcasecmp(twalk->columnname, testnames[testi]->name)); twalk = twalk->next);
		if (!twalk) continue;

		use_columndoc(testnames[testi]->name);
		fprintf(stdout, "<tr>");
		fprintf(stdout, "<td valign=top>%s</td>", testnames[testi]->name);
		fprintf(stdout, "<td valign=top>%s</td>", criticalval(host->hostname, testnames[testi]->name, alerts));

		fprintf(stdout, "<td valign=top>");
		if (twalk->b1 || twalk->b2 || twalk->b3) {
			fprintf(stdout, "%d/%d/%d", twalk->b1, twalk->b2, twalk->b3);
		}
		else {
			fprintf(stdout, "-/-/-");
		}
		fprintf(stdout, "</td>");

		fprintf(stdout, "<td valign=top>");
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
		fprintf(stdout, "<td><table border=0 cellpadding=\"3\" cellspacing=\"5\" summary=\"%s local tests\">\n", host->hostname);
		fprintf(stdout, "<tr><th align=left valign=top>Service</th><th align=left valign=top>Critical</th><th align=left valign=top>C/Y/R limits</th><th align=left valign=top>Configuration <i>(NB: Thresholds on client may differ)</i></th></tr>\n");
	}
	for (testi = 0; (testi < testcount); testi++) {
		tag_t *twalk;

		for (twalk = taghead; (twalk && strcasecmp(twalk->columnname, testnames[testi]->name)); twalk = twalk->next);
		if (twalk) continue;

		use_columndoc(testnames[testi]->name);
		fprintf(stdout, "<tr>");
		fprintf(stdout, "<td valign=top>%s</td>", testnames[testi]->name);
		fprintf(stdout, "<td valign=top>%s</td>", criticalval(host->hostname, testnames[testi]->name, alerts));
		fprintf(stdout, "<td valign=top>-/-/-</td>");

		/* Make up some default configuration data */
		fprintf(stdout, "<td valign=top>");
		if (strcmp(testnames[testi]->name, "cpu") == 0) {
			fprintf(stdout, "UNIX - Yellow: Load average > 1.5, Red: Load average > 3.0<br>");
			fprintf(stdout, "Windows - Yellow: CPU utilisation > 80%%, Red: CPU utilisation > 95%%");
		}
		else if (strcmp(testnames[testi]->name, "disk") == 0) {
			fprintf(stdout, "Default limits: Yellow 90%% full, Red 95%% full<br>\n");
			print_disklist(host->hostname);
		}
		else if (strcmp(testnames[testi]->name, "memory") == 0) {
			fprintf(stdout, "Yellow: swap/pagefile use > 80%%, Red: swap/pagefile use > 90%%");
		}
		else if (strcmp(testnames[testi]->name, "procs") == 0) {
			htnames_t *walk;

			if (!host->procs) fprintf(stdout, "No processes monitored<br>\n");

			for (walk = host->procs; (walk); walk = walk->next) {
				fprintf(stdout, "%s<br>\n", walk->name);
			}
		}
		else if (strcmp(testnames[testi]->name, "svcs") == 0) {
			htnames_t *walk;

			if (!host->svcs) fprintf(stdout, "No services monitored<br>\n");

			for (walk = host->svcs; (walk); walk = walk->next) {
				fprintf(stdout, "%s<br>\n", walk->name);
			}
		}
		else {
			fprintf(stdout, "&nbsp;");
		}
		fprintf(stdout, "</td>");

		fprintf(stdout, "</tr>");
	}
	if (netcount != testcount) {
		fprintf(stdout, "</table></td>\n");
		fprintf(stdout, "</tr>\n");
	}

	/* Do the alerts */
	alert = (activealerts_t *)calloc(1, sizeof(activealerts_t));
	alert->hostname = host->hostname;
	alert->location = xmh_item(hinfo, XMH_ALLPAGEPATHS);
	alert->ip = strdup("127.0.0.1");
	alert->color = COL_RED;
	alert->pagemessage = "";
	alert->state = A_PAGING;
	strcpy(alert->cookie, "12345");
	alert_printmode(2);
	for (testi = 0; (testi < testcount); testi++) {
		alert->testname = testnames[testi]->name;
		if (have_recipient(alert, NULL)) print_alert_recipients(alert, buf);
	}
	xfree(alert);

	if (STRBUFLEN(buf) > 0) {
		fprintf(stdout, "<tr>\n");
		fprintf(stdout, "<th align=left valign=top>Alerts</th>\n");
		fprintf(stdout, "<td><table border=0 cellpadding=\"3\" cellspacing=\"5\" summary=\"%s alerts\">\n", host->hostname);
		fprintf(stdout, "<tr><th>Service</th><th>Recipient</th><th>1st Delay</th><th>Stop after</th><th>Repeat</th><th>Time of Day</th><th>Colors</th></tr>\n");

		fprintf(stdout, "%s", STRBUF(buf));

		fprintf(stdout, "</table></td>\n");
		fprintf(stdout, "</tr>\n");
	}

	/* Finish off this host */
	fprintf(stdout, "</table>\n");

	freestrbuffer(buf);
}


static int coltext_compare(const void *v1, const void *v2)
{
	coltext_t **t1 = (coltext_t **)v1;
	coltext_t **t2 = (coltext_t **)v2;

	return strcmp((*t1)->colname, (*t2)->colname);
}

void load_columndocs(void)
{
	char fn[PATH_MAX];
	FILE *fd;
	strbuffer_t *inbuf;

	sprintf(fn, "%s/etc/columndoc.csv", xgetenv("XYMONHOME"));
	fd = fopen(fn, "r"); if (!fd) return;

	inbuf = newstrbuffer(0);
	initfgets(fd);

	/* Skip the header line */
	if (!unlimfgets(inbuf, fd)) { fclose(fd); freestrbuffer(inbuf); return; }

	while (unlimfgets(inbuf, fd)) {
		char *s1 = NULL, *s2 = NULL;

		s1 = strtok(STRBUF(inbuf), coldelim);
		if (s1) s2 = strtok(NULL, coldelim);

		if (s1 && s2) {
			coltext_t *newitem = (coltext_t *)calloc(1, sizeof(coltext_t));
			newitem->colname = strdup(s1);
			newitem->coldescr = strdup(s2);
			newitem->next = chead;
			chead = newitem;
			ccount++;
		}
	}
	fclose(fd);
	freestrbuffer(inbuf);
}


void print_columndocs(void)
{
	coltext_t **clist;
	coltext_t *cwalk;
	int i;

	clist = (coltext_t **)malloc(ccount * sizeof(coltext_t *));
	for (i=0, cwalk=chead; (cwalk); cwalk=cwalk->next,i++) clist[i] = cwalk;
	qsort(&clist[0], ccount, sizeof(coltext_t **), coltext_compare);

	fprintf(stdout, "<p style=\"page-break-before: always\">\n"); 
	fprintf(stdout, "<table width=\"100%%\" border=1 summary=\"Column descriptions\">\n");
	fprintf(stdout, "<tr><th colspan=2>Xymon column descriptions</th></tr>\n");
	for (i=0; (i<ccount); i++) {
		if (clist[i]->used) {
			fprintf(stdout, "<tr><td align=left valign=top>%s</td><td>%s</td></tr>\n",
				clist[i]->colname, clist[i]->coldescr);
		}
	}

	fprintf(stdout, "</table>\n");
}

htnames_t *get_proclist(char *hostname, char *statusbuf)
{
	char *bol, *eol;
	char *marker;
	htnames_t *head = NULL, *tail = NULL;

	if (!statusbuf) return NULL;

	marker = (char *)malloc(strlen(hostname) + 3);
	sprintf(marker, "\n%s|", hostname);
	if (strncmp(statusbuf, marker+1, strlen(marker)-1) == 0) {
		/* Found at start of buffer */
		bol = statusbuf;
	}
	else {
		bol = strstr(statusbuf, marker);
		if (bol) bol++;
	}
	xfree(marker);

	if (!bol) return NULL;

	bol += strlen(hostname) + 1;  /* Skip hostname and delimiter */
	marker = bol;
	eol = strchr(bol, '\n'); if (eol) *eol = '\0';
	marker = strstr(marker, "\\n&");
	while (marker) {
		char *p;
		htnames_t *newitem;

		marker += strlen("\\n&");
		if      (strncmp(marker, "green", 5) == 0) marker += 5;
		else if (strncmp(marker, "yellow", 6) == 0) marker += 6;
		else if (strncmp(marker, "red", 3) == 0) marker += 3;
		else marker = NULL;

		if (marker) {
			marker += strspn(marker, " \t");

			p = strstr(marker, "\\n"); if (p) *p = '\0';
			newitem = (htnames_t *)malloc(sizeof(htnames_t));
			newitem->name = strdup(marker);
			newitem->next = NULL;
			if (!tail) {
				head = tail = newitem;
			}
			else {
				tail->next = newitem;
				tail = newitem;
			}

			if (p) {
				*p = '\\';
			}

			marker = strstr(marker, "\\n&");
		}
	}
	if (eol) *eol = '\n';

	return head;
}

int main(int argc, char *argv[])
{
	int argi, hosti, testi;
	char *pagepattern = NULL, *hostpattern = NULL;
	char *cookie = NULL, *nexthost;
	char *xymoncmd = NULL, *procscmd = NULL, *svcscmd = NULL;
        int alertcolors, alertinterval;
	char configfn[PATH_MAX];
	char *respbuf = NULL, *procsbuf = NULL, *svcsbuf = NULL;
	hostlist_t *hwalk;
	htnames_t *twalk;
	hostlist_t **allhosts = NULL;
	htnames_t **alltests = NULL;
	int hostcount = 0, maxtests = 0;
	time_t now = getcurrenttime(NULL);
	sendreturn_t *sres;
	char *critconfigfn = NULL;
	int patternerror = 0;

	libxymon_init(argv[0]);
	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--delimiter=")) {
			char *p = strchr(argv[argi], '=');
			coldelim = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--critical") == 0) {
			criticalonly = 1;
		}
		else if (strcmp(argv[argi], "--old-critical-config") == 0) {
			newcritconfig = 0;
		}
		else if (argnmatch(argv[argi], "--critical-config=")) {
			char *p = strchr(argv[argi], '=');
			critconfigfn = strdup(p+1);
		}
		else if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
	}

	redirect_cgilog(programname);

	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
	load_critconfig(critconfigfn);

	/* Setup the filter we use for the report */
	cookie = get_cookie("pagepath"); if (cookie && *cookie) pagepattern = strdup(cookie);
	cookie = get_cookie("host");     if (cookie && *cookie) hostpattern = strdup(cookie);

	/* Fetch the list of host+test statuses we currently know about */
	if (pagepattern) {
		pcre *dummy;
		char *re;

		re = (char *)malloc(8 + 2*strlen(pagepattern));
		sprintf(re, "^%s$|^%s/.+", pagepattern, pagepattern);
		dummy = compileregex(re);
		if (dummy) {
			freeregex(dummy);

			xymoncmd = (char *)malloc(2*strlen(pagepattern) + 1024);
			procscmd = (char *)malloc(2*strlen(pagepattern) + 1024);
			svcscmd = (char *)malloc(2*strlen(pagepattern) + 1024);

			sprintf(xymoncmd, "xymondboard page=%s fields=hostname,testname", re);
			sprintf(procscmd,  "xymondboard page=%s test=procs fields=hostname,msg", re);
			sprintf(svcscmd,   "xymondboard page=%s test=svcs fields=hostname,msg", re);
		}
		else
			patternerror = 1;

		xfree(re);
	}
	else if (hostpattern) {
		pcre *dummy;
		char *re;

		re = (char *)malloc(3 + strlen(hostpattern));
		sprintf(re, "^%s$", hostpattern);
		dummy = compileregex(re);
		if (dummy) {
			freeregex(dummy);

			xymoncmd = (char *)malloc(strlen(hostpattern) + 1024);
			procscmd = (char *)malloc(strlen(hostpattern) + 1024);
			svcscmd = (char *)malloc(strlen(hostpattern) + 1024);

			sprintf(xymoncmd, "xymondboard host=^%s$ fields=hostname,testname", hostpattern);
			sprintf(procscmd,  "xymondboard host=^%s$ test=procs fields=hostname,msg", hostpattern);
			sprintf(svcscmd,   "xymondboard host=^%s$ test=svcs fields=hostname,msg", hostpattern);
		}
		else
			patternerror = 1;

		xfree(re);
	}
	else {
		xymoncmd = (char *)malloc(1024);
		procscmd = (char *)malloc(1024);
		svcscmd = (char *)malloc(1024);

		sprintf(xymoncmd, "xymondboard fields=hostname,testname");
		sprintf(procscmd,  "xymondboard test=procs fields=hostname,msg");
		sprintf(svcscmd,   "xymondboard test=svcs fields=hostname,msg");
	}

	if (patternerror) {
		errormsg("Invalid host/page filter\n");
		return 1;
	}

	sres = newsendreturnbuf(1, NULL);

	if (sendmessage(xymoncmd, NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
		errormsg("Cannot contact the Xymon server\n");
		return 1;
	}
	respbuf = getsendreturnstr(sres, 1);
	if (sendmessage(procscmd, NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
		errormsg("Cannot contact the Xymon server\n");
		return 1;
	}
	procsbuf = getsendreturnstr(sres, 1);
	if (sendmessage(svcscmd, NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
		errormsg("Cannot contact the Xymon server\n");
		return 1;
	}
	svcsbuf = getsendreturnstr(sres, 1);

	freesendreturnbuf(sres);

	if (!respbuf) {
		errormsg("Unable to find host information\n");
		return 1;
	}

	/* Parse it into a usable list */
	nexthost = respbuf;
	do {
		char *hname, *tname, *eoln;
		int wanted = 1;

		eoln = strchr(nexthost, '\n'); if (eoln) *eoln = '\0';
		hname = nexthost;
		tname = strchr(nexthost, '|'); if (tname) { *tname = '\0'; tname++; }

		if (criticalonly) {
			void *hinfo = hostinfo(hname);
			char *alerts = xmh_item(hinfo, XMH_NK);

			if (newcritconfig) {
				if (strcmp(criticalval(hname, tname, alerts), "No") == 0 ) wanted = 0;
			} else {
				if (!alerts) wanted = 0;
			}
		}

		if (wanted && hname && tname && strcmp(hname, "summary") && strcmp(tname, xgetenv("INFOCOLUMN")) && strcmp(tname, xgetenv("TRENDSCOLUMN"))) {
			htnames_t *newitem = (htnames_t *)malloc(sizeof(htnames_t));

			for (hwalk = hosthead; (hwalk && strcmp(hwalk->hostname, hname)); hwalk = hwalk->next);
			if (!hwalk) {
				hwalk = (hostlist_t *)calloc(1, sizeof(hostlist_t));
				hwalk->hostname = strdup(hname);
				hwalk->procs = get_proclist(hname, procsbuf);
				hwalk->svcs  = get_proclist(hname, svcsbuf);
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

	if (hostcount > 0) {
		allhosts = (hostlist_t **) malloc(hostcount * sizeof(hostlist_t *));
		for (hwalk = hosthead, hosti=0; (hwalk); hwalk = hwalk->next, hosti++) {
			allhosts[hosti] = hwalk;
			if (hwalk->testcount > maxtests) maxtests = hwalk->testcount;
		}
		if (maxtests > 0) alltests = (htnames_t **) malloc(maxtests * sizeof(htnames_t *));
		qsort(&allhosts[0], hostcount, sizeof(hostlist_t **), host_compare);
	}

	if ((hostcount == 0) || (maxtests == 0)) {
		printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		printf("<html><body><h1>No hosts or tests to report!</h1></body></html>\n");
		return 0;
	}

	/* Get the static info */
	load_all_links();
	init_tcp_services();
	pingcolumn = xgetenv("PINGCOLUMN");
	pingplus = (char *)malloc(strlen(pingcolumn) + 2);
	sprintf(pingplus, "%s=", pingcolumn);

	/* Load alert config */
	alertcolors = colorset(xgetenv("ALERTCOLORS"), ((1 << COL_GREEN) | (1 << COL_BLUE)));
	alertinterval = 60*atoi(xgetenv("ALERTREPEAT"));
	sprintf(configfn, "%s/etc/alerts.cfg", xgetenv("XYMONHOME"));
	load_alertconfig(configfn, alertcolors, alertinterval);
	load_columndocs();


	printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	sethostenv("", "", "", colorname(COL_BLUE), NULL);
	headfoot(stdout, "confreport", "", "header", COL_BLUE);

	fprintf(stdout, "<table width=\"100%%\" border=0>\n");
	fprintf(stdout, "<tr><th align=center colspan=2><font size=\"+2\">Xymon configuration Report</font></th></tr>\n");
	fprintf(stdout, "<tr><th valign=top align=left>Date</th><td>%s</td></tr>\n", ctime(&now));
	fprintf(stdout, "<tr><th valign=top align=left>%d hosts included</th><td>\n", hostcount);
	for (hosti=0; (hosti < hostcount); hosti++) {
		fprintf(stdout, "%s ", allhosts[hosti]->hostname);
	}
	fprintf(stdout, "</td></tr>\n");
	if (criticalonly) {
		fprintf(stdout, "<tr><th valign=top align=left>Filter</th><td>Only data for the &quot;Critical Systems&quot; view reported</td></tr>\n");
	}
	fprintf(stdout, "</table>\n");

	headfoot(stdout, "confreport", "", "front", COL_BLUE);

	for (hosti=0; (hosti < hostcount); hosti++) {
		for (twalk = allhosts[hosti]->tests, testi = 0; (twalk); twalk = twalk->next, testi++) {
			alltests[testi] = twalk;
		}
		qsort(&alltests[0], allhosts[hosti]->testcount, sizeof(htnames_t **), test_compare);

		print_host(allhosts[hosti], alltests, allhosts[hosti]->testcount);
	}

	headfoot(stdout, "confreport", "", "back", COL_BLUE);
	print_columndocs();

	headfoot(stdout, "confreport", "", "footer", COL_BLUE);

	return 0;
}

