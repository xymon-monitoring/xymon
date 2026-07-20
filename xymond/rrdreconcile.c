/*----------------------------------------------------------------------------*/
/* Xymon RRD schema reconciliation tool.                                      */
/*                                                                            */
/* RRD files freeze the schema they were created with; declarations move on. */
/* This tool walks $XYMONRRDS and compares each file against what is         */
/* currently declared: the consolidation functions read by matching          */
/* graphs.cfg definitions (as at create time in xymond_rrd), and the         */
/* heartbeats recorded in the fileset index from the producer's DS specs.    */
/* Divergence is repaired with "rrdtool tune" - added archives fill forward  */
/* only (no backfill, no approximate seeding), changed heartbeats take       */
/* effect from now. By default the plan is only printed; --apply executes.   */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                          */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),      */
/* version 2. See the file "COPYING" for details.                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

#include "libxymon.h"

#define MAXRRA 64
#define MAXDS  64

typedef struct rrainfo_t {
	char cf[20];
	int steps, rows;
	double xff;
} rrainfo_t;

typedef struct dsinfo_t {
	char name[32];
	long heartbeat;
} dsinfo_t;

static char *rrdtoolcmd = "rrdtool";
static int doapply = 0;
static int nfiles = 0, ndiverged = 0, ntuned = 0, nfailed = 0;

/* Single-quote a path for the shell commands we hand to rrdtool. */
static void shellquote(strbuffer_t *out, const char *s)
{
	addtobuffer(out, "'");
	while (*s) {
		if (*s == '\'') addtobuffer(out, "'\\''");
		else addtobufferraw(out, (char *)s, 1);
		s++;
	}
	addtobuffer(out, "'");
}

/* Read one file's live schema via "rrdtool info". Returns 0 when the
 * file cannot be inspected (not an RRD, unreadable, rrdtool missing).
 * Sets *rraoverflow when the file has archives past MAXRRA, i.e. the
 * rra[] view is incomplete. */
static int read_schema(char *fpath, dsinfo_t *ds, int *dscount, rrainfo_t *rra, int *rracount, int *rraoverflow)
{
	strbuffer_t *cmd = newstrbuffer(0);
	FILE *fd;
	char line[1024];

	addtobuffer(cmd, rrdtoolcmd);
	addtobuffer(cmd, " info ");
	shellquote(cmd, fpath);
	addtobuffer(cmd, " 2>/dev/null");

	*dscount = *rracount = *rraoverflow = 0;
	fd = popen(STRBUF(cmd), "r");
	freestrbuffer(cmd);
	if (!fd) return 0;
	while (fgets(line, sizeof(line), fd)) {
		char name[32], cf[20];
		int idx; long lval; double dval;

		if ((sscanf(line, "rra[%d]", &idx) == 1) && (idx >= MAXRRA)) {
			*rraoverflow = 1;
		}
		else if ((sscanf(line, "ds[%31[^]]].minimal_heartbeat = %ld", name, &lval) == 2) && (*dscount < MAXDS)) {
			strncpy(ds[*dscount].name, name, sizeof(ds[0].name)); ds[*dscount].name[sizeof(ds[0].name)-1] = '\0';
			ds[*dscount].heartbeat = lval;
			(*dscount)++;
		}
		else if ((sscanf(line, "rra[%d].cf = \"%19[^\"]\"", &idx, cf) == 2) && (idx >= 0) && (idx < MAXRRA)) {
			strncpy(rra[idx].cf, cf, sizeof(rra[0].cf)); rra[idx].cf[sizeof(rra[0].cf)-1] = '\0';
			if (idx >= *rracount) *rracount = idx+1;
		}
		else if ((sscanf(line, "rra[%d].rows = %ld", &idx, &lval) == 2) && (idx >= 0) && (idx < MAXRRA)) {
			rra[idx].rows = (int)lval;
			if (idx >= *rracount) *rracount = idx+1;
		}
		else if ((sscanf(line, "rra[%d].pdp_per_row = %ld", &idx, &lval) == 2) && (idx >= 0) && (idx < MAXRRA)) {
			rra[idx].steps = (int)lval;
			if (idx >= *rracount) *rracount = idx+1;
		}
		else if ((sscanf(line, "rra[%d].xff = %lf", &idx, &dval) == 2) && (idx >= 0) && (idx < MAXRRA)) {
			rra[idx].xff = dval;
			if (idx >= *rracount) *rracount = idx+1;
		}
	}
	return (pclose(fd) == 0) && (*dscount > 0) && (*rracount > 0);
}

/* Reconcile one RRD file. hbspec is the index's h= record ("ds:hb[,...]")
 * or NULL when the file predates heartbeat recording. */
static void process_file(char *hostname, char *hostdir, char *fn, char *hbspec)
{
	char fpath[PATH_MAX];
	dsinfo_t ds[MAXDS];
	rrainfo_t rra[MAXRRA];
	int dscount, rracount, rraoverflow, i;
	int existing = 0, wanted, missing;
	strbuffer_t *tuneargs;

	snprintf(fpath, sizeof(fpath), "%s/%s", hostdir, fn);
	memset(rra, 0, sizeof(rra));
	nfiles++;
	if (!read_schema(fpath, ds, &dscount, rra, &rracount, &rraoverflow)) {
		errprintf("%s/%s: cannot read schema, skipped\n", hostname, fn);
		return;
	}
	tuneargs = newstrbuffer(0);

	/* Heartbeats: the recorded declaration is authoritative */
	if (hbspec) {
		char *spec = strdup(hbspec), *tok, *sp = NULL;

		tok = strtok_r(spec, ",", &sp);
		while (tok) {
			char *colon = strrchr(tok, ':');
			if (colon && (colon > tok)) {
				long declared = atol(colon+1);
				*colon = '\0';
				for (i = 0; (i < dscount) && strcmp(ds[i].name, tok); i++) ;
				/* The name rides an unquoted system() argument. The
				 * strcmp gate above already limits it to names read
				 * off the live file (rrdtool constrains those to
				 * [A-Za-z0-9_]), but enforce the charset HERE so the
				 * safety is local, not inherited. */
				if (strspn(tok, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != strlen(tok)) i = dscount;
				if ((declared > 0) && (i < dscount) && (ds[i].heartbeat != declared)) {
					char arg[80];
					snprintf(arg, sizeof(arg), " --heartbeat %s:%ld", tok, declared);
					addtobuffer(tuneargs, arg);
				}
			}
			tok = strtok_r(NULL, ",", &sp);
		}
		xfree(spec);
	}

	/* Archives: the union of CFs read by matching graph definitions.
	 * 0 = no matching gdef declares any; the stock set is left alone. */
	for (i = 0; (i < rracount); i++) {
		if (strcmp(rra[i].cf, "AVERAGE") == 0) existing |= XYMON_CF_AVERAGE;
		else if (strcmp(rra[i].cf, "MIN") == 0) existing |= XYMON_CF_MIN;
		else if (strcmp(rra[i].cf, "MAX") == 0) existing |= XYMON_CF_MAX;
		else if (strcmp(rra[i].cf, "LAST") == 0) existing |= XYMON_CF_LAST;
	}
	if (rraoverflow) {
		/* Archives past MAXRRA make this an incomplete view - deriving
		 * "missing" CFs from it would append duplicates on every run. */
		errprintf("%s/%s: more than %d RRAs, archive reconciliation skipped\n", hostname, fn, MAXRRA);
		wanted = 0;
	}
	else wanted = xymon_gdef_cfs_forfile(fn);
	missing = (wanted & ~existing);
	if (missing) {
		struct { int bit; char *name; } cfmap[] = {
			{ XYMON_CF_AVERAGE, "AVERAGE" }, { XYMON_CF_MIN, "MIN" },
			{ XYMON_CF_MAX, "MAX" }, { XYMON_CF_LAST, "LAST" }, { 0, NULL }
		};
		int m;

		/* Clone every AVERAGE archive's geometry per missing CF - the
		 * same shape the writer would have created the file with. The
		 * new archives start empty and fill forward only. */
		for (m = 0; (cfmap[m].name); m++) {
			if (!(missing & cfmap[m].bit)) continue;
			for (i = 0; (i < rracount); i++) {
				char arg[120];
				if (strcmp(rra[i].cf, "AVERAGE") || (rra[i].rows <= 0) || (rra[i].steps <= 0)) continue;
				snprintf(arg, sizeof(arg), " RRA:%s:%g:%d:%d", cfmap[m].name, rra[i].xff, rra[i].steps, rra[i].rows);
				addtobuffer(tuneargs, arg);
			}
		}
	}

	if (STRBUFLEN(tuneargs)) {
		strbuffer_t *cmd = newstrbuffer(0);

		ndiverged++;
		addtobuffer(cmd, rrdtoolcmd);
		addtobuffer(cmd, " tune ");
		shellquote(cmd, fpath);
		addtobuffer(cmd, STRBUF(tuneargs));
		if (!doapply) {
			printf("would run: %s\n", STRBUF(cmd));
		}
		else {
			printf("running: %s\n", STRBUF(cmd));
			if (system(STRBUF(cmd)) == 0) ntuned++;
			else { nfailed++; errprintf("%s/%s: rrdtool tune failed\n", hostname, fn); }
		}
		freestrbuffer(cmd);
	}
	freestrbuffer(tuneargs);
}

static void process_host(char *rrddir, char *hostname)
{
	char hostdir[PATH_MAX], idxfn[PATH_MAX];
	FILE *fd;
	char line[FSIDX_LINEMAX];

	if (((size_t)snprintf(hostdir, sizeof(hostdir), "%s/%s", rrddir, hostname) >= sizeof(hostdir)) ||
	    ((size_t)snprintf(idxfn, sizeof(idxfn), "%s/.fileset-index", hostdir) >= sizeof(idxfn))) {
		errprintf("host directory path for %s exceeds PATH_MAX - skipped\n", hostname);
		return;
	}
	fd = fopen(idxfn, "r");
	if (!fd) {
		/* No index (host predates it, or non-host directory): fall back
		 * to the files themselves - RRA reconciliation still works,
		 * heartbeat reconciliation needs the index's h= records. */
		DIR *dir = opendir(hostdir);
		struct dirent *d;

		if (!dir) return;
		while ((d = readdir(dir)) != NULL) {
			size_t len = strlen(d->d_name);
			if ((len > 4) && (strcmp(d->d_name + len - 4, ".rrd") == 0))
				process_file(hostname, hostdir, d->d_name, NULL);
		}
		closedir(dir);
		return;
	}

	while (fgets(line, sizeof(line), fd)) {
		char *name, *tok, *hb, *sp = NULL;
		char fpath[PATH_MAX];
		struct stat st;

		if (line[0] == '#') continue;
		name = strtok_r(line, " \t\r\n", &sp);
		if (!name || !strtok_r(NULL, " \t\r\n", &sp)) continue;
		/* Index entries are bare basenames by construction; a corrupt
		 * line containing '/' (which also covers "..") would escape the
		 * host directory once spliced into the path below. */
		if (strchr(name, '/')) {
			errprintf("%s: index entry '%s' contains '/', skipped\n", hostname, name);
			continue;
		}
		hb = NULL;
		while ((tok = strtok_r(NULL, " \t\r\n", &sp)) != NULL) {
			if (strncmp(tok, "h=", 2) == 0) hb = tok+2;
		}
		/* A record whose file is gone has nothing to reconcile */
		if ((size_t)snprintf(fpath, sizeof(fpath), "%s/%s", hostdir, name) >= sizeof(fpath)) continue;
		if ((stat(fpath, &st) != 0) || !S_ISREG(st.st_mode)) continue;
		process_file(hostname, hostdir, name, hb);
	}
	fclose(fd);
}

int main(int argc, char *argv[])
{
	char *rrddir = NULL, *hostfilter = NULL, *envarea = NULL;
	DIR *dir;
	struct dirent *d;
	int argi;

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
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
		else if (argnmatch(argv[argi], "--host=")) {
			char *p = strchr(argv[argi], '=');
			hostfilter = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--config=")) {
			char *p = strchr(argv[argi], '=');
			xymon_gdef_meta_source(p+1);
		}
		else if (argnmatch(argv[argi], "--rrdtool=")) {
			char *p = strchr(argv[argi], '=');
			rrdtoolcmd = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--apply") == 0) {
			doapply = 1;
		}
		else if (strcmp(argv[argi], "--dry-run") == 0) {
			doapply = 0;
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else {
			printf("Usage: %s [--env=FILE] [--rrddir=DIR] [--host=NAME] [--config=graphs.cfg] [--rrdtool=PATH] [--apply]\n", argv[0]);
			printf("\nCompares every RRD file against the currently declared schema\n");
			printf("(archive CFs from graphs.cfg DEF lines, heartbeats from the\n");
			printf("fileset index) and reconciles divergence with 'rrdtool tune'.\n");
			printf("Added archives fill forward only. Adding archives with rrdtool\n");
			printf("tune needs rrdtool 1.5 or later.\n");
			printf("Default is a dry run printing the plan; --apply executes it.\n");
			return (strcmp(argv[argi], "--help") == 0) ? 0 : 1;
		}
	}

	if (!rrddir) rrddir = xgetenv("XYMONRRDS");
	if (!rrddir || !(*rrddir)) {
		errprintf("No RRD directory - set XYMONRRDS (--env=xymonserver.cfg) or use --rrddir\n");
		return 1;
	}

	dir = opendir(rrddir);
	if (!dir) {
		errprintf("Cannot open RRD directory %s: %s\n", rrddir, strerror(errno));
		return 1;
	}
	while ((d = readdir(dir)) != NULL) {
		char hostdir[PATH_MAX];
		struct stat st;

		if (d->d_name[0] == '.') continue;
		if (hostfilter && strcasecmp(d->d_name, hostfilter)) continue;
		snprintf(hostdir, sizeof(hostdir), "%s/%s", rrddir, d->d_name);
		if ((stat(hostdir, &st) != 0) || !S_ISDIR(st.st_mode)) continue;
		process_host(rrddir, d->d_name);
	}
	closedir(dir);

	printf("%d files scanned, %d diverged%s\n", nfiles, ndiverged,
	       (doapply ? "" : " (dry run - use --apply to reconcile)"));
	if (doapply && ndiverged) printf("%d tuned, %d failed\n", ntuned, nfailed);
	return (nfailed ? 1 : 0);
}
