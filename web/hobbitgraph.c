/*----------------------------------------------------------------------------*/
/* Hobbit RRD graph generator.                                                */
/*                                                                            */
/* This is a CGI script for generating graphs from the data stored in the     */
/* RRD databases.                                                             */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitgraph.c,v 1.30 2005-05-15 07:40:32 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include <pcre.h>
#include <rrd.h>

#include "libbbgen.h"

#define HOUR_GRAPH  "e-48h"
#define DAY_GRAPH   "e-12d"
#define WEEK_GRAPH  "e-48d"
#define MONTH_GRAPH "e-576d"

char *hostname = NULL;
char *displayname = NULL;
char *service = NULL;
char *period = NULL;
time_t persecs = 0;
char *gtype = NULL;
char *glegend = NULL;
enum {ACT_MENU, ACT_SELZOOM, ACT_SHOWZOOM, ACT_VIEW} action = ACT_VIEW;
time_t graphstart = 0;
time_t graphend = 0;
int haveupper = 0;
double upperlimit = 0.0;
int havelower = 0;
double lowerlimit = 0.0;

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
	printf("Content-type: text/html\n\n");
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

void parse_query(void)
{
	char *query, *token;

	if (xgetenv("QUERY_STRING") == NULL) {
		errormsg("Missing request");
		return;
	}
	else query = urldecode("QUERY_STRING");

	if (!urlvalidate(query, NULL)) {
		errormsg("Invalid request");
		return;
	}

	token = strtok(query, "&");
	while (token) {
		char *val = NULL;
		val = strchr(token, '='); if (val) { *val = '\0'; val++; }
		if (strcmp(token, "host") == 0) {
			hostname = strdup(val);
		}
		else if (strcmp(token, "service") == 0) {
			service = strdup(val);
		}
		else if (strcmp(token, "disp") == 0) {
			displayname = strdup(val);
		}
		else if (strcmp(token, "graph") == 0) {
			if (strcmp(val, "hourly") == 0) {
				period = HOUR_GRAPH;
				persecs = 48*60*60;
				gtype = strdup(val);
				glegend = "Last 48 Hours";
			}
			else if (strcmp(val, "daily") == 0) {
				period = DAY_GRAPH;
				persecs = 12*24*60*60;
				gtype = strdup(val);
				glegend = "Last 12 Days";
			}
			else if (strcmp(val, "weekly") == 0) {
				period = WEEK_GRAPH;
				persecs = 48*24*60*60;
				gtype = strdup(val);
				glegend = "Last 48 Days";
			}
			else if (strcmp(val, "monthly") == 0) {
				period = MONTH_GRAPH;
				persecs = 576*24*60*60;
				gtype = strdup(val);
				glegend = "Last 576 Days";
			}
			else if (strcmp(val, "custom") == 0) {
				period = NULL;
				persecs = 0;
				gtype = strdup(val);
				glegend = "";
			}
		}
		else if (strcmp(token, "first") == 0) {
			firstidx = atoi(val) - 1;
		}
		else if (strcmp(token, "count") == 0) {
			idxcount = atoi(val);
			lastidx = firstidx + idxcount - 1;
		}
		else if (strcmp(token, "action") == 0) {
			if (val) {
				if      (strcmp(val, "menu") == 0) action = ACT_MENU;
				else if (strcmp(val, "selzoom") == 0) action = ACT_SELZOOM;
				else if (strcmp(val, "showzoom") == 0) action = ACT_SHOWZOOM;
				else if (strcmp(val, "view") == 0) action = ACT_VIEW;
			}
		}
		else if (strcmp(token, "graph_start") == 0) {
			if (val) graphstart = atoi(val);
		}
		else if (strcmp(token, "graph_end") == 0) {
			if (val) graphend = atoi(val);
		}
		else if (strcmp(token, "upper") == 0) {
			if (val) { upperlimit = atof(val); haveupper = 1; }
		}
		else if (strcmp(token, "lower") == 0) {
			if (val) { lowerlimit = atof(val); havelower = 1; }
		}

		token = strtok(NULL, "&");
	}

	if ((hostname == NULL) || (service == NULL)) errormsg("Invalid request - no host or service");
	if (displayname == NULL) displayname = hostname;
	if (graphstart && graphend) persecs = (graphend - graphstart);
}


void load_gdefs(char *fn)
{
	FILE *fd;
	char l[4096];
	char *p;
	gdef_t *newitem = NULL;
	char **alldefs = NULL;
	int alldefcount = 0, alldefidx = 0;

	fd = stackfopen(fn, "r");
	if (fd == NULL) errormsg("Cannot load graph definitions");
	while (stackfgets(l, sizeof(l), "include", NULL)) {
		p = strchr(l, '\n'); if (p) *p = '\0';
		p = l; p += strspn(p, " \t");
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
		else if (strncmp(p, "FNPATTERN", 9) == 0) {
			p += 9; p += strspn(p, " \t");
			newitem->fnpat = strdup(p);
		}
		else if (strncmp(p, "EXFNPATTERN", 11) == 0) {
			p += 11; p += strspn(p, " \t");
			newitem->exfnpat = strdup(p);
		}
		else if (strncmp(p, "TITLE", 5) == 0) {
			p += 5; p += strspn(p, " \t");
			newitem->title = strdup(p);
		}
		else if (strncmp(p, "YAXIS", 5) == 0) {
			p += 5; p += strspn(p, " \t");
			newitem->yaxis = strdup(p);
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
	static char result[4096];
	char *inp, *outp, *p;

	if (strchr(tpl, '@') == NULL) return tpl;

	*result = '\0'; inp = tpl; outp = result;
	while (*inp) {
		p = strchr(inp, '@');
		if (p == NULL) {
			strcpy(outp, inp);
			inp += strlen(inp);
			outp += strlen(outp);
			continue;
		}

		*p = '\0';
		if (strlen(inp)) {
			strcpy(outp, inp);
			inp = p;
			outp += strlen(outp);
		}
		*p = '@';

		if (strncmp(inp, "@RRDFN@", 7) == 0) {
			strcpy(outp, colon_escape(rrddbs[rrdidx].rrdfn));
			inp += 7;
			outp += strlen(outp);
		}
		else if (strncmp(inp, "@RRDPARAM@", 10) == 0) {
			/* 
			 * We do a colon-escape first, then change all commas to slashes as
			 * this is a common mangling used by multiple backends (disk, http, iostat...)
			 */
			if (rrddbs[rrdidx].rrdparam) {
				char *p;
				sprintf(outp, "%-*s", paramlen, colon_escape(rrddbs[rrdidx].rrdparam));
				p = outp; while ((p = strchr(p, ',')) != NULL) *p = '/';
				outp += strlen(outp);
			}
			inp += 10;
		}
		else if (strncmp(inp, "@RRDIDX@", 8) == 0) {
			char numstr[10];

			sprintf(numstr, "%d", rrdidx);
			strcpy(outp, numstr);
			inp += 8;
			outp += strlen(outp);
		}
		else if (strncmp(inp, "@SERVICE@", 9) == 0) {
			strcpy(outp, service);
			inp += 9;
			outp += strlen(outp);
		}
		else if (strncmp(inp, "@COLOR@", 7) == 0) {
			strcpy(outp, colorlist[coloridx]);
			inp += 7;
			outp += strlen(outp);
			coloridx++; if (colorlist[coloridx] == NULL) coloridx = 0;
		}
		else {
			strcpy(outp, "@");
			inp += 1;
			outp += 1;
		}
	}
	*outp = '\0';

	return result;
}

int rrd_name_compare(const void *v1, const void *v2)
{
	rrddb_t *r1 = (rrddb_t *)v1;
	rrddb_t *r2 = (rrddb_t *)v2;

	return strcmp(r1->key, r2->key);
}

void graph_link(FILE *output, char *uri, char *grtype, time_t seconds)
{
	time_t gstart, gend;

	fprintf(output, "<tr>\n");

	switch (action) {
	  case ACT_MENU:
		fprintf(output, "  <td align=\"left\"><img src=\"%s&amp;action=view&amp;graph=%s\" alt=\"%s graph\"></td>\n",
			uri, grtype, grtype);
		fprintf(output, "  <td align=\"left\" valign=\"top\"> <a href=\"%s&amp;graph=%s&amp;action=selzoom\"> <img src=\"%s/zoom.gif\" border=0 alt=\"Zoom graph\" style='padding: 3px'> </a> </td>\n",
			uri, grtype, getenv("BBSKIN"));
		break;

	  case ACT_SELZOOM:
	  case ACT_SHOWZOOM:
		if (graphend == 0) gend = time(NULL); else gend = graphend;
		if (graphstart == 0) gstart = gend - persecs; else gstart = graphstart;

		fprintf(output, "  <td align=\"left\"><img id='zoomGraphImage' src=\"%s&amp;graph=%s&amp;action=view&amp;graph_start=%u&amp;graph_end=%u&amp;graph_height=120&amp;graph_width=575\" alt=\"Zoom source image\"></td>\n",
			uri, grtype, (int) gstart, (int) gend);
		break;

	  case ACT_VIEW:
		break;
	}

	fprintf(output, "</tr>\n");
}

int main(int argc, char *argv[])
{
	char *rrddir = NULL;
	char *gdeffn = NULL;
	char *graphfn = "-";
	char *envarea = NULL;

	gdef_t *gdef = NULL, *gdefuser = NULL;
	int wantsingle = 0;
	char **rrdargs = NULL;

	DIR *dir;
	char **calcpr  = NULL;
	int argi, pcount, argcount, rrdargcount, xsize, ysize, result;
	double ymin, ymax;
	time_t now;
	char timestamp[100];
	char graphtitle[1024];
	char graphopt[30];

	char okuri[32768];
	char *p;

	/* See what we want to do - i.e. get hostname, service and graph-type */
	parse_query();
	now = time(NULL);

	/* Handle any commandline args */
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

	strcpy(okuri, xgetenv("REQUEST_URI"));
	p = strchr(okuri, '?'); if (p) *p = '\0'; else p = okuri + strlen(okuri);
	p += sprintf(p, "?host=%s&amp;service=%s", hostname, service);
	if (displayname != hostname) p += sprintf(p, "&amp;disp=%s", displayname);
	if (firstidx != -1) p += sprintf(p, "&amp;first=%d", firstidx+1);
	if (idxcount != -1) p += sprintf(p, "&amp;count=%d", idxcount);

	switch (action) {
	  case ACT_MENU:
		fprintf(stdout, "Content-type: text/html\n\n");
		sethostenv(displayname, "", service, colorname(COL_GREEN));
		headfoot(stdout, "graphs", "", "header", COL_GREEN);

		fprintf(stdout, "<table align=\"center\" summary=\"Graphs\">\n");

		graph_link(stdout, okuri, "hourly",      48*60*60);
		graph_link(stdout, okuri, "daily",    12*24*60*60);
		graph_link(stdout, okuri, "weekly",   48*24*60*60);
		graph_link(stdout, okuri, "monthly", 576*24*60*60);

		fprintf(stdout, "</table>\n");

		headfoot(stdout, "graphs", "", "footer", COL_GREEN);
		return 0;

	  case ACT_SELZOOM:
		fprintf(stdout, "Content-type: text/html\n\n");
		sethostenv(displayname, "", service, colorname(COL_GREEN));
		headfoot(stdout, "graphs", "", "header", COL_GREEN);


		fprintf(stdout, "  <div id='zoomBox' style='position:absolute; overflow:none; left:0px; top:0px; width:0px; height:0px; visibility:visible; background:red; filter:alpha(opacity=50); -moz-opacity:0.5; -khtml-opacity:0.5'></div>\n");
		fprintf(stdout, "  <div id='zoomSensitiveZone' style='position:absolute; overflow:none; left:0px; top:0px; width:0px; height:0px; visibility:visible; cursor:crosshair; background:blue; filter:alpha(opacity=0); -moz-opacity:0; -khtml-opacity:0'></div>\n");

		fprintf(stdout, "<table align=\"center\" summary=\"Graphs\">\n");
		graph_link(stdout, okuri, gtype, 0);
		fprintf(stdout, "</table>\n");

		{
			char zoomjsfn[PATH_MAX];
			struct stat st;

			sprintf(zoomjsfn, "%s/web/zoom.js", xgetenv("BBHOME"));
			if (stat(zoomjsfn, &st) == 0) {
				FILE *fd;
				char *buf;
				int n;
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


		headfoot(stdout, "graphs", "", "footer", COL_GREEN);
		return 0;

	  case ACT_SHOWZOOM:
		fprintf(stdout, "Content-type: text/html\n\n");
		sethostenv(displayname, "", service, colorname(COL_GREEN));
		headfoot(stdout, "graphs", "", "header", COL_GREEN);

		fprintf(stdout, "<table align=\"center\" summary=\"Graphs\">\n");

		graph_link(stdout, okuri, gtype, 0);

		fprintf(stdout, "</table>\n");

		headfoot(stdout, "graphs", "", "footer", COL_GREEN);
		return 0;

	  case ACT_VIEW:
		break;
	}

	/* Find the config-file and load it */
	if (gdeffn == NULL) {
		char fnam[PATH_MAX];
		sprintf(fnam, "%s/etc/hobbitgraph.cfg", xgetenv("BBHOME"));
		gdeffn = strdup(fnam);
	}
	load_gdefs(gdeffn);

	/* Determine the directory with the host RRD files, and go there. */
	if (rrddir == NULL) {
		char dnam[PATH_MAX];

		if (xgetenv("BBRRDS")) sprintf(dnam, "%s/%s", xgetenv("BBRRDS"), hostname);
		else sprintf(dnam, "%s/rrd/%s", xgetenv("BBVAR"), hostname);

		rrddir = strdup(dnam);
	}
	if (chdir(rrddir)) errormsg("Cannot access RRD directory");


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
	 * If that fails, then we try mapping it via the BB service -> LARRD map.
	 */
	for (gdef = gdefs; (gdef && strcmp(service, gdef->name)); gdef = gdef->next) ;
	if (gdef == NULL) {
		if (gdefuser) {
			gdef = gdefuser;
		}
		else {
			larrdrrd_t *ldef = find_larrd_rrd(service, NULL);
			if (ldef) {
				for (gdef = gdefs; (gdef && strcmp(ldef->larrdrrdname, gdef->name)); gdef = gdef->next) ;
				wantsingle = 1;
			}
		}
	}
	if (gdef == NULL) errormsg("Unknown graph requested");

	/* What RRD files do we have matching this request? */
	if (gdef->fnpat == NULL) {
		/*
		 * No pattern, just a single file. It doesnt matter if it exists, because
		 * these types of graphs usually have a hard-coded value for the RRD filename
		 * in the graph definition.
		 */
		rrddbcount = rrddbsize = 1;
		rrddbs = (rrddb_t *)malloc((rrddbsize + 1) * sizeof(rrddb_t));

		rrddbs[0].key = strdup(service);
		rrddbs[0].rrdfn = (char *)malloc(strlen(gdef->name) + strlen(".rrd") + 1);
		sprintf(rrddbs[0].rrdfn, "%s.rrd", gdef->name);
		rrddbs[0].rrdparam = NULL;
	}
	else {
		struct dirent *d;
		pcre *pat, *expat = NULL;
		const char *errmsg;
		int errofs, result;
		int ovector[30];

		/* Scan the directory to see what RRD files are there that match */
		dir = opendir("."); if (dir == NULL) errormsg("Unexpected error while accessing RRD directory");

		/* Setup the pattern to match filenames against */
		pat = pcre_compile(gdef->fnpat, PCRE_CASELESS, &errmsg, &errofs, NULL);
		if (!pat) {
			char msg[8192];

			snprintf(msg, sizeof(msg), "hobbitgraph.cfg error, PCRE pattern %s invalid: %s, offset %d\n",
				 gdef->fnpat, errmsg, errofs);
			errormsg(msg);
		}
		if (gdef->exfnpat) {
			expat = pcre_compile(gdef->exfnpat, PCRE_CASELESS, &errmsg, &errofs, NULL);
			if (!expat) {
				char msg[8192];

				snprintf(msg, sizeof(msg), 
					 "hobbitgraph.cfg error, PCRE pattern %s invalid: %s, offset %d\n",
					 gdef->exfnpat, errmsg, errofs);
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
			fgets(graphtitle, sizeof(graphtitle), pfd);
			pclose(pfd);
		}

		/* Drop any newline at end of the title */
		p = strchr(graphtitle, '\n'); if (p) *p = '\0';
	}
	else {
		sprintf(graphtitle, "%s %s %s", displayname, gdef->title, glegend);
	}

	/*
	 * Setup the arguments for calling rrd_graph. 
	 * There's 11 standard arguments, plus the 
	 * graph-specific ones (which may be repeated if
	 * there are multiple RRD-files to handle).
	 */
	for (pcount = 0; (gdef->defs[pcount]); pcount++) ;
	argcount = (15 + pcount*rrddbcount + 1); argi = 0;
	rrdargs = (char **) calloc(argcount+1, sizeof(char *));
	rrdargs[argi++]  = "rrdgraph";
	rrdargs[argi++]  = graphfn;
	rrdargs[argi++]  = "--title";
	rrdargs[argi++]  = graphtitle;
	rrdargs[argi++]  = "-w576";
	rrdargs[argi++]  = "-h120";
	rrdargs[argi++]  = "-v";
	rrdargs[argi++]  = gdef->yaxis;
	rrdargs[argi++]  = "-a";
	rrdargs[argi++] = "PNG";

	if (haveupper) {
		sprintf(graphopt, "-u %f", upperlimit);
		rrdargs[argi++] = strdup(graphopt);
	}
	if (havelower) {
		sprintf(graphopt, "-l %f", lowerlimit);
		rrdargs[argi++] = strdup(graphopt);
	}
	if (haveupper || havelower) rrdargs[argi++] = "--rigid";

	if (graphstart) sprintf(graphopt, "-s %u", (unsigned int) graphstart);
	else sprintf(graphopt, "-s %s", period);
	rrdargs[argi++] = strdup(graphopt);

	if (graphend) {
		sprintf(graphopt, "-e %u", (unsigned int) graphend);
		rrdargs[argi++] = strdup(graphopt);
	}

	for (rrdidx=0; (rrdidx < rrddbcount); rrdidx++) {
		if ((firstidx == -1) || ((rrdidx >= firstidx) && (rrdidx <= lastidx))) {
			int i;
			for (i=0; (gdef->defs[i]); i++) rrdargs[argi++] = strdup(expand_tokens(gdef->defs[i]));
		}
	}

#ifdef RRDTOOL12
	strftime(timestamp, sizeof(timestamp), "COMMENT:Updated\\: %d-%b-%Y %H\\:%M\\:%S", localtime(&now));
#else
	strftime(timestamp, sizeof(timestamp), "COMMENT:Updated: %d-%b-%Y %H:%M:%S", localtime(&now));
#endif
	rrdargs[argi++] = strdup(timestamp);


	rrdargcount = argi; rrdargs[argi++] = NULL;


	if (debug) { for (argi=0; (argi < rrdargcount); argi++) dprintf("%s\n", rrdargs[argi]); }

	/* If sending to stdout, print the HTTP header first. */
	if (strcmp(graphfn, "-") == 0) {
		time_t expiretime = now + 300;
		char expirehdr[100];

		printf("Content-type: image/png\n");
		strftime(expirehdr, sizeof(expirehdr), "Expires: %a, %d %b %Y %H:%M:%S GMT", gmtime(&expiretime));
		printf("%s\n", expirehdr);
		printf("\n");
	}

	/* All set - generate the graph */
	rrd_clear_error();
#ifdef RRDTOOL12
	result = rrd_graph(rrdargcount, rrdargs, &calcpr, &xsize, &ysize, NULL, &ymin, &ymax);
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

	if (displayname != hostname) { xfree(displayname); displayname = NULL; }
	if (hostname) { xfree(hostname); hostname = NULL; }
	if (service) { xfree(service); service = NULL; }
	if (gtype) { xfree(gtype); gtype = NULL; }
	glegend = period = NULL; /* Dont free these - they are static strings in the program. */
	for (argi=0; (argi < rrddbcount); argi++) {
		if (rrddbs[argi].key) xfree(rrddbs[argi].key);
		if (rrddbs[argi].rrdfn) xfree(rrddbs[argi].rrdfn);
		if (rrddbs[argi].rrdparam) xfree(rrddbs[argi].rrdparam);
	}
	xfree(rrddbs);
	rrddbs = NULL;
	coloridx = rrddbcount = rrddbsize = rrdidx = paramlen = 0;

	gdef = gdefuser = NULL;
	wantsingle = 0;

#if 0
	/* Why does this cause the program to crash ? */
	for (argi=11; (argi < argcount); argi++) {
		if (rrdargs[argi]) xfree(rrdargs[argi]);
	}
	xfree(rrdargs);
	rrdargs = NULL;
#endif

	return 0;
}

