/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* This Xymon worker module saves the client messages that arrive on the      */
/* CLICHG channel, for use when looking at problems with a host.              */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include "libxymon.h"
#include "xymond_worker.h"

#include <signal.h>


#define MAX_META 20	/* The maximum number of meta-data items in a message */

#define DEFAULT_RECENTPERIOD 3600	/* --recent-period fallback, in seconds */
#define DEFAULT_RECENTCOUNT  5		/* --recent-count fallback */
#define MAX_RECENTCOUNT      10000	/* --recent-count cap: bounds the per-host history allocation */
/* DEFAULT_MINLOGSPACE is shared with xymond_history via lib/misc.h */

typedef struct savetimes_t {
	char *hostname;
	time_t *tstamp;		/* Ring of the 'maxrecentcount' most recent save times, NULL when the throttle is off */
	int oldest;		/* Oldest slot in tstamp[] - the next one overwritten */
	time_t suppress_until;	/* Skip saves for this host until this time (write-error backoff) */
} savetimes_t;
void * savetimes;

static char *clientlogdir = NULL;
int nextfscheck = 0;


static void free_savetimes(savetimes_t *itm)
{
	if (!itm) return;
	free(itm->hostname);
	free(itm->tstamp);
	free(itm);
}

/* Drop a host's throttle bookkeeping so a removed host does not leak its
 * savetimes entry (hostname + tstamp ring) for the life of the process. */
static void drop_savetimes(char *hostname)
{
	savetimes_t *itm = (savetimes_t *)xtreeDelete(savetimes, hostname);
	free_savetimes(itm);
}

/*
 * Find or create the per-host throttle entry, keyed on the sanitized host
 * name the files are stored under (so path-prefixed variants of one host
 * cannot each claim a separate budget). The tstamp ring is allocated only
 * when the throttle is enabled (recentperiod > 0 and maxrecentcount > 0); the
 * small entry itself is always kept, so the per-host write-error backoff has
 * somewhere to record its state even with the throttle off. Returns NULL
 * (error logged) only on allocation failure.
 */
static savetimes_t *get_savetimes(char *hostname, int recentperiod, int maxrecentcount)
{
	xtreePos_t handle = xtreeFind(savetimes, hostname);
	savetimes_t *itm;

	if (handle != xtreeEnd(savetimes)) return (savetimes_t *)xtreeData(savetimes, handle);

	itm = (savetimes_t *)calloc(1, sizeof(savetimes_t));
	if (itm) {
		itm->hostname = strdup(hostname);
		/* Sized from --recent-count: deciding "N or more saves in the
		 * window" only ever needs the N most recent save times. */
		if ((recentperiod > 0) && (maxrecentcount > 0)) itm->tstamp = (time_t *)calloc(maxrecentcount, sizeof(time_t));
	}
	if (!itm || !itm->hostname || ((recentperiod > 0) && (maxrecentcount > 0) && !itm->tstamp)) {
		errprintf("Out of memory tracking saves for host %s, dropping message\n", hostname);
		free_savetimes(itm);
		return NULL;
	}
	xtreeAdd(savetimes, itm->hostname, itm);
	return itm;
}

/*
 * A save for this host failed for a reason chkfreespace() cannot see - a
 * read-only mount or a permission problem, where free space still looks
 * fine. Back off this host only for the same 5-minute window a full disk
 * uses, so a persistent failure retries and logs at most once per window
 * without suppressing saves for every other host. The save-time ring is left
 * untouched - it counts real saves, so a failed attempt must not consume a
 * host's throttle budget.
 */
static void suppress_host_after_write_error(savetimes_t *itm, time_t now)
{
	itm->suppress_until = now + 300;
	errprintf("Pausing hostdata saves for host %s for 5 minutes after a write error\n", itm->hostname);
}

/*
 * Confine a channel-supplied name to a single path component. safe_basename()
 * rejects '/'-bearing and the degenerate ".", "..", "" names (a bare "..",
 * which POSIX basename() would pass through, must not escape the hostdata
 * tree). Returns the confined name (pointing into 'raw'), or NULL with an
 * error logged; 'what' names the field for the message.
 */
static char *confine_name(char *raw, const char *what)
{
	char *safe = safe_basename(raw);
	if (!*safe) {
		errprintf("Unsafe %s '%s' in a hostdata message, dropping it\n", what, raw);
		return NULL;
	}
	return safe;
}

/* Join "<base>/<leaf>" into 'out'; return 0 with an error logged if it would
 * not fit. 'what' names the path for the message. */
static int join_path(char *out, size_t sz, const char *base, const char *leaf, const char *what)
{
	if (snprintf(out, sz, "%s/%s", base, leaf) >= (int)sz) {
		errprintf("Hostdata path too long (%s), dropping the message\n", what);
		return 0;
	}
	return 1;
}


void sig_handler(int signum)
{
	/*
	 * Why this? Because we must have our own signal handler installed to call wait()
	 */
	switch (signum) {
	  case SIGCHLD:
		  break;

	  case SIGHUP:
		  nextfscheck = 0;
		  break;
	}
}

void update_locator_hostdata(char *id)
{
	DIR *fd;
	struct dirent *d;

	fd = opendir(clientlogdir);
	if (fd == NULL) {
		errprintf("Cannot scan directory %s\n", clientlogdir);
		return;
	}

	while ((d = readdir(fd)) != NULL) {
		if (*(d->d_name) == '.') continue;
		locator_register_host(d->d_name, ST_HOSTDATA, id);
	}

	closedir(fd);
}


int main(int argc, char *argv[])
{
	char *msg;
	int running;
	int argi, seq;
	int recentperiod = DEFAULT_RECENTPERIOD;
	int maxrecentcount = DEFAULT_RECENTCOUNT;
	int logdirfull = 0;
	int minlogspace = DEFAULT_MINLOGSPACE;
	struct sigaction sa;

	/* Handle program options. */
	for (argi = 1; (argi < argc); argi++) {
                if (argnmatch(argv[argi], "--logdir=")) {
			clientlogdir = strchr(argv[argi], '=')+1;
		}
		else if (argnmatch(argv[argi], "--recent-period=")) {
			/* Minutes. 0 disables the throttle: the cutoff lands at
			 * "now", so no earlier save is ever inside the window.
			 * Negatives clamp to 0 - with atoi they also meant
			 * "always save" (a future cutoff counts no saves). */
			recentperiod = 60*parse_int_opt(argv[argi], 0, INT_MAX/60, DEFAULT_RECENTPERIOD/60);
		}
		else if (argnmatch(argv[argi], "--recent-count=")) {
			/* 0 keeps its historical meaning: never save anything;
			 * negatives clamp to it (they also never saved with atoi).
			 * The cap keeps the per-host save-time history (8 bytes
			 * per slot) at a sane size. */
			maxrecentcount = parse_int_opt(argv[argi], 0, MAX_RECENTCOUNT, DEFAULT_RECENTCOUNT);
		}
		else if (argnmatch(argv[argi], "--minimum-free=")) {
			/* Percent of filesystem space; 0 disables the check, and
			 * negatives clamp to it (they also disabled it with atoi). */
			minlogspace = parse_int_opt(argv[argi], 0, 100, DEFAULT_MINLOGSPACE);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			/*
			 * A global "debug" variable is available. If
			 * it is set, then "dbgprintf()" outputs debug messages.
			 */
			debug = 1;
		}
		else if (net_worker_option(argv[argi])) {
			/* Handled in the subroutine */
		}
	}

	if (clientlogdir == NULL) clientlogdir = xgetenv("CLIENTLOGS");
	if (clientlogdir == NULL) {
		clientlogdir = (char *)malloc(strlen(xgetenv("XYMONVAR")) + 10);
		sprintf(clientlogdir, "%s/hostdata", xgetenv("XYMONVAR"));
	}

	save_errbuf = 0;

	/* Do the network stuff if needed */
	net_worker_run(ST_HOSTDATA, LOC_STICKY, update_locator_hostdata);

	setup_signalhandler("xymond_hostdata");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	signal(SIGCHLD, SIG_IGN);
	sigaction(SIGHUP, &sa, NULL);
	signal(SIGPIPE, SIG_DFL);

	savetimes = xtreeNew(strcasecmp);

	running = 1;
	while (running) {
		char *eoln, *restofmsg, *p;
		char *metadata[MAX_META+1];
		int metacount;

		msg = get_xymond_message(C_CLICHG, "xymond_hostdata", &seq, NULL);
		if (msg == NULL) {
			/*
			 * get_xymond_message will return NULL if xymond_channel closes
			 * the input pipe. We should shutdown when that happens.
			 */
			running = 0;
			continue;
		}

		if (nextfscheck < gettimer()) {
			logdirfull = (chkfreespace(clientlogdir, minlogspace, minlogspace) != 0);
			if (logdirfull) errprintf("Hostdata directory %s has less than %d%% free space - disabling save of data for 5 minutes\n", clientlogdir, minlogspace);
			nextfscheck = gettimer() + 300;
		}

		/* Split the message in the first line (with meta-data), and the rest */
 		eoln = strchr(msg, '\n');
		if (eoln) {
			*eoln = '\0';
			restofmsg = eoln+1;
		}
		else {
			restofmsg = "";
		}

		metacount = 0; 
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < MAX_META)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;

		/* @@clichg|timestamp|sender|hostname|testname|... */
		if ((metacount > 4) && (strncmp(metadata[0], "@@clichg", 8) == 0)) {
			savetimes_t *itm;
			int forced;
			time_t now = gettimer();
			time_t cutoff;
			char hostdir[PATH_MAX];
			char fn[PATH_MAX];
			char *safehost, *safetest;
			FILE *fd;

			forced = ((metacount > 5) && (strcmp(metadata[5], "forced") == 0));

			/* --recent-count=0 disables ordinary saves; explicit requests bypass it. */
			if (!forced && (maxrecentcount == 0)) continue;

			safehost = confine_name(metadata[3], "hostname");
			safetest = confine_name(metadata[4], "testname");
			if (!safehost || !safetest) continue;

			itm = get_savetimes(safehost, recentperiod, maxrecentcount);
			if (!itm) continue;

			/* Per-host write-error backoff: skip this host until its window
			 * passes, so a persistent failure does not retry on every change. */
			if (now < itm->suppress_until) continue;

			/*
			 * Save unless the host already had its --recent-count saves
			 * in the past 'recentperiod' seconds. tstamp[] is a ring of
			 * the most recent save times, with 'oldest' indexing the
			 * oldest slot, so the limit is reached exactly when that
			 * slot is still inside the window. A NULL ring means the
			 * throttle is off (--recent-period=0): there is no limit.
			 *
			 * Floor the cutoff at 0: gettimer() is monotonic (seconds
			 * since boot), so a never-used slot's calloc'ed zero is not
			 * "long ago" - without the floor it would land inside the
			 * window whenever uptime is below 'recentperiod', and all
			 * client data would be dropped unlogged until the machine
			 * has been up that long.
			 */
			cutoff = (now > recentperiod) ? (now - recentperiod) : 0;
			if (!logdirfull && (forced || !itm->tstamp || (itm->tstamp[itm->oldest] <= cutoff))) {
				int written, closestatus, ok = 1;

				if (!join_path(hostdir, sizeof(hostdir), clientlogdir, safehost, "host directory")) continue;
				mkdir(hostdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
				if (!join_path(fn, sizeof(fn), hostdir, safetest, "filename")) continue;
				fd = fopen(fn, "w");
				if (fd == NULL) {
					errprintf("Cannot create file %s: %s\n", fn, strerror(errno));
					suppress_host_after_write_error(itm, now);
					continue;
				}
				written = fwrite(restofmsg, 1, strlen(restofmsg), fd);
				if (written != strlen(restofmsg)) {
					errprintf("Cannot write hostdata file %s: %s\n", fn, strerror(errno));
					closestatus = fclose(fd);	/* Ignore any close errors */
					ok = 0;
				}
				else {
					closestatus = fclose(fd);
					if (closestatus != 0) {
						errprintf("Cannot write hostdata file %s: %s\n", fn, strerror(errno));
						ok = 0;
					}
				}

				if (!ok) {
					remove(fn);
					suppress_host_after_write_error(itm, now);
					continue;
				}

				/* Only a completed save consumes a throttle slot: a failed
				 * attempt stored nothing, so counting it would let an I/O
				 * problem eat the budget and then drop good data for up to
				 * a full window after the problem clears. */
				if (!forced && itm->tstamp) {
					itm->tstamp[itm->oldest] = now;
					itm->oldest = (itm->oldest + 1) % maxrecentcount;
				}
			}
		}

		/*
		 * A "shutdown" message is sent when the master daemon
		 * terminates. The child workers should shutdown also.
		 */
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			running = 0;
			continue;
		}
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			/* Ignored */
			continue;
		}

		/*
		 * A "logrotate" message is sent when the Xymon logs are
		 * rotated. The child workers must re-open their logfiles,
		 * typically stdin and stderr - the filename is always
		 * provided in the XYMONCHANNEL_LOGFILENAME environment.
		 */
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("XYMONCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				reopen_file(fn, "a", stdout);
				reopen_file(fn, "a", stderr);
			}
			continue;
		}

		else if ((metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			/* @@drophost|timestamp|sender|hostname */
			char hostdir[PATH_MAX];
			char *safehost = confine_name(metadata[3], "hostname");
			if (!safehost) continue;
			if (!join_path(hostdir, sizeof(hostdir), clientlogdir, safehost, "host directory")) continue;
			dropdirectory(hostdir, 1);
			drop_savetimes(safehost);
		}

		else if ((metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			/* @@renamehost|timestamp|sender|hostname|newhostname */
			char oldhostdir[PATH_MAX], newhostdir[PATH_MAX];
			char *safeold = confine_name(metadata[3], "hostname");
			char *safenew = confine_name(metadata[4], "new hostname");
			if (!safeold || !safenew) continue;
			if (!join_path(oldhostdir, sizeof(oldhostdir), clientlogdir, safeold, "host directory") ||
			    !join_path(newhostdir, sizeof(newhostdir), clientlogdir, safenew, "host directory")) continue;
			rename(oldhostdir, newhostdir);
			/* The old name's throttle state moves with the directory:
			 * drop it so it neither leaks nor lingers under the old key.
			 * The new name gets a fresh entry on its next clichg. */
			drop_savetimes(safeold);

			if (net_worker_locatorbased()) locator_rename_host(metadata[3], metadata[4], ST_HOSTDATA);
		}
		else if (strncmp(metadata[0], "@@reload", 8) == 0) {
			/* Do nothing */
		}
	}

	return 0;
}

