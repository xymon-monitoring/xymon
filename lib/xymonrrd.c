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

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "libxymon.h"
#include "version.h"

/* This is for mapping a status-name -> RRD file */
xymonrrd_t *xymonrrds = NULL;
void * xymonrrdtree;

/* This is the information needed to generate links on the trends column page  */
xymongraph_t *xymongraphs = NULL;

static const char *xymonlinkfmt = "<table summary=\"%s Graph\"><tr><td><A HREF=\"%s&amp;action=menu\"><IMG BORDER=0 SRC=\"%s&amp;graph=hourly&amp;action=view\" ALT=\"xymongraph %s\"></A></td><td> <td align=\"left\" valign=\"top\"> <a href=\"%s&amp;graph=custom&amp;action=selzoom\"> <img src=\"%s/zoom.%s\" border=0 alt=\"Zoom graph\" style='padding: 3px'> </a> </td></tr></table>\n";

static const char *metafmt = "<RRDGraph>\n  <GraphType>%s</GraphType>\n  <GraphLink><![CDATA[%s]]></GraphLink>\n  <GraphImage><![CDATA[%s&amp;graph=hourly]]></GraphImage>\n</RRDGraph>\n";


/* The RRD-handler id a TEST binds its column to, or NULL if it does not map
 * cleanly to one (pseudo-columns, multi-metric self-describing tests). Order
 * follows the RFC: an explicit HANDLER wins; otherwise a single metric routes
 * to the "ncv" handler when it carries an NCV/SPLITNCV spec, else to its own
 * name (the plain TEST2RRD binding). */
static const char *testcfg_rrdname(tc_test_t *t)
{
	if (!t) return NULL;
	if (t->handler) return t->handler;
	if (!t->metrics || t->metrics->next) return NULL;
	if (t->metrics->ncv) return "ncv";
	/* Belt: an empty name must never become an rrd binding (the loader
	 * rejects nameless METRICs, but this is the last line of defense) */
	return (*t->metrics->name ? t->metrics->name : NULL);
}

/*
 * Graph metadata read from the [name] sections of graphs.cfg: keywords
 * that belong with the graph definition but are needed by the page
 * renderers (which never parse the full rrdtool definitions). Only the
 * section headers and the known keywords are scanned.
 */
typedef struct gdefmeta_t {
	char *name;
	int maxinstancesperimage;		/* MAXINSTANCESPERIMAGE N: instances per image when paging */
	int trends;		/* TRENDS: show on the trends page */
	char *exstorepat;	/* EXSTOREPATTERN: instances never stored */
	char *storepat;		/* STOREPATTERN: only these stored */
	char *fnpat;		/* FNPATTERN: the fileset's filename regex */
	int thresholds;		/* THRESHOLDS ON|OFF: 0 unset, 1 on, -1 off */
	char *exstalepat;	/* EXSTALEPATTERN: instances never stale */
	int cfset;		/* XYMON_CF_* bits: consolidations this gdef's DEFs read */
	pcre2_code *exstore;	/* compiled on demand (NULL after a failed compile too) */
	pcre2_code *store;
	pcre2_code *exstale;
	pcre2_code *fnpat_re;
	int exstore_tried, store_tried, exstale_tried, fnpat_tried;
	struct gdefmeta_t *next;
} gdefmeta_t;
static gdefmeta_t *gdefmetahead = NULL;

static char *gdefmeta_srcfn = NULL;
static int gdefmeta_loaded = 0;

/* Point the metadata reader at a non-default graphs.cfg - showgraph's
 * --config option must govern the meta (THRESHOLDS, FNPATTERN, ...) too,
 * or a custom config's keywords would be silently ignored. Must be called
 * before the first metadata lookup. */
void xymon_gdef_meta_source(char *fn)
{
	if (gdefmeta_loaded) {
		/* The metadata is a load-once cache: a redirect after the
		 * first lookup would be silently ignored - say so instead. */
		errprintf("xymon_gdef_meta_source('%s') after the first metadata lookup - ignored\n", (fn ? fn : "(null)"));
		return;
	}
	if (gdefmeta_srcfn) xfree(gdefmeta_srcfn);
	gdefmeta_srcfn = (fn ? strdup(fn) : NULL);
}

static void load_gdef_meta(void)
{
	char fn[PATH_MAX];
	FILE *fd;
	strbuffer_t *inbuf;
	gdefmeta_t *cur = NULL;

	if (gdefmeta_loaded) return;
	gdefmeta_loaded = 1;

	if (gdefmeta_srcfn) snprintf(fn, sizeof(fn), "%s", gdefmeta_srcfn);
	else snprintf(fn, sizeof(fn), "%s/etc/graphs.cfg", xgetenv("XYMONHOME"));
	fd = stackfopen(fn, "r", NULL);
	if (fd == NULL) return;

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		char *p = STRBUF(inbuf);
		p += strspn(p, " \t");

		if (*p == '[') {
			char *delim = strchr(p, ']');
			cur = NULL;
			if (delim) {
				*delim = '\0';
				cur = (gdefmeta_t *)calloc(1, sizeof(gdefmeta_t));
				cur->name = strdup(p+1);
				cur->next = gdefmetahead;
				gdefmetahead = cur;
			}
		}
		else if (cur && (strncasecmp(p, "MAXINSTANCESPERIMAGE", 20) == 0) && isspace((int)p[20])) {
			cur->maxinstancesperimage = atoi(p+20);
			if (cur->maxinstancesperimage < 0) cur->maxinstancesperimage = 0;
		}
		else if (cur && (strncasecmp(p, "TRENDS", 6) == 0) && ((p[6] == '\0') || isspace((int)p[6]))) {
			cur->trends = 1;
		}
		else if (cur && (strncasecmp(p, "EXSTOREPATTERN", 14) == 0) && isspace((int)p[14])) {
			char *pat = p + 14 + strspn(p+14, " \t");
			pat[strcspn(pat, " \t\r\n")] = '\0';
			if (*pat) { if (cur->exstorepat) xfree(cur->exstorepat); cur->exstorepat = strdup(pat); }
		}
		else if (cur && (strncasecmp(p, "STOREPATTERN", 12) == 0) && isspace((int)p[12])) {
			char *pat = p + 12 + strspn(p+12, " \t");
			pat[strcspn(pat, " \t\r\n")] = '\0';
			if (*pat) { if (cur->storepat) xfree(cur->storepat); cur->storepat = strdup(pat); }
		}
		else if (cur && (strncasecmp(p, "FNPATTERN", 9) == 0) && isspace((int)p[9])) {
			char *pat = p + 9 + strspn(p+9, " \t");
			pat[strcspn(pat, " \t\r\n")] = '\0';
			if (*pat) { if (cur->fnpat) xfree(cur->fnpat); cur->fnpat = strdup(pat); }
		}
		else if (cur && (strncmp(p, "DEF:", 4) == 0)) {
			/* A definition line: note which consolidation function it
			 * reads - the writer derives the archives a new file needs
			 * from these. The spec is DEF:vname=rrdfile:ds-name:CF
			 * [:step=...:reduce=...], so the CF is the THIRD colon
			 * field, not the last (options may follow it), and a "\:"
			 * in the rrdfile is an escaped colon, not a separator. */
			char *cf = p + 4;
			int field = 0;

			while (*cf && (field < 2)) {
				if ((*cf == '\\') && cf[1]) cf += 2;
				else { if (*cf == ':') field++; cf++; }
			}
			if (field == 2) {
				cf[strcspn(cf, ": \t\r\n")] = '\0';
				if (strcmp(cf, "AVERAGE") == 0) cur->cfset |= XYMON_CF_AVERAGE;
				else if (strcmp(cf, "MIN") == 0) cur->cfset |= XYMON_CF_MIN;
				else if (strcmp(cf, "MAX") == 0) cur->cfset |= XYMON_CF_MAX;
				else if (strcmp(cf, "LAST") == 0) cur->cfset |= XYMON_CF_LAST;
			}
		}
		else if (cur && (strncasecmp(p, "EXSTALEPATTERN", 14) == 0) && isspace((int)p[14])) {
			char *pat = p + 14 + strspn(p+14, " \t");
			pat[strcspn(pat, " \t\r\n")] = '\0';
			if (*pat) { if (cur->exstalepat) xfree(cur->exstalepat); cur->exstalepat = strdup(pat); }
		}
		else if (cur && (strncasecmp(p, "THRESHOLDS", 10) == 0) && isspace((int)p[10])) {
			char *arg = p + 10 + strspn(p+10, " \t");
			arg[strcspn(arg, " \t\r\n")] = '\0';
			if (strcasecmp(arg, "OFF") == 0) cur->thresholds = -1;
			else if (strcasecmp(arg, "ON") == 0) cur->thresholds = 1;
		}
		else if (cur && (strncasecmp(p, "INCLUDE", 7) == 0) && isspace((int)p[7])) {
			/* A variant inherits the base's metadata; its own keywords
			 * override - later wins. The base must be defined EARLIER
			 * in the file (gdefmetahead holds only prior sections); a
			 * forward reference inherits nothing. */
			char *bname = p + 7; 
			gdefmeta_t *base;
			bname += strspn(bname, " \t");
			bname[strcspn(bname, " \t\r\n")] = '\0';
			for (base = gdefmetahead; (base && strcmp(base->name, bname)); base = base->next) ;
			if (base && (base != cur)) {
				if (cur->maxinstancesperimage == 0) cur->maxinstancesperimage = base->maxinstancesperimage;
				if (base->trends) cur->trends = 1;
				if (base->fnpat && !cur->fnpat) cur->fnpat = strdup(base->fnpat);
				if (base->thresholds && !cur->thresholds) cur->thresholds = base->thresholds;
				if (base->exstalepat && !cur->exstalepat) cur->exstalepat = strdup(base->exstalepat);
				cur->cfset |= base->cfset;
				if (base->exstorepat && !cur->exstorepat) cur->exstorepat = strdup(base->exstorepat);
				if (base->storepat && !cur->storepat) cur->storepat = strdup(base->storepat);
			}
		}
	}
	stackfclose(fd);
	freestrbuffer(inbuf);
}

/* Match a gdefmeta entry against a GRAPHS/test.cfg token. The token may
 * carry a "::N" split-size suffix ("disk::8") - renderer paging syntax,
 * not part of the graph's name; ignoring it here would silently bypass
 * EXSTALEPATTERN/MAXINSTANCESPERIMAGE for such entries. Returns 0 on
 * match, following the strcmp find-loop idiom. */
static int gdefmeta_namecmp(const char *gdefname, const char *entry)
{
	size_t n = strlen(gdefname);

	return !((strncmp(gdefname, entry, n) == 0) &&
		 ((entry[n] == '\0') || ((entry[n] == ':') && (entry[n+1] == ':'))));
}

int xymon_gdef_maxinstancesperimage(char *name)
{
	gdefmeta_t *walk;

	load_gdef_meta();
	for (walk = gdefmetahead; (walk && gdefmeta_namecmp(walk->name, name)); walk = walk->next) ;
	return ((walk && (walk->maxinstancesperimage > 0)) ? walk->maxinstancesperimage : 0);
}


/* Filename lookup: match "name.instance.rrd" against the graph
 * definitions, with the same partial-match boundary rule as
 * find_xymon_graph(). The scan must be loaded first. */
static gdefmeta_t *gdefmeta_forfile(char *fn)
{
	gdefmeta_t *walk;

	load_gdef_meta();
	for (walk = gdefmetahead; (walk); walk = walk->next) {
		int nlen = strlen(walk->name);
		if (strncmp(walk->name, fn, nlen) != 0) continue;
		if ((fn[nlen] != '.') && (fn[nlen] != ',') && (fn[nlen] != '\0')) continue;
		return walk;
	}
	return NULL;
}

static pcre2_code *storepat_compile(char *pattern)
{
	int err;
	PCRE2_SIZE errofs;
	pcre2_code *result = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, PCRE2_CASELESS, &err, &errofs, NULL);

	if (!result) {
		char msg[256];
		pcre2_get_error_message(err, (PCRE2_UCHAR *)msg, sizeof(msg));
		errprintf("graphs.cfg store pattern '%s' invalid at offset %d: %s\n", pattern, (int)errofs, msg);
	}
	return result;
}

static int storepat_match(pcre2_code *pat, char *fn, size_t fnlen)
{
	pcre2_match_data *md;
	int result;

	md = pcre2_match_data_create_from_pattern(pat, NULL);
	if (!md) return 0;	/* allocation failed: no match, not a crash */
	result = pcre2_match(pat, (PCRE2_SPTR)fn, fnlen, 0, 0, md, NULL);
	pcre2_match_data_free(md);
	return (result >= 0);
}

/*
 * The RRD writer's storage gate: may this file be written at all?
 * Patterns match the filename minus its ".rrd" suffix, case-insensitively.
 * Returns 0 = drop, 1 = store.
 */
int xymon_gdef_store_allowed(char *fn)
{
	gdefmeta_t *walk = gdefmeta_forfile(fn);
	size_t fnlen;

	if (!walk || (!walk->exstorepat && !walk->storepat)) return 1;

	fnlen = strlen(fn);
	if ((fnlen > 4) && (strcmp(fn+fnlen-4, ".rrd") == 0)) fnlen -= 4;

	if (walk->exstorepat) {
		if (!walk->exstore && !walk->exstore_tried) {
			walk->exstore = storepat_compile(walk->exstorepat);
			walk->exstore_tried = 1;	/* compile once; a broken pattern fails open */
		}
		if (walk->exstore && storepat_match(walk->exstore, fn, fnlen)) return 0;
	}
	if (walk->storepat) {
		if (!walk->store && !walk->store_tried) {
			walk->store = storepat_compile(walk->storepat);
			walk->store_tried = 1;
		}
		if (walk->store && !storepat_match(walk->store, fn, fnlen)) return 0;
	}
	return 1;
}

/* Does this graph's config make its file set diverge from what a status
 * message shows? Then a message-derived paging count cannot be trusted. */
int xymon_gdef_fileset_unknown(char *name)
{
	gdefmeta_t *walk;

	load_gdef_meta();
	for (walk = gdefmetahead; (walk && gdefmeta_namecmp(walk->name, name)); walk = walk->next) ;
	return (walk && (walk->exstorepat || walk->storepat));
}

/* Union of the consolidation functions read by the DEF lines of every
 * graph definition matching this file - FNPATTERN when the gdef has one,
 * else the name-prefix boundary rule. 0 = no matching gdef declares any;
 * the writer then creates exactly the stock archive set. */
int xymon_gdef_cfs_forfile(char *fn)
{
	gdefmeta_t *walk;
	int cfs = 0;

	load_gdef_meta();
	for (walk = gdefmetahead; (walk); walk = walk->next) {
		if (!walk->cfset) continue;
		if (walk->fnpat) {
			if (!walk->fnpat_tried) {
				walk->fnpat_tried = 1;
				walk->fnpat_re = compileregex(walk->fnpat);
				if (!walk->fnpat_re) errprintf("Invalid FNPATTERN '%s' in graph definition [%s]\n", walk->fnpat, walk->name);
			}
			if (walk->fnpat_re && matchregex(fn, walk->fnpat_re)) cfs |= walk->cfset;
		}
		else {
			int nlen = strlen(walk->name);
			if ((strncmp(walk->name, fn, nlen) == 0) &&
			    ((fn[nlen] == '.') || (fn[nlen] == ',') || (fn[nlen] == '\0'))) cfs |= walk->cfset;
		}
	}
	return cfs;
}

/* Is this file exempt from the staleness window (EXSTALEPATTERN)? The
 * one fixed window (XYMON_STALE_WINDOW, main's historic 86400) governs
 * BOTH the renderer's stale-file filter and the fileset-index counts;
 * exemption is per instance, matched like the storage patterns (name
 * minus its ".rrd" suffix, case-insensitively). */
int xymon_gdef_stale_exempt(char *name, char *fn)
{
	gdefmeta_t *walk;
	size_t fnlen;

	load_gdef_meta();
	for (walk = gdefmetahead; (walk && gdefmeta_namecmp(walk->name, name)); walk = walk->next) ;
	if (!walk || !walk->exstalepat) return 0;
	if (!walk->exstale && !walk->exstale_tried) {
		walk->exstale = storepat_compile(walk->exstalepat);
		walk->exstale_tried = 1;	/* compile once; a broken pattern exempts nothing */
	}
	if (!walk->exstale) return 0;
	fnlen = strlen(fn);
	if ((fnlen > 4) && (strcmp(fn+fnlen-4, ".rrd") == 0)) fnlen -= 4;
	return storepat_match(walk->exstale, fn, fnlen);
}

/* THRESHOLDS OFF in the graph definition: the admin's say on whether
 * declared threshold relations are co-plotted. Default (unset/ON) plots. */
int xymon_gdef_thresholds_off(char *name)
{
	gdefmeta_t *walk;

	load_gdef_meta();
	for (walk = gdefmetahead; (walk && gdefmeta_namecmp(walk->name, name)); walk = walk->next) ;
	return (walk && (walk->thresholds == -1));
}

/* The exact fileset size of a graph for one host, from the writer-kept
 * fileset index: entries matched by the gdef's FNPATTERN when it has one,
 * else by the "<name>.<instance>.rrd" prefix rule (the synthetic-gdef
 * default). -1 = no index (or a broken pattern) - callers keep their
 * previous fallback behaviour. */
int xymon_gdef_fileset_count(char *hostname, char *name)
{
	gdefmeta_t *walk;
	void *exempt = NULL;

	load_gdef_meta();
	for (walk = gdefmetahead; (walk && gdefmeta_namecmp(walk->name, name)); walk = walk->next) ;

	/* Same window and same exemption as the renderer's stale filter, so
	 * the count always equals what renders. The exempt pattern compiles
	 * through the accessor's own once-only path. */
	if (walk && walk->exstalepat) {
		xymon_gdef_stale_exempt(name, "");	/* force the compile */
		exempt = walk->exstale;
	}

	if (walk && walk->fnpat) {
		if (!walk->fnpat_tried) {
			walk->fnpat_tried = 1;
			walk->fnpat_re = compileregex(walk->fnpat);
			if (!walk->fnpat_re) errprintf("Invalid FNPATTERN '%s' in graph definition [%s]\n", walk->fnpat, walk->name);
		}
		if (!walk->fnpat_re) return -1;
		return fsidx_count_pattern(hostname, walk->fnpat_re, XYMON_STALE_WINDOW, exempt);
	}

	{
		/* Prefix rule: strip a "::N" split-size suffix, it is not part
		 * of the filename prefix. */
		char base[PATH_MAX];
		char *sfx = strstr(name, "::");

		if (sfx) {
			size_t n = (size_t)(sfx - name);
			if (n >= sizeof(base)) return -1;
			memcpy(base, name, n); base[n] = '\0';
			name = base;
		}
		return fsidx_count_prefix(hostname, name, XYMON_STALE_WINDOW, exempt);
	}
}


/*
 * Define the mapping between Xymon columns and RRD graphs.
 * Normally they are identical, but some RRD's use different names.
 */
static void rrd_setup(void)
{
	static time_t setup_done = 0;
	SBUF_DEFINE(lenv);
	char *ldef, *p, *services;
	SBUF_DEFINE(tcptests);
	int count;
	xymonrrd_t *lrec;
	xymongraph_t *grec;
	tc_test_t *tclist, *tc;


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
	if (*lenv) { p = lenv+strlen(lenv)-1; if (*p == ',') *p = '\0'; }	/* Drop a trailing comma */
	p = strtok(tcptests, " "); 
	while (p) {
		unsigned int curlen = strlen(lenv);
		snprintf(lenv+curlen, (lenv_buflen - curlen), ",%s=tcp", p); 
		p = strtok(NULL, " ");
	}
	xfree(tcptests);
	xfree(services);

	/* Reserve extra table slots for test.cfg column bindings not already
	 * present in TEST2RRD - they are overlaid after the env fill below. */
	tclist = testcfg_load();
	/* Count entries without ever reading past lenv: on an EMPTY list
	 * (test.cfg-era configs may clear TEST2RRD/GRAPHS) the old
	 * strchr(lenv+1, ...) idiom read beyond a one-byte allocation. */
	count = (*lenv != '\0'); for (p = strchr(lenv, ','); (p); p = strchr(p+1, ',')) count++;
	for (tc = tclist; (tc); tc = tc->next) count += (testcfg_rrdname(tc) != NULL);
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

	/* Overlay test.cfg: a single-metric TEST binds its column to that
	 * metric, overriding TEST2RRD for the same column, or adding a new
	 * one. This is the TEST2RRD env replacement; multi-metric and
	 * pseudo-column tests bind no single RRD name and are skipped. */
	for (tc = tclist; (tc); tc = tc->next) {
		const char *rrdname = testcfg_rrdname(tc);
		xtreePos_t h;

		if (!rrdname) continue;
		h = xtreeFind(xymonrrdtree, tc->name);
		if (h != xtreeEnd(xymonrrdtree)) {
			xymonrrd_t *ex = (xymonrrd_t *)xtreeData(xymonrrdtree, h);

			/* An IMPLICIT binding - no HANDLER, no NCV, just the
			 * metric's name - never overrides a working env mapping:
			 * a documentation-only "TEST myapp { METRIC myapp }" next
			 * to TEST2RRD="myapp=ncv" would silently rebind the
			 * column to a nonexistent handler and kill collection.
			 * Explicit intent (HANDLER, NCV) still wins. */
			if (!tc->handler && !(tc->metrics && tc->metrics->ncv) && (strcasecmp(ex->xymonrrdname, rrdname) != 0)) {
				errprintf("test.cfg: TEST %s implies handler '%s' but the environment maps it to '%s' - keeping the env mapping (use HANDLER to override)\n",
					  tc->name, rrdname, ex->xymonrrdname);
				continue;
			}
			if (ex->xymonrrdname != ex->svcname) xfree(ex->xymonrrdname);
			ex->xymonrrdname = strdup(rrdname);
		}
		else {
			lrec->svcname = strdup(tc->name);
			lrec->xymonrrdname = strdup(rrdname);
			xtreeAdd(xymonrrdtree, lrec->svcname, lrec);
			lrec++;
		}
	}

	/* Setup the xymongraphs table, describing how to make graphs from an RRD.
	 * Graph metadata from graphs.cfg contributes too: gdefs marked TRENDS
	 * become table members without a GRAPHS env entry. */
	load_gdef_meta();
	lenv = strdup(xgetenv("GRAPHS"));
	if (*lenv) { p = lenv+strlen(lenv)-1; if (*p == ',') *p = '\0'; }	/* Drop a trailing comma */
	/* Count entries without ever reading past lenv: on an EMPTY list
	 * (test.cfg-era configs may clear TEST2RRD/GRAPHS) the old
	 * strchr(lenv+1, ...) idiom read beyond a one-byte allocation. */
	count = (*lenv != '\0'); for (p = strchr(lenv, ','); (p); p = strchr(p+1, ',')) count++;
	{
		gdefmeta_t *meta;
		for (meta = gdefmetahead; (meta); meta = meta->next) count += (meta->trends != 0);
	}
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

	/* Append gdefs marked TRENDS in graphs.cfg that GRAPHS did not list */
	{
		gdefmeta_t *meta;
		for (meta = gdefmetahead; (meta); meta = meta->next) {
			xymongraph_t *walk;
			if (!meta->trends) continue;
			for (walk = xymongraphs; (walk->xymonrrdname && strcmp(walk->xymonrrdname, meta->name)); walk++) ;
			if (walk->xymonrrdname == NULL) {
				walk->xymonrrdname = strdup(meta->name);
				grec = walk;
			}
		}
	}

	/* MAXINSTANCESPERIMAGE in the graph definition overrides a legacy ::N suffix:
	 * the split size belongs with the graph, not in a second file. */
	for (grec = xymongraphs; (grec->xymonrrdname); grec++) {
		int maxinstancesperimage = xymon_gdef_maxinstancesperimage(grec->xymonrrdname);
		if (maxinstancesperimage > 0) grec->maxgraphs = maxinstancesperimage;
	}

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

		/* The item count comes from status content (a linecount override
		 * or a count= marker), so an absurd or negative value must not
		 * drive the part loop into building a giant page - such a graph
		 * renders unsliced instead. */
		if ((itemcount < 0) || (itemcount > 4096)) itemcount = 0;

		step = (graphdef->maxgraphs > 0 ? graphdef->maxgraphs : 5);
		if (itemcount) {
			/* Spread itemcount instances evenly over the needed number of
			 * graphs. gcount is the graph count (ceil); the per-graph step
			 * must round UP too, otherwise a count that gcount does not
			 * divide leaves every graph under-filled below maxgraphs and
			 * spawns extra graphs - e.g. 25 items at maxgraphs=2 gives
			 * gcount=13 but a floored step=1, so 25 single-item graphs
			 * instead of 13. Rounding up yields step=2 (last graph holds
			 * the remainder), never exceeding maxgraphs. */
			int gcount = (itemcount / step); if ((gcount*step) != itemcount) gcount++;
			step = ((itemcount + gcount - 1) / gcount);
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


