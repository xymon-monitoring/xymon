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
#include <math.h>
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

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <rrd.h>

#include "libxymon.h"
#include "../lib/rrd_api_compat.h"

#define HOUR_GRAPH  "e-48h"
#define DAY_GRAPH   "e-12d"
#define WEEK_GRAPH  "e-48d"
#define MONTH_GRAPH "e-576d"

unsigned char blankimg[] = "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\x04\x67\x41\x4d\x41\x00\x00\xb1\x8f\x0b\xfc\x61\x05\x00\x00\x00\x06\x62\x4b\x47\x44\x00\xff\x00\xff\x00\xff\xa0\xbd\xa7\x93\x00\x00\x00\x09\x70\x48\x59\x73\x00\x00\x0b\x12\x00\x00\x0b\x12\x01\xd2\xdd\x7e\xfc\x00\x00\x00\x07\x74\x49\x4d\x45\x07\xd1\x01\x14\x12\x21\x14\x7e\x4a\x3a\xd2\x00\x00\x00\x0d\x49\x44\x41\x54\x78\xda\x63\x60\x60\x60\x60\x00\x00\x00\x05\x00\x01\x7a\xa8\x57\x50\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";


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
	int  dscount;	/* DSCOUNT directive: enables @DSIDX@/@PREVDSIDX@ expansion 1..dscount */
	int  dsidx_runtime;	/* 1 = block uses @DSIDX@ without explicit DSCOUNT; expand at render time so N
				 *     can be derived from each RRD file's actual DS list */
	char **defs;
	char **rawdefs;	/* pre-DSCOUNT-expansion templates, kept so an INCLUDE
			 * variant declaring its own DSCOUNT can re-expand; NULL
			 * when defs was never expanded */
	struct gdef_t *next;
} gdef_t;
gdef_t *gdefs = NULL;

typedef struct rrddb_t {
	char *key;
	char *rrdfn;
	char *rrdparam;
	int   rrdparamfinal;	/* rrdparam is the final legend already (rrdinstance-decoded);
				 * skip the legacy comma->slash un-mangling at render time. */
} rrddb_t;

rrddb_t *rrddbs = NULL;
int rrddbcount = 0;
int rrddbsize = 0;
int rrdidx = 0;
int paramlen = 0;
int firstidx = -1;
int idxcount = -1;
int lastidx = 0;

/* Quote a value for safe inclusion as a single word in a /bin/sh command
 * line: wrap it in single quotes and render each embedded single quote as
 * '\'' (close, escaped quote, reopen). The result contains no shell-active
 * character outside the admin's own command, so a CGI-supplied value
 * cannot break out. Caller frees. */
static char *shellquote(const char *s)
{
	strbuffer_t *b = newstrbuffer(0);
	const char *p;
	char *result;

	addtobuffer(b, "'");
	for (p = (s ? s : ""); (*p); p++) {
		if (*p == '\'') addtobuffer(b, "'\\''");
		else addtobufferraw(b, (char *)p, 1);
	}
	addtobuffer(b, "'");
	result = strdup(STRBUF(b));
	freestrbuffer(b);

	return result;
}

void errormsg(char *msg)
{
	/* Command-line invocation (--emit-gdef): plain error on stderr,
	 * not an HTML page on stdout. */
	if (getenv("REQUEST_METHOD") == NULL) {
		fprintf(stderr, "%s\n", msg);
		exit(1);
	}

	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

void request_cacheflush(char *hostname)
{
	/* Build a cache-flush request, and send it to all of the $XYMONTMP/rrdctl.* sockets */
	SBUF_DEFINE(req);
	char *bufp;
	int bytesleft;
	DIR *dir;
	struct dirent *d;
	int ctlsocket = -1;

	ctlsocket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (ctlsocket == -1) {
		errprintf("Cannot get socket: %s\n", strerror(errno));
		return;
	}
	fcntl(ctlsocket, F_SETFL, O_NONBLOCK);

	dir = opendir(xgetenv("XYMONTMP"));
	if (!dir) {
		errprintf("Cannot access $XYMONTMP directory: %s\n", strerror(errno));
		return;
	}

	SBUF_MALLOC(req, strlen(hostname)+3);
	snprintf(req, req_buflen, "/%s/", hostname);

	while ((d = readdir(dir)) != NULL) {
		if (strncmp(d->d_name, "rrdctl.", 7) == 0) {
			struct sockaddr_un myaddr;
			socklen_t myaddrsz = 0;
			int n, sendfailed = 0;
			SBUF_DEFINE(fnam);

			memset(&myaddr, 0, sizeof(myaddr));
			myaddr.sun_family = AF_UNIX;

			SBUF_MALLOC(fnam, strlen(xgetenv("XYMONTMP"))+ strlen(d->d_name) + 2);
			snprintf(fnam, fnam_buflen, "%s/%s", xgetenv("XYMONTMP"), d->d_name);
			if (strlen(fnam) > sizeof(myaddr.sun_path)) {
				errprintf("rrdctl files located in XYMONTMP with too long pathname - max %d characters\n", sizeof(myaddr.sun_path));
				return;
			}
			strncpy(myaddr.sun_path, fnam, sizeof(myaddr.sun_path));
			xfree(fnam);

			myaddrsz = sizeof(myaddr);
			bufp = req; bytesleft = strlen(req);
			do {
				n = sendto(ctlsocket, bufp, bytesleft, 0, (struct sockaddr *)&myaddr, myaddrsz);
				if (n == -1) {
					if (errno == EDESTADDRREQ) {
						/* Probably a left-over rrdctl file, ignore it */
					}
					else if (errno == EAGAIN) {
						/* Harmless */
					}
					else {
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
	char *stp = NULL;

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
		snprintf(glegend, 40, "%s - %s", t1, t2);
	}
}

#include "dsidx.inc.c"

/* Count DSes in `rrdfn` whose names start with `prefix` and end with a
 * positive integer (e.g. prefix="ping" matches ping1..pingN). Returns 0
 * if the file can't be opened or has no matches. */
static int rrd_count_ds_with_prefix(const char *rrdfn, const char *prefix)
{
	rrd_info_t *info, *p;
	int count = 0;
	size_t plen;

	if (!rrdfn || !prefix) return 0;
	plen = strlen(prefix);

	rrd_clear_error();
	info = rrd_info_r((char *)rrdfn);
	if (!info) { rrd_clear_error(); return 0; }

	/* rrd_info keys for DSes look like: ds[NAME].type, ds[NAME].minimal_heartbeat, ...
	 * Count once per unique NAME that starts with `prefix` and whose
	 * remainder is a positive integer. We use the ".index" suffix because
	 * it appears exactly once per DS. */
	for (p = info; p; p = p->next) {
		const char *k = p->key;
		const char *rb;
		const char *q;
		int has_digit = 0;
		if (strncmp(k, "ds[", 3) != 0) continue;
		rb = strchr(k + 3, ']');
		if (!rb) continue;
		if (strcmp(rb, "].index") != 0) continue;
		if ((size_t)(rb - (k + 3)) <= plen) continue;
		if (strncmp(k + 3, prefix, plen) != 0) continue;
		for (q = k + 3 + plen; q < rb; q++) {
			if (*q < '0' || *q > '9') { has_digit = 0; break; }
			has_digit = 1;
		}
		if (has_digit) count++;
	}

	rrd_info_free(info);
	return count;
}

/* Decide how many copies to expand @DSIDX@ into for *this* render pass.
 * Order of precedence:
 *   1. count actually present in `rrdfn` (matched against the first
 *      @DSIDX@-using DEF's DS-name prefix),
 *   2. 0 -> leaves templates literal (rrdtool will error loudly).
 * The file-derived count makes the graph match what the writer actually
 * stored, even when hosts differ in their sample counts. */
static int derive_dscount_for_file(char *const *templates, const char *rrdfn)
{
	int i;

	for (i = 0; templates && templates[i]; i++) {
		char *fn, *dsname, *prefix;
		int n;
		if (!strstr(templates[i], "@DSIDX@")) continue;
		fn = def_rrdfile(templates[i]);
		dsname = def_dsname(templates[i]);
		if (!fn || !dsname) { free(fn); free(dsname); continue; }
		prefix = dsname_prefix(dsname);
		if (!prefix) { free(fn); free(dsname); continue; }
		/* If the template uses @RRDFN@ as its filename, rrdtool's own
		 * cwd-relative substitution applies at graph time; we ask the
		 * concrete file passed in via rrdfn instead. */
		n = rrd_count_ds_with_prefix(
			(strstr(fn, "@RRDFN@") != NULL) ? rrdfn : fn,
			prefix);
		free(fn); free(dsname); free(prefix);
		if (n > 0) return n;
		break;	/* file present but no matching DS */
	}

	/* No usable file: leave the templates literal (rrdtool errors
	 * loudly). A probe-specific default can be layered on later. */
	return 0;
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
				expand_dsidx_in_block(newitem);
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
		else if ((strncasecmp(p, "DSCOUNT", 7) == 0) && isspace((int)p[7])) {
			if (newitem == NULL) {
				errprintf("graphs.cfg error: DSCOUNT before any [section]\n");
				continue;
			}
			p += 7; p += strspn(p, " \t");
			if (*p == '$') {
				/* DSCOUNT $VAR -- read from xymonserver.cfg env */
				char *v = xgetenv(p + 1);
				newitem->dscount = (v ? atoi(v) : 0);
			}
			else {
				newitem->dscount = atoi(p);
			}
			if (newitem->dscount < 0) newitem->dscount = 0;
		}
		else if ((strncasecmp(p, "MAXINSTANCESPERIMAGE", 20) == 0) && isspace((int)p[20])) {
			/* Page-renderer metadata (instances per image when paging);
			 * consumed by lib/xymonrrd.c, not an rrdtool argument. */
			continue;
		}
		else if ((strncasecmp(p, "TRENDS", 6) == 0) && ((p[6] == '\0') || isspace((int)p[6]))) {
			/* Trends-page membership; consumed by lib/xymonrrd.c */
			continue;
		}
		else if ((strncasecmp(p, "EXSTOREPATTERN", 14) == 0) && isspace((int)p[14])) {
			/* Storage filter; consumed by lib/xymonrrd.c */
			continue;
		}
		else if ((strncasecmp(p, "STOREPATTERN", 12) == 0) && isspace((int)p[12])) {
			continue;
		}
		else if ((strncasecmp(p, "THRESHOLDS", 10) == 0) && isspace((int)p[10])) {
			/* Threshold co-plot gate; consumed by lib/xymonrrd.c */
			continue;
		}
		else if ((strncasecmp(p, "EXSTALEPATTERN", 14) == 0) && isspace((int)p[14])) {
			/* Staleness exemption; consumed by lib/xymonrrd.c */
			continue;
		}
		else if ((strncasecmp(p, "INCLUDE", 7) == 0) && isspace((int)p[7])) {
			/* Inherit an earlier-defined gdef: header keywords copied
			 * now (later keywords in this section override), its
			 * definition lines copied in place; further lines append. */
			char *bname = p + 7;
			gdef_t *base;
			int i;

			if (newitem == NULL) {
				errprintf("graphs.cfg error: INCLUDE before any [section]\n");
				continue;
			}
			bname += strspn(bname, " \t");
			bname[strcspn(bname, " \t\r\n")] = '\0';
			for (base = gdefs; (base && strcmp(bname, base->name)); base = base->next) ;
			if (base == NULL) {
				errprintf("graphs.cfg error: [%s] includes unknown definition '%s'\n", newitem->name, bname);
				continue;
			}

			/* The variant's own keywords always win, wherever they
			 * appear relative to the INCLUDE line - inherit only
			 * what the variant has not set itself. */
			if (base->fnpat && !newitem->fnpat) newitem->fnpat = strdup(base->fnpat);
			if (base->exfnpat && !newitem->exfnpat) newitem->exfnpat = strdup(base->exfnpat);
			if (base->title && !newitem->title) newitem->title = strdup(base->title);
			if (base->yaxis && !newitem->yaxis) newitem->yaxis = strdup(base->yaxis);
			if (base->graphopts && !newitem->graphopts) newitem->graphopts = strdup(base->graphopts);
			if (!newitem->novzoom) newitem->novzoom = base->novzoom;
			if (!newitem->dscount) newitem->dscount = base->dscount;
			if (!newitem->dsidx_runtime) newitem->dsidx_runtime = base->dsidx_runtime;
			{
				/* Copy the base's pre-expansion templates when it has
				 * them: the variant's own DSCOUNT (even one declared
				 * later in the section) expands them at section close.
				 * The expanded lines would pass through expansion
				 * unchanged, freezing the base's dataset count. */
				char **srcdefs = (base->rawdefs ? base->rawdefs : base->defs);

				for (i = 0; (srcdefs[i]); i++) {
					if (alldefidx == alldefcount) {
						alldefcount += 5;
						alldefs = (char **)realloc(alldefs, (alldefcount+1) * sizeof(char *));
					}
					alldefs[alldefidx++] = strdup(srcdefs[i]);
				}
			}
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
		expand_dsidx_in_block(newitem);
		newitem->next = gdefs;
		gdefs = newitem;
	}

	stackfclose(fd);
	freestrbuffer(inbuf);
}

char *lookup_meta(char *keybuf, char *rrdfn)
{
	FILE *fd;
	SBUF_DEFINE(metafn);
	char *p;
	int servicelen = strlen(service);
	int keylen = strlen(keybuf);
	int found;
	static char buf[1024]; /* Must be static since it is returned to caller */

	SBUF_MALLOC(metafn, PATH_MAX);

	p = strrchr(rrdfn, '/');
	if (!p) {
		strncpy(metafn, "rrd.meta", metafn_buflen);
	}
	else {
		metafn = (char *)malloc(strlen(rrdfn) + 10);
		*p = '\0';
		snprintf(metafn, metafn_buflen, "%s/rrd.meta", rrdfn);
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
	/* Change all colons to "\:" */
	static char *result = NULL;
	int count = 0;
	char *p, *inp, *outp;

	if (!buf) return "";
	p = buf; while ((p = strchr(p, ':')) != NULL) { count++; p++; }
	if (count == 0) return buf;

	if (result) xfree(result);
	result = (char *) malloc(strlen(buf) + count + 1); /* Add one backslash per colon */
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
				SBUF_DEFINE(resultstr);

				val = colon_escape(rrddbs[rrdidx].rrdparam);
				if (!rrddbs[rrdidx].rrdparamfinal) { p = val; while ((p = strchr(p, ',')) != NULL) *p = '/'; }

				/* rrdparam strings may be very long. */
				if (strlen(val) > 100) *(val+100) = '\0';

				/*
				 * "paramlen" holds the longest string of the any of the matching files' rrdparam.
				 * However, because this goes through colon_escape(), the actual string length 
				 * passed to librrd functions may be longer (since ":" must be escaped as "\:").
				 */
				vallen = strlen(val);
				if (vallen < paramlen) vallen = paramlen;

				SBUF_MALLOC(resultstr, vallen + 1);
				snprintf(resultstr, resultstr_buflen, "%-*s", paramlen, val);
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
				if (!rrddbs[rrdidx].rrdparamfinal) { p = val; while ((p = strchr(p, ',')) != NULL) *p = '/'; }

				metaval = lookup_meta(val, rrddbs[rrdidx].rrdfn);
				if (metaval) addtobuffer(result, metaval);
			}
			inp += 9;
		}
		else if (strncmp(inp, "@RRDIDX@", 8) == 0) {
			char numstr[10];

			snprintf(numstr, sizeof(numstr), "%d", rrdidx);
			addtobuffer(result, numstr);
			inp += 8;
		}
		else if (strncmp(inp, "@STACKIT@", 9) == 0) {
			/* Contributed by Gildas Le Nadan <gn1@sanger.ac.uk> */

			/* Note that the first entry mustn't contain the keyword
			 * STACK at all, so we need a different treatment for the
			 * first rrdidx.
			 */
			char numstr[10];

			if (rrdidx == 0) {
				strncpy(numstr, "", sizeof(numstr));
			}
			else {
				snprintf(numstr, sizeof(numstr), "STACK");
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

/* Sort mode for the comparator, decided ONCE for the whole set before qsort
 * (rrd_set_sort_mode below). Deciding per pair -- numeric only when BOTH keys
 * of that pair were numeric -- made it intransitive on mixed sets, which is
 * undefined behaviour for qsort(3). */
static int rrd_keys_all_numeric = 0;

/* Aggregate-token parser + RPN emitter. Lives in its own file so
 * web/test-aggregate-tokens.c can include the same source: any future
 * fix here is visible to both without manual mirroring. The file
 * relies on rrddbcount/firstidx/lastidx being declared earlier in
 * this TU (they are -- file-static above). */
#include "aggregate-tokens.inc.c"

static int def_uses_rrd_context(char *def)
{
	return ((strstr(def, "@RRDFN@") != NULL) ||
		(strstr(def, "@RRDIDX@") != NULL) ||
		(strstr(def, "@RRDPARAM@") != NULL) ||
		(strstr(def, "@RRDMETA@") != NULL) ||
		(strstr(def, "@STACKIT@") != NULL));
}

/* Walk the template once, replacing each aggregate token with its RPN
 * expansion, then hand the result to expand_tokens() for the @RRDFN@/
 * @RRDIDX@/@RRDPARAM@/@COLOR@ family. Aggregate tokens are NOT nested:
 * "@AVG:@SUM:t@@" is parsed as @AVG: with name="@SUM:t" and stops at
 * the next @ -- the inner @SUM: is not re-expanded. Same applies to any
 * "@RRDIDX@" written inside an aggregate operand (see comment near
 * add_graphdef_arg). The single-pass design keeps the parser simple at
 * the cost of these two limitations; graphs.cfg blocks shouldn't need
 * nesting because per-RRD selection already happens inside the
 * aggregate. */
static char *expand_aggregate_tokens(char *tpl)
{
	/* result is kept as a file-static strbuffer for the lifetime of the
	 * CGI: the caller (add_graphdef_arg) strdup's the contents before
	 * the next call clobbers them, so the lifetime is "until the next
	 * call." Do NOT hold a pointer into STRBUF(result) across another
	 * expand_aggregate_tokens / expand_tokens call -- the static buffer
	 * is shared and will be cleared. */
	static strbuffer_t *result = NULL;
	char *inp, *p;

	if (!def_uses_aggregate(tpl)) return expand_tokens(tpl);

	if (!result) result = newstrbuffer(2048); else clearstrbuffer(result);

	inp = tpl;
	while (*inp) {
		p = strchr(inp, '@');
		if (p == NULL) {
			addtobuffer(result, inp);
			inp += strlen(inp);
			continue;
		}

		if (p > inp) addtobufferraw(result, inp, (p - inp));

		{
			char *op, *name;
			int oplen, toklen;

			if (is_aggregate_token(p, &op, &name, &oplen, &toklen)) {
				char *endp = p + toklen - 1;

				add_aggregate_rpn(result, op, name, (endp - name));
				inp = p + toklen;
			}
			else {
				addtobuffer(result, "@");
				inp = p + 1;
			}
		}
	}

	return expand_tokens(STRBUF(result));
}

static void add_graphdef_arg(char **rrdargs, int *argi, char *def)
{
	char *expanded = expand_aggregate_tokens(def);
	char *copy = (expanded ? strdup(expanded) : NULL);
	if (!copy) {
		errprintf("strdup of expanded graphdef failed; skipping arg\n");
		return;
	}
	rrdargs[(*argi)++] = copy;
}

/* NOTE on token-pass ordering: expand_aggregate_tokens runs the aggregate
 * pass first, then feeds the result through expand_tokens() for the
 * @RRDFN@/@RRDIDX@/@RRDPARAM@/@COLOR@ family. Consequence: @RRDIDX@ inside
 * an aggregate operand (e.g. @AVG:p@RRDIDX@@) is NOT expanded -- the
 * outer parser stops at the first @ when locating the operand terminator
 * and the @RRDIDX@ token is consumed as part of the surrounding text. If
 * a graphs.cfg author wants the aggregate to span multiple matched RRDs
 * they should use the natural form @AVG:p@ with DEF lines that produce
 * p0, p1, ..., pN; the per-RRD selection happens inside the aggregate. */

static void add_graphdef_rrd_block(char **rrdargs, int *argi, gdef_t *gdef, int firstdef, int lastdef)
{
	int i;

	for (rrdidx=0; (rrdidx < rrddbcount); rrdidx++) {
		if (selected_rrdidx(rrdidx)) {
			for (i = firstdef; (i < lastdef); i++) {
				add_graphdef_arg(rrdargs, argi, gdef->defs[i]);
			}
		}
	}
}

static void add_graphdef_args(char **rrdargs, int *argi, gdef_t *gdef)
{
	int i, have_aggregate = 0, first_aggregate = -1, first_rrd_context = -1;

	for (i = 0; (gdef->defs[i]); i++) {
		if (def_uses_aggregate(gdef->defs[i])) {
			have_aggregate = 1;
			if (first_aggregate == -1) first_aggregate = i;
		}
	}

	if (!have_aggregate) {
		add_graphdef_rrd_block(rrdargs, argi, gdef, 0, i);
		return;
	}

	for (i = 0; (i < first_aggregate); i++) {
		if (def_uses_rrd_context(gdef->defs[i])) {
			if (first_rrd_context == -1) first_rrd_context = i;
		}
		else {
			if (first_rrd_context != -1) {
				add_graphdef_rrd_block(rrdargs, argi, gdef, first_rrd_context, i);
				first_rrd_context = -1;
			}
			add_graphdef_arg(rrdargs, argi, gdef->defs[i]);
		}
	}
	if (first_rrd_context != -1) add_graphdef_rrd_block(rrdargs, argi, gdef, first_rrd_context, i);

	for (i = first_aggregate; (gdef->defs[i]); i++) {
		/* Aggregate is dominant: an aggregate def is emitted once even if it
		 * also references @RRDFN@/@RRDIDX@, since looping would produce
		 * duplicate CDEF names and break rrd_graph. */
		if (def_uses_aggregate(gdef->defs[i])) {
			if (def_uses_rrd_context(gdef->defs[i])) {
				/* Expand @RRDFN@/@RRDIDX@ from the first selected file,
				 * not from wherever the loops above left the global
				 * rrdidx (the NULL-filename sentinel, typically). */
				for (rrdidx = 0; ((rrdidx < rrddbcount) && !selected_rrdidx(rrdidx)); rrdidx++) ;
				if (rrdidx == rrddbcount) continue;	/* no file: nothing to reference */
			}
			add_graphdef_arg(rrdargs, argi, gdef->defs[i]);
		}
		else if (def_uses_rrd_context(gdef->defs[i])) {
			for (rrdidx=0; (rrdidx < rrddbcount); rrdidx++) {
				if (selected_rrdidx(rrdidx)) {
					add_graphdef_arg(rrdargs, argi, gdef->defs[i]);
				}
			}
		}
		else {
			add_graphdef_arg(rrdargs, argi, gdef->defs[i]);
		}
	}
}

#include "namecompare.inc.c"

int rrd_name_compare(const void *v1, const void *v2)
{
	return instance_key_compare(((rrddb_t *)v1)->key, ((rrddb_t *)v2)->key);
}

/* Decide the sort mode ONCE for the whole set, before qsort. Numeric only when
 * every key is numeric, which is what the comparator always intended. A named
 * function rather than a block inside the caller so that the regression test
 * drives this decision instead of a copy of it: a copy keeps passing after the
 * real one goes back to pairwise or lexical-only. */
static void rrd_set_sort_mode(rrddb_t *dbs, int count)
{
	int i;

	rrd_keys_all_numeric = (count > 0);
	for (i = 0; (i < count) && rrd_keys_all_numeric; i++) {
		char *endptr;

		strtol(dbs[i].key, &endptr, 10);
		rrd_keys_all_numeric = ((endptr != dbs[i].key) && (*endptr == '\0'));
	}
}

static int rrd_param_matches_service(const char *param, const char *svc)
{
	if ((param == NULL) || (svc == NULL) || (*svc == '\0')) return 0;

	/* For bundle fall-backs, FNPATTERN group 1 is exactly the service component */
	return (strcmp(param, svc) == 0);
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
		fprintf(output, "  <td align=\"left\" valign=\"top\"> <a href=\"%s&amp;graph=%s&amp;action=selzoom&amp;color=%s\"> <img src=\"%s/zoom.%s\" border=0 alt=\"Zoom graph\" style='padding: 3px'> </a> </td>\n",
			uri, grtype_s, colorname(bgcolor), xgetenv("XYMONSKIN"), xgetenv("IMAGEFILETYPE"));
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
	if (haveupperlimit) {
		snprintf(numbuf, sizeof(numbuf)-1, "%f", upperlimit);
		addtobuffer(result, "&amp;upper=");
		addtobuffer(result, urlencode(numbuf));
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


/*
 * Self-describing statuses (XYMON METRICS markers) create RRD files named
 * <name>.<instance>.rrd without requiring a graphs.cfg entry. When no gdef
 * exists for such a name, synthesize a generic one; its definition lines
 * are generated later from the dataset names of the first matching file,
 * one line per dataset. A hand-written [name] section in graphs.cfg always
 * wins - this is only the fallback.
 */
#define SYNTHETIC_DSMAX 10

static gdef_t *synthetic_gdef(char *name)
{
	gdef_t *newitem;
	size_t patlen;
	int len = strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");

	/* Only for names safe as a filename prefix and regex literal */
	if ((len == 0) || (len > 64) || (name[len] != '\0')) return NULL;

	newitem = (gdef_t *)calloc(1, sizeof(gdef_t));
	newitem->name = strdup(name);
	patlen = strlen(name) + sizeof("^\\.(.+)\\.rrd");
	newitem->fnpat = (char *)malloc(patlen);
	snprintf(newitem->fnpat, patlen, "^%s\\.(.+)\\.rrd", name);
	newitem->title = strdup(name);
	newitem->yaxis = strdup("Value");
	newitem->defs = NULL;	/* generated later from the first matching RRD */

	return newitem;
}

/* Renderer knowledge about well-known units: never policy, only rendering
 * hints. Aliases converge spellings at render time (the wire keeps what the
 * producer said), canon becomes the YAXIS label, and graphopts carries the
 * rrdtool options the unit implies (base 1024 for byte quantities, a fixed
 * exponent for percentages that must not print as "0.9 k"). An unknown
 * unit is fully legal: verbatim label, default rendering. */
static struct unithint_t {
	char *name, *canon, *graphopts;
} unithints[] = {
	{ "B",      "B",      "-b 1024" },
	{ "bytes",  "B",      "-b 1024" },
	{ "KB",     "KB",     "-b 1024" },
	{ "MB",     "MB",     "-b 1024" },
	{ "B/s",    "B/s",    "-b 1024" },
	{ "ms",     "ms",     NULL },
	{ "msec",   "ms",     NULL },
	{ "s",      "s",      NULL },
	{ "sec",    "s",      NULL },
	{ "%",      "%",      "--units-exponent 0" },
	{ "pct",    "%",      "--units-exponent 0" },
	{ NULL, NULL, NULL }
};

static struct unithint_t *unithint_lookup(const char *unit)
{
	int i;
	for (i = 0; (unithints[i].name); i++) {
		if (strcmp(unithints[i].name, unit) == 0) return &unithints[i];
	}
	return NULL;
}

/* The unit declared for `dsname` in a "ds:unit[,...]" spec; malloc'd, or
 * NULL when the spec has no entry for it. */
static char *unitspec_for(const char *spec, const char *dsname)
{
	const char *p = spec;
	size_t dlen = strlen(dsname);

	while (p && *p) {
		const char *colon = strchr(p, ':');
		const char *end = p + strcspn(p, ",");

		if (colon && (colon < end) && ((size_t)(colon - p) == dlen) && (strncmp(p, dsname, dlen) == 0)) {
			char *out = (char *)malloc(end - colon);
			memcpy(out, colon+1, end - colon - 1);
			out[end - colon - 1] = '\0';
			return out;
		}
		p = ((*end == ',') ? end+1 : NULL);
	}
	return NULL;
}

static int synthetic_ds_skipped = 0;	/* nonzero: last synthesis omitted datasets beyond SYNTHETIC_DSMAX */

static char **synthetic_defs(char *rrdfn, gdef_t *gd)
{
	rrd_info_t *info, *iwalk;
	char *dsnames[SYNTHETIC_DSMAX];
	int dscount = 0, i;
	char **defs;
	char buf[320];

	if (!rrdfn) errormsg("No RRD files match this graph");

	info = rrd_info_r(rrdfn);
	if (!info) errormsg("Cannot read RRD file for this graph");

	synthetic_ds_skipped = 0;
	for (iwalk = info; (iwalk); iwalk = iwalk->next) {
		char *bracket;
		size_t nlen;

		if (strncmp(iwalk->key, "ds[", 3) != 0) continue;
		bracket = strchr(iwalk->key+3, ']');
		if (!bracket) continue;
		nlen = bracket - (iwalk->key+3);
		if ((nlen == 0) || (nlen > 64)) continue;

		/* Each dataset appears with several keys - record it once */
		for (i=0; (i < dscount); i++) {
			if ((strlen(dsnames[i]) == nlen) && (strncmp(dsnames[i], iwalk->key+3, nlen) == 0)) break;
		}
		if (i < dscount) continue;

		if (dscount >= SYNTHETIC_DSMAX) {
			/* Keep scanning so the truncation is known, not silent.
			 * (A flag, not a count: unstored names cannot be deduped
			 * across their several info keys.) */
			synthetic_ds_skipped = 1;
			continue;
		}
		dsnames[dscount] = (char *)malloc(nlen+1);
		memcpy(dsnames[dscount], iwalk->key+3, nlen); dsnames[dscount][nlen] = '\0';
		dscount++;
	}
	rrd_info_free(info);

	if (dscount == 0) errormsg("RRD file has no datasets");

	/* Derived YAXIS: when the writer recorded units for this fileset and
	 * every dataset shares one (after alias normalization), that unit is
	 * the axis, and its hint may add rrdtool options (base 1024, fixed
	 * exponent). Absent or disagreeing units = the generic "Value" axis,
	 * exactly as before. */
	if (gd && hostname) {
		char *base = strrchr(rrdfn, '/');
		char *spec = fsidx_units(hostname, (base ? base+1 : rrdfn));

		if (spec) {
			char *common = NULL;
			int agreed = 1;

			for (i = 0; (agreed && (i < dscount)); i++) {
				char *u = unitspec_for(spec, dsnames[i]);
				struct unithint_t *hint = (u ? unithint_lookup(u) : NULL);
				char *canon = (hint ? hint->canon : u);

				if (!u || !(*u)) agreed = 0;	/* absent or empty unit */
				else if (!common) common = strdup(canon);
				else if (strcmp(common, canon)) agreed = 0;
				if (u) free(u);
			}
			/* The axis must be honest for the whole IMAGE, not just the
			 * first file: any other RENDERED instance declaring a
			 * different unit - or none - drops the derivation, but an
			 * off-image instance never vetoes an image whose own
			 * instances agree. The generic "Value" axis never lies
			 * about a curve. */
			{
				int fi;

				for (fi = 0; (agreed && common && (fi < rrddbcount)); fi++) {
					char *ofn = rrddbs[fi].rrdfn;
					char *ospec;

					if (!selected_rrdidx(fi)) continue;
					if (!ofn || (strcmp(ofn, (base ? base+1 : rrdfn)) == 0)) continue;
					ospec = fsidx_units(hostname, ofn);
					if (!ospec) { agreed = 0; continue; }
					{
						char *tok, *osp = NULL;

						for (tok = strtok_r(ospec, ",", &osp); (agreed && tok); tok = strtok_r(NULL, ",", &osp)) {
							char *colon = strrchr(tok, ':');
							struct unithint_t *ohint;

							/* An absent or empty unit vetoes here just as
							 * it does on the first file above */
							if (!colon || !colon[1]) { agreed = 0; continue; }
							ohint = unithint_lookup(colon+1);
							if (strcmp(common, (ohint ? ohint->canon : colon+1))) agreed = 0;
						}
					}
					free(ospec);
				}
			}
			if (agreed && common) {
				struct unithint_t *hint = unithint_lookup(common);

				if (gd->yaxis) free(gd->yaxis);
				gd->yaxis = strdup(common);
				if (hint && hint->graphopts && !gd->graphopts) gd->graphopts = strdup(hint->graphopts);
			}
			if (common) free(common);
			free(spec);
		}
	}

	/* Declared threshold relations (fileset index): an operand DS is a
	 * threshold curve, never a peer metric - excluded from the peer
	 * plot always, and co-plotted threshold-styled only on a
	 * single-instance image (multi-instance images would drown in other
	 * instances' thresholds) unless the gdef says THRESHOLDS OFF.
	 * Literal operands become HRULEs. The declaration never forces a
	 * pixel: this is all derivation, and a hand-written gdef wins. */
	{
		struct threl_t { char *base; char *operand; int warn; int isds; } rel[16];
		int nrel = 0, is_thr[SYNTHETIC_DSMAX] = { 0, };
		int coplot, outi = 0, ndefs, j;
		char *thr = NULL;

		if (gd && hostname && rrdfn) {
			char *bn = strrchr(rrdfn, '/');
			thr = fsidx_thresholds(hostname, (bn ? bn+1 : rrdfn));
		}
		if (thr) {
			char *tok, *sp = NULL;

			for (tok = strtok_r(thr, ",", &sp); (tok && (nrel < 16)); tok = strtok_r(NULL, ",", &sp)) {
				char *c1 = strchr(tok, ':');
				char *c2 = (c1 ? strrchr(tok, ':') : NULL);
				char *expr, *operand, *endp;

				size_t rlen;

				if (!c1 || (c2 == c1)) continue;
				*c1 = '\0'; *c2 = '\0';
				expr = c1+1;
				operand = expr + strspn(expr, "<>=");
				rlen = (size_t)(operand - expr);
				/* Same relop validation as the producer: a corrupt or
				 * hand-edited index must not smuggle in relations the
				 * wire would reject (they would silently suppress
				 * datasets from the graph). */
				if (!(*operand) ||
				    !(((rlen == 1) && ((*expr == '>') || (*expr == '<'))) ||
				      ((rlen == 2) && ((*expr == '>') || (*expr == '<')) && (expr[1] == '=')))) continue;
				rel[nrel].base = tok;
				rel[nrel].operand = operand;
				rel[nrel].warn = (strcasecmp(c2+1, "warn") == 0);
				rel[nrel].isds = 0;
				for (i = 0; (i < dscount); i++) {
					if (strcmp(dsnames[i], operand) == 0) { rel[nrel].isds = 1; is_thr[i] = 1; break; }
				}
				if (!rel[nrel].isds) {
					double hv;

					/* Finite decimal only: "inf"/"nan"/hex would make
					 * rrd_graph reject the whole graph as an HRULE */
					if (strspn(operand, "0123456789.+-") != strlen(operand)) continue;
					errno = 0;
					hv = strtod(operand, &endp);
					if ((endp == operand) || (*endp != '\0')) continue;	/* neither DS nor number */
					/* Overflow only: ERANGE also flags underflow, but a
					 * denormal draws fine as ~0 - only inf is rejected */
					if ((errno == ERANGE) && ((hv == HUGE_VAL) || (hv == -HUGE_VAL))) continue;
				}
				nrel++;
			}
		}
		/* Co-plot on single-instance IMAGES: count the instances on
		 * this render slice, not the whole fileset - a paged view at
		 * one instance per image deserves its thresholds. In emit mode
		 * nothing is selected (0), which is the single-file case. */
		{
			int nsel = 0;
			for (i = 0; (i < rrddbcount); i++) {
				if (selected_rrdidx(i)) nsel++;
			}
			coplot = ((nrel > 0) && (nsel <= 1) && !(gd && xymon_gdef_thresholds_off(gd->name)));
		}

		ndefs = 2*dscount + 1;
		if (coplot) ndefs += 2*nrel;
		defs = (char **)calloc(ndefs, sizeof(char *));
		for (i=0; (i < dscount); i++) {
			if (is_thr[i]) continue;	/* a threshold, not a peer metric */
			snprintf(buf, sizeof(buf), "DEF:v%d@RRDIDX@=@RRDFN@:%s:AVERAGE", i, dsnames[i]);
			defs[outi++] = strdup(buf);
			snprintf(buf, sizeof(buf), "LINE1:v%d@RRDIDX@#@COLOR@:@RRDPARAM@ %s", i, dsnames[i]);
			defs[outi++] = strdup(buf);
		}
		if (coplot) {
			for (j = 0; (j < nrel); j++) {
				char *color = (rel[j].warn ? "FFCC00" : "FF0000");
				char *sevname = (rel[j].warn ? "warn" : "crit");

				if (rel[j].isds) {
					if (snprintf(buf, sizeof(buf), "DEF:t%d@RRDIDX@=@RRDFN@:%s:AVERAGE", j, rel[j].operand) >= (int)sizeof(buf)) continue;
					defs[outi++] = strdup(buf);
					snprintf(buf, sizeof(buf), "LINE1:t%d@RRDIDX@#%s:%s %s", j, color, rel[j].operand, sevname);
					defs[outi++] = strdup(buf);
				}
				else {
					/* Index fields are unbounded short of FSIDX_LINEMAX; a
					 * truncated def would make rrd_graph reject the whole
					 * image, so an oversized token is dropped instead. */
					if (snprintf(buf, sizeof(buf), "HRULE:%s#%s:%s %s (%s)", rel[j].operand, color, rel[j].base, sevname, rel[j].operand) >= (int)sizeof(buf)) continue;
					defs[outi++] = strdup(buf);
				}
			}
		}
		if (outi == 0) {
			/* Corrupt or self-referential relations excluded every
			 * dataset: ignore them and plot all datasets as peers -
			 * an empty def list would break the whole graph. */
			for (i=0; (i < dscount); i++) {
				snprintf(buf, sizeof(buf), "DEF:v%d@RRDIDX@=@RRDFN@:%s:AVERAGE", i, dsnames[i]);
				defs[outi++] = strdup(buf);
				snprintf(buf, sizeof(buf), "LINE1:v%d@RRDIDX@#@COLOR@:@RRDPARAM@ %s", i, dsnames[i]);
				defs[outi++] = strdup(buf);
			}
		}
		defs[outi] = NULL;
		if (thr) free(thr);
	}
	for (i=0; (i < dscount); i++) xfree(dsnames[i]);

	return defs;
}

/* One-shot scaffold mode (--emit-gdef): find the first RRD file of the
 * fileset so the synthesizer can read its datasets. */
static char *emit_find_rrd(char *rrddir, char *name)
{
	DIR *tld;
	struct dirent *hent;
	size_t plen = strlen(name);
	static char result[4096 + NAME_MAX + 2];	/* hostdir + "/" + d_name always fit */

	tld = opendir(rrddir);
	if (!tld) return NULL;
	while ((hent = readdir(tld)) != NULL) {
		char hostdir[4096];
		DIR *hd;
		struct dirent *fent;
		struct stat st;

		if (hent->d_name[0] == '.') continue;
		snprintf(hostdir, sizeof(hostdir), "%s/%s", rrddir, hent->d_name);
		if ((stat(hostdir, &st) != 0) || !S_ISDIR(st.st_mode)) continue;
		hd = opendir(hostdir);
		if (!hd) continue;
		while ((fent = readdir(hd)) != NULL) {
			size_t flen = strlen(fent->d_name);

			if ((flen > plen + 5) &&
			    (strncmp(fent->d_name, name, plen) == 0) && (fent->d_name[plen] == '.') &&
			    (strcmp(fent->d_name + flen - 4, ".rrd") == 0)) {
				snprintf(result, sizeof(result), "%s/%s", hostdir, fent->d_name);
				/* Remember which host the file belongs to, so the
				 * unit lookup (fileset index) works in emit mode */
				if (!hostname) hostname = strdup(hent->d_name);
				closedir(hd); closedir(tld);
				return result;
			}
		}
		closedir(hd);
	}
	closedir(tld);
	return NULL;
}

/* Print the gdef the runtime synthesizer would use for a marker-created
 * fileset, for the admin to capture into graphs.d/ and customize. A
 * one-shot scaffold, never a sync: once edited the definition is the
 * admin's - a hand-written gdef always wins over the synthesized one. */
static int emit_gdef(char *name, char *rrddir)
{
	gdef_t *gdef;
	char *rrdfn;
	char **defs;
	int i;

	gdef = synthetic_gdef(name);
	if (!gdef) {
		fprintf(stderr, "Invalid graph name '%s'\n", name);
		return 1;
	}
	if (!rrddir) rrddir = xgetenv("XYMONRRDS");
	rrdfn = emit_find_rrd(rrddir, name);
	if (!rrdfn) {
		fprintf(stderr, "No RRD files matching '%s.*.rrd' under %s\n", name, rrddir);
		return 1;
	}

	defs = synthetic_defs(rrdfn, gdef);
	printf("[%s]\n", name);
	printf("\tFNPATTERN %s\n", gdef->fnpat);
	printf("\tTITLE %s\n", gdef->title);
	printf("\tYAXIS %s\n", gdef->yaxis);
	/* As a GRAPHOPTIONS line: a bare definition line would reload as ONE
	 * rrdtool argument ("--units-exponent 0" as a single token), which
	 * rrd_graph rejects - the captured gdef must round-trip. */
	if (gdef->graphopts) printf("\tGRAPHOPTIONS %s\n", gdef->graphopts);
	for (i = 0; (defs[i]); i++) printf("\t%s\n", defs[i]);
	printf("\t# Scaffolded from %s only: on multi-file filesets the runtime\n", rrdfn);
	printf("\t# synthesizer suppresses threshold co-plot and cross-checks YAXIS\n");
	printf("\t# units across instances - this capture does neither. Edit to taste.\n");
	if (synthetic_ds_skipped)
		printf("\t# NOTE: the file has more datasets; only the first %d are scaffolded\n", SYNTHETIC_DSMAX);
	return 0;
}

/* The disk-handler family: every file these services write since the
 * encoding cutover carries a canonically ENCODED instance name (the
 * mount/qtree/volume is rrdinstance_encode()d into the filename). For
 * any other service, a capture that merely looks like encoder output
 * (a tcp.http URL with a literal %XX run) is a legacy name and must
 * keep the legacy un-mangling. */
static int gdef_is_diskfamily(char *name)
{
	return ((strncmp(name, "disk", 4) == 0) || (strncmp(name, "inode", 5) == 0) ||
		(strncmp(name, "qtree", 5) == 0) || (strncmp(name, "quotas", 6) == 0) ||
		(strncmp(name, "snapshot", 8) == 0) || (strncmp(name, "tablespace", 10) == 0));
}

static int name_in_list(char **list, int count, char *name)
{
	int i;

	for (i = 0; (i < count); i++)
		if (strcmp(list[i], name) == 0) return 1;
	return 0;
}

void generate_graph(char *gdeffn, char *rrddir, char *graphfn)
{
	gdef_t *gdef = NULL, *gdefuser = NULL;
	int wantsingle = 0;
	int rrdparamisservice = 0;
	int svcrejects = 0;
	DIR *dir;
	time_t now = getcurrenttime(NULL);

	int argi, pcount;
	size_t rrdargs_cap = 0;	/* current allocated entries in rrdargs[] (grows on runtime-expansion overshoot) */

	/* Options for rrd_graph() */
	int  rrdargcount;
	xymon_rrd_argv_item_t *rrdargs = NULL;	/* The full argv[] table of string pointers to arguments */
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
		snprintf(fnam, sizeof(fnam), "%s/etc/graphs.cfg", xgetenv("XYMONHOME"));
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
		if (*(delim+1) == '\0') errormsg("Missing graph service name");
		realservice = strdup(delim+1);

		/* The requested gdef only acts as a fall-back solution so don't set gdef here. */
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
			rrdparamisservice = 1;
		}
		else {
			xymonrrd_t *ldef = find_xymon_rrd(service, NULL);
			if (ldef) {
				for (gdef = gdefs; (gdef && strcmp(ldef->xymonrrdname, gdef->name)); gdef = gdef->next) ;
				wantsingle = 1;
				rrdparamisservice = 1;
			}
		}
	}
	/* Synthesis enumerates ONE host's fileset index; a multi-host
	 * request has no single index to enumerate, and the synthetic
	 * fnpat would defeat the -multi lookup below and then be taken
	 * as a literal filename. Keep the classic failure instead. */
	if ((gdef == NULL) && (hostlist == NULL)) gdef = synthetic_gdef(service);
	if (gdef == NULL) errormsg("Unknown graph requested");
	/* A config section carrying only display keywords (THRESHOLDS,
	 * EXSTALEPATTERN, ...) is metadata, not a definition: the graph
	 * itself stays synthesized, as graphs.cfg(5) documents. Adopt the
	 * synthetic scaffold for whatever the section did not write. A
	 * legacy single-file gdef always has definition lines, so the
	 * empty-defs test cannot misfire on one. */
	if ((hostlist == NULL) && !gdef->fnpat && (!gdef->defs || !gdef->defs[0])) {
		gdef_t *syn = synthetic_gdef(gdef->name);

		if (syn) {
			gdef->fnpat = syn->fnpat;
			if (!gdef->title) gdef->title = syn->title; else free(syn->title);
			if (!gdef->yaxis) gdef->yaxis = syn->yaxis; else free(syn->yaxis);
			if (gdef->defs) free(gdef->defs);
			gdef->defs = NULL;	/* render-time synthesis from the fileset */
			free(syn->name);
			free(syn);
		}
	}
	if (hostlist && (gdef->fnpat == NULL)) {
		SBUF_DEFINE(multiname);

		SBUF_MALLOC(multiname, strlen(gdef->name) + 7);
		snprintf(multiname, multiname_buflen, "%s-multi", gdef->name);
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

		if (hostlist) snprintf(dnam, sizeof(dnam), "%s", xgetenv("XYMONRRDS"));
		else snprintf(dnam, sizeof(dnam), "%s/%s", xgetenv("XYMONRRDS"), hostname);

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
			size_t buflen = strlen(gdef->name) + strlen(".rrd") + 1;

			rrddbs[0].key = strdup(service);
			rrddbs[0].rrdfn = (char *)malloc(buflen);
			snprintf(rrddbs[0].rrdfn, buflen, "%s.rrd", gdef->name);
			rrddbs[0].rrdparam = NULL;
			rrddbs[0].rrdparamfinal = 0;
		}
		else {
			int i, maxlen;
			char paramfmt[20];

			for (i=0, maxlen=0; (i < hostlistsize); i++) {
				if (strlen(hostlist[i]) > maxlen) maxlen = strlen(hostlist[i]);
			}
			snprintf(paramfmt, sizeof(paramfmt), "%%-%ds", maxlen+1);

			for (i=0; (i < hostlistsize); i++) {
				size_t buflen;

				rrddbs[i].key = strdup(service);
				buflen = strlen(hostlist[i]) + strlen(gdef->fnpat) + 2;
				rrddbs[i].rrdfn = (char *)malloc(buflen);
				snprintf(rrddbs[i].rrdfn, buflen, "%s/%s", hostlist[i], gdef->fnpat);

				buflen = maxlen + 2;
				rrddbs[i].rrdparam = (char *)malloc(buflen);
				snprintf(rrddbs[i].rrdparam, buflen, paramfmt, hostlist[i]);
				rrddbs[i].rrdparamfinal = 0;
			}
		}
	}
	else {
		struct dirent *d;
		pcre2_code *pat, *expat = NULL;
		char errmsg[120];
		int err, result;
		PCRE2_SIZE errofs;
		pcre2_match_data *ovector;
		struct stat st;
		time_t now = getcurrenttime(NULL);
		char **decfiles = NULL;
		int decfilecount = 0;

		/* Scan the directory to see what RRD files are there that match */
		dir = opendir("."); if (dir == NULL) errormsg("Unexpected error while accessing RRD directory");

		/* Filenames whose fileset-index record carries a declaration
		 * generation (g=). Only the block writers (XYMON METRICS /
		 * DEVMON RRD) stamp one, and everything they write has a
		 * canonically encoded instance name - so g= membership decides
		 * the decode below where the name shape alone cannot: the
		 * directory-scan seeding puts LEGACY files in the index too,
		 * just without declarations. */
		{
			FILE *idxfd = fopen(".fileset-index", "r");

			if (idxfd) {
				char idxline[FSIDX_LINEMAX];

				while (fgets(idxline, sizeof(idxline), idxfd)) {
					char *nm, *tok, *sp = NULL;

					/* A corrupt overlong line splits into chunks whose
					 * tail could carry a bogus g= token - same defense
					 * as every lib-side index reader. */
					if (fsidx_line_truncated(idxline, idxfd)) continue;
					if (idxline[0] == '#') continue;
					nm = strtok_r(idxline, " \t\r\n", &sp);
					if (!nm) continue;
					while ((tok = strtok_r(NULL, " \t\r\n", &sp)) != NULL) {
						if (strncmp(tok, "g=", 2) != 0) continue;
						decfiles = (char **)realloc(decfiles, (decfilecount+1) * sizeof(char *));
						decfiles[decfilecount++] = strdup(nm);
						break;
					}
				}
				fclose(idxfd);
			}
		}

		/* Setup the pattern to match filenames against */
		pat = pcre2_compile(gdef->fnpat, strlen(gdef->fnpat), PCRE2_CASELESS, &err, &errofs, NULL);
		if (!pat) {
			char msg[8192];

			pcre2_get_error_message(err, errmsg, sizeof(errmsg));
			snprintf(msg, sizeof(msg), "graphs.cfg error, PCRE pattern %s invalid: %s, offset %zu\n",
				 htmlquoted(gdef->fnpat), errmsg, errofs);
			errormsg(msg);
		}
		if (gdef->exfnpat) {
			expat = pcre2_compile(gdef->exfnpat, strlen(gdef->exfnpat), PCRE2_CASELESS, &err, &errofs, NULL);
			if (!expat) {
				char msg[8192];

				pcre2_get_error_message(err, errmsg, sizeof(errmsg));
				snprintf(msg, sizeof(msg), 
					 "graphs.cfg error, PCRE pattern %s invalid: %s, offset %zu\n",
					 htmlquoted(gdef->exfnpat), errmsg, errofs);
				errormsg(msg);
			}
		}

		/* Allocate an initial filename table */
		rrddbsize = 5;
		rrddbs = (rrddb_t *) malloc((rrddbsize+1) * sizeof(rrddb_t));

		ovector = pcre2_match_data_create(30, NULL);
		while ((d = readdir(dir)) != NULL) {
			char *ext;
			char param[PATH_MAX];
			PCRE2_SIZE l = sizeof(param);
			int haveparam;

			/* Ignore dot-files and files with names shorter than ".rrd" */
			if (*(d->d_name) == '.') continue;
			ext = d->d_name + strlen(d->d_name) - strlen(".rrd");
			if ((ext <= d->d_name) || (strcmp(ext, ".rrd") != 0)) continue;

			/* First check the exclude pattern. */
			if (expat) {
				result = pcre2_match(expat, d->d_name, strlen(d->d_name), 0, 0,
						     ovector, NULL);
				if (result >= 0) continue;
			}

			/* Then see if the include pattern matches. */
			result = pcre2_match(pat, d->d_name, strlen(d->d_name), 0, 0,
					     ovector, NULL);
			if (result < 0) continue;
			haveparam = (pcre2_substring_copy_bynumber(ovector, 1, param, &l) == 0);

			if (rrdparamisservice && haveparam) {
				/* Single service out of a bundle (tcp): match against the FNPATTERN
				 * capture, not an unanchored substring - "conn" must not pick up
				 * tcp.proxyconn.rrd (issue #20). */
				if (!rrd_param_matches_service(param, service)) { svcrejects++; continue; }
			}
			else if (wantsingle) {
				/* Resolved to its own gdef (tcp.http -> [http], ncv:slab -> [slab]),
				 * where the capture is a subitem rather than the service - or a
				 * fall-back without a capture group: keep the substring match. */
				if (strstr(d->d_name, service) == NULL) { svcrejects++; continue; }
			}

			/* 
			 * Has it been updated recently (within the past 24 hours) ? 
			 * We don't want old graphs to mess up multi-displays.
			 */
			if (ignorestalerrds && (stat(d->d_name, &st) == 0) && ((now - st.st_mtime) > XYMON_STALE_WINDOW) &&
			    !xymon_gdef_stale_exempt(gdef->name, d->d_name)) {
				continue;
			}

			/* We have a matching file! */
			rrddbs[rrddbcount].rrdfn = strdup(d->d_name);
			rrddbs[rrddbcount].rrdparamfinal = 0;
			if (haveparam) {
				/*
				 * This is ugly, but I cannot find a pretty way of un-mangling
				 * the disk- and http-data that has been molested by the back-end.
				 */
				if ((strcmp(param, ",root") == 0) &&
				    ((strncmp(gdef->name, "disk", 4) == 0) || (strncmp(gdef->name, "inode", 5) == 0)) ) {
					rrddbs[rrddbcount].rrdparam = strdup(",");
				}
				else if ((strcmp(gdef->name, "http") == 0) && (strncmp(param, "http", 4) != 0)) {
					size_t buflen = strlen("http://")+strlen(param)+1;
					rrddbs[rrddbcount].rrdparam = (char *)malloc(buflen);
					snprintf(rrddbs[rrddbcount].rrdparam, buflen, "http://%s", param);
				}
				else {
					/* Reverse rrdinstance_encode() for encoded files (disk,
					 * inode, METRICS blocks): %XX -> original byte. If the
					 * capture is canonical encoder output, the decoded value
					 * is the final legend and must NOT be run through the
					 * legacy comma->slash un-mangling below (a mount like
					 * "/a,b" would otherwise turn back into "/a/b").
					 * "Encoded" is decided by evidence, not name shape: a
					 * g=-declared index record proves a block writer made
					 * the file (decode even escape-free names - the legend
					 * must not flip when a flat instance materializes); the
					 * disk family always encodes, so its captures may take
					 * the canonical-shape probe. Everything else keeps the
					 * old behaviour untouched - a legacy name that merely
					 * looks like encoder output (a URL with a literal %XX
					 * run in a tcp.http filename) is NOT decoded. */
					char *raw = param;
					char *dec;

					/* The stock disk/inode FNPATTERN ("^disk(.*).rrd") was
					 * written for the old "disk,var" names, so on encoded
					 * files it captures the "." separator too (".%2Fvar").
					 * The separator is not part of the instance - absorb it
					 * here, because deployed graphs.cfg copies are not
					 * rewritten by an upgrade. */
					if (((strncmp(gdef->name, "disk", 4) == 0) || (strncmp(gdef->name, "inode", 5) == 0)) &&
					    (raw[0] == '.') && (strchr(raw, '%') != NULL)) raw++;
					if (name_in_list(decfiles, decfilecount, d->d_name)) dec = rrdinstance_decode(raw);
					else if (gdef_is_diskfamily(gdef->name)) dec = rrdinstance_decode_ifencoded(raw);
					else dec = NULL;
					if (dec) {
						rrddbs[rrddbcount].rrdparam = dec;
						rrddbs[rrddbcount].rrdparamfinal = 1;
					}
					else {
						rrddbs[rrddbcount].rrdparam = strdup(raw);
						rrddbs[rrddbcount].rrdparamfinal = 0;
					}
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

		pcre2_code_free(pat);
		if (expat) pcre2_code_free(expat);
		pcre2_match_data_free(ovector);
		closedir(dir);
		{
			int di;

			for (di = 0; (di < decfilecount); di++) xfree(decfiles[di]);
			if (decfiles) xfree(decfiles);
		}
	}
	rrddbs[rrddbcount].key = rrddbs[rrddbcount].rrdfn = rrddbs[rrddbcount].rrdparam = NULL;

	if ((rrddbcount == 0) && svcrejects) {
		if (rrdparamisservice)
			errprintf("showgraph: no RRD file matched service '%s' - check that FNPATTERN group 1 captures the service component\n", service);
		else
			errprintf("showgraph: no RRD file matched service '%s'\n", service);
	}

	/* Sort them so the display looks prettier. */
	rrd_set_sort_mode(rrddbs, rrddbcount);
	qsort(&rrddbs[0], rrddbcount, sizeof(rrddb_t), rrd_name_compare);

	/* Setup the title */
	if (!gdef->title) gdef->title = strdup("");
	if (strncmp(gdef->title, "exec:", 5) == 0) {
		strbuffer_t *cmd = newstrbuffer(0);
		FILE *pfd;
		char *p;
		int i;

		/* The admin's command line (gdef->title+5, from graphs.cfg)
		 * runs verbatim - it is trusted and may carry its own options
		 * or pipes on purpose. Every value appended after it, however,
		 * is CGI- or filename-derived (displayname from the "disp="
		 * parameter, the matched RRD filenames), so each is shell-quoted
		 * and reaches the command as one literal argument, whatever
		 * characters it contains. */
		addtobuffer(cmd, gdef->title+5);
		p = shellquote(displayname); addtobuffer(cmd, " "); addtobuffer(cmd, p); xfree(p);
		p = shellquote(service);     addtobuffer(cmd, " "); addtobuffer(cmd, p); xfree(p);
		p = shellquote(glegend);     addtobuffer(cmd, " "); addtobuffer(cmd, p); xfree(p);
		for (i=0; (i<rrddbcount); i++) {
			if ((firstidx == -1) || ((i >= firstidx) && (i <= lastidx))) {
				p = shellquote(rrddbs[i].rrdfn);
				addtobuffer(cmd, " "); addtobuffer(cmd, p); xfree(p);
			}
		}

		pfd = popen(STRBUF(cmd), "r");
		if (pfd) {
			if (fgets(graphtitle, sizeof(graphtitle), pfd) == NULL) *graphtitle = '\0';
			pclose(pfd);
		}
		freestrbuffer(cmd);

		/* Drop any newline at end of the title */
		p = strchr(graphtitle, '\n'); if (p) *p = '\0';
	}
	else {
		snprintf(graphtitle, sizeof(graphtitle), "%s %s %s", displayname, gdef->title, glegend);
	}

	snprintf(heightopt, sizeof(heightopt), "-h%d", graphheight);
	snprintf(widthopt, sizeof(widthopt), "-w%d", graphwidth);

	/*
	 * Setup the arguments for calling rrd_graph. 
	 * There's up to 16 standard arguments, plus the 
	 * graph-specific ones (which may be repeated if
	 * there are multiple RRD-files to handle).
	 */
	if (gdef->defs == NULL) {
		/* Synthetic gdef: definition lines come from the datasets of the
		 * first SELECTED RRD file - the slice actually being rendered -
		 * so a paged render never references another page's schema. */
		char *synfn = NULL;
		int i;

		for (rrdidx = 0; (rrdidx < rrddbcount); rrdidx++) {
			if (selected_rrdidx(rrdidx) && rrddbs[rrdidx].rrdfn) { synfn = rrddbs[rrdidx].rrdfn; break; }
		}
		for (i = 0; (!synfn && (i < rrddbcount)); i++) synfn = rrddbs[i].rrdfn;
		if (!synfn && (rrddbcount > 0)) {
			/* An entirely-flat fileset: every instance is a virtual
			 * baseline record. Nothing to DEF - the graph is drawn
			 * from HRULEs alone. */
			gdef->defs = (char **)calloc(1, sizeof(char *));
		}
		else gdef->defs = synthetic_defs(synfn, gdef);
	}

	/*
	 * Grab additional rrd_graph options: the gdef's own (which synthesis
	 * above may just have derived from the declared unit - this capture
	 * MUST follow it), else the RRDGRAPHOPTS environment.
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

	for (pcount = 0; (gdef->defs[pcount]); pcount++) { /* count only */ }

	/* The emit-once aggregate pass adds at most one extra slot per def
	 * (the +1 in pcount*(rrddbcount+1)). Runtime @DSIDX@ expansion can
	 * balloon the count further; track capacity so the runtime path can
	 * realloc inside the per-file loop. */
	{
		size_t initial_cap = 16 + pcount*(rrddbcount+1) + useroptcount + 1;
		rrdargs_cap = initial_cap;
		rrdargs = calloc(initial_cap, sizeof(*rrdargs));
	}


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
		snprintf(upperopt, sizeof(upperopt), "-u %f", upperlimit);
		rrdargs[argi++] = upperopt;
	}
	if (havelowerlimit) {
		snprintf(loweropt, sizeof(loweropt), "-l %f", lowerlimit);
		rrdargs[argi++] = loweropt;
	}
	if (haveupperlimit || havelowerlimit) rrdargs[argi++] = "--rigid";

	if (graphstart) snprintf(startopt, sizeof(startopt), "-s %u", (unsigned int) graphstart);
	else snprintf(startopt, sizeof(startopt), "-s %s", period);
	rrdargs[argi++] = startopt;

	if (graphend) {
		snprintf(endopt, sizeof(endopt), "-e %u", (unsigned int) graphend);
		rrdargs[argi++] = endopt;
	}

	for (useroptidx=0; (useroptidx < useroptcount); useroptidx++) {
		char *o = useropts[useroptidx];

		/* When the CGI supplies the Y range (selzoom), a static
		 * -u/-l from GRAPHOPTIONS/RRDGRAPHOPTS would come later in
		 * argv and win rrd_graph's last-option parsing - the zoom box
		 * would never change the range. Same filter as the def-line
		 * scan applies at config load; here the option and its value
		 * are separate (or glued/=-joined) tokens. */
		if (haveupperlimit && ((strcmp(o, "-u") == 0) || (strcmp(o, "--upper-limit") == 0))) { useroptidx++; continue; }
		if (havelowerlimit && ((strcmp(o, "-l") == 0) || (strcmp(o, "--lower-limit") == 0))) { useroptidx++; continue; }
		if (haveupperlimit && ((strncmp(o, "-u", 2) == 0) && o[2] && (strchr("0123456789.-+", o[2]) != NULL))) continue;
		if (havelowerlimit && ((strncmp(o, "-l", 2) == 0) && o[2] && (strchr("0123456789.-+", o[2]) != NULL))) continue;
		if (haveupperlimit && (strncmp(o, "--upper-limit=", 14) == 0)) continue;
		if (havelowerlimit && (strncmp(o, "--lower-limit=", 14) == 0)) continue;
		rrdargs[argi++] = o;
	}

	/* Two paths, intentionally separate:
	 *
	 * - Smoke's runtime @DSIDX@ path (gdef->dsidx_runtime) needs the
	 *   per-RRD DS count from rrd_info_r and may expand a single def
	 *   line into N defs. It runs its own per-file loop and grows
	 *   rrdargs on the fly. Aggregate tokens inside such blocks are
	 *   not supported yet (Phase 3 adds @DSMEDIAN:/etc. that
	 *   aggregate within one file's DSes).
	 *
	 * - Standard path uses trends's add_graphdef_args which splits
	 *   defs into per-RRD-context vs aggregate (the aggregate is
	 *   emitted once, the rest are emitted per RRD). */
	if (gdef->dsidx_runtime) {
		/* Per LINE, in definition order: a line that is dsidx-templated
		 * or uses per-RRD context (@RRDFN@/@RRDIDX@/...) is emitted once
		 * per selected file, expanded with THAT file's DS count; any
		 * other line - plain CDEFs, aggregates, comments - is emitted
		 * exactly once. Re-emitting those per file would duplicate their
		 * vnames, and rrd_graph rejects the whole graph. */
		int i, j;
		int nmin = -1, nmax = 0;
		int *fcount = (int *)calloc((rrddbcount > 0 ? rrddbcount : 1), sizeof(int));

		for (rrdidx=0; (rrdidx < rrddbcount); rrdidx++) {
			if (!selected_rrdidx(rrdidx)) continue;
			fcount[rrdidx] = derive_dscount_for_file(gdef->defs, rrddbs[rrdidx].rrdfn);
			if (fcount[rrdidx] > nmax) nmax = fcount[rrdidx];
			/* Files with no derivable DS count are skipped below when
			 * any sibling is usable, so they must not drag the
			 * emit-once clamp to 0. */
			if ((fcount[rrdidx] > 0) && ((nmin < 0) || (fcount[rrdidx] < nmin))) nmin = fcount[rrdidx];
		}
		if (nmin < 0) nmin = 0;
		if (nmin != nmax)
			dbgprintf("runtime dsidx: matched files disagree on DS count (min %d, max %d); emit-once aggregates clamp to %d\n", nmin, nmax, nmin);

		for (i = 0; gdef->defs[i]; i++) {
			char *body;
			int start, templated;
			char *line = strdup(gdef->defs[i]);

			templated = classify_dsidx_line(line, &body, &start);
			free(line);

			if (templated || def_uses_rrd_context(gdef->defs[i])) {
				for (rrdidx=0; (rrdidx < rrddbcount); rrdidx++) {
					char *one[2];
					char **rt_defs;
					int per_this = 0;
					size_t need;

					if (!selected_rrdidx(rrdidx)) continue;
					/* A file we cannot derive a DS count for (missing
					 * DS prefix, unreadable/corrupt file) cannot
					 * produce valid templated DEFs; emitting its lines
					 * anyway makes rrd_graph reject the WHOLE image,
					 * healthy siblings included. Skip it when any
					 * sibling is usable; if no file is usable, fall
					 * through so the literal template surfaces the
					 * error (documented fail-fast). */
					if ((fcount[rrdidx] == 0) && (nmax > 0)) {
						dbgprintf("runtime dsidx: skipping %s (no derivable DS count)\n", rrddbs[rrdidx].rrdfn);
						continue;
					}
					one[0] = gdef->defs[i]; one[1] = NULL;
					rt_defs = expand_dsidx_array(one, fcount[rrdidx]);
					for (j = 0; rt_defs[j]; j++) per_this++;
					need = (size_t)(argi + per_this + 2 /* timestamp + NULL */);
					if (need > rrdargs_cap) {
						rrdargs_cap = need;
						rrdargs = realloc(rrdargs, rrdargs_cap * sizeof(*rrdargs));
						if (rrdargs == NULL) errormsg("Out of memory expanding graph arguments");
					}

					/* Expose this file's DS count to the aggregate parser
					 * so within-file tokens (@DSMEDIAN:) emit a 1..N
					 * indexed RPN matching the @DSIDX@-produced DEFs. */
					aggregate_dscount = fcount[rrdidx];
					for (j = 0; rt_defs[j]; j++) {
						rrdargs[argi++] = xstrdup(expand_aggregate_tokens(rt_defs[j]));
					}
					aggregate_dscount = 0;

					for (j = 0; rt_defs[j]; j++) free(rt_defs[j]);
					free(rt_defs);
				}
			}
			else {
				size_t need = (size_t)(argi + 3);
				if (need > rrdargs_cap) {
					rrdargs_cap = need;
					rrdargs = realloc(rrdargs, rrdargs_cap * sizeof(*rrdargs));
					if (rrdargs == NULL) errormsg("Out of memory expanding graph arguments");
				}
				/* Emit-once aggregates see the SMALLEST per-file count,
				 * so their RPN only references datasets every file's
				 * DEFs define; with one matched file (the common runtime
				 * shape) that is exactly that file's count. */
				aggregate_dscount = nmin;
				rrdargs[argi++] = xstrdup(expand_aggregate_tokens(gdef->defs[i]));
				aggregate_dscount = 0;
			}
		}
		free(fcount);
	}
	else {
		/* A parse-time-expanded DSCOUNT block may still carry within-file
		 * aggregate tokens (@DSMEDIAN:) - they need the declared count,
		 * or their RPN silently becomes UNKN. */
		aggregate_dscount = gdef->dscount;
		add_graphdef_args(rrdargs, &argi, gdef);
		aggregate_dscount = 0;
	}

	strftime(timestamp, sizeof(timestamp), "COMMENT:Updated\\: %d-%b-%Y %H\\:%M\\:%S", localtime(&now));
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

		/* It works, but we still get the "zoom" magnifying glass which looks odd */
		if (rrddbcount == 0) {
			/* No graph */
			fwrite(blankimg, 1, sizeof(blankimg), stdout);
			return;
		}
	}

	/* All set - generate the graph */
	rrd_clear_error();

	result = xymon_rrd_graph(rrdargcount, rrdargs, &calcpr, &xsize, &ysize, NULL, &ymin, &ymax);

	/*
	 * If we have neither the upper- nor lower-limits of the graph, AND we allow vertical
	 * zooming of this graph, then save the upper/lower limit values and flag that we have
	 * them. The values are then used for the zoom URL we construct later on.
	 */
	if (!haveupperlimit && !havelowerlimit) {
		upperlimit = ymax; haveupperlimit = 1;
		lowerlimit = ymin; havelowerlimit = 1;
	}

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

		snprintf(zoomjsfn, sizeof(zoomjsfn), "%s/web/zoom.js", xgetenv("XYMONHOME"));
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

				zoomrightoffsetp = strstr(buf, zoomrightoffsetmarker);
				if (zoomrightoffsetp) {
					zoomrightoffsetp += strlen(zoomrightoffsetmarker);
					memcpy(zoomrightoffsetp, "30", 2);
				}

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
	char *emitgdef = NULL;		/* --emit-gdef: print the synthesized gdef and exit */

	char *selfURI;

	/* Setup defaults */
	graphwidth = atoi(xgetenv("RRDWIDTH"));
	graphheight = atoi(xgetenv("RRDHEIGHT"));

	/* --emit-gdef is a command-line scaffold mode, not a web request:
	 * skip CGI query parsing (which rejects host-less requests). */
	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--emit-gdef=")) emitgdef = strdup(strchr(argv[argi], '=')+1);
	}

	/* See what we want to do - i.e. get hostname, service and graph-type */
	if (!emitgdef) parse_query();

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
			/* The graph METADATA (THRESHOLDS, FNPATTERN, ...) must
			 * come from the same file as the definitions */
			xymon_gdef_meta_source(gdeffn);
		}
		else if (strcmp(argv[argi], "--save=") == 0) {
			char *p = strchr(argv[argi], '=');
			graphfn = strdup(p+1);
		}
	}

	if (emitgdef) return emit_gdef(emitgdef, rrddir);

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
