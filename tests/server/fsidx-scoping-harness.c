/* Harness for the fileset index's write economics: a real RRD file's
 * freshness is its own mtime, so plain data updates must never write the
 * index - only index-only state does (schema declarations, baselines,
 * and a new entry joining an EXISTING index). Compiled against
 * the real filesetindex.c.
 *
 * Usage: harness <rrddir> <scenario>
 *   legacy       plain updates on a host with no index-only state
 *                -> no index file ever materializes
 *   schema-once  a declared entry creates the index once; plain commits
 *                never rewrite it; a NEW file joining the host does
 *   count        reader freshness: files count by mtime
 *   census       writer seed: loaded real-file entries carry mtime
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#include "libxymon.h"

static char *rrddir;

static void mkfile(const char *host, const char *name)
{
	char fn[PATH_MAX];
	FILE *fd;

	snprintf(fn, sizeof(fn), "%s/%s/%s", rrddir, host, name);
	fd = fopen(fn, "w");
	if (!fd) { perror(fn); exit(1); }
	fprintf(fd, "x");
	fclose(fd);
}

static void statindex(const char *host, const char *tag)
{
	char fn[PATH_MAX];
	struct stat st;

	snprintf(fn, sizeof(fn), "%s/%s/.fileset-index", rrddir, host);
	if (stat(fn, &st) == 0) printf("%s=present ino:%lu\n", tag, (unsigned long)st.st_ino);
	else printf("%s=absent\n", tag);
}

static void dumpindex(const char *host)
{
	char fn[PATH_MAX];
	FILE *fd;
	char line[4096];

	snprintf(fn, sizeof(fn), "%s/%s/.fileset-index", rrddir, host);
	fd = fopen(fn, "r");
	if (!fd) { perror(fn); exit(1); }
	while (fgets(line, sizeof(line), fd)) fputs(line, stdout);
	fclose(fd);
}

static void census_cb(const char *fn, time_t ts, const char *dsnames, void *userdata)
{
	int *fresh = (int *)userdata;

	(void)fn; (void)dsnames;
	if ((time(NULL) - ts) <= 600) (*fresh)++;
}

int main(int argc, char *argv[])
{
	char *scenario;
	time_t now = time(NULL);

	if (argc < 3) { fprintf(stderr, "usage: %s <rrddir> <scenario>\n", argv[0]); return 2; }
	rrddir = argv[1];
	scenario = argv[2];

	if (strcmp(scenario, "legacy") == 0) {
		/* A pure-legacy host: real files, data flowing, nothing
		 * self-describing. Three "polling cycles" of schema+commit
		 * notes and flushes must never materialize an index. */
		int c;

		mkfile("h1", "f.a.rrd");
		mkfile("h1", "f.b.rrd");
		for (c = 0; (c < 3); c++) {
			fsidx_note_schema(rrddir, "h1", "f.a.rrd", now + 300*c);
			fsidx_note_schema(rrddir, "h1", "f.b.rrd", now + 300*c);
			fsidx_note_commit(rrddir, "h1", "f.a.rrd", now + 300*c);
			fsidx_note_commit(rrddir, "h1", "f.b.rrd", now + 300*c);
			fsidx_flush(rrddir, "h1");
			sleep(2);	/* past FSIDX_FLUSHIVL=1: no throttle excuse */
		}
		fsidx_flush_all(rrddir);	/* shutdown must not materialize one either */
		statindex("h1", "index");
	}
	else if (strcmp(scenario, "schema-once") == 0) {
		/* A declaration creates the index once. Plain commits never
		 * rewrite it, however much time passes; a new plain file
		 * joining the host does (the index stays complete). */
		int c;

		mkfile("h1", "f.a.rrd");
		fsidx_set_units("v:pct");
		fsidx_note_schema(rrddir, "h1", "f.a.rrd", now);
		fsidx_set_units(NULL);
		fsidx_flush(rrddir, "h1");
		statindex("h1", "created");
		sleep(2);
		for (c = 1; (c <= 3); c++) {
			fsidx_note_commit(rrddir, "h1", "f.a.rrd", now + 300*c);
			fsidx_flush(rrddir, "h1");
			sleep(2);
		}
		statindex("h1", "aftercommits");
		mkfile("h1", "f.c.rrd");
		fsidx_note_schema(rrddir, "h1", "f.c.rrd", now + 1200);
		fsidx_flush(rrddir, "h1");
		statindex("h1", "afteradd");
		dumpindex("h1");
	}
	else if (strcmp(scenario, "count") == 0) {
		/* Reader freshness: shell crafted the index and the files.
		 * Files count by mtime (persisted ts is stale); a record
		 * whose file is gone ages out. */
		printf("disk=%d\n", fsidx_count_prefix("h1", "disk", 600, NULL));
		printf("gone=%d\n", fsidx_count_prefix("h1", "gone", 600, NULL));
	}
	else if (strcmp(scenario, "census") == 0) {
		/* Writer seed: loading an index whose real-file ts is stale
		 * must pick up the file's mtime, or the AGGDS warm-up census
		 * undercounts after a restart. */
		int fresh = 0;

		fsidx_note_schema(rrddir, "h1", "seed.x.rrd", now);	/* triggers the seed */
		fsidx_entry_foreach("h1", census_cb, &fresh);
		printf("fresh=%d\n", fresh);
	}
	else {
		fprintf(stderr, "unknown scenario %s\n", scenario);
		return 2;
	}

	return 0;
}
