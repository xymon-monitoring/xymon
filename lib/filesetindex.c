/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* The writer-kept fileset index. See filesetindex.h for the contract.       */
/*                                                                            */
/* Writer model: an in-memory tree per host, seeded on first touch from the  */
/* existing index file - or, when none exists, from a one-off scan of the    */
/* host's RRD directory (the rebuild path after deletion/corruption).        */
/* Flushes are atomic (tmp + rename) and merge with the on-disk file under   */
/* flock, because the status- and data-channel xymond_rrd instances both     */
/* write the same hosts; last-write timestamps merge by max.                 */
/*                                                                            */
/* Write economics: a real RRD file's freshness is already durable - it is   */
/* the file's own mtime - so advancing it never dirties the index. Only      */
/* index-only state does: schema declarations flush immediately, and a       */
/* plain new-file entry only joins an index that already exists. A host      */
/* with no self-describing state never materializes an index file at all.    */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                          */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),      */
/* version 2. See the file "COPYING" for details.                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char filesetindex_rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include "libxymon.h"

#define FSIDX_NAME ".fileset-index"
#define FSIDX_HEADER "# xymon fileset index v1"
/* FSIDX_SPECMAX / FSIDX_LINEMAX live in filesetindex.h (readers outside
 * this file need the line bound too). */
#if defined(MAX_LINE_LEN) && (MAX_LINE_LEN > 16384)
#error "FSIDX_LINEMAX (filesetindex.h) assumes MAX_LINE_LEN <= 16384"
#endif

typedef struct fsidx_host_t {
	void *files;		/* rrdfn (char*) -> (time_t) last data write, cast in a slot */
	int dirty_new;		/* index-only state changed (schema): flush now */
	int dirty_add;		/* a plain entry was added: flush only an EXISTING index */
	int needseed;		/* dropped host: reseed from disk on next touch */
} fsidx_host_t;

typedef struct fsidx_entry_t {
	char *fn;
	time_t ts;
	char *units;		/* "ds:unit[,ds:unit...]" or NULL */
	char *heartbeats;	/* "ds:heartbeat[,...]" as currently declared, or NULL */
	char *thresholds;	/* "base:relop-operand:sev[,...]" or NULL */
	char *dsnames;		/* "ds1,ds2,...": positional DS names */
	time_t gen;		/* when the schema fields (u/h/d/t) were last declared live */
} fsidx_entry_t;

static void *fsidx_hosts = NULL;	/* hostname -> fsidx_host_t */
static char *fsidx_pending_units = NULL;	/* sticky per-block writer state, see fsidx_set_units() */
static char *fsidx_pending_thresholds = NULL;	/* ditto, see fsidx_set_thresholds() */
static char *fsidx_pending_dsnames = NULL;	/* ditto: "ds1,ds2" - positional names for flat values */
static char *fsidx_pending_heartbeats = NULL;	/* ditto: "ds:heartbeat[,...]" - the declared heartbeats */

static int fsidx_path(char *buf, size_t bufsz, const char *rrddir, const char *hostname, const char *suffix)
{
	/* A silently truncated path would make the index, its ".lock" and
	 * its ".tmp.<pid>" collapse into the SAME name, so rename/unlink
	 * would hit the wrong file. Refuse instead. */
	if ((size_t)snprintf(buf, bufsz, "%s/%s/%s%s", rrddir, hostname, FSIDX_NAME, suffix) >= bufsz) {
		errprintf("fileset index: path for host '%s' exceeds PATH_MAX, ignored\n", hostname);
		return -1;
	}
	return 0;
}

/* Timestamps off an index file are untrusted digit strings: atol() on
 * an overflowing value is UB. Garbage or out-of-range parses to 0,
 * which every caller already treats as invalid. */
static time_t fsidx_parse_ts(const char *s)
{
	char *endp;
	long v;

	if (!s || !isdigit((int)(unsigned char)*s)) return 0;
	errno = 0;
	v = strtol(s, &endp, 10);
	if ((errno == ERANGE) || (v <= 0) || (*endp != '\0')) return 0;
	return (time_t)v;
}

/* fgets() splits a physical line longer than the buffer into chunks, and
 * a tail chunk can parse as a plausible "name ts" record - which the
 * loader would then republish durably on the next flush, laundering
 * corruption into permanence. Returns 1 (and discards the tail) when the
 * just-read chunk was not a complete line. Public: every index reader
 * (in-tree or CGI-side) must apply the same defense. */
int fsidx_line_truncated(char *line, FILE *fd)
{
	int ch;

	if (strchr(line, '\n') != NULL) return 0;
	if (feof(fd)) return 0;	/* last line without newline: complete */
	while (((ch = fgetc(fd)) != EOF) && (ch != '\n')) ;
	return 1;
}

/* Hostnames reach this API raw off the channel and are interpolated into
 * "$XYMONRRDS/<hostname>/...": one carrying '/' would walk out of the RRD
 * tree and flock()/unlink()/rewrite foreign files. Every public entry
 * point that turns a hostname into a path rejects it here. */
static int fsidx_valid_hostname(const char *hostname)
{
	if (!hostname || !(*hostname)) return 0;
	if (strchr(hostname, '/') != NULL) {
		errprintf("fileset index: hostname '%s' contains '/', ignored\n", hostname);
		return 0;
	}
	/* "." and ".." need no '/' to escape: they would make the parent
	 * (or the RRD root itself) the lock/unlink/rewrite target. */
	if ((strcmp(hostname, ".") == 0) || (strcmp(hostname, "..") == 0)) {
		errprintf("fileset index: hostname '%s' is not a host directory, ignored\n", hostname);
		return 0;
	}
	return 1;
}

/* rrdfns become the first token of a space-separated index record; one
 * carrying a blank or line break would split the record on the way back
 * in (its tail parsing as bogus extra records), and a leading '#' reads
 * back as a comment. No writer produces such names (setupfn maps blanks,
 * instances are percent-encoded), so reject loudly at the entry points
 * rather than corrupt the file. */
static int fsidx_valid_rrdfn(const char *rrdfn)
{
	if (!rrdfn || !(*rrdfn)) return 0;
	if ((*rrdfn == '#') || (strpbrk(rrdfn, " \t\r\n") != NULL)) {
		errprintf("fileset index: rrd filename '%s' cannot be indexed, ignored\n", rrdfn);
		return 0;
	}
	return 1;
}

/* strong units (a live write's declaration) replace what the entry has;
 * weak units (merged from the on-disk file) only fill an empty slot. */
static void fsidx_set(fsidx_host_t *h, const char *fn, time_t ts, const char *units, int strongunits)
{
	xtreePos_t handle = xtreeFind(h->files, (char *)fn);
	fsidx_entry_t *e;

	if (handle == xtreeEnd(h->files)) {
		e = (fsidx_entry_t *)xcalloc(1, sizeof(fsidx_entry_t));
		e->fn = xstrdup(fn);
		e->ts = ts;
		xtreeAdd(h->files, e->fn, e);
		h->dirty_add = 1;
	}
	else {
		e = (fsidx_entry_t *)xtreeData(h->files, handle);
		/* A real file's freshness is its own mtime - durable for free,
		 * so advancing the in-memory ts never dirties the index. */
		if (ts > e->ts) e->ts = ts;
	}

	if (units && (strongunits || !e->units)) {
		if (!e->units || strcmp(e->units, units)) {
			if (e->units) xfree(e->units);
			e->units = xstrdup(units);
			h->dirty_new = 1;	/* schema info: flush immediately */
		}
	}
}

static void fsidx_set_entry_thresholds(fsidx_host_t *h, const char *fn, const char *thr, int strong)
{
	xtreePos_t handle = xtreeFind(h->files, (char *)fn);
	fsidx_entry_t *e;

	if (handle == xtreeEnd(h->files)) return;
	e = (fsidx_entry_t *)xtreeData(h->files, handle);
	if (thr && (strong || !e->thresholds)) {
		if (!e->thresholds || strcmp(e->thresholds, thr)) {
			if (e->thresholds) xfree(e->thresholds);
			e->thresholds = xstrdup(thr);
			h->dirty_new = 1;
		}
	}
}

static void fsidx_set_entry_heartbeats(fsidx_host_t *h, const char *fn, const char *hb, int strong)
{
	xtreePos_t handle = xtreeFind(h->files, (char *)fn);
	fsidx_entry_t *e;

	if (handle == xtreeEnd(h->files)) return;
	e = (fsidx_entry_t *)xtreeData(h->files, handle);
	if (hb && (strong || !e->heartbeats)) {
		if (!e->heartbeats || strcmp(e->heartbeats, hb)) {
			if (e->heartbeats) xfree(e->heartbeats);
			e->heartbeats = xstrdup(hb);
			h->dirty_new = 1;
		}
	}
}

/* Replace one schema field outright with a declaration's value - NULL
 * retracts the field. Returns 1 when the value changed. */
static int fsidx_adopt_field(char **slot, const char *val)
{
	if (!val && !*slot) return 0;
	if (val && *slot && (strcmp(*slot, val) == 0)) return 0;
	if (*slot) xfree(*slot);
	*slot = (val ? xstrdup(val) : NULL);
	return 1;
}

/* Merge the on-disk index (possibly written by the other channel's writer)
 * into the in-memory tree. The schema fields (u/h/d/t) merge by their
 * declaration timestamp (g=): a NEWER disk bundle replaces ours outright
 * - including fields it no longer carries, which are retracted - an
 * older or equal one is ignored, and only g-LESS entries keep the legacy
 * weak-fill of empty slots. This is what stops the two writers from
 * ping-ponging a changed spec: the stale process adopts instead of
 * republishing. Unknown trailing fields are ignored - future versions
 * carry record extensions there. */
static void fsidx_load_file(fsidx_host_t *h, const char *fn)
{
	FILE *fd = fopen(fn, "r");
	char line[FSIDX_LINEMAX];

	if (!fd) return;
	while (fgets(line, sizeof(line), fd)) {
		char *name, *tsstr, *tok, *units, *thr, *dsn, *hb, *sp = NULL;
		time_t ts, gen;

		if (fsidx_line_truncated(line, fd)) {
			errprintf("fileset index: discarding overlong record in %s\n", fn);
			continue;
		}
		if (line[0] == '#') continue;
		name = strtok_r(line, " \t\r\n", &sp);
		tsstr = (name ? strtok_r(NULL, " \t\r\n", &sp) : NULL);
		if (!name || !tsstr) continue;
		ts = fsidx_parse_ts(tsstr);
		if (ts <= 0) continue;
		units = NULL; thr = NULL; dsn = NULL; hb = NULL; gen = 0;
		while ((tok = strtok_r(NULL, " \t\r\n", &sp)) != NULL) {
			if (strncmp(tok, "u=", 2) == 0) units = tok+2;
			else if (strncmp(tok, "h=", 2) == 0) hb = tok+2;
			else if (strncmp(tok, "t=", 2) == 0) thr = tok+2;
			else if (strncmp(tok, "d=", 2) == 0) dsn = tok+2;
			else if (strncmp(tok, "g=", 2) == 0) gen = fsidx_parse_ts(tok+2);
			/* unknown fields (including the retired b= baselines):
			 * record extensions, ignored */
		}
		/* The writers cap every spec at FSIDX_SPECMAX - that is what
		 * makes FSIDX_LINEMAX an invariant. A hand-edited/corrupt file
		 * must not smuggle an oversized field back in, or the NEXT
		 * flush writes a record that splits on every later read. */
		if (units && (strlen(units) > FSIDX_SPECMAX)) units = NULL;
		if (hb && (strlen(hb) > FSIDX_SPECMAX)) hb = NULL;
		if (thr && (strlen(thr) > FSIDX_SPECMAX)) thr = NULL;
		if (dsn && (strlen(dsn) > FSIDX_SPECMAX)) dsn = NULL;
		/* A real file's persisted ts is only as fresh as the last
		 * index-worthy flush - the file's own mtime is the durable
		 * freshness. The index lives in the host's RRD directory, so
		 * the record's file is a sibling of fn. */
		{
			char fpath[PATH_MAX];
			char *lastslash = strrchr(fn, '/');
			struct stat fst;

			if (lastslash &&
			    ((size_t)snprintf(fpath, sizeof(fpath), "%.*s/%s", (int)(lastslash - (char *)fn), fn, name) < sizeof(fpath)) &&
			    (stat(fpath, &fst) == 0) && (fst.st_mtime > ts)) ts = fst.st_mtime;
		}
		fsidx_set(h, name, ts, NULL, 0);
		{
			xtreePos_t eh = xtreeFind(h->files, name);
			fsidx_entry_t *e;

			if (eh == xtreeEnd(h->files)) continue;
			e = (fsidx_entry_t *)xtreeData(h->files, eh);
			if (gen > e->gen) {
				/* Newer declaration on disk: adopt the bundle */
				int changed = 0;
				changed |= fsidx_adopt_field(&e->units, units);
				changed |= fsidx_adopt_field(&e->heartbeats, hb);
				changed |= fsidx_adopt_field(&e->dsnames, dsn);
				changed |= fsidx_adopt_field(&e->thresholds, thr);
				e->gen = gen;
				if (changed) h->dirty_new = 1;
			}
			else if ((gen == e->gen) && (gen == 0)) {
				/* Legacy g-less entries only. A NONZERO equal
				 * generation owns its whole bundle just like a newer
				 * one, so it must not weak-fill: a field retracted in
				 * the same wall-clock second it was declared (the
				 * generation is second-granular) would be resurrected
				 * from our own just-published copy. */
				if (units) fsidx_set(h, name, ts, units, 0);
				if (hb) fsidx_set_entry_heartbeats(h, name, hb, 0);
				if (thr) fsidx_set_entry_thresholds(h, name, thr, 0);
				if (dsn && !e->dsnames) e->dsnames = xstrdup(dsn);
			}
			/* gen < e->gen (or a nonzero tie): ours is the current
			 * declaration, ignore disk */
		}
	}
	fclose(fd);
}

/* One-off rebuild: seed from the RRD files actually on disk. Runs only when
 * a host has no index file at all (first run, deletion, corruption). */
static void fsidx_scan_dir(fsidx_host_t *h, const char *rrddir, const char *hostname)
{
	char dirname[PATH_MAX];
	DIR *dir;
	struct dirent *d;

	if ((size_t)snprintf(dirname, sizeof(dirname), "%s/%s", rrddir, hostname) >= sizeof(dirname)) return;
	dir = opendir(dirname);
	if (!dir) return;
	while ((d = readdir(dir)) != NULL) {
		size_t len = strlen(d->d_name);
		char fpath[PATH_MAX];
		struct stat st;

		if ((len < 5) || (strcmp(d->d_name + len - 4, ".rrd") != 0)) continue;
		if ((size_t)snprintf(fpath, sizeof(fpath), "%s/%s", dirname, d->d_name) >= sizeof(fpath)) continue;
		if ((stat(fpath, &st) != 0) || !S_ISREG(st.st_mode)) continue;
		fsidx_set(h, d->d_name, st.st_mtime, NULL, 0);
	}
	closedir(dir);
}

static fsidx_host_t *fsidx_gethost(char *rrddir, char *hostname)
{
	xtreePos_t handle;
	fsidx_host_t *h;

	if (!fsidx_hosts) fsidx_hosts = xtreeNew(strcasecmp);
	handle = xtreeFind(fsidx_hosts, hostname);
	if (handle != xtreeEnd(fsidx_hosts)) {
		h = (fsidx_host_t *)xtreeData(fsidx_hosts, handle);
		/* The tree is case-insensitive but paths are not: every FS
		 * operation for this host must use ONE spelling (the first
		 * seen), or case-variant senders split the index across
		 * case-variant files. */
		hostname = (char *)xtreeKey(fsidx_hosts, handle);
		/* A dropped host that re-appears (rename back, re-added host)
		 * must start from what is actually on disk, not from the stale
		 * emptied tree - fall through to the seed below. */
		if (!h->needseed) return h;
		h->needseed = 0;
	}
	else {
		h = (fsidx_host_t *)xcalloc(1, sizeof(fsidx_host_t));
		h->files = xtreeNew(strcmp);
		xtreeAdd(fsidx_hosts, xstrdup(hostname), h);
	}

	/* Seed: prefer the existing index; else scan the directory once */
	{
		char fn[PATH_MAX];
		struct stat st;
		int hadfile = ((fsidx_path(fn, sizeof(fn), rrddir, hostname, "") == 0) &&
			       (stat(fn, &st) == 0));

		int loaded;

		if (hadfile) fsidx_load_file(h, fn);
		loaded = (xtreeFirst(h->files) != xtreeEnd(h->files));
		/* An absent file - or one that yielded no entries (crash
		 * leftovers, corruption) - triggers the one-off rebuild scan */
		if (!loaded) fsidx_scan_dir(h, rrddir, hostname);
		/* Seeding mirrors what disk already holds - it is not new
		 * content and must not write anything by itself. Exception: an
		 * existing file that yielded nothing (crash leftovers,
		 * corruption) is repaired from the scan. */
		h->dirty_new = h->dirty_add = 0;
		if (hadfile && !loaded) h->dirty_add = 1;
	}

	return h;
}

/* Strong-apply the sticky live declarations to one entry and stamp its
 * declaration generation - the merge authority for the schema bundle.
 * A declaring sample carries the WHOLE truth: the block writer resets
 * every pending at block open, so a field with no pending was dropped
 * from the declaration and is retracted here - keeping it would
 * republish the stale value under the fresh generation forever. A
 * sample with no pendings at all (a legacy handler's write) declares
 * nothing and leaves the fields alone. */
static void fsidx_apply_pendings(fsidx_host_t *h, fsidx_entry_t *e)
{
	if (!fsidx_pending_units && !fsidx_pending_heartbeats &&
	    !fsidx_pending_dsnames && !fsidx_pending_thresholds) return;

	if (fsidx_adopt_field(&e->units, fsidx_pending_units)) h->dirty_new = 1;
	if (fsidx_adopt_field(&e->heartbeats, fsidx_pending_heartbeats)) h->dirty_new = 1;
	if (fsidx_adopt_field(&e->dsnames, fsidx_pending_dsnames)) h->dirty_new = 1;
	if (fsidx_adopt_field(&e->thresholds, fsidx_pending_thresholds)) h->dirty_new = 1;
	e->gen = getcurrenttime(NULL);
}

/* Event-time bookkeeping: ensure the entry exists (a new one is stamped
 * with the sample's own timestamp, once) and record the live schema
 * declarations. Does NOT advance an existing entry's freshness - that
 * belongs to fsidx_note_commit, after rrdtool accepts the data. */
void fsidx_note_schema(char *rrddir, char *hostname, char *rrdfn, time_t ts)
{
	fsidx_host_t *h;
	xtreePos_t handle;
	fsidx_entry_t *e;

	if (!rrddir || !fsidx_valid_rrdfn(rrdfn) || (ts <= 0)) return;
	if (!fsidx_valid_hostname(hostname)) return;
	h = fsidx_gethost(rrddir, hostname);
	handle = xtreeFind(h->files, rrdfn);
	if (handle == xtreeEnd(h->files)) {
		e = (fsidx_entry_t *)xcalloc(1, sizeof(fsidx_entry_t));
		e->fn = xstrdup(rrdfn);
		e->ts = ts;
		xtreeAdd(h->files, e->fn, e);
		h->dirty_add = 1;
	}
	else e = (fsidx_entry_t *)xtreeData(h->files, handle);
	fsidx_apply_pendings(h, e);
}

/* Commit-time freshness: the update was ACCEPTED by rrdtool, so the
 * entry's last-write advances to the applied data timestamp. A rejected
 * update never reaches here - a chronically broken producer goes stale
 * on schedule instead of looking forever fresh. */
void fsidx_note_commit(char *rrddir, char *hostname, char *rrdfn, time_t ts)
{
	fsidx_host_t *h;

	if (!rrddir || !fsidx_valid_rrdfn(rrdfn) || (ts <= 0)) return;
	if (!fsidx_valid_hostname(hostname)) return;
	h = fsidx_gethost(rrddir, hostname);
	fsidx_set(h, rrdfn, ts, NULL, 0);
}

/* Sticky positional DS names for following writes, same lifecycle as
 * fsidx_set_units(). Consumers (AGGDS) map values to datasets by name. */
void fsidx_set_dsnames(char *dsnspec)
{
	if (fsidx_pending_dsnames) { xfree(fsidx_pending_dsnames); fsidx_pending_dsnames = NULL; }
	if (dsnspec && (strlen(dsnspec) > FSIDX_SPECMAX)) return;
	if (dsnspec && *dsnspec) fsidx_pending_dsnames = xstrdup(dsnspec);
}

/* Smallest declared heartbeat among the pending block's DS specs
 * ("ds:hb[,...]"), 0 when the current block declared none. The write-
 * thinning gate derives its keepalive interval from this - the client's
 * declaration IS the consent to sparse updates. */
int fsidx_pending_min_heartbeat(void)
{
	char *p = fsidx_pending_heartbeats;
	int min = 0;

	while (p && *p) {
		char *colon = strchr(p, ':');
		if (colon) {
			int hb = atoi(colon+1);
			if ((hb > 0) && ((min == 0) || (hb < min))) min = hb;
		}
		p = strchr(p, ',');
		if (p) p++;
	}
	return min;
}

/* Sticky declared heartbeats for following writes, same lifecycle as
 * fsidx_set_units(). Spec: "ds:heartbeat[,...]", covering EVERY declared
 * DS (defaults included) so a changed declaration replaces the record
 * outright - the schema-reconcile tool compares files against this. */
void fsidx_set_heartbeats(char *hbspec)
{
	if (fsidx_pending_heartbeats) { xfree(fsidx_pending_heartbeats); fsidx_pending_heartbeats = NULL; }
	if (hbspec && (strlen(hbspec) > FSIDX_SPECMAX)) {
		errprintf("fileset index: heartbeat spec too long (%d), ignored\n", (int)strlen(hbspec));
		return;
	}
	if (hbspec && *hbspec) fsidx_pending_heartbeats = xstrdup(hbspec);
}

/* Sticky per-block writer state: the block writer declares the units of
 * the DS specs it is about to create files from; every note_write until
 * the next call carries them. NULL clears (a block without units, another
 * handler's writes). */
void fsidx_set_units(char *unitspec)
{
	if (fsidx_pending_units) { xfree(fsidx_pending_units); fsidx_pending_units = NULL; }
	if (unitspec && (strlen(unitspec) > FSIDX_SPECMAX)) {
		errprintf("fileset index: unit spec too long (%d), ignored\n", (int)strlen(unitspec));
		return;
	}
	if (unitspec && *unitspec) fsidx_pending_units = xstrdup(unitspec);
}

/* Sticky threshold relations for following writes, same lifecycle as
 * fsidx_set_units(). Spec: "base:relop-operand:sev[,...]". */
void fsidx_set_thresholds(char *thrspec)
{
	if (fsidx_pending_thresholds) { xfree(fsidx_pending_thresholds); fsidx_pending_thresholds = NULL; }
	if (thrspec && (strlen(thrspec) > FSIDX_SPECMAX)) {
		errprintf("fileset index: threshold spec too long (%d), ignored\n", (int)strlen(thrspec));
		return;
	}
	if (thrspec && *thrspec) fsidx_pending_thresholds = xstrdup(thrspec);
}

/* Iterate every loaded entry. cb may be NULL to only probe/count.
 * Returns -1 when the host is not loaded at all ("no knowledge",
 * distinct from "zero entries"), else the entry count. */
int fsidx_entry_foreach(char *hostname, void (*cb)(const char *, time_t, const char *, void *), void *userdata)
{
	xtreePos_t handle, fh;
	fsidx_host_t *h;
	int n = 0;

	if (!fsidx_hosts || !hostname) return -1;
	handle = xtreeFind(fsidx_hosts, hostname);
	if (handle == xtreeEnd(fsidx_hosts)) return -1;
	h = (fsidx_host_t *)xtreeData(fsidx_hosts, handle);
	for (fh = xtreeFirst(h->files); (fh != xtreeEnd(h->files)); fh = xtreeNext(h->files, fh)) {
		fsidx_entry_t *e = (fsidx_entry_t *)xtreeData(h->files, fh);
		if (cb) cb(e->fn, e->ts, e->dsnames, userdata);
		n++;
	}
	return n;
}

void fsidx_flush(char *rrddir, char *hostname)
{
	xtreePos_t handle, fh;
	fsidx_host_t *h;
	char fn[PATH_MAX], tmpfn[PATH_MAX];
	FILE *fd;
	int lockfd;
	time_t now = getcurrenttime(NULL);

	if (!fsidx_hosts || !rrddir || !fsidx_valid_hostname(hostname)) return;
	handle = xtreeFind(fsidx_hosts, hostname);
	if (handle == xtreeEnd(fsidx_hosts)) return;
	h = (fsidx_host_t *)xtreeData(fsidx_hosts, handle);
	/* One spelling for all FS operations, see fsidx_gethost() */
	hostname = (char *)xtreeKey(fsidx_hosts, handle);

	if (!h->dirty_new && !h->dirty_add) return;
	if (fsidx_path(fn, sizeof(fn), rrddir, hostname, "") != 0) return;
	if (!h->dirty_new) {
		struct stat st;

		/* Plain new-file entries only maintain an index that already
		 * exists - they never materialize one. A host acquires an
		 * index the first time it has index-only state to remember
		 * (schema declarations: dirty_new). */
		if (stat(fn, &st) != 0) return;
	}
	/* Per-process tmp name: even unserialized writers must never share one */
	{
		char pidsuf[32];
		snprintf(pidsuf, sizeof(pidsuf), ".tmp.%d", (int)getpid());
		if (fsidx_path(tmpfn, sizeof(tmpfn), rrddir, hostname, pidsuf) != 0) return;
	}

	/* The status- and data-channel writers share this file: serialize the
	 * read-merge-write. The lock lives on a DEDICATED lockfile - locking
	 * the index itself would be meaningless after the rename replaces it
	 * (the blocked process would acquire the orphaned inode's lock while
	 * the file it guards is already a different one). */
	{
		char lockfn[PATH_MAX];
		if (fsidx_path(lockfn, sizeof(lockfn), rrddir, hostname, ".lock") != 0) return;
		lockfd = open(lockfn, O_RDWR | O_CREAT, 0644);
	}
	if (lockfd == -1) {
		/* Host directory may not exist yet (no file ever created) */
		return;
	}
	if (flock(lockfd, LOCK_EX) != 0) {
		errprintf("fileset index: cannot lock %s/%s: %s - skipping flush\n", hostname, FSIDX_NAME, strerror(errno));
		close(lockfd);
		return;
	}

	fsidx_load_file(h, fn);

	fd = fopen(tmpfn, "w");
	if (fd) {
		int ok;

		fprintf(fd, "%s\n", FSIDX_HEADER);
		for (fh = xtreeFirst(h->files); (fh != xtreeEnd(h->files)); fh = xtreeNext(h->files, fh)) {
			fsidx_entry_t *e = (fsidx_entry_t *)xtreeData(h->files, fh);
			fprintf(fd, "%s %ld", e->fn, (long)e->ts);
			if (e->units) fprintf(fd, " u=%s", e->units);
			if (e->heartbeats) fprintf(fd, " h=%s", e->heartbeats);
			if (e->dsnames) fprintf(fd, " d=%s", e->dsnames);
			if (e->thresholds) fprintf(fd, " t=%s", e->thresholds);
			if (e->gen) fprintf(fd, " g=%ld", (long)e->gen);
			fprintf(fd, "\n");
		}
		/* fclose alone is not enough: a write error at an intermediate
		 * stdio flush (ENOSPC) only sets the stream error flag, and a
		 * truncated file must never replace the good one. fsync before
		 * the rename, or a crash right after it can publish an empty
		 * file - and this file is the durable declaration store. */
		ok = ((fflush(fd) == 0) && !ferror(fd) && (fsync(fileno(fd)) == 0));
		ok = (fclose(fd) == 0) && ok;
		if (ok && (rename(tmpfn, fn) == 0)) {
			/* Only a published file clears the dirty state - a failed
			 * flush must retry, or a one-shot change is lost */
			h->dirty_new = h->dirty_add = 0;
			/* The file's data is synced, but the rename lives in the
			 * DIRECTORY - a crash before its metadata hits disk can
			 * still lose the publish. Some filesystems refuse fsync
			 * on a directory fd, so failure only logs. */
			{
				char hostdir[PATH_MAX];
				int dirfd;

				snprintf(hostdir, sizeof(hostdir), "%s/%s", rrddir, hostname);
				dirfd = open(hostdir, O_RDONLY);
				if ((dirfd == -1) || (fsync(dirfd) != 0))
					errprintf("fileset index: cannot sync %s: %s\n", hostdir, strerror(errno));
				if (dirfd != -1) close(dirfd);
			}
		}
		else {
			errprintf("fileset index: cannot publish %s: %s\n", tmpfn, strerror(errno));
			unlink(tmpfn);
		}
	}
	else {
		errprintf("fileset index: cannot write %s: %s\n", tmpfn, strerror(errno));
	}

	flock(lockfd, LOCK_UN);
	close(lockfd);
}

void fsidx_flush_all(char *rrddir)
{
	xtreePos_t handle;

	if (!fsidx_hosts) return;
	for (handle = xtreeFirst(fsidx_hosts); (handle != xtreeEnd(fsidx_hosts)); handle = xtreeNext(fsidx_hosts, handle)) {
		fsidx_flush(rrddir, (char *)xtreeKey(fsidx_hosts, handle));
	}
}

/* Flush one host at the moments when the file must be current NOW - a
 * rename is about to move it, and the in-memory tree that holds the
 * pending state is about to be dropped. */
void fsidx_flush_now(char *rrddir, char *hostname)
{
	if (!fsidx_hosts || !hostname) return;
	fsidx_flush(rrddir, hostname);
}

void fsidx_drop(char *rrddir, char *hostname)
{
	xtreePos_t handle, fh;
	fsidx_host_t *h;
	char fn[PATH_MAX];

	if (!rrddir || !fsidx_valid_hostname(hostname)) return;
	/* One spelling for all FS operations, see fsidx_gethost() */
	if (fsidx_hosts) {
		handle = xtreeFind(fsidx_hosts, hostname);
		if (handle != xtreeEnd(fsidx_hosts)) hostname = (char *)xtreeKey(fsidx_hosts, handle);
	}
	if (fsidx_path(fn, sizeof(fn), rrddir, hostname, "") != 0) return;
	/* Serialize with a flush in flight in the other channel's writer:
	 * its tmp+rename must land before the unlink, or the rename would
	 * resurrect the index we just removed. No O_CREAT - if no lockfile
	 * exists there is no flush to wait for, and creating one here would
	 * drop a fresh file into a directory being deleted. The lockfile
	 * itself is left in place: unlinking it would hand a flusher still
	 * blocked in flock() a lock on the orphaned inode while a third
	 * writer re-creates the name, letting two flushes run unserialized.
	 * The drophost directory teardown removes it with everything else. */
	{
		char lockfn[PATH_MAX];
		int lockfd;

		if (fsidx_path(lockfn, sizeof(lockfn), rrddir, hostname, ".lock") != 0) return;
		lockfd = open(lockfn, O_RDWR);
		if (lockfd != -1) flock(lockfd, LOCK_EX);
		unlink(fn);
		if (lockfd != -1) {
			flock(lockfd, LOCK_UN);
			close(lockfd);
		}
	}

	if (!fsidx_hosts) return;
	handle = xtreeFind(fsidx_hosts, hostname);
	if (handle == xtreeEnd(fsidx_hosts)) return;
	h = (fsidx_host_t *)xtreeData(fsidx_hosts, handle);

	/* Reset in place: xtree deletion leaves tombstones. needseed makes
	 * fsidx_gethost reseed (index load / scan) on the next touch, so a
	 * re-added host starts from what is actually on disk. */
	for (fh = xtreeFirst(h->files); (fh != xtreeEnd(h->files)); fh = xtreeNext(h->files, fh)) {
		fsidx_entry_t *e = (fsidx_entry_t *)xtreeData(h->files, fh);
		xfree(e->fn);
		if (e->units) xfree(e->units);
		if (e->heartbeats) xfree(e->heartbeats);
		if (e->dsnames) xfree(e->dsnames);
		if (e->thresholds) xfree(e->thresholds);
		xfree(e);
	}
	xtreeDestroy(h->files);
	h->files = xtreeNew(strcmp);
	h->dirty_new = h->dirty_add = 0;
	h->needseed = 1;
}

/* Fetch one named field ("u=", "t=") from a file's index entry. */
static char *fsidx_field(char *hostname, char *rrdfn, const char *fieldtag)
{
	char fn[PATH_MAX];
	FILE *fd;
	char line[FSIDX_LINEMAX];
	char *result = NULL;
	size_t taglen = strlen(fieldtag);

	if (!rrdfn || !fsidx_valid_hostname(hostname)) return NULL;
	if ((size_t)snprintf(fn, sizeof(fn), "%s/%s/%s", xgetenv("XYMONRRDS"), hostname, FSIDX_NAME) >= sizeof(fn)) return NULL;
	fd = fopen(fn, "r");
	if (!fd) return NULL;

	while (!result && fgets(line, sizeof(line), fd)) {
		char *name, *tok, *sp = NULL;

		if (fsidx_line_truncated(line, fd)) continue;
		if (line[0] == '#') continue;
		name = strtok_r(line, " \t\r\n", &sp);
		if (!name || strcmp(name, rrdfn)) continue;
		while ((tok = strtok_r(NULL, " \t\r\n", &sp)) != NULL) {
			if (strncmp(tok, fieldtag, taglen) == 0) { result = xstrdup(tok+taglen); break; }
		}
		break;
	}
	fclose(fd);

	return result;
}

char *fsidx_thresholds(char *hostname, char *rrdfn)
{
	return fsidx_field(hostname, rrdfn, "t=");
}

/* Match an index filename against a caller-compiled exemption pattern
 * (a pcre2_code*), the storage-pattern way: name minus its ".rrd"
 * suffix. Used by the counters for EXSTALEPATTERN. */
static int fsidx_name_matches(const char *name, void *pattern)
{
	pcre2_match_data *md;
	size_t nlen = strlen(name);
	int result;

	if ((nlen > 4) && (strcmp(name + nlen - 4, ".rrd") == 0)) nlen -= 4;
	md = pcre2_match_data_create_from_pattern((pcre2_code *)pattern, NULL);
	if (!md) return 0;
	result = pcre2_match((pcre2_code *)pattern, (PCRE2_SPTR)name, nlen, 0, 0, md, NULL);
	pcre2_match_data_free(md);
	return (result >= 0);
}

/* Reader-side freshness of one index record. A file's persisted ts is
 * only as fresh as the writer's last index-worthy flush - the durable
 * freshness is the RRD file's own mtime. A record whose file is gone
 * keeps the (stale) ts and ages out naturally. */
static time_t fsidx_reader_freshness(char *hostname, const char *name, time_t ts)
{
	char fpath[PATH_MAX];
	struct stat st;

	if ((size_t)snprintf(fpath, sizeof(fpath), "%s/%s/%s", xgetenv("XYMONRRDS"), hostname, name) >= sizeof(fpath)) return ts;
	if ((stat(fpath, &st) == 0) && (st.st_mtime > ts)) return st.st_mtime;
	return ts;
}

/* The per-DS units recorded for one file: a malloc'd "ds:unit[,...]"
 * spec, or NULL when the host has no index or the file no units. */
char *fsidx_units(char *hostname, char *rrdfn)
{
	char fn[PATH_MAX];
	FILE *fd;
	char line[FSIDX_LINEMAX];
	char *result = NULL;

	if (!rrdfn || !fsidx_valid_hostname(hostname)) return NULL;
	if ((size_t)snprintf(fn, sizeof(fn), "%s/%s/%s", xgetenv("XYMONRRDS"), hostname, FSIDX_NAME) >= sizeof(fn)) return NULL;
	fd = fopen(fn, "r");
	if (!fd) return NULL;

	while (!result && fgets(line, sizeof(line), fd)) {
		char *name, *tok, *sp = NULL;

		if (fsidx_line_truncated(line, fd)) continue;
		if (line[0] == '#') continue;
		name = strtok_r(line, " \t\r\n", &sp);
		if (!name || strcmp(name, rrdfn)) continue;
		while ((tok = strtok_r(NULL, " \t\r\n", &sp)) != NULL) {
			if (strncmp(tok, "u=", 2) == 0) { result = xstrdup(tok+2); break; }
		}
		break;
	}
	fclose(fd);

	return result;
}

int fsidx_count_pattern(char *hostname, void *pattern, time_t maxage, void *exemptpat)
{
	char fn[PATH_MAX];
	FILE *fd;
	char line[FSIDX_LINEMAX];
	int count = 0;
	time_t now = getcurrenttime(NULL);

	if (!pattern || !fsidx_valid_hostname(hostname)) return -1;
	if ((size_t)snprintf(fn, sizeof(fn), "%s/%s/%s", xgetenv("XYMONRRDS"), hostname, FSIDX_NAME) >= sizeof(fn)) return -1;
	fd = fopen(fn, "r");
	if (!fd) return -1;

	while (fgets(line, sizeof(line), fd)) {
		char *name, *tsstr, *sp = NULL;
		time_t ts;

		if (fsidx_line_truncated(line, fd)) continue;
		if (line[0] == '#') continue;
		name = strtok_r(line, " \t\r\n", &sp);
		tsstr = (name ? strtok_r(NULL, " \t\r\n", &sp) : NULL);
		if (!name || !tsstr) continue;
		if (!matchregex(name, (pcre2_code *)pattern)) continue;
		ts = fsidx_reader_freshness(hostname, name, fsidx_parse_ts(tsstr));
		if (maxage && ((now - ts) > maxage) &&
		    !(exemptpat && fsidx_name_matches(name, exemptpat))) continue;
		count++;
	}
	fclose(fd);

	return count;
}

int fsidx_count_prefix(char *hostname, char *prefix, time_t maxage, void *exemptpat)
{
	char fn[PATH_MAX];
	FILE *fd;
	char line[FSIDX_LINEMAX];
	size_t plen;
	int count = 0;
	time_t now = getcurrenttime(NULL);

	if (!prefix || !fsidx_valid_hostname(hostname)) return -1;
	if ((size_t)snprintf(fn, sizeof(fn), "%s/%s/%s", xgetenv("XYMONRRDS"), hostname, FSIDX_NAME) >= sizeof(fn)) return -1;
	fd = fopen(fn, "r");
	if (!fd) return -1;

	plen = strlen(prefix);
	while (fgets(line, sizeof(line), fd)) {
		char *name, *tsstr, *sp = NULL;
		size_t nlen;
		time_t ts;

		if (fsidx_line_truncated(line, fd)) continue;
		if (line[0] == '#') continue;
		name = strtok_r(line, " \t\r\n", &sp);
		tsstr = (name ? strtok_r(NULL, " \t\r\n", &sp) : NULL);
		if (!name || !tsstr) continue;

		nlen = strlen(name);
		/* '.' is the instance separator; ',' is its legacy spelling
		 * (pre-encode disk/devmon files) - both are real instances of
		 * this prefix, and the gdef-side matchers accept both. */
		if ((nlen <= plen + 5) || (strncmp(name, prefix, plen) != 0) ||
		    ((name[plen] != '.') && (name[plen] != ','))) continue;
		if (strcmp(name + nlen - 4, ".rrd") != 0) continue;
		ts = fsidx_reader_freshness(hostname, name, fsidx_parse_ts(tsstr));
		if (maxage && ((now - ts) > maxage) &&
		    !(exemptpat && fsidx_name_matches(name, exemptpat))) continue;
		count++;
	}
	fclose(fd);

	return count;
}
