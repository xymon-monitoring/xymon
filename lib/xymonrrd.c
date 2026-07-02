/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for working with RRD graphs.                          */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include "libxymon.h"
#include "version.h"
#include "stackio.h"

/* This is for mapping a status-name -> RRD file */
xymonrrd_t *xymonrrds = NULL;
void * xymonrrdtree;

/* This is the information needed to generate links on the trends column page  */
xymongraph_t *xymongraphs = NULL;

static const char *xymonlinkfmt = "<table summary=\"%s Graph\"><tr><td><A HREF=\"%s&amp;action=menu\"><IMG BORDER=0 SRC=\"%s&amp;graph=hourly&amp;action=view\" ALT=\"xymongraph %s\"></A></td><td> <td align=\"left\" valign=\"top\"> <a href=\"%s&amp;graph=custom&amp;action=selzoom\"> <img src=\"%s/zoom.%s\" border=0 alt=\"Zoom graph\" style='padding: 3px'> </a> </td></tr></table>\n";

static const char *metafmt = "<RRDGraph>\n  <GraphType>%s</GraphType>\n  <GraphLink><![CDATA[%s]]></GraphLink>\n  <GraphImage><![CDATA[%s&amp;graph=hourly]]></GraphImage>\n</RRDGraph>\n";


/*
 * Define the mapping between Xymon columns and RRD graphs.
 * Normally they are identical, but some RRD's use different names.
 */
void rrd_setup(void)
{
	static int setup_done = 0;
	SBUF_DEFINE(lenv);
	char *ldef, *p, *services;
	SBUF_DEFINE(tcptests);
	int count;
	xymonrrd_t *lrec;
	xymongraph_t *grec;


	/* Do nothing if we have been called within the past 5 minutes */
	if ((setup_done + 300) >= getcurrenttime(NULL)) return;


	/* 
	 * Must free any old data first.
	 * NB: These lists are NOT null-terminated ! 
	 *     Stop when svcname becomes a NULL.
	 */
	lrec = xymonrrds;
	while (lrec && lrec->svcname) {
		if (lrec->xymonrrdname != lrec->svcname) xfree(lrec->xymonrrdname);
		xfree(lrec->svcname);
		lrec++;
	}
	if (xymonrrds) {
		xfree(xymonrrds);
		xtreeDestroy(xymonrrdtree);
	}

	grec = xymongraphs;
	while (grec && grec->xymonrrdname) {
		if (grec->xymonpartname) xfree(grec->xymonpartname);
		xfree(grec->xymonrrdname);
		grec++;
	}
	if (xymongraphs) xfree(xymongraphs);


	/* Get the tcp services, and count how many there are */
	services = strdup(init_tcp_services());
	SBUF_MALLOC(tcptests, strlen(services)+1);
	strncpy(tcptests, services, tcptests_buflen);
	count = 0; p = strtok(tcptests, " "); while (p) { count++; p = strtok(NULL, " "); }
	strncpy(tcptests, services, tcptests_buflen);

	/* Setup the xymonrrds table, mapping test-names to RRD files */
	SBUF_MALLOC(lenv, strlen(xgetenv("TEST2RRD")) + strlen(tcptests) + count*strlen(",=tcp") + 1);
	strncpy(lenv, xgetenv("TEST2RRD"), lenv_buflen); 
	p = lenv+strlen(lenv)-1; if (*p == ',') *p = '\0';	/* Drop a trailing comma */
	p = strtok(tcptests, " "); 
	while (p) {
		unsigned int curlen = strlen(lenv);
		snprintf(lenv+curlen, (lenv_buflen - curlen), ",%s=tcp", p); 
		p = strtok(NULL, " ");
	}
	xfree(tcptests);
	xfree(services);

	count = 0; p = lenv; do { count++; p = strchr(p+1, ','); } while (p);
	xymonrrds = (xymonrrd_t *)calloc((count+1), sizeof(xymonrrd_t));

	xymonrrdtree = xtreeNew(strcasecmp);
	lrec = xymonrrds; ldef = strtok(lenv, ",");
	while (ldef) {
		p = strchr(ldef, '=');
		if (p) {
			*p = '\0'; 
			lrec->svcname = strdup(ldef);
			lrec->xymonrrdname = strdup(p+1);
		}
		else {
			lrec->svcname = lrec->xymonrrdname = strdup(ldef);
		}
		xtreeAdd(xymonrrdtree, lrec->svcname, lrec);

		ldef = strtok(NULL, ",");
		lrec++;
	}
	xfree(lenv);

	/* Setup the xymongraphs table, describing how to make graphs from an RRD */
	lenv = strdup(xgetenv("GRAPHS"));
	p = lenv+strlen(lenv)-1; if (*p == ',') *p = '\0';	/* Drop a trailing comma */
	count = 0; p = lenv; do { count++; p = strchr(p+1, ','); } while (p);
	xymongraphs = (xymongraph_t *)calloc((count+1), sizeof(xymongraph_t));

	grec = xymongraphs; ldef = strtok(lenv, ",");
	while (ldef) {
		p = strchr(ldef, ':');
		if (p) {
			*p = '\0'; 
			grec->xymonrrdname = strdup(ldef);
			grec->xymonpartname = strdup(p+1);
			p = strchr(grec->xymonpartname, ':');
			if (p) {
				*p = '\0';
				grec->maxgraphs = atoi(p+1);
				if (strlen(grec->xymonpartname) == 0) {
					xfree(grec->xymonpartname);
					grec->xymonpartname = NULL;
				}
			}
		}
		else {
			grec->xymonrrdname = strdup(ldef);
		}

		ldef = strtok(NULL, ",");
		grec++;
	}
	xfree(lenv);

	setup_done = getcurrenttime(NULL);
}


xymonrrd_t *find_xymon_rrd(char *service, char *flags)
{
	/* Lookup an entry in the xymonrrds table */
	xtreePos_t handle;

	rrd_setup();

	if (flags && (strchr(flags, 'R') != NULL)) {
		/* Don't do RRD's for reverse tests, since they have no data */
		return NULL;
	}

	handle = xtreeFind(xymonrrdtree, service);
	if (handle == xtreeEnd(xymonrrdtree)) 
		return NULL;
	else {
		return (xymonrrd_t *)xtreeData(xymonrrdtree, handle);
	}
}

xymongraph_t *find_xymon_graph(char *rrdname)
{
	/* Lookup an entry in the xymongraphs table */
	xymongraph_t *grec;
	int found = 0;
	char *dchar;

	rrd_setup();
	grec = xymongraphs; 
	while (!found && (grec->xymonrrdname != NULL)) {
		found = (strncmp(grec->xymonrrdname, rrdname, strlen(grec->xymonrrdname)) == 0);
		if (found) {
			/* Check that it's not a partial match, e.g. "ftp" matches "ftps" */
			dchar = rrdname + strlen(grec->xymonrrdname);
			if ( (*dchar != '.') && (*dchar != ',') && (*dchar != '\0') ) found = 0;
		}

		if (!found) grec++;
	}

	return (found ? grec : NULL);
}


static char *xymon_graph_text(char *hostname, char *dispname, char *service, int bgcolor,
			      xymongraph_t *graphdef, int itemcount, hg_stale_rrds_t nostale, const char *fmt,
			      int locatorbased, time_t starttime, time_t endtime)
{
	STATIC_SBUF_DEFINE(rrdurl);
	static int gwidth = 0, gheight = 0;
	SBUF_DEFINE(svcurl);
	int rrdparturlsize;
	char rrdservicename[100];
	char *cgiurl = xgetenv("CGIBINURL");

	MEMDEFINE(rrdservicename);

	if (locatorbased) {
		char *qres = locator_query(hostname, ST_RRD, &cgiurl);
		if (!qres) {
			errprintf("Cannot find RRD files for host %s\n", hostname);
			return "";
		}
	}

	if (!gwidth) {
		gwidth = atoi(xgetenv("RRDWIDTH"));
		gheight = atoi(xgetenv("RRDHEIGHT"));
	}

	dbgprintf("rrdlink_url: host %s, rrd %s (partname:%s, maxgraphs:%d, count=%d)\n", 
		hostname, 
		graphdef->xymonrrdname, textornull(graphdef->xymonpartname), graphdef->maxgraphs, itemcount);

	if ((service != NULL) && (strcmp(graphdef->xymonrrdname, "tcp") == 0)) {
		snprintf(rrdservicename, sizeof(rrdservicename), "tcp:%s", service);
	}
	else if ((service != NULL) && (strcmp(graphdef->xymonrrdname, "ncv") == 0)) {
		snprintf(rrdservicename, sizeof(rrdservicename), "ncv:%s", service);
	}
	else if ((service != NULL) && (strcmp(graphdef->xymonrrdname, "devmon") == 0)) {
		snprintf(rrdservicename, sizeof(rrdservicename), "devmon:%s", service);
	}
	else {
		strncpy(rrdservicename, graphdef->xymonrrdname, sizeof(rrdservicename));
	}

	SBUF_MALLOC(svcurl, 
		    2048                    + 
		    strlen(cgiurl)          +
		    strlen(hostname)        + 
		    strlen(rrdservicename)  + 
		    strlen(urlencode(dispname ? dispname : hostname)));

	rrdparturlsize = 2048 +
			 strlen(fmt)        +
			 3*svcurl_buflen    +
			 strlen(rrdservicename) +
			 strlen(xgetenv("XYMONSKIN"));

	if (rrdurl == NULL) {
		SBUF_MALLOC(rrdurl, rrdparturlsize);
	}
	*rrdurl = '\0';

	{
		SBUF_DEFINE(rrdparturl);
		int first = 1;
		int step;

		step = (graphdef->maxgraphs ? graphdef->maxgraphs : 5);
		if (itemcount) {
			int gcount = (itemcount / step); if ((gcount*step) != itemcount) gcount++;
			step = (itemcount / gcount);
		}

		SBUF_MALLOC(rrdparturl, rrdparturlsize);
		do {
			if (itemcount > 0) {
				snprintf(svcurl, svcurl_buflen, 
					"%s/showgraph.sh?host=%s&amp;service=%s&amp;graph_width=%d&amp;graph_height=%d&amp;first=%d&amp;count=%d", 
					cgiurl, hostname, rrdservicename, 
					gwidth, gheight,
					first, step);
			}
			else {
				snprintf(svcurl, svcurl_buflen,
					"%s/showgraph.sh?host=%s&amp;service=%s&amp;graph_width=%d&amp;graph_height=%d", 
					cgiurl, hostname, rrdservicename,
					gwidth, gheight);
			}

			strncat(svcurl, "&amp;disp=", (svcurl_buflen - strlen(svcurl)));
			strncat(svcurl, urlencode(dispname ? dispname : hostname), (svcurl_buflen - strlen(svcurl)));

			if (nostale == HG_WITHOUT_STALE_RRDS) strncat(svcurl, "&amp;nostale", (svcurl_buflen - strlen(svcurl)));
			if (bgcolor != -1) snprintf(svcurl+strlen(svcurl), (svcurl_buflen - strlen(svcurl)), "&amp;color=%s", colorname(bgcolor));
			snprintf(svcurl+strlen(svcurl), (svcurl_buflen - strlen(svcurl)), "&amp;graph_start=%d&amp;graph_end=%d", (int)starttime, (int)endtime);

			snprintf(rrdparturl, rrdparturl_buflen, fmt, rrdservicename, svcurl, svcurl, rrdservicename, svcurl, xgetenv("XYMONSKIN"), xgetenv("IMAGEFILETYPE"));
			if ((strlen(rrdparturl) + strlen(rrdurl) + 1) >= rrdurl_buflen) {
				SBUF_REALLOC(rrdurl, rrdurl_buflen + strlen(rrdparturl) + 4096);
			}
			strncat(rrdurl, rrdparturl, (rrdurl_buflen - strlen(rrdurl)));
			first += step;
		} while (first <= itemcount);
		xfree(rrdparturl);
	}

	dbgprintf("URLtext: %s\n", rrdurl);

	xfree(svcurl);

	MEMUNDEFINE(rrdservicename);

	return rrdurl;
}

char *xymon_graph_data(char *hostname, char *dispname, char *service, int bgcolor,
			xymongraph_t *graphdef, int itemcount,
			hg_stale_rrds_t nostale, hg_link_t wantmeta, int locatorbased,
			time_t starttime, time_t endtime)
{
	return xymon_graph_text(hostname, dispname, 
				 service, bgcolor, graphdef, 
				 itemcount, nostale,
				 ((wantmeta == HG_META_LINK) ? metafmt : xymonlinkfmt),
				 locatorbased, starttime, endtime);
}


rrdtpldata_t *setup_template(char *params[])
{
	int i;
	rrdtpldata_t *result;
	rrdtplnames_t *nam;
	int dsindex = 1;

	result = (rrdtpldata_t *)calloc(1, sizeof(rrdtpldata_t));
	result->dsnames = xtreeNew(strcmp);

	for (i = 0; (params[i]); i++) {
		if (strncasecmp(params[i], "DS:", 3) == 0) {
			char *pname, *pend;

			pname = params[i] + 3;
			pend = strchr(pname, ':');
			if (pend) {
				int plen = (pend - pname);

				nam = (rrdtplnames_t *)calloc(1, sizeof(rrdtplnames_t));
				nam->idx = dsindex++;

				if (result->template == NULL) {
					result->template = (char *)malloc(plen + 1);
					*result->template = '\0';
					nam->dsnam = (char *)malloc(plen+1); strncpy(nam->dsnam, pname, plen); nam->dsnam[plen] = '\0';
				}
				else {
					/* Hackish way of getting the colon delimiter */
					pname--; plen++;
					result->template = (char *)realloc(result->template, strlen(result->template) + plen + 1);
					nam->dsnam = (char *)malloc(plen); strncpy(nam->dsnam, pname+1, plen-1); nam->dsnam[plen-1] = '\0';
				}
				strncat(result->template, pname, plen);

				xtreeAdd(result->dsnames, nam->dsnam, nam);
			}
		}
	}

	return result;
}

/*
 * Cached graphs.cfg section patterns. graphs.cfg is parsed ONCE per process
 * (it does not change during a CGI run) instead of once per graph. The trends
 * page counts dozens of graphs per host, so reparsing the file - and re-walking
 * the RRD directory - on every iteration was the bulk of the cost.
 */
typedef struct rrdgraphsection_t {
	char *section;
	char *fnpattern;
	char *exfnpattern;
	char *defrrd;
	struct rrdgraphsection_t *next;
} rrdgraphsection_t;
static rrdgraphsection_t *rrdgraphsections = NULL;
static rrdgraphsection_t *rrdgraphsections_tail = NULL;
static int rrdgraphsections_loaded = 0;

static void load_graphs_cfg(void)
{
	char graphcfgfn[PATH_MAX];
	FILE *graphcfg;
	rrdgraphsection_t *cursect = NULL;
	strbuffer_t *linebuf;

	if (rrdgraphsections_loaded) return;
	rrdgraphsections_loaded = 1;

	snprintf(graphcfgfn, sizeof(graphcfgfn), "%s/etc/graphs.cfg", xgetenv("XYMONHOME"));
	graphcfg = stackfopen(graphcfgfn, "r", NULL);	/* stackfgets() follows "include" directives */
	if (!graphcfg) {
		errprintf("Cannot open graphs.cfg at '%s': %s\n", graphcfgfn, strerror(errno));
		return;
	}

	linebuf = newstrbuffer(0);
	while (stackfgets(linebuf, NULL) != NULL) {
		char *line = STRBUF(linebuf);
		char *p = line + strcspn(line, "\r\n"); *p = '\0';	/* strip CR/LF on the dynamic buffer */

		if (line[0] == '[') {
			char *delim = strchr(line, ']');
			if (delim) {
				rrdgraphsection_t *ns = (rrdgraphsection_t *)calloc(1, sizeof(rrdgraphsection_t));
				*delim = '\0';
				ns->section = strdup(line+1);
				/* Append in file order so a section lookup returns the first occurrence */
				if (rrdgraphsections_tail) rrdgraphsections_tail->next = ns;
				else                       rrdgraphsections = ns;
				rrdgraphsections_tail = ns;
				cursect = ns;
			}
		}
		else if (cursect) {
			char *line_start = line + strspn(line, " \t");
			if (*line_start == '#') continue;	/* skip comments */
			if (strncasecmp(line_start, "EXFNPATTERN", 11) == 0) {
				char *pval = line_start + 11; pval += strspn(pval, " \t=");
				if (!cursect->exfnpattern) cursect->exfnpattern = strdup(pval);
			}
			else if (strncasecmp(line_start, "FNPATTERN", 9) == 0) {
				char *pval = line_start + 9; pval += strspn(pval, " \t=");
				if (!cursect->fnpattern) cursect->fnpattern = strdup(pval);
			}
			else if (strncasecmp(line_start, "DEF:", 4) == 0) {
				/*
				 * No-FNPATTERN sections (e.g. [vmstat1]) draw from a hard-coded
				 * RRD file named in the first DEF line - "DEF:var=file.rrd:ds:CF".
				 * Capture that filename so the section name (vmstat1) is not
				 * mistaken for the RRD file (vmstat.rrd).
				 */
				char *eq = strchr(line_start, '=');
				char *colon = eq ? strchr(eq + 1, ':') : NULL;
				if (eq && colon && !cursect->defrrd) {
					size_t len = colon - (eq + 1);
					cursect->defrrd = (char *)malloc(len + 1);
					memcpy(cursect->defrrd, eq + 1, len); cursect->defrrd[len] = '\0';
				}
			}
		}
	}
	freestrbuffer(linebuf);
	stackfclose(graphcfg);
}

static rrdgraphsection_t *find_graph_section(const char *name)
{
	rrdgraphsection_t *s, *hit = NULL;
	if (!name) return NULL;
	/*
	 * Return the LAST matching section. showgraph.c's load_gdefs() prepends as
	 * it reads, so its lookup finds the last-defined section - i.e. a later
	 * "include" that redefines a stock section wins. Match that here so the
	 * counted definition is the same one that gets rendered.
	 */
	for (s = rrdgraphsections; s; s = s->next)
		if (strcmp(s->section, name) == 0) hit = s;
	return hit;
}

/*
 * Cached listing of a host's RRD directory (.rrd files only). The trends page
 * counts every graph for one host, so the directory is read ONCE and reused;
 * a different hostname rebuilds the cache.
 */
static char *rrddir_host = NULL;
static char **rrddir_files = NULL;
static int rrddir_count = 0;
static int rrddir_alloc = 0;

static void load_rrd_dir(const char *hostname, const char *rrdpath)
{
	DIR *dir;
	struct dirent *entry;
	int i;

	if (rrddir_host && (strcmp(rrddir_host, hostname) == 0)) return;	/* already cached */

	for (i = 0; i < rrddir_count; i++) xfree(rrddir_files[i]);
	rrddir_count = 0;
	if (rrddir_host) xfree(rrddir_host);
	rrddir_host = strdup(hostname);

	/* Absolute path to opendir() so the CGI's working directory is never changed. */
	dir = opendir(rrdpath);
	if (!dir) {
		errprintf("Cannot open RRD directory '%s' for host '%s': %s\n",
			  rrdpath, hostname, strerror(errno));
		return;
	}
	while ((entry = readdir(dir)) != NULL) {
		size_t n = strlen(entry->d_name);
		if ((n > 4) && (strcmp(entry->d_name + n - 4, ".rrd") == 0)) {
			if (rrddir_count >= rrddir_alloc) {
				rrddir_alloc = rrddir_alloc ? rrddir_alloc * 2 : 64;
				rrddir_files = (char **)realloc(rrddir_files, rrddir_alloc * sizeof(char *));
			}
			rrddir_files[rrddir_count++] = strdup(entry->d_name);
		}
	}
	closedir(dir);
}

int count_rrd_files_for_graph(char *hostname, char *graphname)
{
	char rrdpath[PATH_MAX];
	rrdgraphsection_t *sect;
	char *fnpattern, *exfnpattern, *defrrd;
	int rrd_count = 0;
	int i;

	snprintf(rrdpath, sizeof(rrdpath), "%s/%s", xgetenv("XYMONRRDS"), hostname);

	/*
	 * This runs in the CGI when rendering status/trends pages, NOT in
	 * xymond_filestore during status updates - the RRD scanning cost lands on
	 * page views, not on every status message. Both the graphs.cfg parse and the
	 * directory scan are cached, so a trends page pays each cost once, not per graph.
	 */
	load_graphs_cfg();
	load_rrd_dir(hostname, rrdpath);

	sect = find_graph_section(graphname);

	fnpattern   = sect ? sect->fnpattern   : NULL;
	exfnpattern = sect ? sect->exfnpattern : NULL;
	defrrd      = sect ? sect->defrrd      : NULL;

	if (fnpattern) {
		/* FNPATTERN graphs (e.g. disk): count cached RRD names matching the pattern,
		   honouring EXFNPATTERN exclusions. */
		pcre2_code *pat = compileregex(fnpattern);		/* PCRE2, PCRE2_CASELESS */
		pcre2_code *expat = exfnpattern ? compileregex(exfnpattern) : NULL;

		if (!pat) {
			errprintf("Bad FNPATTERN '%s' (graph=%s host=%s)\n", fnpattern, graphname, hostname);
		}
		else {
			for (i = 0; i < rrddir_count; i++) {
				if (matchregex(rrddir_files[i], pat) &&
				    !(expat && matchregex(rrddir_files[i], expat))) {
					rrd_count++;
				}
			}
		}
		freeregex(pat);		/* freeregex() is NULL-safe */
		freeregex(expat);
	}
	else {
		/*
		 * No FNPATTERN: single RRD file. Prefer the filename from the section's
		 * DEF line (so [vmstat1] -> vmstat.rrd), falling back to "<graphname>.rrd"
		 * for the common 1:1 case (e.g. la -> la.rrd).
		 */
		char expected_rrd[PATH_MAX];

		if (defrrd) snprintf(expected_rrd, sizeof(expected_rrd), "%s", defrrd);
		else        snprintf(expected_rrd, sizeof(expected_rrd), "%s.rrd", graphname);
		for (i = 0; i < rrddir_count; i++) {
			if (strcmp(rrddir_files[i], expected_rrd) == 0) { rrd_count = 1; break; }
		}
	}

	return rrd_count;
}

