/*----------------------------------------------------------------------------*/
/* Hobbit service locator daemon                                              */
/*                                                                            */
/* hobbitd_channel allows you to distribute data across multiple servers, eg. */
/* have several servers handling the RRD updates - for performance and/or     */
/* resilience purposes.                                                       */
/* For this to work, there must be a way of determining which server handles  */
/* a particular task for some ID - e.g. which server holds the RRD files for  */
/* host "foo" - so Hobbit sends data and requests to the right place.         */
/*                                                                            */
/* This daemon provides the locator service. Tasks may register ID's and      */
/* services, allowing others to lookup where they are located.                */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_locator.c,v 1.3 2006-11-23 21:12:52 henrik Exp $";

#include "config.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>         /* Someday I'll move to GNU Autoconf for this ... */
#endif
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>

#include "version.h"
#include "libbbgen.h"

volatile int keeprunning = 1;
char *logfile = NULL;


/*
 * For each TYPE of service, we keep two trees:
 * - a tree with information about what servers provide this service; and
 * - a tree with information about the hosts that have been registered to
 *   run on this particular server.
 * Some types of services dont have any host-specific data (e.g. "client"),
 * then the host-specific tree will just be empty.
 */
typedef struct serverinfo_t {
	char *servername, *serverextras;
	int  serverconfweight, serveractualweight, serverweightleft;
	enum locator_sticky_t sticky;
} serverinfo_t;
RbtHandle sitree[ST_MAX];
RbtIterator sicurrent[ST_MAX];

typedef struct hostinfo_t {
	char *hostname;
	serverinfo_t *server; /* Which server handles this host ? */
} hostinfo_t;
RbtHandle hitree[ST_MAX];


void tree_init(void)
{
	enum locator_servicetype_t stype;

	for (stype = 0; (stype < ST_MAX); stype++) {
		sitree[stype] = rbtNew(name_compare);
		hitree[stype] = rbtNew(name_compare);
	}
}

void recalc_current(enum locator_servicetype_t stype)
{
	/* Point our "last-used-server" pointer at the server with the most negative weight */
	RbtIterator handle;
	serverinfo_t *wantedserver = NULL, *oneserver;
	int minweight = 0;

	for (handle = rbtBegin(sitree[stype]); (handle != rbtEnd(sitree[stype])); handle = rbtNext(sitree[stype], handle)) {
		oneserver = (serverinfo_t *)gettreeitem(sitree[stype], handle);
		if (oneserver->serveractualweight < minweight) {
			wantedserver = oneserver;
			minweight = oneserver->serveractualweight;
		}
	}

	sicurrent[stype] = wantedserver;
}

serverinfo_t *register_server(char *servername, enum locator_servicetype_t servicetype, int weight, enum locator_sticky_t sticky, char *extras)
{
	RbtIterator handle;
	serverinfo_t *itm = NULL;

	handle = rbtFind(sitree[servicetype], servername);
	if (handle == rbtEnd(sitree[servicetype])) {
		itm = (serverinfo_t *)calloc(1, sizeof(serverinfo_t));

		itm->servername = strdup(servername);
		rbtInsert(sitree[servicetype], itm->servername, itm);
	}
	else {
		/* Update existing item */
		itm = gettreeitem(sitree[servicetype], handle);
	}

	itm->serverconfweight = itm->serveractualweight = weight;
	itm->serverweightleft = 0;
	itm->sticky = sticky;

	if (itm->serverextras) xfree(itm->serverextras);
	itm->serverextras = (extras ? strdup(extras) : NULL);

	if (weight < 0) recalc_current(servicetype);

	return itm;
}

serverinfo_t *downup_server(char *servername, enum locator_servicetype_t servicetype, char action)
{
	RbtIterator handle;
	serverinfo_t *itm = NULL;

	handle = rbtFind(sitree[servicetype], servername);
	if (handle == rbtEnd(sitree[servicetype])) return NULL;

	/* Update existing item */
	itm = gettreeitem(sitree[servicetype], handle);
	switch (action) {
	  case 'F':
		/* Flag the hosts that point to this server as un-assigned */
		for (handle = rbtBegin(hitree[servicetype]); (handle != rbtEnd(hitree[servicetype])); handle = rbtNext(hitree[servicetype], handle)) {
			hostinfo_t *hitm = (hostinfo_t *)gettreeitem(hitree[servicetype], handle);
			if (hitm->server == itm) hitm->server = NULL;
		}
		/* Fall through */

	  case 'D':
		dbgprintf("Downing server '%s' type %s\n", servername, servicetype_names[servicetype]);
		itm->serveractualweight = 0;
		itm->serverweightleft = 0;
		if (itm->serverconfweight < 0) recalc_current(servicetype);
		break;

	  case 'U':
		dbgprintf("Upping server '%s' type %s to weight %d\n", servername, servicetype_names[servicetype], itm->serverconfweight);
		itm->serveractualweight = itm->serverconfweight;
		/* Dont mess with serverweightleft - this may just be an "i'm alive" message */
		if (itm->serverconfweight < 0) recalc_current(servicetype);
		break;
	}

	return itm;
}


hostinfo_t *register_host(char *hostname, enum locator_servicetype_t servicetype, char *servername)
{
	RbtIterator handle;
	hostinfo_t *itm = NULL;

	handle = rbtFind(hitree[servicetype], hostname);
	if (handle == rbtEnd(hitree[servicetype])) {
		itm = (hostinfo_t *)calloc(1, sizeof(hostinfo_t));

		itm->hostname = strdup(hostname);
		rbtInsert(hitree[servicetype], itm->hostname, itm);
	}
	else {
		itm = gettreeitem(hitree[servicetype], handle);
	}

	/* If we dont know this server, then we must register it. If we do, just update the host record */
	handle = rbtFind(sitree[servicetype], servername);
	if (handle == rbtEnd(sitree[servicetype])) {
		dbgprintf("Registering default server '%s'\n", servername);
		itm->server = register_server(servername, servicetype, 1, 1, NULL);
	}
	else {
		serverinfo_t *newserver = gettreeitem(sitree[servicetype], handle);

		if (itm->server && (itm->server != newserver)) {
			errprintf("Warning: Host %s:%s moved from %s to %s\n", 
				  hostname, servicetype_names[servicetype], itm->server->servername, newserver->servername);
		}

		itm->server = newserver;
	}

	return itm;
}

hostinfo_t *rename_host(char *oldhostname, enum locator_servicetype_t servicetype, char *newhostname)
{
	RbtIterator handle, newhandle;
	hostinfo_t *itm = NULL;

	handle = rbtFind(hitree[servicetype], oldhostname);
	if (handle == rbtEnd(hitree[servicetype])) return NULL;
	newhandle = rbtFind(hitree[servicetype], newhostname);
	if (newhandle != rbtEnd(hitree[servicetype])) {
		errprintf("Ignored rename of %s to %s - new name already exists\n", oldhostname, newhostname);
		return NULL;
	}

	itm = gettreeitem(hitree[servicetype], handle);

	rbtErase(hitree[servicetype], handle); xfree(itm->hostname);
	itm->hostname = strdup(newhostname);
	rbtInsert(hitree[servicetype], itm->hostname, itm);

	return itm;
}

serverinfo_t *find_server_by_type(enum locator_servicetype_t servicetype)
{
	serverinfo_t *nextserver = NULL;
	RbtIterator endmarker = rbtEnd(sitree[servicetype]);

	/* 
	 * We must do weight handling here.
	 *
	 * Some weights (the serveractualweight attribute) have special meaning:
	 * Negative: Only one server receives requests. We should use the
	 *           server with the lowest weight.
	 * 0       : This server is down and cannot receive any requests.
	 * 1       : Server is up, but should only receive requests for hosts
	 *           that have been registered as resident on this server.
	 * >1      : Server is up and handles any request.
	 *
	 * When looking for a server, we do a weighted round-robin of the
	 * servers that are available. The idea is that each server has 
	 * "serveractualweight" tokens, and we use them one by one for each 
	 * request. "serveractualweightleft" tells how many tokens are left.
	 * When a server has been used once, we go to the next server which 
	 * has any tokens left. When all tokens have been used, we replenish 
	 * the token counts from "serveractualweight" and start over.
	 *
	 * sicurrent[servicetype] points to the last server that was used.
	 */

	if (sicurrent[servicetype] != endmarker) {
		serverinfo_t *lastserver;

		/* We have a last-server-used for this request */
		lastserver = gettreeitem(sitree[servicetype], sicurrent[servicetype]);

		/* 
		 * See if our the last server used handles all requests.
		 * If it does, then sicurrent[servicetype] will ALWAYS
		 * contain the server we want to use (it is updated
		 * whenever servers register or weights change).
		 */
		if (lastserver->serveractualweight < 0) return lastserver;

		/* OK, we have now used one token from this server. */
		if (lastserver->serveractualweight > 1) lastserver->serverweightleft -= 1;

		/* Go to the next server with any tokens left */
		do {
			sicurrent[servicetype] = rbtNext(sitree[servicetype], sicurrent[servicetype]);
			if (sicurrent[servicetype] == endmarker) {
				/* Start from the beginning again */
				sicurrent[servicetype] = rbtBegin(sitree[servicetype]);
			}

			nextserver = gettreeitem(sitree[servicetype], sicurrent[servicetype]);
		} while ((nextserver->serverweightleft == 0) && (nextserver != lastserver));

		if ((nextserver == lastserver) && (nextserver->serverweightleft == 0)) {
			/* Could not find any servers with a token left for us to use */
			nextserver = NULL;
		}
	}

	if (nextserver == NULL) {
		/* Restart server RR walk */
		int totalweight = 0;
		RbtIterator handle, firstok;
		serverinfo_t *srv;

		firstok = endmarker;

		/* Walk the list of servers, calculate total weight and find the first active server */
		for (handle = rbtBegin(sitree[servicetype]); 
			( (handle != endmarker) && (totalweight >= 0) ); 
			 handle = rbtNext(sitree[servicetype], handle) ) {

			srv = gettreeitem(sitree[servicetype], handle);
			if (srv->serveractualweight <= 1) continue;

			srv->serverweightleft = (srv->serveractualweight - 1);
			totalweight += srv->serverweightleft;

			if (firstok == endmarker) firstok = handle;
		}

		sicurrent[servicetype] = firstok;
		nextserver = (firstok != endmarker) ?  gettreeitem(sitree[servicetype], firstok) : NULL;
	}

	return nextserver;
}


serverinfo_t *find_server_by_host(enum locator_servicetype_t servicetype, char *hostname)
{
	RbtIterator handle;
	hostinfo_t *hinfo;

	handle = rbtFind(hitree[servicetype], hostname);
	if (handle == rbtEnd(hitree[servicetype])) {
		return NULL;
	}

	hinfo = gettreeitem(hitree[servicetype], handle);
	return hinfo->server;
}


void load_state(void)
{
	char *tmpdir;
	char *fn;
	FILE *fd;
	char buf[4096];
	char *tname, *sname, *sconfweight, *sactweight, *ssticky, *sextra, *hname;
	enum locator_servicetype_t stype;

	tmpdir = xgetenv("BBTMP"); if (!tmpdir) tmpdir = "/tmp";
	fn = (char *)malloc(strlen(tmpdir) + 100);

	sprintf(fn, "%s/locator.servers.chk", tmpdir);
	fd = fopen(fn, "r");
	if (fd) {
		while (fgets(buf, sizeof(buf), fd)) {
			serverinfo_t *srv;

			tname = sname = sconfweight = sactweight = ssticky = sextra = NULL;

			tname = strtok(buf, "|\n");
			if (tname) sname = strtok(NULL, "|\n");
			if (sname) sconfweight = strtok(NULL, "|\n");
			if (sconfweight) sactweight = strtok(NULL, "|\n");
			if (sactweight) ssticky = strtok(NULL, "|\n");
			if (ssticky) sextra = strtok(NULL, "\n");

			if (tname && sname && sconfweight && sactweight && ssticky) {
				enum locator_sticky_t sticky = (atoi(ssticky) == 1) ? LOC_STICKY : LOC_ROAMING;

				stype = get_servicetype(tname);
				srv = register_server(sname, stype, atoi(sconfweight), sticky, sextra);
				srv->serveractualweight = atoi(sactweight);
				dbgprintf("Loaded server %s/%s (cweight %d, aweight %d, %s)\n",
					srv->servername, tname, srv->serverconfweight, srv->serveractualweight,
					(srv->sticky ? "sticky" : "not sticky"));
			}
		}
		fclose(fd);
	}

	for (stype = 0; (stype < ST_MAX); stype++) recalc_current(stype);

	sprintf(fn, "%s/locator.hosts.chk", tmpdir);
	fd = fopen(fn, "r");
	if (fd) {
		while (fgets(buf, sizeof(buf), fd)) {
			tname = hname = sname = NULL;

			tname = strtok(buf, "|\n");
			if (tname) hname = strtok(NULL, "|\n");
			if (hname) sname = strtok(NULL, "|\n");

			if (tname && hname && sname) {
				enum locator_servicetype_t stype = get_servicetype(tname);

				register_host(hname, stype, sname);
				dbgprintf("Loaded host %s/%s for server %s\n", hname, tname, sname);
			}
		}
		fclose(fd);
	}
}

void save_state(void)
{
	char *tmpdir;
	char *fn;
	FILE *fd;
	int tidx;

	tmpdir = xgetenv("BBTMP"); if (!tmpdir) tmpdir = "/tmp";
	fn = (char *)malloc(strlen(tmpdir) + 100);

	sprintf(fn, "%s/locator.servers.chk", tmpdir);
	fd = fopen(fn, "w");
	if (fd == NULL) {
		errprintf("Cannot save state to %s: %s\n", fn, strerror(errno));
		return;
	}
	for (tidx = 0; (tidx < ST_MAX); tidx++) {
		const char *tname = servicetype_names[tidx];
		RbtIterator handle;
		serverinfo_t *itm;

		for (handle = rbtBegin(sitree[tidx]); (handle != rbtEnd(sitree[tidx])); handle = rbtNext(sitree[tidx], handle)) {
			itm = gettreeitem(sitree[tidx], handle);
			fprintf(fd, "%s|%s|%d|%d|%d|%s\n",
				tname, itm->servername, itm->serverconfweight, itm->serveractualweight,
				((itm->sticky == LOC_STICKY) ? 1 : 0), 
				(itm->serverextras ? itm->serverextras : ""));
		}
	}
	fclose(fd);

	sprintf(fn, "%s/locator.hosts.chk", tmpdir);
	fd = fopen(fn, "w");
	if (fd == NULL) {
		errprintf("Cannot save state to %s: %s\n", fn, strerror(errno));
		return;
	}
	for (tidx = 0; (tidx < ST_MAX); tidx++) {
		const char *tname = servicetype_names[tidx];
		RbtIterator handle;
		hostinfo_t *itm;

		for (handle = rbtBegin(hitree[tidx]); (handle != rbtEnd(hitree[tidx])); handle = rbtNext(hitree[tidx], handle)) {
			itm = gettreeitem(hitree[tidx], handle);
			if (itm->server) {
				fprintf(fd, "%s|%s|%s\n",
					tname, itm->hostname, itm->server->servername);
			}
		}
	}
	fclose(fd);
}

void sigmisc_handler(int signum)
{
	switch (signum) {
	  case SIGTERM:
		errprintf("Caught TERM signal, terminating\n");
		keeprunning = 0;
		break;

	  case SIGHUP:
		if (logfile) {
			freopen(logfile, "a", stdout);
			freopen(logfile, "a", stderr);
			errprintf("Caught SIGHUP, reopening logfile\n");
		}
		break;
	}
}


void handle_request(char *buf)
{
	const char *delims = "|\r\n\t ";

	switch (buf[0]) {
	  case 'S':
		/* Register server|type|weight|sticky|extras */
		{
			char *tok, *servername = NULL;
			enum locator_servicetype_t servicetype = ST_MAX;
			int serverweight = 0;
			enum locator_sticky_t sticky = LOC_ROAMING;
			char *serverextras = NULL;

			tok = strtok(buf, delims); if (tok) { tok = strtok(NULL, delims); }
			if (tok) { servername = tok; tok = strtok(NULL, delims); }
			if (tok) { servicetype = get_servicetype(tok); tok = strtok(NULL, delims); }
			if (tok) { serverweight = atoi(tok); tok = strtok(NULL, delims); }
			if (tok) { sticky = ((atoi(tok) == 1) ? LOC_STICKY : LOC_ROAMING); tok = strtok(NULL, delims); }
			if (tok) { serverextras = tok; tok = strtok(NULL, delims); }

			if (servername && (servicetype != ST_MAX)) {
				dbgprintf("Registering server '%s' handling %s (weight %d, %s)\n",
					servername, servicetype_names[servicetype], serverweight,
					(sticky == LOC_STICKY ? "sticky" : "not sticky"));
				register_server(servername, servicetype, serverweight, sticky, serverextras);
				strcpy(buf, "OK");
			}
			else strcpy(buf, "BADSYNTAX");
		}
		break;

	  case 'D': case 'U': case 'F':
		/* Down/Up/Forget server|type */
		{
			char *tok, *servername = NULL;
			enum locator_servicetype_t servicetype = ST_MAX;

			tok = strtok(buf, delims); if (tok) { tok = strtok(NULL, delims); }
			if (tok) { servername = tok; tok = strtok(NULL, delims); }
			if (tok) { servicetype = get_servicetype(tok); tok = strtok(NULL, delims); }

			if (servername && (servicetype != ST_MAX)) {
				downup_server(servername, servicetype, buf[0]);
				strcpy(buf, "OK");
			}
			else strcpy(buf, "BADSYNTAX");
		}
		break;

	  case 'H':
		/* Register host|type|server */
		{
			char *tok, *hostname = NULL, *servername = NULL;
			enum locator_servicetype_t servicetype = ST_MAX;

			tok = strtok(buf, delims); if (tok) { tok = strtok(NULL, delims); }
			if (tok) { hostname = tok; tok = strtok(NULL, delims); }
			if (tok) { servicetype = get_servicetype(tok); tok = strtok(NULL, delims); }
			if (tok) { servername = tok; tok = strtok(NULL, delims); }

			if (hostname && (servicetype != ST_MAX) && servername) {
				dbgprintf("Registering type/host %s/%s handled by server %s\n",
					  servicetype_names[servicetype], hostname, servername);
				register_host(hostname, servicetype, servername);
				strcpy(buf, "OK");
			}
			else strcpy(buf, "BADSYNTAX");
		}
		break;

	  case 'M':
		/* Rename host|type|newhostname */
		{
			char *tok, *oldhostname = NULL, *newhostname = NULL;
			enum locator_servicetype_t servicetype = ST_MAX;

			tok = strtok(buf, delims); if (tok) { tok = strtok(NULL, delims); }
			if (tok) { oldhostname = tok; tok = strtok(NULL, delims); }
			if (tok) { servicetype = get_servicetype(tok); tok = strtok(NULL, delims); }
			if (tok) { newhostname = tok; tok = strtok(NULL, delims); }

			if (oldhostname && (servicetype != ST_MAX) && newhostname) {
				dbgprintf("Renaming type/host %s/%s to %s\n",
					  servicetype_names[servicetype], oldhostname, newhostname);
				if (rename_host(oldhostname, servicetype, newhostname)) {
					strcpy(buf, "OK");
				}
				else {
					strcpy(buf, "FAILED");
				}
			}
			else strcpy(buf, "BADSYNTAX");
		}
		break;

	  case 'X':
	  case 'Q':
		/* Query type|host */
		{
			char *tok, *hostname = NULL;
			enum locator_servicetype_t servicetype = ST_MAX;
			int extquery = (buf[0] == 'X');
			serverinfo_t *res = NULL;

			tok = strtok(buf, delims); if (tok) { tok = strtok(NULL, delims); }
			if (tok) { servicetype = get_servicetype(tok); tok = strtok(NULL, delims); }
			if (tok) { hostname = tok; tok = strtok(NULL, delims); }

			if ((servicetype != ST_MAX) && hostname) {
				res = find_server_by_host(servicetype, hostname);

				if (res) {
					/* This host is fixed on a specific server ... */
					if (res->serveractualweight > 0) {
						/* ... and that server is UP */
						sprintf(buf, "!|%s", res->servername);
					}
					else {
						/* ... and the server is DOWN, so we cannot service the request */
						strcpy(buf, "?");
					}
				}
				else {
					/* Roaming or un-registered host */
					res = find_server_by_type(servicetype);
					if (res) {
						if (res->sticky == LOC_STICKY) {
							dbgprintf("Host %s/%s now fixed on server %s\n", 
								  hostname, servicetype_names[servicetype], res->servername);
							register_host(hostname, servicetype, res->servername);
						}
						sprintf(buf, "*|%s", res->servername);
					}
					else {
						strcpy(buf, "?");
					}
				}

				if (res && extquery) {
					int blen = strlen(buf);

					snprintf(buf+blen, sizeof(buf)-blen-1, "|%s", res->serverextras);
				}
			}
			else strcpy(buf, "BADSYNTAX");
		}
		break;

	  case 'p':
		/* Locator ping */
		sprintf(buf, "PONG|%s", VERSION);
		break;

	  case '@':
		/* Save state */
		save_state();
		strcpy(buf, "OK");
		break;

	  default:
		strcpy(buf, "BADREQUEST");
		break;
	}
}


int main(int argc, char *argv[])
{
	int daemonize = 0;
	int lsocket;
	struct sockaddr_in laddr;
	struct sigaction sa;
	int argi, opt;

	/* Dont save the output from errprintf() */
	save_errbuf = 0;

	memset(&laddr, 0, sizeof(laddr));
	laddr.sin_addr.s_addr = htonl(INADDR_ANY);
	laddr.sin_port = htons(1984);
	laddr.sin_family = AF_INET;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--listen=")) {
			char *locaddr, *p;
			int locport;

			locaddr = strchr(argv[argi], '=')+1;
			p = strchr(locaddr, ':');
			if (p) { locport = atoi(p+1); *p = '\0'; } else locport = 1984;

			memset(&laddr, 0, sizeof(laddr));
			laddr.sin_port = htons(locport);
			laddr.sin_family = AF_INET;
			if (inet_aton(locaddr, (struct in_addr *) &laddr.sin_addr.s_addr) == 0) {
				errprintf("Invalid listen address %s\n", locaddr);
				return 1;
			}
		}
		else if (strcmp(argv[argi], "--daemon") == 0) {
			daemonize = 1;
		}
		else if (strcmp(argv[argi], "--no-daemon") == 0) {
			daemonize = 0;
		}
		else if (argnmatch(argv[argi], "--logfile=")) {
			char *p = strchr(argv[argi], '=');
			logfile = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
	}

	/* Set up a socket to listen for new connections */
	lsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (lsocket == -1) {
		errprintf("Cannot create listen socket (%s)\n", strerror(errno));
		return 1;
	}

	opt = 1;
	setsockopt(lsocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	if (bind(lsocket, (struct sockaddr *)&laddr, sizeof(laddr)) == -1) {
		errprintf("Cannot bind to listener address (%s)\n", strerror(errno));
		return 1;
	}

	/* Redirect logging to the logfile, if requested */
	if (logfile) {
		freopen(logfile, "a", stdout);
		freopen(logfile, "a", stderr);
	}

	errprintf("Hobbit locator version %s starting\n", VERSION);
	errprintf("Listening on %s:%d\n", inet_ntoa(laddr.sin_addr), ntohs(laddr.sin_port));

	if (daemonize) {
		pid_t childpid;

		freopen("/dev/null", "a", stdin);

		/* Become a daemon */
		childpid = fork();
		if (childpid < 0) {
			/* Fork failed */
			errprintf("Could not fork\n");
			exit(1);
		}
		else if (childpid > 0) {
			/* Parent - exit */
			exit(0);
		}
		/* Child (daemon) continues here */
		setsid();
	}

	setup_signalhandler("hobbitd_locator");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigmisc_handler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	tree_init();
	load_state();

	do {
		ssize_t n;
		struct sockaddr_in remaddr;
		socklen_t remaddrsz;
		char buf[32768];

		remaddrsz = sizeof(remaddr);
		n = recvfrom(lsocket, buf, sizeof(buf), MSG_WAITALL, (struct sockaddr *)&remaddr, &remaddrsz);
		if (n == -1) {
			errprintf("Recv error: %s\n", strerror(errno));
			continue;
		}
		else if (n == 0) {
			continue;
		}

		buf[n] = '\0';
		dbgprintf("Got message from %s:%d : '%s'\n", 
				inet_ntoa(remaddr.sin_addr), ntohs(remaddr.sin_port), buf);

		handle_request(buf);

		n = sendto(lsocket, buf, strlen(buf)+1, MSG_DONTWAIT, (struct sockaddr *)&remaddr, remaddrsz);
		if (n == -1) {
			if (errno == EAGAIN) {
				errprintf("Out-queue full to %s, dropping response\n", inet_ntoa(remaddr.sin_addr));
			}
			else {
				errprintf("Send failure %s while sending to %s\n", 
					  strerror(errno), inet_ntoa(remaddr.sin_addr));
			}
		}

	} while (keeprunning);

	save_state();

	return 0;
}

