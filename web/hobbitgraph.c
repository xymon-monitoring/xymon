/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a CGI script for generating graphs from the data stored in the     */
/* RRD databases.                                                             */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitgraph.c,v 1.7 2004-12-26 22:43:49 henrik Exp $";

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
char *gtype = NULL;
char *glegend = NULL;

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

	if (getenv("QUERY_STRING") == NULL) {
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
		char *val;
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
		else if (strcmp(val, "hourly") == 0) {
			period = HOUR_GRAPH;
			gtype = strdup(val);
			glegend = "Last 48 Hours";
		}
		else if (strcmp(val, "daily") == 0) {
			period = DAY_GRAPH;
			gtype = strdup(val);
			glegend = "Last 12 Days";
		}
		else if (strcmp(val, "weekly") == 0) {
			period = WEEK_GRAPH;
			gtype = strdup(val);
			glegend = "Last 48 Days";
		}
		else if (strcmp(val, "monthly") == 0) {
			period = MONTH_GRAPH;
			gtype = strdup(val);
			glegend = "Last 576 Days";
		}

		token = strtok(NULL, "&");
	}

	if ((hostname == NULL) || (service == NULL)) errormsg("Invalid request - no host or service");
	if (displayname == NULL) displayname = hostname;
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
	int count;
	char *p, *inp, *outp;

	p = buf; while ((p = strchr(p, ':')) != NULL) { count++; p++; }
	if (count == 0) return buf;

	if (result) free(result);
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
		else if ((strncmp(inp, "@RRDPARAM@", 10) == 0) && rrddbs[rrdidx].rrdparam) {
			/* 
			 * We do a colon-escape first, then change all commas to slashes as
			 * this is a common mangling used by multiple backends (disk, http, iostat...)
			 */
			char *p;
			sprintf(outp, "%-*s", paramlen, colon_escape(rrddbs[rrdidx].rrdparam));
			p = outp; while ((p = strchr(p, ',')) != NULL) *p = '/';
			inp += 10;
			outp += strlen(outp);
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

int main(int argc, char *argv[])
{
	char *rrddir = NULL;
	char *gdeffn = NULL;
	char *graphfn = "-";

	gdef_t *gdef = NULL, *gdefuser = NULL;
	int wantsingle = 0;
	char **rrdargs = NULL;

	DIR *dir;
	char **calcpr  = NULL;
	int argi, pcount, argcount, xsize, ysize, result;
	time_t now;
	char timestamp[100];
	char graphtitle[1024];

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
			loadenv(p+1);
		}
		else if (argnmatch(argv[argi], "--rrddir=")) {
			char *p = strchr(argv[argi], '=');
			rrddir = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--config=")) {
			char *p = strchr(argv[argi], '=');
			gdeffn = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--save") == 0) {
			char fnam[PATH_MAX];

			sprintf(fnam, "%s_%s_%s.png", hostname, service, gtype);
			graphfn = strdup(fnam);
		}
	}

	/*
	 * If no period requested, generate a response that builds a webpage
	 * with links to all of the period graphs.
	 */
	if (period == NULL) {
		fprintf(stdout, "Content-type: text/html\n\n");
		sethostenv(displayname, "", service, colorname(COL_GREEN));
		headfoot(stdout, "graphs", "", "header", COL_GREEN);

		fprintf(stdout, "<table align=\"center\">\n");
		fprintf(stdout, "<tr><th><br>Hourly</th></tr>\n");
		fprintf(stdout, "<tr><td><img src=\"%s&graph=hourly\" alt=\"%s Hourly\"></td></tr>\n",
			getenv("REQUEST_URI"), service);
		fprintf(stdout, "<tr><th><br>Daily</th></tr>\n");
		fprintf(stdout, "<tr><td><img src=\"%s&graph=daily\" alt=\"%s Daily\"></td></tr>\n",
			getenv("REQUEST_URI"), service);
		fprintf(stdout, "<tr><th><br>Weekly</th></tr>\n");
		fprintf(stdout, "<tr><td><img src=\"%s&graph=weekly\" alt=\"%s Weekly\"></td></tr>\n",
			getenv("REQUEST_URI"), service);
		fprintf(stdout, "<tr><th><br>Monthly</th></tr>\n");
		fprintf(stdout, "<tr><td><img src=\"%s&graph=monthly\" alt=\"%s Monthly\"></td></tr>\n",
			getenv("REQUEST_URI"), service);
		fprintf(stdout, "</table>\n");

		headfoot(stdout, "graphs", "", "footer", COL_GREEN);
		return 0;
	}

	/* Find the config-file and load it */
	if (gdeffn == NULL) {
		char fnam[PATH_MAX];
		sprintf(fnam, "%s/etc/bb-larrdgraph.cfg", getenv("BBHOME"));
		gdeffn = strdup(fnam);
	}
	load_gdefs(gdeffn);

	/* Dont use local names for week-days etc - the RRD fonts dont support it. */
	putenv("LC_TIME=C");

	/* Determine the directory with the host RRD files, and go there. */
	if (rrddir == NULL) {
		char dnam[PATH_MAX];

		if (getenv("BBRRDS")) sprintf(dnam, "%s/%s", getenv("BBRRDS"), hostname);
		else sprintf(dnam, "%s/rrd/%s", getenv("BBVAR"), hostname);

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

		free(realservice);
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
		if (gdef->exfnpat) expat = pcre_compile(gdef->exfnpat, PCRE_CASELESS, &errmsg, &errofs, NULL);

		/* Allocate an initial filename table */
		rrddbsize = 5;
		rrddbs = (rrddb_t *) malloc((rrddbsize+1) * sizeof(rrddb_t));

		while ((d = readdir(dir)) != NULL) {
			char *ext;
			char param[1024];

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
				if ((strcmp(gdef->name, "disk") == 0) && (strcmp(param, ",root") == 0)) {
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
	sprintf(graphtitle, "%s %s %s", displayname, gdef->title, glegend);

	/*
	 * Setup the arguments for calling rrd_graph. 
	 * There's 11 standard arguments, plus the 
	 * graph-specific ones (which may be repeated if
	 * there are multiple RRD-files to handle).
	 */
	for (pcount = 0; (gdef->defs[pcount]); pcount++) ;
	argcount = (11 + pcount*rrddbcount + 1); argi = 0;
	rrdargs = (char **) calloc(argcount+1, sizeof(char *));
	rrdargs[argi++]  = "rrdgraph";
	rrdargs[argi++]  = graphfn;
	rrdargs[argi++]  = "-s";
	rrdargs[argi++]  = period;
	rrdargs[argi++]  = "--title";
	rrdargs[argi++]  = graphtitle;
	rrdargs[argi++]  = "-w576";
	rrdargs[argi++]  = "-v";
	rrdargs[argi++]  = gdef->yaxis;
	rrdargs[argi++]  = "-a";
	rrdargs[argi++] = "PNG";
	for (rrdidx=0; (rrdidx < rrddbcount); rrdidx++) {
		int i;
		for (i=0; (gdef->defs[i]); i++) rrdargs[argi++] = strdup(expand_tokens(gdef->defs[i]));
	}
	strftime(timestamp, sizeof(timestamp), "COMMENT:Updated: %d-%b-%Y %H:%M:%S", localtime(&now));
	rrdargs[argi++] = strdup(timestamp);
	rrdargs[argi++] = NULL;

	if (debug) { for (argi=0; (argi < argcount); argi++) dprintf("%s\n", rrdargs[argi]); }

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
	result = rrd_graph(argcount, rrdargs, &calcpr, &xsize, &ysize);

	/* Was it OK ? */
	if (rrd_test_error() || (result != 0)) {
		if (calcpr) { 
			int i;
			for (i=0; (calcpr[i]); i++) free(calcpr[i]);
			calcpr = NULL;
		}

		errormsg(rrd_get_error());
	}

	if (displayname != hostname) { free(displayname); displayname = NULL; }
	if (hostname) { free(hostname); hostname = NULL; }
	if (service) { free(service); service = NULL; }
	if (gtype) { free(gtype); gtype = NULL; }
	glegend = period = NULL; /* Dont free these - they are static strings in the program. */
	for (argi=0; (argi < rrddbcount); argi++) {
		if (rrddbs[argi].key) free(rrddbs[argi].key);
		if (rrddbs[argi].rrdfn) free(rrddbs[argi].rrdfn);
		if (rrddbs[argi].rrdparam) free(rrddbs[argi].rrdparam);
	}
	free(rrddbs);
	rrddbs = NULL;
	coloridx = rrddbcount = rrddbsize = rrdidx = paramlen = 0;

	gdef = gdefuser = NULL;
	wantsingle = 0;
#if 0
	/* Why does this cause the program to crash ? */
	for (argi=11; (argi < argcount); argi++) {
		if (rrdargs[argi]) free(rrdargs[argi]);
	}
	free(rrdargs);
#endif
	rrdargs = NULL;

	return 0;
}

