/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* This is a xymond worker module for the "status" and "data" channels.       */
/* This module maintains the RRD database-files, updating them as new         */
/* data arrives.                                                              */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <limits.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>

#include "libxymon.h"
#include "xymond_worker.h"

#include "xymond_rrd.h"
#include "do_rrd.h"
#include "client_config.h"

#include <signal.h>


#define MAX_META 20	/* The maximum number of meta-data items in a message */

int seq = 0;
static int running = 1;
static time_t reloadtime = 0;
static int reloadextprocessor = 0;

typedef struct rrddeftree_t {
	char *key;
	int count;
	char **defs;
} rrddeftree_t;
static void * rrddeftree;

static void sig_handler(int signum)
{
	switch (signum) {
	  case SIGHUP:
		  reloadtime = 0;
		  break;
	  case SIGPIPE:
		  reloadextprocessor = 1;
		  break;
	  case SIGCHLD:
		  break;
	  case SIGINT:
	  case SIGTERM:
		  running = 0;
		  break;
	}
}

/* Drop barrier: @@drophost forks the host directory deletion, and messages
 * for that host already queued in the channel can still arrive afterwards
 * and recreate files inside the dying directory.
 *
 * A straggler is told apart from a legitimate message by *message time*,
 * not by a wall-clock window. Anything generated before the drop was issued
 * carries a timestamp at or before the drop's own; anything the host sends
 * once the name is in use again carries a later one. So the barrier lifts
 * itself the moment that happens - re-adding a dropped host, renaming a host
 * back, or renaming a name onto itself all resume with the very next message.
 *
 * A fixed window cannot do that. It goes on discarding a live host's data for
 * the length of the window, with nothing to show for it above --debug, and
 * that is a worse failure than the race it closes: the race needs a drop and
 * an unlucky interleaving, while "rename a host and rename it back" is a
 * thing operators do on purpose.
 */
static void *recentdrops = NULL;

/* Nothing sitting in a channel backlog is an hour old, so an entry older than
 * this can only match messages that can no longer arrive. Pruned whenever the
 * next drop is recorded, so a site that churns hostnames does not accumulate
 * one record per name for the life of the process. */
#define DROPKEEP 3600

static void prune_hostdrops(time_t now)
{
	xtreePos_t handle;
	char **stale = NULL, **grown;
	int nstale = 0, i;

	/* Two phases: deleting while traversing the tree is not safe. */
	for (handle = xtreeFirst(recentdrops); (handle != xtreeEnd(recentdrops)); handle = xtreeNext(recentdrops, handle)) {
		if ((now - *(time_t *)xtreeData(recentdrops, handle)) <= DROPKEEP) continue;
		grown = (char **)realloc(stale, (nstale+1) * sizeof(char *));
		if (!grown) { if (stale) xfree(stale); return; }	/* prune next time */
		stale = grown;
		stale[nstale++] = xtreeKey(recentdrops, handle);
	}

	for (i = 0; (i < nstale); i++) {
		/* xtreeDelete() releases the record but not the key, and stops
		 * referencing it - so the key is ours to free afterwards. */
		time_t *ts = (time_t *)xtreeDelete(recentdrops, stale[i]);
		if (ts) xfree(ts);
		xfree(stale[i]);
	}
	if (stale) xfree(stale);
}

static void note_hostdrop(char *hostname, time_t droptime)
{
	xtreePos_t handle;

	if (!recentdrops) recentdrops = xtreeNew(strcasecmp);
	prune_hostdrops(droptime);

	handle = xtreeFind(recentdrops, hostname);
	if (handle != xtreeEnd(recentdrops)) {
		time_t *ts = (time_t *)xtreeData(recentdrops, handle);
		if (droptime > *ts) *ts = droptime;
	}
	else {
		time_t *ts = (time_t *)malloc(sizeof(time_t));

		if (!ts) {
			errprintf("Out of memory recording the drop of host %s - a straggler for it may recreate its RRD files\n",
				  hostname);
			return;
		}
		*ts = droptime;
		xtreeAdd(recentdrops, strdup(hostname), ts);
	}
}

static int hostdrop_barrier(char *hostname, time_t msgtime)
{
	xtreePos_t handle;

	if (!recentdrops) return 0;
	handle = xtreeFind(recentdrops, hostname);
	if (handle == xtreeEnd(recentdrops)) return 0;
	/* At or before the drop: this message was already on its way when the
	 * host went. Later than it: the name is legitimately in use again. */
	return (msgtime <= *(time_t *)xtreeData(recentdrops, handle));
}

static void update_locator_hostdata(char *id)
{
	DIR *fd;
	struct dirent *d;

	fd = opendir(rrddir);
	if (fd == NULL) {
		errprintf("Cannot scan directory %s\n", rrddir);
		return;
	}

	while ((d = readdir(fd)) != NULL) {
		if (*(d->d_name) == '.') continue;
		locator_register_host(d->d_name, ST_RRD, id);
	}

	closedir(fd);
}

static void load_rrddefs(void)
{
	char fn[PATH_MAX];
	FILE *fd;
	strbuffer_t *inbuf = newstrbuffer(0);
	char *key = NULL, *p;
	char **defs = NULL;
	int defcount = 0;
	rrddeftree_t *newrec;

	rrddeftree = xtreeNew(strcasecmp);

	sprintf(fn, "%s/etc/rrddefinitions.cfg", xgetenv("XYMONHOME"));
	fd = stackfopen(fn, "r", NULL);
	if (fd == NULL) goto loaddone;

	while (stackfgets(inbuf, NULL)) {
		sanitize_input(inbuf, 1, 0); if (STRBUFLEN(inbuf) == 0) continue;

		if (*(STRBUF(inbuf)) == '[') {
			if (key && (defcount > 0)) {
				/* Save the current record */
				newrec = (rrddeftree_t *)malloc(sizeof(rrddeftree_t));
				newrec->key = key;
				newrec->defs = defs;
				newrec->count = defcount;
				xtreeAdd(rrddeftree, newrec->key, newrec);

				key = NULL; defs = NULL; defcount = 0;
			}

			key = strdup(STRBUF(inbuf)+1);
			p = strchr(key, ']'); if (p) *p = '\0';
		}
		else if (key) {
			if (!defs) {
				defcount = 1;
				defs = (char **)malloc(sizeof(char *));
			}
			else {
				defcount++;
				defs = (char **)realloc(defs, defcount * sizeof(char *));
			}
			p = STRBUF(inbuf); p += strspn(p, " \t");
			defs[defcount-1] = strdup(p);
		}
	}

	if (key && (defcount > 0)) {
		/* Save the last record */
		newrec = (rrddeftree_t *)malloc(sizeof(rrddeftree_t));
		newrec->key = key;
		newrec->defs = defs;
		newrec->count = defcount;
		xtreeAdd(rrddeftree, newrec->key, newrec);
	}

	stackfclose(fd);

loaddone:
	freestrbuffer(inbuf);

	/* Check if the default record exists */
	if (xtreeFind(rrddeftree, "") == xtreeEnd(rrddeftree)) {
		/* Create the default record */
		newrec = (rrddeftree_t *)malloc(sizeof(rrddeftree_t));
		newrec->key = strdup("");
		newrec->defs = (char **)malloc(4 * sizeof(char *));;
		newrec->defs[0] = strdup("RRA:AVERAGE:0.5:1:576");
		newrec->defs[1] = strdup("RRA:AVERAGE:0.5:6:576");
		newrec->defs[2] = strdup("RRA:AVERAGE:0.5:24:576");
		newrec->defs[3] = strdup("RRA:AVERAGE:0.5:288:576");
		newrec->count = 4;
		xtreeAdd(rrddeftree, newrec->key, newrec);
	}
}

char **get_rrd_definition(char *key, int *count)
{
	xtreePos_t handle;
	rrddeftree_t *rec;

	handle = xtreeFind(rrddeftree, key);
	if (handle == xtreeEnd(rrddeftree)) {
		handle = xtreeFind(rrddeftree, "");	/* The default record */
	}
	rec = (rrddeftree_t *)xtreeData(rrddeftree, handle);

	*count = rec->count;
	return rec->defs;
}

int main(int argc, char *argv[])
{
	char *msg;
	int argi;
	struct sigaction sa;
	char *exthandler = NULL;
	char *extids = NULL;
	char *processor = NULL;
	struct sockaddr_un ctlsockaddr;
	int ctlsocket;
	int usebackfeedqueue = 0;

	/* Handle program options. */
	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--rrddir=")) {
			char *p = strchr(argv[argi], '=');
			rrddir = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--extra-script=")) {
			char *p = strchr(argv[argi], '=');
			exthandler = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--extra-tests=")) {
			char *p = strchr(argv[argi], '=');
			extids = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--processor=")) {
			char *p = strchr(argv[argi], '=');
			processor = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--no-cache") == 0) {
			use_rrd_cache = 0;
		}
		else if (strcmp(argv[argi], "--no-rrd") == 0) {
			no_rrd = 1;
		}
		else if (net_worker_option(argv[argi])) {
			/* Handled in the subroutine */
		}
	}

	save_errbuf = 0;

	if (no_rrd && !processor) errprintf("RRD writing disabled, but no external processor has been specified.\n");

	if ((rrddir == NULL) && xgetenv("XYMONRRDS")) {
		rrddir = strdup(xgetenv("XYMONRRDS"));
	}

	if (exthandler && extids) setup_exthandler(exthandler, extids);

	usebackfeedqueue = (sendmessage_init_local() > 0);

	/* Do the network stuff if needed */
	net_worker_run(ST_RRD, LOC_STICKY, update_locator_hostdata);

	setup_signalhandler("xymond_rrd");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);

	/* Setup the control socket that receives cache-flush commands */
	memset(&ctlsockaddr, 0, sizeof(ctlsockaddr));
	{
		/* sun_path is tiny (~108 bytes); refuse an XYMONTMP that cannot
		 * fit rather than overflow (showgraph guards the sender side). */
		char *xymontmp = xgetenv("XYMONTMP");
		int n = snprintf(ctlsockaddr.sun_path, sizeof(ctlsockaddr.sun_path),
				 "%s/rrdctl.%lu", xymontmp, (unsigned long)getpid());
		if ((n < 0) || (n >= (int)sizeof(ctlsockaddr.sun_path))) {
			/* Report the limit on XYMONTMP itself: sun_path minus the suffix. */
			int maxtmp = (int)sizeof(ctlsockaddr.sun_path) - 1
				   - snprintf(NULL, 0, "/rrdctl.%lu", (unsigned long)getpid());
			errprintf("Cannot set up cache-control socket: XYMONTMP is too long for a socket path (%d characters; max %d with this process ID)\n",
				  (int)strlen(xymontmp), maxtmp);
			return 1;
		}
	}
	unlink(ctlsockaddr.sun_path);     /* In case it was accidentally left behind */
	ctlsockaddr.sun_family = AF_UNIX;
	ctlsocket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (ctlsocket == -1) {
		errprintf("Cannot create cache-control socket (%s)\n", strerror(errno));
		return 1;
	}
	fcntl(ctlsocket, F_SETFL, O_NONBLOCK);
	if (bind(ctlsocket, (struct sockaddr *)&ctlsockaddr, sizeof(ctlsockaddr)) == -1) {
		errprintf("Cannot bind to cache-control socket (%s)\n", strerror(errno));
		return 1;
	}
	/* Linux obeys filesystem permissions on the socket file, so make it world-accessible */
	if (chmod(ctlsockaddr.sun_path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) == -1) {
		errprintf("Setting permissions on cache-control socket failed: %s\n", strerror(errno));
	}

	/* Load the RRD definitions */
	load_rrddefs();

	/* If we are passing data to an external processor, create the pipe to it */
	setup_extprocessor(processor);

	while (running) {
		char *eoln, *restofmsg = NULL;
		char *metadata[MAX_META+1];
		int metacount;
		char *p;
		char *hostname = NULL, *testname = NULL, *sender = NULL, *classname = NULL, *pagepaths = NULL;
		xymonrrd_t *ldef = NULL;
		time_t tstamp;
		int childstat;
                ssize_t n;
		char ctlbuf[PATH_MAX];
		int gotcachectlmessage;
		time_t now;

		/* If we need to re-open our external processor, do so */
		if (reloadextprocessor) {
			shutdown_extprocessor(); // Just in case we got a PIPE, but the pipe needs to be cleaned up still
			setup_extprocessor(processor);
			reloadextprocessor = 0;
		}

		/* See if we have any cache-control messages pending */
		do {
			n = recv(ctlsocket, ctlbuf, sizeof(ctlbuf), 0);
			gotcachectlmessage = (n > 0);
			if (gotcachectlmessage) {
				/* We have a control message */
				char *bol, *eol;

				ctlbuf[n] = '\0';
				bol = ctlbuf;
				do {
					eol = strchr(bol, '\n'); if (eol) *eol = '\0';
					rrdcacheflushhost(bol);
					if (eol) { bol = eol+1; } else bol = NULL;
				} while (bol && *bol);
			}
		} while (gotcachectlmessage);

		/* Get next message */
		msg = get_xymond_message(C_LAST, argv[0], &seq, NULL);
		if (msg == NULL) {
			running = 0;
			continue;
		}

		now = gettimer();
		if (reloadtime < now) {
			/* Reload configuration files */
			load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
			load_client_config(NULL);
			reloadtime = now + 600;
		}

		/* Split the message in the first line (with meta-data), and the rest */
 		eoln = strchr(msg, '\n');
		if (eoln) {
			*eoln = '\0';
			restofmsg = eoln+1;
		}

		if (usebackfeedqueue) combo_start_local(); else combo_start();

		/* Parse the meta-data */
		metacount = 0; 
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < MAX_META)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;

		if ((metacount >= 14) && (strncmp(metadata[0], "@@status", 8) == 0) && restofmsg) {
			/*
			 * @@status|timestamp|sender|origin|hostname|testname|expiretime|color|testflags|\
			 * prevcolor|changetime|ackexpiretime|ackmessage|disableexpiretime|disablemessage|\
			 * clienttstamp|flapping|classname|pagepaths
			 */
			int color = parse_color(metadata[7]);

			switch (color) {
			  case COL_GREEN:
			  case COL_YELLOW:
			  case COL_RED:
			  case COL_BLUE: /* Blue is OK, because it only arrives here when an update is sent */
			  case COL_CLEAR: /* Clear is OK, because it could still contain valid metric data */
				tstamp = atoi(metadata[1]);
				sender = metadata[2];
				hostname = metadata[4]; 
				testname = metadata[5];
				classname = (metadata[17] ? metadata[17] : "");
				pagepaths = (metadata[18] ? metadata[18] : "");
				if (hostdrop_barrier(hostname, tstamp)) {
					dbgprintf("Dropping straggler status (predates the drop) for host %s\n", hostname);
					break;
				}
				ldef = find_xymon_rrd(testname, metadata[8]);
				update_rrd(hostname, testname, restofmsg, tstamp, sender, ldef, classname, pagepaths);
				break;

			  default:
				/* Ignore reports with purple - they have no data we want. */
				break;
			}
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@data", 6) == 0) && restofmsg) {
			/* @@data|timestamp|sender|origin|hostname|testname|classname|pagepaths */
			tstamp = atoi(metadata[1]);
			sender = metadata[2];
			hostname = metadata[4]; 
			testname = metadata[5];
			classname = (metadata[6] ? metadata[6] : "");
			pagepaths = (metadata[7] ? metadata[7] : "");
			if (hostdrop_barrier(hostname, tstamp)) {
				dbgprintf("Dropping straggler data (predates the drop) for host %s\n", hostname);
			}
			else {
				ldef = find_xymon_rrd(testname, "");
				update_rrd(hostname, testname, restofmsg, tstamp, sender, ldef, classname, pagepaths);
			}
		}
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			running = 0;
			continue;
		}
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			/* Ignored */
			continue;
		}
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("XYMONCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				reopen_file(fn, "a", stdout);
				reopen_file(fn, "a", stderr);
			}
			continue;
		}
		else if (strncmp(metadata[0], "@@reload", 8) == 0) {
			reloadtime = 0;
		}
		else if ((metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			char hostdir[PATH_MAX];
			char *safehost;

			MEMDEFINE(hostdir);

			tstamp = atoi(metadata[1]);
			/* One name for all three. The directory that is deleted,
			 * the barrier that guards it and the cache entries that are
			 * discarded must agree: the name arrives as the admin typed
			 * it, and if only the path is reduced to a basename then a
			 * straggler for the host that was actually deleted walks
			 * straight past a barrier held under the unreduced name. */
			safehost = basename(metadata[3]);
			hostname = safehost;
			sprintf(hostdir, "%s/%s", rrddir, safehost);
			/* Barrier and discard cached updates BEFORE the forked
			 * deletion starts - nothing may write into the dying dir. */
			note_hostdrop(safehost, tstamp);
			rrdcache_drop_host(safehost, 0);
			dropdirectory(hostdir, 1);

			MEMUNDEFINE(hostdir);
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@droptest", 10) == 0)) {
			/*
			 * Not implemented. Mappings of testnames -> rrd files is
			 * too complex, so on the rare occasion that a single test
			 * is deleted, they will have to delete the rrd files themselves.
			 */
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			char oldhostdir[PATH_MAX];
			char newhostdir[PATH_MAX];
			char *newhostname;

			MEMDEFINE(oldhostdir);
			MEMDEFINE(newhostdir);

			hostname = metadata[3];
			newhostname = metadata[4];
			sprintf(oldhostdir, "%s/%s", rrddir, hostname);
			sprintf(newhostdir, "%s/%s", rrddir, newhostname);
			tstamp = atoi(metadata[1]);

			/* Flush pending updates into the old-named files BEFORE
			 * they move. Safe whether or not the rename then works:
			 * the values land in the files they were cached for. */
			rrdcache_drop_host(hostname, 1);

			/* Barrier the old name unconditionally - including when
			 * the rename fails.
			 *
			 * It costs nothing when the files did not move: the
			 * barrier only discards messages older than this one, so a
			 * host still living under the old name keeps writing. And
			 * it is needed exactly when the rename "fails" because a
			 * sibling already did it: the stock install runs one
			 * xymond_rrd per channel over a shared rrddir
			 * (tasks.cfg.DIST [rrdstatus] and [rrddata]) and xymond
			 * posts the marker to both, so the second worker to see it
			 * finds the directory gone - and still has its own queue of
			 * stragglers to stop. Skipping the barrier there would
			 * leave the race open in that worker on every rename. */
			note_hostdrop(hostname, tstamp);

			if ((rename(oldhostdir, newhostdir) != 0) && (errno != ENOENT)) {
				/* ENOENT is routine, not an error: a host that has
				 * produced no RRD data has no directory, and neither
				 * does one the sibling worker already renamed. */
				errprintf("Cannot rename RRD directory %s to %s: %s\n",
					  oldhostdir, newhostdir, strerror(errno));
			}

			/* The locator maps names to servers, not directories, so it
			 * is told regardless of whether this worker held any files -
			 * a host with no RRDs yet still gets renamed. */
			if (net_worker_locatorbased()) locator_rename_host(hostname, newhostname, ST_RRD);

			MEMUNDEFINE(newhostdir);
			MEMUNDEFINE(oldhostdir);
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			/* Not implemented. See "droptest". */
		}

		combo_end();

		/* 
		 * We fork a subprocess when processing drophost requests.
		 * Pickup any finished child processes to avoid zombies
		 */
		while (wait3(&childstat, WNOHANG, NULL) > 0) ;
	}

	/* Flush all cached updates to disk */
	errprintf("Shutting down, flushing cached updates to disk\n");
	rrdcacheflushall();
	errprintf("Cache flush completed\n");

	/* Close the external processor */
	shutdown_extprocessor();

	/* Close the control socket */
	close(ctlsocket);
	unlink(ctlsockaddr.sun_path);

	sendmessage_finish_local();

	return 0;
}

