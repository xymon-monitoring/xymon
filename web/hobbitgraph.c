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

static char rcsid[] = "$Id: hobbitgraph.c,v 1.2 2004-12-25 23:57:02 henrik Exp $";

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
char *period = HOUR_GRAPH;	/* The hourly graph is the default one */
char *gtype = "hourly";
char *glegend = "Last 48 Hours";

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
	char *title;
	char *yaxis;
	char **defs;
	struct gdef_t *next;
} gdef_t;
gdef_t *gdefs = NULL;

char **rrdfns = NULL;
char **rrdparams = NULL;
int rrdfncount = 0;
int rrdfnsize = 0;
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

	fd = fopen(fn, "r");
	if (fd == NULL) errormsg("Cannot load graph definitions");
	while (fgets(l, sizeof(l), fd)) {
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

	fclose(fd);
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
			strcpy(outp, rrdfns[rrdidx]);
			inp += 7;
			outp += strlen(outp);
		}
		else if ((strncmp(inp, "@RRDPARAM@", 10) == 0) && rrdparams[rrdidx]) {
			sprintf(outp, "%-*s", paramlen, rrdparams[rrdidx]);
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

int main(int argc, char *argv[])
{
	char *rrddir = NULL;
	char *gdeffn = NULL;
	char *graphfn = "-";
	char *graphtitle = NULL;
	char timestamp[100];

	larrdrrd_t *ldef = NULL;
	gdef_t *gdef = NULL;

	DIR *dir;

	char **rrdargs = NULL;
	char **calcpr  = NULL;
	int argi, pcount, argcount, xsize, ysize, result;
	time_t now = time(NULL);

	/* See what we want to do - i.e. get hostname, service and graph-type */
	parse_query();

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

	/* Lookup which RRD file corresponds to the service-name, and how we handle this graph */
	ldef = find_larrd_rrd(service, NULL);
	for (gdef = gdefs; (gdef && strcmp(ldef->larrdrrdname, gdef->name)); gdef = gdef->next) ;
	if ((ldef == NULL) || (gdef == NULL)) errormsg("Unknown LARRD definition");

	/* What RRD files do we have matching this request? */
	if (gdef->fnpat == NULL) {
		rrdfncount = 1;
		rrdfns = (char **)malloc((rrdfncount + 1) * sizeof(char *));
		rrdfns[0] = (char *)malloc(strlen(gdef->name) + strlen(".rrd") + 1);
		sprintf(rrdfns[0], "%s.rrd", gdef->name);
	}
	else {
		struct dirent *d;
		pcre *exp;
		const char *errmsg;
		int errofs, result;
		int ovector[30];

		/* Scan the directory to see what RRD files are there that match */
		dir = opendir("."); if (dir == NULL) errormsg("Unexpected error while accessing RRD directory");

		/* Setup the pattern to match filenames against */
		exp = pcre_compile(gdef->fnpat, PCRE_CASELESS, &errmsg, &errofs, NULL);

		/* Allocate an initial filename table */
		rrdfnsize = 5;
		rrdfns = (char **) malloc((rrdfnsize+1) * sizeof(char *));
		rrdparams = (char **) malloc((rrdfnsize+1) * sizeof(char *));

		while ((d = readdir(dir)) != NULL) {
			char *ext;
			char param[1024];

			if (*(d->d_name) == '.') continue;

			ext = d->d_name + strlen(d->d_name) - strlen(".rrd");
			if ((ext <= d->d_name) || (strcmp(ext, ".rrd") != 0)) continue;
			result = pcre_exec(exp, NULL, d->d_name, strlen(d->d_name), 0, 0, 
					   ovector, (sizeof(ovector)/sizeof(int)));

			if (result >= 0) {
				rrdfns[rrdfncount] = strdup(d->d_name);
				if (pcre_copy_substring(d->d_name, ovector, result, 1, param, sizeof(param)) > 0) {
					rrdparams[rrdfncount] = strdup(param);
					if (strlen(param) > paramlen) paramlen = strlen(param);
				}
				else {
					rrdparams[rrdfncount] = NULL;
				}

				rrdfncount++;
				if (rrdfncount == rrdfnsize) {
					rrdfnsize += 5;
					rrdfns = (char **)realloc(rrdfns, (rrdfnsize+1) * sizeof(char *));
					rrdparams = (char **)realloc(rrdparams, (rrdfnsize+1) * sizeof(char *));
				}
			}
		}
		pcre_free(exp);
		closedir(dir);
	}
	rrdfns[rrdfncount] = rrdparams[rrdfncount] = NULL;

	/* Setup the title */
	if (graphtitle == NULL) {
		char title[1024];

		sprintf(title, "%s %s %s", displayname, gdef->title, glegend);
		graphtitle = strdup(title);
	}

	/*
	 * Setup the arguments for calling rrd_graph. 
	 * There's 11 standard arguments, plus the 
	 * graph-specific ones (which may be repeated if
	 * there are multiple RRD-files to handle).
	 */
	for (pcount = 0; (gdef->defs[pcount]); pcount++) ;
	argcount = (11 + pcount*rrdfncount + 1); argi = 0;
	rrdargs = (char **) calloc(argcount, sizeof(char *));
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
	for (rrdidx=0; (rrdidx < rrdfncount); rrdidx++) {
		int i;

		for (i=0; (gdef->defs[i]); i++) rrdargs[argi++] = strdup(expand_tokens(gdef->defs[i]));
	}
	strftime(timestamp, sizeof(timestamp), "COMMENT:Updated: %d-%b-%Y %H:%M:%S", localtime(&now));
	rrdargs[argi++] = strdup(timestamp);
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
		}

		errormsg(rrd_get_error());
		return 1;
	}

	return 0;
}

