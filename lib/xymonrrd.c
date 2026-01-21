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
#include <pcre.h>

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

int count_rrd_files_for_graph(char *hostname, char *graphname)
{
	char rrdpath[PATH_MAX];
	char *saved_cwd = NULL;
	int need_chdir_back = 0;
	int rrd_count = 0;

	snprintf(rrdpath, sizeof(rrdpath), "%s/%s", xgetenv("XYMONRRDS"), hostname);

	/* Save current directory before chdir */
	saved_cwd = getcwd(NULL, 0);
	if (chdir(rrdpath) != 0) {
		errprintf("Cannot chdir to RRD directory '%s' for host '%s': %s\n",
			  rrdpath, hostname, strerror(errno));
		if (saved_cwd) free(saved_cwd);
		return 0;
	}
	need_chdir_back = 1;

	/* Load the graph definition to get FNPATTERN and EXFNPATTERN */
	char graphcfgfn[PATH_MAX];
	FILE *graphcfg;
	char line[1024];
	char *fnpattern = NULL;
	char *exfnpattern = NULL;
	int insection = 0;

	snprintf(graphcfgfn, sizeof(graphcfgfn), "%s/etc/graphs.cfg", xgetenv("XYMONHOME"));
	graphcfg = stackfopen(graphcfgfn, "r", NULL);

	if (!graphcfg) {
		errprintf("Cannot open graphs.cfg at '%s': %s\n",
			  graphcfgfn, strerror(errno));
	}

	if (graphcfg) {
		strbuffer_t *linebuf = newstrbuffer(0);
		char *lineptr;

		while ((lineptr = stackfgets(linebuf, NULL)) != NULL) {
			strncpy(line, STRBUF(linebuf), sizeof(line)-1);
			line[sizeof(line)-1] = '\0';

			/* Remove newline */
			char *nl = strchr(line, '\n');
			if (nl) *nl = '\0';
			nl = strchr(line, '\r');
			if (nl) *nl = '\0';

			/* Check for section header like [smart-temp] */
			if (line[0] == '[' && line[strlen(line)-1] == ']') {
				line[strlen(line)-1] = '\0';  /* remove ']' */
				if (strcmp(line+1, graphname) == 0) {
					insection = 1;
				} else {
					/* Exiting our section - stop parsing */
					if (insection) break;
					insection = 0;
				}
			}
			else if (insection) {
				/* Check for FNPATTERN and EXFNPATTERN with possible leading whitespace */
				char *line_start = line + strspn(line, " \t");
				if (strncasecmp(line_start, "EXFNPATTERN", 11) == 0) {
					char *pval = line_start + 11;
					pval += strspn(pval, " \t=");  /* Skip whitespace and equals */
					if (!exfnpattern) exfnpattern = strdup(pval);
				}
				else if (strncasecmp(line_start, "FNPATTERN", 9) == 0) {
					char *pval = line_start + 9;
					pval += strspn(pval, " \t=");  /* Skip whitespace and equals */
					if (!fnpattern) fnpattern = strdup(pval);
				}
			}
		}
		freestrbuffer(linebuf);
		stackfclose(graphcfg);
	}

	/*
	 * Now count RRD files matching this graph.
	 *
	 * Performance note: This function is called by the CGI when rendering status pages,
	 * NOT by xymond_filestore during status updates. This architectural decision moves
	 * the RRD scanning cost from the background daemon (called continuously) to the web
	 * interface (called only when humans view pages).
	 *
	 * We optimize for two common cases:
	 *
	 * 1. FNPATTERN graphs (e.g., disk with pattern ".*\.rrd"): Must scan directory
	 *    to find all matching files. Uses readdir() which benefits from kernel VFS
	 *    caching of directory entries (dentries on Linux, directory buffer on FreeBSD).
	 *    Subsequent scans of the same directory are fast due to this caching.
	 *
	 * 2. Single-file graphs (e.g., cpu.rrd, memory.rrd): Direct stat() lookup is
	 *    much faster than scanning - O(1) vs O(n). This avoids reading potentially
	 *    hundreds of directory entries to find a single known filename.
	 */
	if (fnpattern) {
		/*
		 * FNPATTERN specified: need to scan directory and match pattern.
		 * Example: disk graphs use FNPATTERN ".*,(.*)\.rrd" to match all
		 * disk partition RRDs like "/dev/sda1.rrd", "/dev/sda2.rrd", etc.
		 *
		 * EXFNPATTERN can be used to exclude certain files from the match.
		 * Example: [tcp] graph uses FNPATTERN "^tcp.(.+).rrd" to match all tcp files,
		 * but EXFNPATTERN "^tcp.http.(.+).rrd" to exclude http tests (which have their own graph).
		 */
		DIR *dir = opendir(".");
		if (!dir) {
			errprintf("Cannot open RRD directory for host '%s' graph '%s': %s\n",
				  hostname, graphname, strerror(errno));
		}

		if (dir) {
			struct dirent *entry;
			const char *errmsg;
			int errofs;
			pcre *pat;
			pcre *expat = NULL;

			pat = pcre_compile(fnpattern, PCRE_CASELESS, &errmsg, &errofs, NULL);
			if (!pat) {
				errprintf("PCRE compilation failed for pattern '%s' (graph=%s, host=%s): %s at offset %d\n",
					  fnpattern, graphname, hostname, errmsg, errofs);
			}

			/* Compile exclusion pattern if present */
			if (exfnpattern) {
				expat = pcre_compile(exfnpattern, PCRE_CASELESS, &errmsg, &errofs, NULL);
				if (!expat) {
					errprintf("PCRE compilation failed for exclusion pattern '%s' (graph=%s, host=%s): %s at offset %d\n",
						  exfnpattern, graphname, hostname, errmsg, errofs);
				}
			}

			if (pat) {
				/*
				 * Scan directory for matching files. readdir() benefits from kernel
				 * VFS caching, so repeated scans of the same directory are fast.
				 */
				while ((entry = readdir(dir)) != NULL) {
					if (strlen(entry->d_name) > 4 &&
						strcmp(entry->d_name + strlen(entry->d_name) - 4, ".rrd") == 0) {
						int result = pcre_exec(pat, NULL, entry->d_name, strlen(entry->d_name), 0, 0, NULL, 0);
						if (result >= 0) {
							/* Matched FNPATTERN - now check if it should be excluded */
							int excluded = 0;
							if (expat) {
								int exresult = pcre_exec(expat, NULL, entry->d_name, strlen(entry->d_name), 0, 0, NULL, 0);
								if (exresult >= 0) {
									excluded = 1;
								}
							}
							if (!excluded) {
								rrd_count++;
							}
						}
					}
				}
				pcre_free(pat);
				if (expat) pcre_free(expat);
			}
			closedir(dir);
		}
		xfree(fnpattern);
		if (exfnpattern) xfree(exfnpattern);
	} else {
		/*
		 * No FNPATTERN: simple 1:1 mapping (e.g., cpu -> cpu.rrd).
		 * Use direct stat() lookup instead of directory scan for much better performance.
		 * This is an O(1) operation vs O(n) directory scan, and avoids reading potentially
		 * hundreds of directory entries just to find one specific file.
		 */
		char expected_rrd[PATH_MAX];
		struct stat st;

		snprintf(expected_rrd, sizeof(expected_rrd), "%s.rrd", graphname);
		if (stat(expected_rrd, &st) == 0) {
			rrd_count = 1;
		}
	}

	/* Restore original directory */
	if (need_chdir_back && saved_cwd) {
		if (chdir(saved_cwd) != 0) {
			errprintf("CRITICAL: Failed to restore directory to '%s' after RRD count (hostname=%s, graph=%s): %s\n",
				  saved_cwd, hostname, graphname, strerror(errno));
			/* This is critical - if we can't restore the directory, subsequent operations will fail */
		}
	}
	if (saved_cwd) free(saved_cwd);

	return rrd_count;
}


