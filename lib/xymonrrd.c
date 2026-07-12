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
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "libxymon.h"
#include "rrdfilter.h"
#include "version.h"

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
static void rrd_setup(void)
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

typedef struct graphcountdef_t {
	char *name;
	char *fnpat;
	char *exfnpat;
	struct graphcountdef_t *next;
} graphcountdef_t;

static graphcountdef_t *graphcountdefs = NULL;
static time_t graphcountsetup = 0;

static void free_graph_count_defs(void)
{
	graphcountdef_t *walk, *next;

	for (walk = graphcountdefs; (walk); walk = next) {
		next = walk->next;
		if (walk->name) xfree(walk->name);
		if (walk->fnpat) xfree(walk->fnpat);
		if (walk->exfnpat) xfree(walk->exfnpat);
		xfree(walk);
	}
	graphcountdefs = NULL;
}

static void load_graph_count_defs(void)
{
	char fn[PATH_MAX];
	FILE *fd;
	strbuffer_t *inbuf;
	char *p;
	graphcountdef_t *newitem = NULL;

	if ((graphcountsetup + 300) >= getcurrenttime(NULL)) return;
	free_graph_count_defs();
	graphcountsetup = getcurrenttime(NULL);

	snprintf(fn, sizeof(fn), "%s/etc/graphs.cfg", xgetenv("XYMONHOME"));
	fd = stackfopen(fn, "r", NULL);
	if (fd == NULL) return;

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		p = strchr(STRBUF(inbuf), '\n'); if (p) *p = '\0';
		p = STRBUF(inbuf); p += strspn(p, " \t");
		if ((strlen(p) == 0) || (*p == '#')) continue;

		if (*p == '[') {
			char *delim;

			newitem = (graphcountdef_t *)calloc(1, sizeof(graphcountdef_t));
			delim = strchr(p, ']'); if (delim) *delim = '\0';
			newitem->name = strdup(p+1);
			newitem->next = graphcountdefs;
			graphcountdefs = newitem;
		}
		else if (newitem && (strncasecmp(p, "FNPATTERN", 9) == 0)) {
			p += 9; p += strspn(p, " \t");
			if (newitem->fnpat) xfree(newitem->fnpat);
			newitem->fnpat = strdup(p);
		}
		else if (newitem && (strncasecmp(p, "EXFNPATTERN", 11) == 0)) {
			p += 11; p += strspn(p, " \t");
			if (newitem->exfnpat) xfree(newitem->exfnpat);
			newitem->exfnpat = strdup(p);
		}
	}
	stackfclose(fd);
	freestrbuffer(inbuf);
}

static graphcountdef_t *find_graph_count_def(char *name)
{
	graphcountdef_t *walk;

	load_graph_count_defs();
	for (walk = graphcountdefs; (walk && strcmp(walk->name, name)); walk = walk->next) ;
	return walk;
}

static int rrd_param_matches_service(const char *param, const char *svc)
{
	if ((param == NULL) || (svc == NULL) || (*svc == '\0')) return 0;
	return (strcmp(param, svc) == 0);
}

static void rrd_graph_service_name(char *rrdservicename, size_t rrdservicenamesz,
				   char *service, xymongraph_t *graphdef)
{
	if ((service != NULL) && (strcmp(graphdef->xymonrrdname, "tcp") == 0)) {
		snprintf(rrdservicename, rrdservicenamesz, "tcp:%s", service);
	}
	else if ((service != NULL) && (strcmp(graphdef->xymonrrdname, "ncv") == 0)) {
		snprintf(rrdservicename, rrdservicenamesz, "ncv:%s", service);
	}
	else if ((service != NULL) && (strcmp(graphdef->xymonrrdname, "devmon") == 0)) {
		snprintf(rrdservicename, rrdservicenamesz, "devmon:%s", service);
	}
	else {
		snprintf(rrdservicename, rrdservicenamesz, "%s", graphdef->xymonrrdname);
	}
}

/*
 * Return the number of RRD files showgraph.cgi would select for this graph
 * link, after graph FNPATTERN/EXFNPATTERN, stale-RRD suppression, and the
 * generic RRDEXCLUDE/RRDINCLUDE filter. -1 means "cannot know here"; callers
 * should keep their legacy estimate.
 */
int xymon_graph_count(char *hostname, char *service, xymongraph_t *graphdef,
		      hg_stale_rrds_t nostale, int locatorbased, time_t now)
{
	char request[100], reqcopy[100];
	char *svcname, *delim;
	graphcountdef_t *gdef = NULL, *gdefuser = NULL;
	int wantsingle = 0, rrdparamisservice = 0;
	char rrddir[PATH_MAX];
	DIR *dir;
	pcre2_code *pat, *expat = NULL;
	pcre2_match_data *ovector;
	char errmsg[120];
	int err, result, count = 0;
	PCRE2_SIZE errofs;
	struct dirent *d;

	if (!hostname || !graphdef || locatorbased) return -1;
	if (!xgetenv("XYMONRRDS") || !xgetenv("XYMONHOME")) return -1;

	rrd_graph_service_name(request, sizeof(request), service, graphdef);
	snprintf(reqcopy, sizeof(reqcopy), "%s", request);
	svcname = reqcopy;

	delim = svcname + strcspn(svcname, ":.");
	if (*delim) {
		*delim = '\0';
		if (*(delim+1) == '\0') return -1;
		gdefuser = find_graph_count_def(svcname);
		svcname = delim+1;
		wantsingle = 1;
	}

	gdef = find_graph_count_def(svcname);
	if (gdef == NULL) {
		if (gdefuser) {
			gdef = gdefuser;
			rrdparamisservice = 1;
		}
		else {
			xymonrrd_t *ldef = find_xymon_rrd(svcname, NULL);
			if (ldef) {
				gdef = find_graph_count_def(ldef->xymonrrdname);
				wantsingle = 1;
				rrdparamisservice = 1;
			}
		}
	}
	if ((gdef == NULL) || (gdef->fnpat == NULL)) return -1;

	snprintf(rrddir, sizeof(rrddir), "%s/%s", xgetenv("XYMONRRDS"), hostname);
	dir = opendir(rrddir);
	if (dir == NULL) return -1;

	pat = pcre2_compile((PCRE2_SPTR)gdef->fnpat, strlen(gdef->fnpat), PCRE2_CASELESS, &err, &errofs, NULL);
	if (!pat) {
		pcre2_get_error_message(err, (PCRE2_UCHAR *)errmsg, sizeof(errmsg));
		errprintf("graphs.cfg error, PCRE pattern %s invalid: %s, offset %zu\n",
			  gdef->fnpat, errmsg, errofs);
		closedir(dir);
		return -1;
	}
	if (gdef->exfnpat) {
		expat = pcre2_compile((PCRE2_SPTR)gdef->exfnpat, strlen(gdef->exfnpat), PCRE2_CASELESS, &err, &errofs, NULL);
		if (!expat) {
			pcre2_get_error_message(err, (PCRE2_UCHAR *)errmsg, sizeof(errmsg));
			errprintf("graphs.cfg error, PCRE pattern %s invalid: %s, offset %zu\n",
				  gdef->exfnpat, errmsg, errofs);
			pcre2_code_free(pat);
			closedir(dir);
			return -1;
		}
	}

	ovector = pcre2_match_data_create(30, NULL);
	while ((d = readdir(dir)) != NULL) {
		char *ext;
		char param[PATH_MAX];
		PCRE2_SIZE l = sizeof(param);
		int haveparam;

		if (*(d->d_name) == '.') continue;
		ext = d->d_name + strlen(d->d_name) - strlen(".rrd");
		if ((ext <= d->d_name) || (strcmp(ext, ".rrd") != 0)) continue;
		if (rrd_is_filtered(svcname, d->d_name)) continue;

		if (expat) {
			result = pcre2_match(expat, (PCRE2_SPTR)d->d_name, strlen(d->d_name), 0, 0, ovector, NULL);
			if (result >= 0) continue;
		}

		result = pcre2_match(pat, (PCRE2_SPTR)d->d_name, strlen(d->d_name), 0, 0, ovector, NULL);
		if (result < 0) continue;
		l = sizeof(param);
		haveparam = (pcre2_substring_copy_bynumber(ovector, 1, (PCRE2_UCHAR *)param, &l) == 0);

		if (rrdparamisservice && haveparam) {
			if (!rrd_param_matches_service(param, svcname)) continue;
		}
		else if (wantsingle) {
			if (strstr(d->d_name, svcname) == NULL) continue;
		}

		if (nostale == HG_WITHOUT_STALE_RRDS) {
			char fn[PATH_MAX];
			struct stat st;

			if (snprintf(fn, sizeof(fn), "%s/%s", rrddir, d->d_name) >= (int)sizeof(fn)) continue;
			if ((stat(fn, &st) == 0) && ((now - st.st_mtime) > 86400)) continue;
		}

		count++;
	}

	pcre2_match_data_free(ovector);
	pcre2_code_free(pat);
	if (expat) pcre2_code_free(expat);
	closedir(dir);

	return count;
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

	rrd_graph_service_name(rrdservicename, sizeof(rrdservicename), service, graphdef);

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
