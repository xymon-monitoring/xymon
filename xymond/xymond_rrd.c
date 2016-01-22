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
	time_t now;
	struct sockaddr_un ctlsockaddr;
	int ctlsocket;
	int usebackfeedqueue = 0;
	int force_backfeedqueue = 0;
	int comboflushtime;
	int checkctltime;
	struct timespec *timeout = NULL;

	libxymon_init(argv[0]);

	/* Handle program options. */
	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--rrddir=")) {
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
		else if (strncmp(argv[argi], "--flushtimeout=", 15) == 0) {
			timeout = (struct timespec *)(malloc(sizeof(struct timespec)));
			timeout->tv_sec = (atoi(argv[argi]+15));
			timeout->tv_nsec = 0;
		}
		else if (strcmp(argv[argi], "--bfq") == 0) {
			force_backfeedqueue = 1;
		}
		else if (strcmp(argv[argi], "--no-bfq") == 0) {
			force_backfeedqueue = -1;
		}
		else if (strcmp(argv[argi], "--no-cache") == 0) {
			use_rrd_cache = 0;
		}
		else if (strcmp(argv[argi], "--extcache") == 0) {
			ext_rrd_cache = 1;
		}
		else if (strcmp(argv[argi], "--no-extcache") == 0) {
			ext_rrd_cache = -1;
		}
		else if (strcmp(argv[argi], "--no-rrd") == 0) {
			no_rrd = 1;
		}
		else if (net_worker_option(argv[argi])) {
			/* Handled in the subroutine */
		}
		else if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
	}

	save_errbuf = 0;

	if (no_rrd && !processor) errprintf("RRD writing disabled, but no external processor has been specified.\n");

	if ((rrddir == NULL) && xgetenv("XYMONRRDS")) {
		rrddir = strdup(xgetenv("XYMONRRDS"));
	}

	/* Has external rrdcached running? Use env by default */
	ext_rrd_cache = ((ext_rrd_cache >= 0) ? (getenv("RRDCACHED_ADDRESS") != NULL) : 0); 
	dbgprintf("xymond_rrd: External cache: %d\n", ext_rrd_cache);

	if (exthandler && extids) setup_exthandler(exthandler, extids);

	/* Open up our extcombo message for any modifications */
	usebackfeedqueue = ((force_backfeedqueue >= 0) ? (sendmessage_init_local() > 0) : 0);
	if (force_backfeedqueue == 1 && usebackfeedqueue <= 0) {
		errprintf("Unable to set up backfeed queue when --bfq given; aborting\n");
		exit(0);
	}
	if (usebackfeedqueue) combo_start_local(); else combo_start();

	/* Do the network stuff if needed */
	net_worker_run(ST_RRD, LOC_STICKY, update_locator_hostdata);
	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
	load_client_config(NULL);
	now = gettimer();
	reloadtime = now + 600;
	comboflushtime = now + 23;
	checkctltime = now + 4;


	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);

	if (use_rrd_cache && !no_rrd) {
	/* Setup the control socket that receives cache-flush commands */
	memset(&ctlsockaddr, 0, sizeof(ctlsockaddr));
	if (xgetenv("XYMONRUNDIR") && mkdir(xgetenv("XYMONRUNDIR"), 0755) != -1) dbgprintf("Created %s\n", xgetenv("XYMONRUNDIR")); // just in case
	sprintf(ctlsockaddr.sun_path, "%s/rrdctl.%lu", xgetenv("XYMONRUNDIR"), (unsigned long)getpid());
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
	}
	else {
		checkctltime = 2147483640;	/* far into the future */
		ctlsocket = -1;
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

		/* If we need to re-open our external processor, do so */
		if (reloadextprocessor) {
			shutdown_extprocessor(); // Just in case we got a PIPE, but the pipe needs to be cleaned up still
			setup_extprocessor(processor);
			reloadextprocessor = 0;
		}

		/* See if we have any cache-control messages pending */
		if ((checkctltime < now) && (ctlsocket != -1)) {
		    dbgprintf("xymond_rrd: checking for rrdctl flush messages\n");
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
		    checkctltime = now + 7;
		}


		/* Get next message */
		msg = get_xymond_message(C_LAST, argv[0], &seq, timeout);
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
			comboflushtime = now + 23;
		}
		if ((comboflushtime < now)) {
			/*
			 * We fork a subprocess when processing drophost requests.
			 * Pickup any finished child processes to avoid zombies
			 */
			while (wait3(&childstat, WNOHANG, NULL) > 0) ;

			/* Make sure any combo of pending modify's goes out */
			/* if we don't have an idle message timeout set */
			if (timeout == NULL) {
				dbgprintf("Flushing any pending extcombo messages\n");
				combo_end();
				if (usebackfeedqueue) combo_start_local(); else combo_start();
			}
			comboflushtime = now + 23;
		}

		/* Split the message in the first line (with meta-data), and the rest */
 		eoln = strchr(msg, '\n');
		if (eoln) {
			*eoln = '\0';
			restofmsg = eoln+1;
		}


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

			/* ignore reversed tests */
			if (metadata[8] && (strchr(metadata[8], 'R') != NULL)) continue;

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
				ldef = find_xymon_rrd(testname, metadata[8]); /* flags passed for future use */
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
			ldef = find_xymon_rrd(testname, NULL);
			update_rrd(hostname, testname, restofmsg, tstamp, sender, ldef, classname, pagepaths);
		}
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			running = 0;
			continue;
		}
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			dbgprintf("Got an 'idle' message\n");
			combo_end();
			if (usebackfeedqueue) combo_start_local(); else combo_start();
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
			hostname = metadata[3];

			sprintf(hostdir, "%s/%s", rrddir, basename(hostname));
			dropdirectory(hostdir, 1);
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

			hostname = metadata[3];
			newhostname = metadata[4];
			sprintf(oldhostdir, "%s/%s", rrddir, hostname);
			sprintf(newhostdir, "%s/%s", rrddir, newhostname);
			rename(oldhostdir, newhostdir);

			if (net_worker_locatorbased()) locator_rename_host(hostname, newhostname, ST_RRD);
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			/* Not implemented. See "droptest". */
		}
	}

	/* Flush all cached updates to disk */
	errprintf("Shutting down, flushing cached updates to disk\n");
	rrdcacheflushall();
	errprintf("Cache flush completed\n");

	/* Close out any modify's waiting to be sent */
	combo_end();
	if (usebackfeedqueue) sendmessage_finish_local();

	/* Close the external processor */
	shutdown_extprocessor();

	/* Close the control socket */
	if (ctlsocket != -1 ) {
		close(ctlsocket);
		unlink(ctlsockaddr.sun_path);
	}

#ifdef DEBUG_FOR_VALGRIND
	rrd_destroy();
#endif
	return 0;
}

