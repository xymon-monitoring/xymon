/*----------------------------------------------------------------------------*/
/* Xymon RRD graph generator.                                                 */
/*                                                                            */
/* This is a CGI script for generating graphs from the data stored in the     */
/* RRD databases.                                                             */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

#include <pcre.h>
#include <rrd.h>

#include "libxymon.h"

#define HOUR_GRAPH  "e-48h"
#define DAY_GRAPH   "e-12d"
#define WEEK_GRAPH  "e-48d"
#define MONTH_GRAPH "e-576d"

/* RRDtool 1.0.x handles graphs with no DS definitions just fine. 1.2.x does not. */
#ifdef RRDTOOL12
#ifndef HIDE_EMPTYGRAPH
#define HIDE_EMPTYGRAPH 1
#endif
#endif

#ifdef HIDE_EMPTYGRAPH
unsigned char blankimg[] = "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\x04\x67\x41\x4d\x41\x00\x00\xb1\x8f\x0b\xfc\x61\x05\x00\x00\x00\x06\x62\x4b\x47\x44\x00\xff\x00\xff\x00\xff\xa0\xbd\xa7\x93\x00\x00\x00\x09\x70\x48\x59\x73\x00\x00\x0b\x12\x00\x00\x0b\x12\x01\xd2\xdd\x7e\xfc\x00\x00\x00\x07\x74\x49\x4d\x45\x07\xd1\x01\x14\x12\x21\x14\x7e\x4a\x3a\xd2\x00\x00\x00\x0d\x49\x44\x41\x54\x78\xda\x63\x60\x60\x60\x60\x00\x00\x00\x05\x00\x01\x7a\xa8\x57\x50\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";
#endif


char *hostname = NULL;
char **hostlist = NULL;
int hostlistsize = 0;
char *displayname = NULL;
char *service = NULL;
char *period = NULL;
time_t persecs = 0;
char *gtype = NULL;
char *glegend = NULL;
enum {ACT_MENU, ACT_SELZOOM, ACT_VIEW} action = ACT_VIEW;
time_t graphstart = 0;
time_t graphend = 0;
double upperlimit = 0.0;
int haveupperlimit = 0;
double lowerlimit = 0.0;
int havelowerlimit = 0;
int graphwidth = 0;
int graphheight = 0;
int ignorestalerrds = 0;
int bgcolor = COL_GREEN;

int coloridx = 0;
char *colorlist[] = { 
	"0000FF", "FF0000", "00CC00", "FF00FF", 
	"555555", "880000", "000088", "008800", 
	"008888", "888888", "880088", "FFFF00", 
	"888800", "00FFFF", "00FF00", "AA8800", 
	"AAAAAA", "DD8833", "DDCC33", "8888FF", 
	"5555AA", "B428D3", "FF5555", "DDDDDD", 
	"AAFFAA", "AAFFFF", "FFAAFF", "FFAA55", 
	"55AAFF", "AA55FF", 
	NULL
};

typedef struct gdef_t {
	char *name;
	char *fnpat;
	char *exfnpat;
	char *title;
	char *yaxis;
	char *graphopts;
	int  novzoom;
	char **defs;
	struct gdef_t *next;
} gdef_t;
gdef_t *gdefs = NULL;

typedef struct rrddb_t {
	char *key;
	char *rrdfn;
	char *rrdparam;
} rrddb_t;

rrddb_t *rrddbs = NULL;
int rrddbcount = 0;
int rrddbsize = 0;
int rrdidx = 0;
int paramlen = 0;
int firstidx = -1;
int idxcount = -1;
int lastidx = 0;

void errormsg(char *msg)
{
	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

void request_cacheflush(char *hostname)
{
	/* Build a cache-flush request, and send it to all of the $XYMONTMP/rrdctl.* sockets */
	char *req, *bufp;
	int bytesleft;
	DIR *dir;
	struct dirent *d;
	int ctlsocket = -1;

	req = (char *)malloc(strlen(hostname)+3);
	sprintf(req, "/%s/", hostname);

	ctlsocket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (ctlsocket == -1) {
		errprintf("Cannot get socket: %s\n", strerror(errno));
		return;
	}
	fcntl(ctlsocket, F_SETFL, O_NONBLOCK);

	dir = opendir(xgetenv("XYMONTMP"));
	while ((d = readdir(dir)) != NULL) {
		if (strncmp(d->d_name, "rrdctl.", 7) == 0) {
			struct sockaddr_un myaddr;
			socklen_t myaddrsz = 0;
			int n, sendfailed = 0;

			memset(&myaddr, 0, sizeof(myaddr));
			myaddr.sun_family = AF_UNIX;
			sprintf(myaddr.sun_path, "%s/%s", xgetenv("XYMONTMP"), d->d_name);
			myaddrsz = sizeof(myaddr);
			bufp = req; bytesleft = strlen(req);
			do {
				n = sendto(ctlsocket, bufp, bytesleft, 0, (struct sockaddr *)&myaddr, myaddrsz);
				if (n == -1) {
					if (errno != EAGAIN) {
						errprintf("Sendto failed: %s\n", strerror(errno));
					}

					sendfailed = 1;
				}
				else {
					bytesleft -= n;
					bufp += n;
				}
			} while ((!sendfailed) && (bytesleft > 0));
		}
	}
	closedir(dir);
	xfree(req);

	/*
	 * Sleep 0.3 secs to allow the cache flush to happen.
	 * Note: It isn't guaranteed to happen in this time, but
	 * there's a good chance that it will.
	 */
	usleep(300000);
}


void parse_query(void)
{
	cgidata_t *cgidata = NULL, *cwalk;
	char *stp;

	cgidata = cgi_request();

	cwalk = cgidata;
	while (cwalk) {
		if (strcmp(cwalk->name, "host") == 0) {
			char *hnames = strdup(cwalk->value);

			hostname = strtok_r(cwalk->value, ",", &stp);
			while (hostname) {
				if (hostlist == NULL) {
					hostlistsize = 1;
					hostlist = (char **)malloc(sizeof(char *));
					hostlist[0] = strdup(hostname);
				}
				else {
					hostlistsize++;
					hostlist = (char **)realloc(hostlist, (hostlistsize * sizeof(char *)));
					hostlist[hostlistsize-1] = strdup(hostname);
				}

				hostname = strtok_r(NULL, ",", &stp);
			}

			xfree(hnames);
			if (hostlist) hostname = hostlist[0];
		}
		else if (strcmp(cwalk->name, "service") == 0) {
			service = strdup(cwalk->value);
		}
		else if (strcmp(cwalk->name, "disp") == 0) {
			displayname = strdup(cwalk->value);
		}
		else if (strcmp(cwalk->name, "graph") == 0) {
			if (strcmp(cwalk->value, "hourly") == 0) {
				period = HOUR_GRAPH;
				persecs = 48*60*60;
				gtype = strdup(cwalk->value);
				glegend = "Last 48 Hours";
			}
			else if (strcmp(cwalk->value, "daily") == 0) {
				period = DAY_GRAPH;
				persecs = 12*24*60*60;
				gtype = strdup(cwalk->value);
				glegend = "Last 12 Days";
			}
			else if (strcmp(cwalk->value, "weekly") == 0) {
				period = WEEK_GRAPH;
				persecs = 48*24*60*60;
				gtype = strdup(cwalk->value);
				glegend = "Last 48 Days";
			}
			else if (strcmp(cwalk->value, "monthly") == 0) {
				period = MONTH_GRAPH;
				persecs = 576*24*60*60;
				gtype = strdup(cwalk->value);
				glegend = "Last 576 Days";
			}
			else if (strcmp(cwalk->value, "custom") == 0) {
				period = NULL;
				persecs = 0;
				gtype = strdup(cwalk->value);
				glegend = "";
			}
		}
		else if (strcmp(cwalk->name, "first") == 0) {
			firstidx = atoi(cwalk->value) - 1;
		}
		else if (strcmp(cwalk->name, "count") == 0) {
			idxcount = atoi(cwalk->value);
			lastidx = firstidx + idxcount - 1;
		}
		else if (strcmp(cwalk->name, "action") == 0) {
			if (cwalk->value) {
				if      (strcmp(cwalk->value, "menu") == 0) action = ACT_MENU;
				else if (strcmp(cwalk->value, "selzoom") == 0) action = ACT_SELZOOM;
				else if (strcmp(cwalk->value, "view") == 0) action = ACT_VIEW;
			}
		}
		else if (strcmp(cwalk->name, "graph_start") == 0) {
			if (cwalk->value) graphstart = atoi(cwalk->value);
		}
		else if (strcmp(cwalk->name, "graph_end") == 0) {
			if (cwalk->value) graphend = atoi(cwalk->value);
		}
		else if (strcmp(cwalk->name, "upper") == 0) {
			if (cwalk->value) { upperlimit = atof(cwalk->value); haveupperlimit = 1; }
		}
		else if (strcmp(cwalk->name, "lower") == 0) {
			if (cwalk->value) { lowerlimit = atof(cwalk->value); havelowerlimit = 1; }
		}
		else if (strcmp(cwalk->name, "graph_width") == 0) {
			if (cwalk->value) graphwidth = atoi(cwalk->value);
		}
		else if (strcmp(cwalk->name, "graph_height") == 0) {
			if (cwalk->value) graphheight = atoi(cwalk->value);
		}
		else if (strcmp(cwalk->name, "nostale") == 0) {
			ignorestalerrds = 1;
		}
		else if (strcmp(cwalk->name, "color") == 0) {
			int color = parse_color(cwalk->value);
			if (color != -1) bgcolor = color;
		}

		cwalk = cwalk->next;
	}

	if (hostlistsize == 1) {
		xfree(hostlist); hostlist = NULL;
	}
	else {
		displayname = hostname = strdup("");
	}

	if ((hostname == NULL) || (service == NULL)) errormsg("Invalid request - no host or service");
	if (displayname == NULL) displayname = hostname;
	if (graphstart && graphend) {
		char t1[15], t2[15];

		persecs = (graphend - graphstart);
		
		strftime(t1, sizeof(t1), "%d/%b/%Y", localtime(&graphstart));
		strftime(t2, sizeof(t2), "%d/%b/%Y", localtime(&graphend));
		glegend = (char *)malloc(40);
		sprintf(glegend, "%s - %s", t1, t2);
	}
}


void load_gdefs(char *fn)
{
	FILE *fd;
	strbuffer_t *inbuf;
	char *p;
	gdef_t *newitem = NULL;
	char **alldefs = NULL;
	int alldefcount = 0, alldefidx = 0;

	inbuf = newstrbuffer(0);
	fd = stackfopen(fn, "r", NULL);
	if (fd == NULL) errormsg("Cannot load graph definitions");
	while (stackfgets(inbuf, NULL)) {
		p = strchr(STRBUF(inbuf), '\n'); if (p) *p = '\0';
		p = STRBUF(inbuf); p += strspn(p, " \t");
		if ((strlen(p) == 0) || (*p == '#')) continue;

		if (*p == '[') {
			char *delim;
			
			if (newitem) {
				/* Save the current one, and start on the next item */
				alldefs[alldefidx] = NULL;
				newitem->defs = alldefs;
				newitem->next = gdefs;
				gdefs = newitem;
			}
			newitem = calloc(1, sizeof(gdef_t));
			delim = strchr(p, ']'); if (delim) *delim = '\0';
			newitem->name = strdup(p+1);
			alldefcount = 10;
			alldefs = (char **)malloc((alldefcount+1) * sizeof(char *));
			alldefidx = 0;
		}
		else if (strncasecmp(p, "FNPATTERN", 9) == 0) {
			p += 9; p += strspn(p, " \t");
			newitem->fnpat = strdup(p);
		}
		else if (strncasecmp(p, "EXFNPATTERN", 11) == 0) {
			p += 11; p += strspn(p, " \t");
			newitem->exfnpat = strdup(p);
		}
		else if (strncasecmp(p, "TITLE", 5) == 0) {
			p += 5; p += strspn(p, " \t");
			newitem->title = strdup(p);
		}
		else if (strncasecmp(p, "YAXIS", 5) == 0) {
			p += 5; p += strspn(p, " \t");
			newitem->yaxis = strdup(p);
		}
		else if (strncasecmp(p, "NOVZOOM", 7) == 0) {
			newitem->novzoom = 1;
		}
		else if (strncasecmp(p, "GRAPHOPTIONS", 12) == 0) {
			p += 12; p += strspn(p, " \t");
			newitem->graphopts = strdup(p);
		}
		else if (haveupperlimit && (strncmp(p, "-u ", 3) == 0)) {
			continue;
		}
		else if (haveupperlimit && (strncmp(p, "-upper ", 7) == 0)) {
			continue;
		}
		else if (havelowerlimit && (strncmp(p, "-l ", 3) == 0)) {
			continue;
		}
		else if (havelowerlimit && (strncmp(p, "-lower ", 7) == 0)) {
			continue;
		}
		else {
			if (alldefidx == alldefcount) {
				/* Must expand alldefs */
				alldefcount += 5;
				alldefs = (char **)realloc(alldefs, (alldefcount+1) * sizeof(char *));
			}
			alldefs[alldefidx++] = strdup(p);
		}
	}

	/* Pick up the last item */
	if (newitem) {
		/* Save the current one, and start on the next item */
		alldefs[alldefidx] = NULL;
		newitem->defs = alldefs;
		newitem->next = gdefs;
		gdefs = newitem;
	}

	stackfclose(fd);
	freestrbuffer(inbuf);
}

char *lookup_meta(char *keybuf, char *rrdfn)
{
	FILE *fd;
	char *metafn, *p;
	int servicelen = strlen(service);
	int keylen = strlen(keybuf);
	int found;
	static char buf[1024]; /* Must be static since it is returned to caller */

	p = strrchr(rrdfn, '/');
	if (!p) {
		metafn = strdup("rrd.meta");
	}
	else {
		metafn = (char *)malloc(strlen(rrdfn) + 10);
		*p = '\0';
		sprintf(metafn, "%s/rrd.meta", rrdfn);
		*p = '/';
	}
	fd = fopen(metafn, "r");
	xfree(metafn);

	if (!fd) return NULL;

	/* Find the first line that has our key and then whitespace */
	found = 0;
	while (!found && fgets(buf, sizeof(buf), fd)) {
		found = ( (strncmp(buf, service, servicelen) == 0) &&
			  (*(buf+servicelen) == ':') &&
			  (strncmp(buf+servicelen+1, keybuf, keylen) == 0) && 
			  isspace(*(buf+servicelen+1+keylen)) );
	}
	fclose(fd);

	if (found) {
		char *eoln, *val;

		val = buf + servicelen + 1 + keylen;
		val += strspn(val, " \t");

		eoln = strchr(val, '\n');
		if (eoln) *eoln = '\0';

		if (strlen(val) > 0) return val;
	}

	return NULL;
}

char *colon_escape(char *buf)
{
	static char *result = NULL;
	int count = 0;
	char *p, *inp, *outp;

	p = buf; while ((p = strchr(p, ':')) != NULL) { count++; p++; }
	if (count == 0) return buf;

	if (result) xfree(result);
	result = (char *) malloc(strlen(buf) + count + 1);
	*result = '\0';

	inp = buf; outp = result;
	while (*inp) {
		p = strchr(inp, ':');
		if (p == NULL) {
			strcat(outp, inp);
			inp += strlen(inp);
			outp += strlen(outp);
		}
		else {
			*p = '\0';
			strcat(outp, inp); strcat(outp, "\\:");
			*p = ':';
			inp = p+1;
			outp = outp + strlen(outp);
		}
	}

	*outp = '\0';
	return result;
}

char *expand_tokens(char *tpl)
{
	static strbuffer_t *result = NULL;
	char *inp, *p;

	if (strchr(tpl, '@') == NULL) return tpl;

	if (!result) result = newstrbuffer(2048); else clearstrbuffer(result);

	inp = tpl;
	while (*inp) {
		p = strchr(inp, '@');
		if (p == NULL) {
			addtobuffer(result, inp);
			inp += strlen(inp);
			continue;
		}

		*p = '\0';
		if (strlen(inp)) {
			addtobuffer(result, inp);
			inp = p;
		}
		*p = '@';

		if (strncmp(inp, "@RRDFN@", 7) == 0) {
			addtobuffer(result, colon_escape(rrddbs[rrdidx].rrdfn));
			inp += 7;
		}
		else if (strncmp(inp, "@RRDPARAM@", 10) == 0) {
			/* 
			 * We do a colon-escape first, then change all commas to slashes as
			 * this is a common mangling used by multiple backends (disk, http, iostat...)
			 */
			if (rrddbs[rrdidx].rrdparam) {
				char *val, *p;
				int vallen;
				char *resultstr;

				val = colon_escape(rrddbs[rrdidx].rrdparam);
				p = val; while ((p = strchr(p, ',')) != NULL) *p = '/';

				/* rrdparam strings may be very long. */
				if (strlen(val) > 100) *(val+100) = '\0';

				/*
				 * "paramlen" holds the longest string of the any of the matching files' rrdparam.
				 * However, because this goes through colon_escape(), the actual string length 
				 * passed to librrd functions may be longer (since ":" must be escaped as "\:").
				 */
				vallen = strlen(val);
				if (vallen < paramlen) vallen = paramlen;

				resultstr = (char *)malloc(vallen + 1);
				sprintf(resultstr, "%-*s", paramlen, val);
				addtobuffer(result, resultstr);
				xfree(resultstr);
			}
			inp += 10;
		}
		else if (strncmp(inp, "@RRDMETA@", 9) == 0) {
			/* 
			 * We do a colon-escape first, then change all commas to slashes as
			 * this is a common mangling used by multiple backends (disk, http, iostat...)
			 */
			if (rrddbs[rrdidx].rrdparam) {
				char *val, *p, *metaval;

				val = colon_escape(rrddbs[rrdidx].rrdparam);
				p = val; while ((p = strchr(p, ',')) != NULL) *p = '/';

				metaval = lookup_meta(val, rrddbs[rrdidx].rrdfn);
				if (metaval) addtobuffer(result, metaval);
			}
			inp += 9;
		}
		else if (strncmp(inp, "@RRDIDX@", 8) == 0) {
			char numstr[10];

			sprintf(numstr, "%d", rrdidx);
			addtobuffer(result, numstr);
			inp += 8;
		}
		else if (strncmp(inp, "@STACKIT@", 9) == 0) {
			/* Contributed by Gildas Le Nadan <gn1@sanger.ac.uk> */

			/* the STACK behavior changed between rrdtool 1.0.x
			 * and 1.2.x, hence the ifdef:
			 * - in 1.0.x, you replace the graph type (AREA|LINE)
			 *  for the graph you want to stack with the  STACK
			 *  keyword
			 * - in 1.2.x, you add the STACK keyword at the end
			 *  of the definition
			 *
			 * Please note that in both cases the first entry
			 * mustn't contain the keyword STACK at all, so
			 * we need a different treatment for the first rrdidx
			 *
			 * examples of graphs.cfg entries:
			 *
			 * - rrdtool 1.0.x
			 * @STACKIT@:la@RRDIDX@#@COLOR@:@RRDPARAM@
			 *
			 * - rrdtool 1.2.x
			 * AREA::la@RRDIDX@#@COLOR@:@RRDPARAM@:@STACKIT@
			 */
			char numstr[10];

			if (rrdidx == 0) {
#ifdef RRDTOOL12
				strcpy(numstr, "");
#else
				sprintf(numstr, "AREA");
#endif
			}
			else {
				sprintf(numstr, "STACK");
			}
			addtobuffer(result, numstr);
			inp += 9;
		}
		else if (strncmp(inp, "@SERVICE@", 9) == 0) {
			addtobuffer(result, service);
			inp += 9;
		}
		else if (strncmp(inp, "@COLOR@", 7) == 0) {
			addtobuffer(result, colorlist[coloridx]);
			inp += 7;
			coloridx++; if (colorlist[coloridx] == NULL) coloridx = 0;
		}
		else {
			addtobuffer(result, "@");
			inp += 1;
		}
	}

	return STRBUF(result);
}

int rrd_name_compare(const void *v1, const void *v2)
{
	rrddb_t *r1 = (rrddb_t *)v1;
	rrddb_t *r2 = (rrddb_t *)v2;
	char *endptr;
	long numkey1, numkey2;
	int key1isnumber, key2isnumber;

	/* See if the keys are all numeric; if yes, then do a numeric sort */
	numkey1 = strtol(r1->key, &endptr, 10); key1isnumber = (*endptr == '\0');
	numkey2 = strtol(r2->key, &endptr, 10); key2isnumber = (*endptr == '\0');

	if (key1isnumber && key2isnumber) {
		if (numkey1 < numkey2) return -1;
		else if (numkey1 > numkey2) return 1;
		else return 0;
	}

	return strcmp(r1->key, r2->key);
}

void graph_link(FILE *output, char *uri, char *grtype, time_t seconds)
{
	time_t gstart, gend;
	char *grtype_s;

	fprintf(output, "<tr>\n");
	grtype_s = htmlquoted(grtype);

	switch (action) {
	  case ACT_MENU:
		fprintf(output, "  <td align=\"left\"><img src=\"%s&amp;action=view&amp;graph=%s\" alt=\"%s graph\"></td>\n",
			uri, grtype_s, grtype_s);
		fprintf(output, "  <td align=\"left\" valign=\"top\"> <a href=\"%s&amp;graph=%s&amp;action=selzoom&amp;color=%s\"> <img src=\"%s/zoom.gif\" border=0 alt=\"Zoom graph\" style='padding: 3px'> </a> </td>\n",
			uri, grtype_s, colorname(bgcolor), getenv("XYMONSKIN"));
		break;

	  case ACT_SELZOOM:
		if (graphend == 0) gend = getcurrenttime(NULL); else gend = graphend;
		if (graphstart == 0) gstart = gend - persecs; else gstart = graphstart;

		fprintf(output, "  <td align=\"left\"><img id='zoomGraphImage' src=\"%s&amp;graph=%s&amp;action=view&amp;graph_start=%u&amp;graph_end=%u&amp;graph_height=%d&amp;graph_width=%d&amp;",
			uri, grtype_s, (int) gstart, (int) gend, graphheight, graphwidth);
		if (haveupperlimit) fprintf(output, "&amp;upper=%f", upperlimit);
		if (havelowerlimit) fprintf(output, "&amp;lower=%f", lowerlimit);
		fprintf(output, "\" alt=\"Zoom source image\"></td>\n");
		break;

	  case ACT_VIEW:
		break;
	}

	fprintf(output, "</tr>\n");
}

char *build_selfURI(void)
{
	strbuffer_t *result = newstrbuffer(2048);
	char numbuf[40];

	addtobuffer(result, xgetenv("SCRIPT_NAME"));

	addtobuffer(result, "?host=");
	if (hostlist) {
		int i;

		addtobuffer(result, urlencode(hostlist[0]));
		for (i = 1; (i < hostlistsize); i++) {
			addtobuffer(result, ",");
			addtobuffer(result, urlencode(hostlist[i]));
		}
	}
	else {
		addtobuffer(result, urlencode(hostname));
	}

	addtobuffer(result, "&amp;color="); addtobuffer(result, colorname(bgcolor));
	if (service) {
		addtobuffer(result, "&amp;service=");
		addtobuffer(result, urlencode(service));
	}
	if (graphheight) {
		snprintf(numbuf, sizeof(numbuf)-1, "%d", graphheight); 
		addtobuffer(result, "&amp;graph_height="); 
		addtobuffer(result, urlencode(numbuf));
	}
	if (graphwidth) {
		snprintf(numbuf, sizeof(numbuf)-1, "%d", graphwidth); 
		addtobuffer(result, "&amp;graph_width="); 
		addtobuffer(result, urlencode(numbuf));
	}

	if (displayname && (displayname != hostname)) {
		addtobuffer(result, "&amp;disp=");
		addtobuffer(result, urlencode(displayname));
	}

	if (firstidx != -1) {
		snprintf(numbuf, sizeof(numbuf)-1, "&amp;first=%d", firstidx+1);
		addtobuffer(result, numbuf);
	}
	if (idxcount != -1) {
		snprintf(numbuf, sizeof(numbuf)-1, "&amp;count=%d", idxcount);
		addtobuffer(result, numbuf);
	}
	if (ignorestalerrds) addtobuffer(result, "&amp;nostale");

	return STRBUF(result);
}


void build_menu_page(char *selfURI, int backsecs)
{
	/* This is special-handled, because we just want to generate an HTML link page */
	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	sethostenv(displayname, "", service, colorname(bgcolor), hostname);
	sethostenv_backsecs(backsecs);

	headfoot(stdout, "graphs", "", "header", bgcolor);

	fprintf(stdout, "<table align=\"center\" summary=\"Graphs\">\n");

	graph_link(stdout, selfURI, "hourly",      48*60*60);
	graph_link(stdout, selfURI, "daily",    12*24*60*60);
	graph_link(stdout, selfURI, "weekly",   48*24*60*60);
	graph_link(stdout, selfURI, "monthly", 576*24*60*60);

	fprintf(stdout, "</table>\n");

	headfoot(stdout, "graphs", "", "footer", bgcolor);
}


void generate_graph(char *gdeffn, char *rrddir, char *graphfn)
{
	gdef_t *gdef = NULL, *gdefuser = NULL;
	int wantsingle = 0;
	DIR *dir;
	time_t now = getcurrenttime(NULL);

	int argi, pcount;

	/* Options for rrd_graph() */
	int  rrdargcount;
	char **rrdargs = NULL;	/* The full argv[] table of string pointers to arguments */
	char heightopt[30];	/* -h HEIGHT */
	char widthopt[30];	/* -w WIDTH */
	char upperopt[30];	/* -u MAX */
	char loweropt[30];	/* -l MIN */
	char startopt[30];	/* -s STARTTIME */
	char endopt[30];	/* -e ENDTIME */
	char graphtitle[1024];	/* --title TEXT */
	char timestamp[50];	/* COMMENT with timestamp graph was generated */

	/* Return variables from rrd_graph() */
	int result;
	char **calcpr = NULL;
	int xsize, ysize;
	double ymin, ymax;

	char *useroptval = NULL;
	char **useropts = NULL;
	int useroptcount = 0, useroptidx;

	/* Find the graphs.cfg file and load it */
	if (gdeffn == NULL) {
		char fnam[PATH_MAX];
		sprintf(fnam, "%s/etc/graphs.cfg", xgetenv("XYMONHOME"));
		gdeffn = strdup(fnam);
	}
	load_gdefs(gdeffn);


	/* Determine the real service name. It might be a multi-service graph */
	if (strchr(service, ':') || strchr(service, '.')) {
		/*
		 * service is "tcp:foo" - so use the "tcp" graph definition, but for a
		 * single service (as if service was set to just "foo").
		 */
		char *delim = service + strcspn(service, ":.");
		char *realservice;

		*delim = '\0';
		realservice = strdup(delim+1);

		/* The requested gdef only acts as a fall-back solution so dont set gdef here. */
		for (gdefuser = gdefs; (gdefuser && strcmp(service, gdefuser->name)); gdefuser = gdefuser->next) ;
		strcpy(service, realservice);
		wantsingle = 1;

		xfree(realservice);
	}

	/*
	 * Lookup which RRD file corresponds to the service-name, and how we handle this graph.
	 * We first lookup the service name in the graph definition list.
	 * If that fails, then we try mapping it via the servicename -> RRD map.
	 */
	for (gdef = gdefs; (gdef && strcmp(service, gdef->name)); gdef = gdef->next) ;
	if (gdef == NULL) {
		if (gdefuser) {
			gdef = gdefuser;
		}
		else {
			xymonrrd_t *ldef = find_xymon_rrd(service, NULL);
			if (ldef) {
				for (gdef = gdefs; (gdef && strcmp(ldef->xymonrrdname, gdef->name)); gdef = gdef->next) ;
				wantsingle = 1;
			}
		}
	}
	if (gdef == NULL) errormsg("Unknown graph requested");
	if (hostlist && (gdef->fnpat == NULL)) {
		char *multiname = (char *)malloc(strlen(gdef->name) + 7);
		sprintf(multiname, "%s-multi", gdef->name);
		for (gdef = gdefs; (gdef && strcmp(multiname, gdef->name)); gdef = gdef->next) ;
		if (gdef == NULL) errormsg("Unknown multi-graph requested");
		xfree(multiname);
	}


	/*
	 * If we're here only to collect the min/max values for the graph but it doesn't
	 * allow vertical zoom, then there's no reason to waste anymore time.
	 */
	if ((action == ACT_SELZOOM) && gdef->novzoom) {
		haveupperlimit = havelowerlimit = 0;
		return;
	}

	/* Determine the directory with the host RRD files, and go there. */
	if (rrddir == NULL) {
		char dnam[PATH_MAX];

		if (hostlist) sprintf(dnam, "%s", xgetenv("XYMONRRDS"));
		else sprintf(dnam, "%s/%s", xgetenv("XYMONRRDS"), hostname);

		rrddir = strdup(dnam);
	}
	if (chdir(rrddir)) errormsg("Cannot access RRD directory");

	/* Request an RRD cache flush from the xymond_rrd update daemon */
	if (hostlist) {
		int i;
		for (i=0; (i < hostlistsize); i++) request_cacheflush(hostlist[i]);
	}
	else if (hostname) request_cacheflush(hostname);

	/* What RRD files do we have matching this request? */
	if (hostlist || (gdef->fnpat == NULL)) {
		/*
		 * No pattern, just a single file. It doesnt matter if it exists, because
		 * these types of graphs usually have a hard-coded value for the RRD filename
		 * in the graph definition.
		 */
		rrddbcount = rrddbsize = (hostlist ? hostlistsize : 1);
		rrddbs = (rrddb_t *)malloc((rrddbsize + 1) * sizeof(rrddb_t));

		if (!hostlist) {
			rrddbs[0].key = strdup(service);
			rrddbs[0].rrdfn = (char *)malloc(strlen(gdef->name) + strlen(".rrd") + 1);
			sprintf(rrddbs[0].rrdfn, "%s.rrd", gdef->name);
			rrddbs[0].rrdparam = NULL;
		}
		else {
			int i, maxlen;
			char paramfmt[10];

			for (i=0, maxlen=0; (i < hostlistsize); i++) {
				if (strlen(hostlist[i]) > maxlen) maxlen = strlen(hostlist[i]);
			}
			sprintf(paramfmt, "%%-%ds", maxlen+1);

			for (i=0; (i < hostlistsize); i++) {
				rrddbs[i].key = strdup(service);
				rrddbs[i].rrdfn = (char *)malloc(strlen(hostlist[i]) + strlen(gdef->fnpat) + 2);
				sprintf(rrddbs[i].rrdfn, "%s/%s", hostlist[i], gdef->fnpat);

				rrddbs[i].rrdparam = (char *)malloc(maxlen + 2);
				sprintf(rrddbs[i].rrdparam, paramfmt, hostlist[i]);
			}
		}
	}
	else {
		struct dirent *d;
		pcre *pat, *expat = NULL;
		const char *errmsg;
		int errofs, result;
		int ovector[30];
		struct stat st;
		time_t now = getcurrenttime(NULL);

		/* Scan the directory to see what RRD files are there that match */
		dir = opendir("."); if (dir == NULL) errormsg("Unexpected error while accessing RRD directory");

		/* Setup the pattern to match filenames against */
		pat = pcre_compile(gdef->fnpat, PCRE_CASELESS, &errmsg, &errofs, NULL);
		if (!pat) {
			char msg[8192];

			snprintf(msg, sizeof(msg), "graphs.cfg error, PCRE pattern %s invalid: %s, offset %d\n",
				 htmlquoted(gdef->fnpat), errmsg, errofs);
			errormsg(msg);
		}
		if (gdef->exfnpat) {
			expat = pcre_compile(gdef->exfnpat, PCRE_CASELESS, &errmsg, &errofs, NULL);
			if (!expat) {
				char msg[8192];

				snprintf(msg, sizeof(msg), 
					 "graphs.cfg error, PCRE pattern %s invalid: %s, offset %d\n",
					 htmlquoted(gdef->exfnpat), errmsg, errofs);
				errormsg(msg);
			}
		}

		/* Allocate an initial filename table */
		rrddbsize = 5;
		rrddbs = (rrddb_t *) malloc((rrddbsize+1) * sizeof(rrddb_t));

		while ((d = readdir(dir)) != NULL) {
			char *ext;
			char param[PATH_MAX];

			/* Ignore dot-files and files with names shorter than ".rrd" */
			if (*(d->d_name) == '.') continue;
			ext = d->d_name + strlen(d->d_name) - strlen(".rrd");
			if ((ext <= d->d_name) || (strcmp(ext, ".rrd") != 0)) continue;

			/* First check the exclude pattern. */
			if (expat) {
				result = pcre_exec(expat, NULL, d->d_name, strlen(d->d_name), 0, 0, 
						   ovector, (sizeof(ovector)/sizeof(int)));
				if (result >= 0) continue;
			}

			/* Then see if the include pattern matches. */
			result = pcre_exec(pat, NULL, d->d_name, strlen(d->d_name), 0, 0, 
					   ovector, (sizeof(ovector)/sizeof(int)));
			if (result < 0) continue;

			if (wantsingle) {
				/* "Single" graph, i.e. a graph for a service normally included in a bundle (tcp) */
				if (strstr(d->d_name, service) == NULL) continue;
			}

			/* 
			 * Has it been updated recently (within the past 24 hours) ? 
			 * We dont want old graphs to mess up multi-displays.
			 */
			if (ignorestalerrds && (stat(d->d_name, &st) == 0) && ((now - st.st_mtime) > 86400)) {
				continue;
			}

			/* We have a matching file! */
			rrddbs[rrddbcount].rrdfn = strdup(d->d_name);
			if (pcre_copy_substring(d->d_name, ovector, result, 1, param, sizeof(param)) > 0) {
				/*
				 * This is ugly, but I cannot find a pretty way of un-mangling
				 * the disk- and http-data that has been molested by the back-end.
				 */
				if ((strcmp(param, ",root") == 0) &&
				    ((strncmp(gdef->name, "disk", 4) == 0) || (strncmp(gdef->name, "inode", 5) == 0)) ) {
					rrddbs[rrddbcount].rrdparam = strdup(",");
				}
				else if ((strcmp(gdef->name, "http") == 0) && (strncmp(param, "http", 4) != 0)) {
					rrddbs[rrddbcount].rrdparam = (char *)malloc(strlen("http://")+strlen(param)+1);
					sprintf(rrddbs[rrddbcount].rrdparam, "http://%s", param);
				}
				else {
					rrddbs[rrddbcount].rrdparam = strdup(param);
				}

				if (strlen(rrddbs[rrddbcount].rrdparam) > paramlen) {
					/*
					 * "paramlen" holds the longest string of the any of the matching files' rrdparam.
					 */
					paramlen = strlen(rrddbs[rrddbcount].rrdparam);
				}

				rrddbs[rrddbcount].key = strdup(rrddbs[rrddbcount].rrdparam);
			}
			else {
				rrddbs[rrddbcount].key = strdup(d->d_name);
				rrddbs[rrddbcount].rrdparam = NULL;
			}

			rrddbcount++;
			if (rrddbcount == rrddbsize) {
				rrddbsize += 5;
				rrddbs = (rrddb_t *)realloc(rrddbs, (rrddbsize+1) * sizeof(rrddb_t));
			}
		}
		pcre_free(pat);
		if (expat) pcre_free(expat);
		closedir(dir);
	}
	rrddbs[rrddbcount].key = rrddbs[rrddbcount].rrdfn = rrddbs[rrddbcount].rrdparam = NULL;

	/* Sort them so the display looks prettier */
	qsort(&rrddbs[0], rrddbcount, sizeof(rrddb_t), rrd_name_compare);

	/* Setup the title */
	if (!gdef->title) gdef->title = strdup("");
	if (strncmp(gdef->title, "exec:", 5) == 0) {
		char *pcmd;
		int i, pcmdlen = 0;
		FILE *pfd;
		char *p;

		pcmdlen = strlen(gdef->title+5) + strlen(displayname) + strlen(service) + strlen(glegend) + 5;
		for (i=0; (i<rrddbcount); i++) pcmdlen += (strlen(rrddbs[i].rrdfn) + 3);

		p = pcmd = (char *)malloc(pcmdlen+1);
		p += sprintf(p, "%s %s %s \"%s\"", gdef->title+5, displayname, service, glegend);
		for (i=0; (i<rrddbcount); i++) {
			if ((firstidx == -1) || ((i >= firstidx) && (i <= lastidx))) {
				p += sprintf(p, " \"%s\"", rrddbs[i].rrdfn);
			}
		}
		pfd = popen(pcmd, "r");
		if (pfd) {
			if (fgets(graphtitle, sizeof(graphtitle), pfd) == NULL) *graphtitle = '\0';
			pclose(pfd);
		}

		/* Drop any newline at end of the title */
		p = strchr(graphtitle, '\n'); if (p) *p = '\0';
	}
	else {
		sprintf(graphtitle, "%s %s %s", displayname, gdef->title, glegend);
	}

	sprintf(heightopt, "-h%d", graphheight);
	sprintf(widthopt, "-w%d", graphwidth);

	/*
	 * Grab user-provided additional rrd_graph options from RRDGRAPHOPTS
	 */
	useroptcount = 0;
	useroptval = gdef->graphopts;
	if (!useroptval) useroptval = getenv("RRDGRAPHOPTS");
	if (useroptval) {
		char *tok;

		useropts = (char **)calloc(1, sizeof(char *));
		useroptval = strdup(useroptval);
		tok = strtok(useroptval, " ");
		while (tok) {
			useroptcount++;
			useropts = (char **)realloc(useropts, (useroptcount+1)*sizeof(char *));
			useropts[useroptcount-1] = tok;
			useropts[useroptcount] = NULL;
			tok = strtok(NULL, " ");
		}
	}

	/*
	 * Setup the arguments for calling rrd_graph. 
	 * There's up to 16 standard arguments, plus the 
	 * graph-specific ones (which may be repeated if
	 * there are multiple RRD-files to handle).
	 */
	for (pcount = 0; (gdef->defs[pcount]); pcount++) ;
	rrdargs = (char **) calloc(16 + pcount*rrddbcount + useroptcount + 1, sizeof(char *));


	argi = 0;
	rrdargs[argi++]  = "rrdgraph";
	rrdargs[argi++]  = (action == ACT_VIEW) ? graphfn : "/dev/null";
	rrdargs[argi++]  = "--title";
	rrdargs[argi++]  = graphtitle;
	rrdargs[argi++]  = widthopt;
	rrdargs[argi++]  = heightopt;
	rrdargs[argi++]  = "-v";
	rrdargs[argi++]  = gdef->yaxis;
	rrdargs[argi++]  = "-a";
	rrdargs[argi++]  = "PNG";

	if (haveupperlimit) {
		sprintf(upperopt, "-u %f", upperlimit);
		rrdargs[argi++] = upperopt;
	}
	if (havelowerlimit) {
		sprintf(loweropt, "-l %f", lowerlimit);
		rrdargs[argi++] = loweropt;
	}
	if (haveupperlimit || havelowerlimit) rrdargs[argi++] = "--rigid";

	if (graphstart) sprintf(startopt, "-s %u", (unsigned int) graphstart);
	else sprintf(startopt, "-s %s", period);
	rrdargs[argi++] = startopt;

	if (graphend) {
		sprintf(endopt, "-e %u", (unsigned int) graphend);
		rrdargs[argi++] = endopt;
	}

	for (useroptidx=0; (useroptidx < useroptcount); useroptidx++) {
		rrdargs[argi++] = useropts[useroptidx];
	}

	for (rrdidx=0; (rrdidx < rrddbcount); rrdidx++) {
		if ((firstidx == -1) || ((rrdidx >= firstidx) && (rrdidx <= lastidx))) {
			int i;
			for (i=0; (gdef->defs[i]); i++) {
				rrdargs[argi++] = strdup(expand_tokens(gdef->defs[i]));
			}
		}
	}

#ifdef RRDTOOL12
	strftime(timestamp, sizeof(timestamp), "COMMENT:Updated\\: %d-%b-%Y %H\\:%M\\:%S", localtime(&now));
#else
	strftime(timestamp, sizeof(timestamp), "COMMENT:Updated: %d-%b-%Y %H:%M:%S", localtime(&now));
#endif
	rrdargs[argi++] = strdup(timestamp);


	rrdargcount = argi; rrdargs[argi++] = NULL;


	if (debug) { for (argi=0; (argi < rrdargcount); argi++) dbgprintf("%s\n", rrdargs[argi]); }

	/* If sending to stdout, print the HTTP header first. */
	if ((action == ACT_VIEW) && (strcmp(graphfn, "-") == 0)) {
		time_t expiretime = now + 300;
		char expirehdr[100];

		printf("Content-type: image/png\n");
		strftime(expirehdr, sizeof(expirehdr), "Expires: %a, %d %b %Y %H:%M:%S GMT", gmtime(&expiretime));
		printf("%s\n", expirehdr);
		printf("\n");

#ifdef HIDE_EMPTYGRAPH
		/* It works, but we still get the "zoom" magnifying glass which looks odd */
		if (rrddbcount == 0) {
			/* No graph */
			fwrite(blankimg, 1, sizeof(blankimg), stdout);
			return;
		}
#endif
	}

	/* All set - generate the graph */
	rrd_clear_error();

#ifdef RRDTOOL12
	result = rrd_graph(rrdargcount, rrdargs, &calcpr, &xsize, &ysize, NULL, &ymin, &ymax);

	/*
	 * If we have neither the upper- nor lower-limits of the graph, AND we allow vertical 
	 * zooming of this graph, then save the upper/lower limit values and flag that we have 
	 * them. The values are then used for the zoom URL we construct later on.
	 */
	if (!haveupperlimit && !havelowerlimit) {
		upperlimit = ymax; haveupperlimit = 1;
		lowerlimit = ymin; havelowerlimit = 1;
	}
#else
	result = rrd_graph(rrdargcount, rrdargs, &calcpr, &xsize, &ysize);
#endif

	/* Was it OK ? */
	if (rrd_test_error() || (result != 0)) {
		if (calcpr) { 
			int i;
			for (i=0; (calcpr[i]); i++) xfree(calcpr[i]);
			calcpr = NULL;
		}

		errormsg(rrd_get_error());
	}

	if (useroptval) xfree(useroptval);
	if (useropts) xfree(useropts);
}

void generate_zoompage(char *selfURI)
{
	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	sethostenv(displayname, "", service, colorname(bgcolor), hostname);
	headfoot(stdout, "graphs", "", "header", bgcolor);


	fprintf(stdout, "  <div id='zoomBox' style='position:absolute; overflow:none; left:0px; top:0px; width:0px; height:0px; visibility:visible; background:red; filter:alpha(opacity=50); -moz-opacity:0.5; opacity:0.5; -khtml-opacity:0.5'></div>\n");
	fprintf(stdout, "  <div id='zoomSensitiveZone' style='position:absolute; overflow:none; left:0px; top:0px; width:0px; height:0px; visibility:visible; cursor:crosshair; background:blue; filter:alpha(opacity=0); opacity:0; -moz-opacity:0; -khtml-opacity:0'></div>\n");

	fprintf(stdout, "<table align=\"center\" summary=\"Graphs\">\n");
	graph_link(stdout, selfURI, gtype, 0);
	fprintf(stdout, "</table>\n");

	{
		char zoomjsfn[PATH_MAX];
		struct stat st;

		sprintf(zoomjsfn, "%s/web/zoom.js", xgetenv("XYMONHOME"));
		if (stat(zoomjsfn, &st) == 0) {
			FILE *fd;
			char *buf;
			size_t n;
			char *zoomrightoffsetmarker = "var cZoomBoxRightOffset = -";
			char *zoomrightoffsetp;

			fd = fopen(zoomjsfn, "r");
			if (fd) {
				buf = (char *)malloc(st.st_size+1);
				n = fread(buf, 1, st.st_size, fd);
				fclose(fd);

#ifdef RRDTOOL12
				zoomrightoffsetp = strstr(buf, zoomrightoffsetmarker);
				if (zoomrightoffsetp) {
					zoomrightoffsetp += strlen(zoomrightoffsetmarker);
					memcpy(zoomrightoffsetp, "30", 2);
				}
#endif

				fwrite(buf, 1, n, stdout);
			}
		}
	}


	headfoot(stdout, "graphs", "", "footer", bgcolor);
}


int main(int argc, char *argv[])
{
	/* Command line settings */
	int argi;
	char *envarea = NULL;
	char *rrddir  = NULL;		/* RRD files top-level directory */
	char *gdeffn  = NULL;		/* graphs.cfg file */
	char *graphfn = "-";		/* Output filename, default is stdout */

	char *selfURI;

	/* Setup defaults */
	graphwidth = atoi(xgetenv("RRDWIDTH"));
	graphheight = atoi(xgetenv("RRDHEIGHT"));

	/* See what we want to do - i.e. get hostname, service and graph-type */
	parse_query();

	/* Handle any command-line args */
	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--rrddir=")) {
			char *p = strchr(argv[argi], '=');
			rrddir = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--config=")) {
			char *p = strchr(argv[argi], '=');
			gdeffn = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--save=") == 0) {
			char *p = strchr(argv[argi], '=');
			graphfn = strdup(p+1);
		}
	}

	redirect_cgilog("showgraph");

	selfURI = build_selfURI();

	if (action == ACT_MENU) {
		build_menu_page(selfURI, graphend-graphstart);
		return 0;
	}

	if ((action == ACT_VIEW) || !(haveupperlimit && havelowerlimit)) {
		generate_graph(gdeffn, rrddir, graphfn);
	}

	if (action == ACT_SELZOOM) {
		generate_zoompage(selfURI);
	}

	return 0;
}

