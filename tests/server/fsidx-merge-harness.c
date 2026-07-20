/* Harness for the fileset index's two-writer schema merge (the g= field).
 * Two xymond_rrd processes (status- and data-channel) share one on-disk
 * index; schema fields merge by declaration timestamp so a stale writer
 * ADOPTS the newer bundle instead of ping-ponging its old copy back.
 * Drives the real filesetindex.c through its public API: seeds a host
 * tree from a hand-written file, mutates the file as "the other writer",
 * flushes, and prints the published lines for the shell to assert on.
 *
 * Usage: harness <rrddir> <scenario>
 *   adopt-newer   memory holds gen 100, disk changes to gen 200 -> adopt
 *   ignore-older  memory holds gen 200, disk regresses to gen 50 -> keep
 *   legacy-fill   no generations anywhere -> weak fill (old behavior)
 *   live-wins     live declaration outranks any on-disk generation
 *   retract-live  a live bundle no longer declaring a field retracts it
 *   adopt-retract a newer disk bundle without a field clears ours
 *   reject-slash  a '/' in the hostname must not escape the RRD tree
 *   reject-badfn  an rrdfn the record format cannot carry is refused
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "libxymon.h"

static char *rrddir;

static void writeindex(const char *host, const char *content)
{
	char fn[PATH_MAX];
	FILE *fd;

	snprintf(fn, sizeof(fn), "%s/%s/.fileset-index", rrddir, host);
	fd = fopen(fn, "w");
	if (!fd) { perror(fn); exit(1); }
	fprintf(fd, "# xymon fileset index v1\n%s", content);
	fclose(fd);
}

static void dumpindex(const char *host)
{
	char fn[PATH_MAX];
	FILE *fd;
	char line[4096];

	snprintf(fn, sizeof(fn), "%s/%s/.fileset-index", rrddir, host);
	fd = fopen(fn, "r");
	if (!fd) { perror(fn); exit(1); }
	while (fgets(line, sizeof(line), fd)) if (line[0] != '#') fputs(line, stdout);
	fclose(fd);
}

int main(int argc, char *argv[])
{
	char *scenario;

	if (argc < 3) { fprintf(stderr, "usage: %s <rrddir> <scenario>\n", argv[0]); return 2; }
	rrddir = argv[1];
	scenario = argv[2];

	if (strcmp(scenario, "adopt-newer") == 0) {
		/* Seed memory from a gen-100 file (the stale writer's view),
		 * then "the other writer" publishes gen 200 with a changed
		 * spec. Our flush must adopt, not republish the stale spec. */
		writeindex("h1", "f.a.rrd 1000 u=v:ms h=v:600 g=100\n");
		fsidx_note_schema(rrddir, "h1", "f.a.rrd", 1000);	/* seeds the tree */
		writeindex("h1", "f.a.rrd 1500 u=v:msec h=v:300 g=200\n");
		fsidx_note_schema(rrddir, "h1", "f.b.rrd", 1000);	/* dirt: makes the flush publish */
		fsidx_flush(rrddir, "h1");
		dumpindex("h1");
	}
	else if (strcmp(scenario, "ignore-older") == 0) {
		/* Memory holds the newer declaration; a regressed (older-gen)
		 * disk copy must not win the merge. */
		writeindex("h1", "f.a.rrd 1000 u=v:msec g=200\n");
		fsidx_note_schema(rrddir, "h1", "f.a.rrd", 1000);
		writeindex("h1", "f.a.rrd 1500 u=v:old g=50\n");
		fsidx_note_schema(rrddir, "h1", "f.b.rrd", 1000);	/* dirt: makes the flush publish */
		fsidx_flush(rrddir, "h1");
		dumpindex("h1");
	}
	else if (strcmp(scenario, "legacy-fill") == 0) {
		/* Pre-g= files: equal (zero) generations weak-fill empty
		 * slots, exactly the old behavior. */
		writeindex("h1", "f.a.rrd 1000\n");
		fsidx_note_schema(rrddir, "h1", "f.a.rrd", 1000);
		writeindex("h1", "f.a.rrd 1500 u=v:legacy\n");
		fsidx_note_schema(rrddir, "h1", "f.b.rrd", 1000);	/* dirt: makes the flush publish */
		fsidx_flush(rrddir, "h1");
		dumpindex("h1");
	}
	else if (strcmp(scenario, "live-wins") == 0) {
		/* A live declaration stamps the current time as generation,
		 * outranking anything a file can carry. */
		writeindex("h1", "f.a.rrd 1000 u=v:stale g=200\n");
		fsidx_set_units("v:live");
		fsidx_note_schema(rrddir, "h1", "f.a.rrd", 1000);
		fsidx_set_units(NULL);
		fsidx_flush(rrddir, "h1");
		dumpindex("h1");
	}
	else if (strcmp(scenario, "retract-live") == 0) {
		/* The sample's declaration bundle is the whole truth: a field
		 * the block stopped declaring (here t=) must leave the record,
		 * not ride every fresh generation forever. The first
		 * note_schema carries no pendings at all (a legacy handler's
		 * write) and must leave the seeded fields alone. */
		writeindex("h1", "f.a.rrd 1000 u=v:ms h=v:600 t=v:>5:warn g=100\n");
		fsidx_note_schema(rrddir, "h1", "f.a.rrd", 1000);	/* undeclared: hands off */
		fsidx_set_units("v:ms");
		fsidx_set_heartbeats("v:600");
		fsidx_note_schema(rrddir, "h1", "f.a.rrd", 1001);	/* declares u+h, retracts t */
		fsidx_set_units(NULL);
		fsidx_set_heartbeats(NULL);
		fsidx_flush(rrddir, "h1");
		dumpindex("h1");
	}
	else if (strcmp(scenario, "adopt-retract") == 0) {
		/* Retraction must cross the two-writer merge too: the other
		 * writer publishes a NEWER bundle without t= - adopting it
		 * means clearing our copy, or a retraction on one channel
		 * would resurrect from the other's memory. */
		writeindex("h1", "f.a.rrd 1000 u=v:ms t=v:>5:warn g=100\n");
		fsidx_note_schema(rrddir, "h1", "f.a.rrd", 1000);
		writeindex("h1", "f.a.rrd 1500 u=v:ms g=200\n");
		fsidx_note_schema(rrddir, "h1", "f.b.rrd", 1000);	/* dirt: makes the flush publish */
		fsidx_flush(rrddir, "h1");
		dumpindex("h1");
	}
	else if (strcmp(scenario, "reject-slash") == 0) {
		/* Hostnames come off the channel raw; one carrying '/' is a
		 * path escape (a drophost could flock/unlink outside the RRD
		 * tree). Every entry point must reject it - the shell asserts
		 * the decoy index planted outside the tree survives intact. */
		fsidx_note_schema(rrddir, "../outside", "f.a.rrd", 1000);
		fsidx_flush(rrddir, "../outside");
		fsidx_flush_now(rrddir, "../outside");
		fsidx_drop(rrddir, "../outside");
		printf("probe=%s\n", (fsidx_entry_foreach("../outside", NULL, NULL) < 0) ? "null" : "leaked");
	}
	else if (strcmp(scenario, "reject-badfn") == 0) {
		/* A blank, line break or leading '#' in an rrdfn would split
		 * the space-separated record on the way back in (or read back
		 * as a comment) - every recording entry point refuses them. */
		writeindex("h1", "");	/* plain entries maintain an existing index, never materialize one */
		fsidx_note_schema(rrddir, "h1", "bad\tname.rrd", 1000);
		fsidx_note_commit(rrddir, "h1", "bad\nname.rrd", 1000);
		fsidx_note_schema(rrddir, "h1", "#lead.rrd", 1000);
		fsidx_note_schema(rrddir, "h1", "f.a.rrd", 1000);	/* a valid entry to publish */
		fsidx_flush(rrddir, "h1");
		dumpindex("h1");
	}
	else {
		fprintf(stderr, "unknown scenario %s\n", scenario);
		return 2;
	}

	return 0;
}
