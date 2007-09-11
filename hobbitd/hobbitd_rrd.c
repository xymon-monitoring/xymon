/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This is a hobbitd worker module for the "status" and "data" channels.      */
/* This module maintains the RRD database-files, updating them as new         */
/* data arrives.                                                              */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_rrd.c,v 1.36 2007-09-11 21:20:54 henrik Exp $";

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

#include "libbbgen.h"
#include "hobbitd_worker.h"

#include "hobbitd_rrd.h"
#include "do_rrd.h"

#include <signal.h>


#define MAX_META 20	/* The maximum number of meta-data items in a message */

static int running = 1;

typedef struct rrddeftree_t {
	char *key;
	int count;
	char **defs;
} rrddeftree_t;
static RbtHandle rrddeftree;

static void sig_handler(int signum)
{
	switch (signum) {
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

	rrddeftree = rbtNew(name_compare);

	sprintf(fn, "%s/etc/hobbit-rrddefinitions.cfg", xgetenv("BBHOME"));
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
				rbtInsert(rrddeftree, newrec->key, newrec);

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
		rbtInsert(rrddeftree, newrec->key, newrec);
	}

	stackfclose(fd);

loaddone:
	freestrbuffer(inbuf);

	/* Check if the default record exists */
	if (rbtFind(rrddeftree, "") == rbtEnd(rrddeftree)) {
		/* Create the default record */
		newrec = (rrddeftree_t *)malloc(sizeof(rrddeftree_t));
		newrec->key = strdup("");
		newrec->defs = (char **)malloc(4 * sizeof(char *));;
		newrec->defs[0] = strdup("RRA:AVERAGE:0.5:1:576");
		newrec->defs[1] = strdup("RRA:AVERAGE:0.5:6:576");
		newrec->defs[2] = strdup("RRA:AVERAGE:0.5:24:576");
		newrec->defs[3] = strdup("RRA:AVERAGE:0.5:288:576");
		newrec->count = 4;
		rbtInsert(rrddeftree, newrec->key, newrec);
	}
}

char **get_rrd_definition(char *key, int *count)
{
	RbtHandle handle;

	handle = rbtFind(rrddeftree, key);
	if (handle == rbtEnd(rrddeftree)) {
		handle = rbtFind(rrddeftree, "");	/* The default record */
	}
	rrddeftree_t *rec = (rrddeftree_t *)gettreeitem(rrddeftree, handle);

	*count = rec->count;
	return rec->defs;
}

int main(int argc, char *argv[])
{
	char *msg;
	int argi, seq;
	struct sigaction sa;
	char *exthandler = NULL;
	char *extids = NULL;
	struct sockaddr_un ctlsockaddr;
	int ctlsocket;

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
		else if (net_worker_option(argv[argi])) {
			/* Handled in the subroutine */
		}
	}

	save_errbuf = 0;

	if ((rrddir == NULL) && xgetenv("BBRRDS")) {
		rrddir = strdup(xgetenv("BBRRDS"));
	}

	if (exthandler && extids) setup_exthandler(exthandler, extids);

	/* Do the network stuff if needed */
	net_worker_run(ST_RRD, LOC_STICKY, update_locator_hostdata);

	setup_signalhandler("hobbitd_rrd");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	signal(SIGPIPE, SIG_DFL);

	/* Setup the control socket that receives cache-flush commands */
	memset(&ctlsockaddr, 0, sizeof(ctlsockaddr));
	sprintf(ctlsockaddr.sun_path, "%s/rrdctl.%d", xgetenv("BBTMP"), getpid());
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

	running = 1;
	while (running) {
		char *eoln, *restofmsg = NULL;
		char *metadata[MAX_META+1];
		int metacount;
		char *p;
		char *hostname = NULL, *testname = NULL, *sender = NULL;
		hobbitrrd_t *ldef = NULL;
		time_t tstamp;
		int childstat;
                ssize_t n;
		char ctlbuf[PATH_MAX];
		int gotcachectlmessage;

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
		msg = get_hobbitd_message(C_LAST, argv[0], &seq, NULL);
		if (msg == NULL) {
			running = 0;
			continue;
		}

		if (timewarp) {
			errprintf("WARNING: Time has gone BACK by %d seconds.\n", timewarp);
			if (timewarp >= 300) errprintf("This will cause problems with RRD updates already registered\n");
			errprintf("hobbitd_rrd module is restarting to pick up new time\n");
			running = 0;
		}

		/* Split the message in the first line (with meta-data), and the rest */
 		eoln = strchr(msg, '\n');
		if (eoln) {
			*eoln = '\0';
			restofmsg = eoln+1;
		}

		/* Parse the meta-data */
		metacount = 0; 
		p = gettok(msg, "|");
		while (p && (metacount < MAX_META)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;

		if ((metacount >= 14) && (strncmp(metadata[0], "@@status", 8) == 0)) {
			/*
			 * @@status|timestamp|sender|origin|hostname|testname|expiretime|color|testflags|\
			 * prevcolor|changetime|ackexpiretime|ackmessage|disableexpiretime|disablemessage 
			 */
			int color = parse_color(metadata[7]);

			switch (color) {
			  case COL_GREEN:
			  case COL_YELLOW:
			  case COL_RED:
			  case COL_BLUE: /* Blue is OK, because it only arrives here when an update is sent */
				tstamp = atoi(metadata[1]);
				sender = metadata[2];
				hostname = metadata[4]; 
				testname = metadata[5];
				ldef = find_hobbit_rrd(testname, metadata[8]);
				update_rrd(hostname, testname, restofmsg, tstamp, sender, ldef);
				break;

			  default:
				/* Ignore reports with purple, blue or clear - they have no data we want. */
				break;
			}
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@data", 6) == 0)) {
			/* @@data|timestamp|sender|origin|hostname|testname */
			tstamp = atoi(metadata[1]);
			sender = metadata[2];
			hostname = metadata[4]; 
			testname = metadata[5];
			ldef = find_hobbit_rrd(testname, "");
			update_rrd(hostname, testname, restofmsg, tstamp, sender, ldef);
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
			char *fn = xgetenv("HOBBITCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				freopen(fn, "a", stdout);
				freopen(fn, "a", stderr);
			}
			continue;
		}
		else if ((metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			char hostdir[PATH_MAX];
			hostname = metadata[3];

			MEMDEFINE(hostdir);

			sprintf(hostdir, "%s/%s", rrddir, hostname);
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
			rename(oldhostdir, newhostdir);

			if (net_worker_locatorbased()) locator_rename_host(hostname, newhostname, ST_RRD);

			MEMUNDEFINE(newhostdir);
			MEMUNDEFINE(oldhostdir);
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			/* Not implemented. See "droptest". */
		}

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

	/* Close the control socket */
	close(ctlsocket);
	unlink(ctlsockaddr.sun_path);

	return 0;
}

