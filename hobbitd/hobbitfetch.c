/*----------------------------------------------------------------------------*/
/* Hobbit monitor                                                             */
/*                                                                            */
/* This is used to pull client data from the client "msgcache" daemon         */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitfetch.c,v 1.13 2006-07-22 11:23:56 henrik Exp $";

#include "config.h"

#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>		/* Someday I'll move to GNU Autoconf for this ... */
#endif
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <ctype.h>
#include <signal.h>

#include "libbbgen.h"

volatile int running = 1;
volatile time_t reloadtime = 0;
volatile int dumpsessions = 0;
char *serverip = "127.0.0.1";
int pollinterval = 60; /* Seconds between polls, +/- 15 seconds */
time_t whentoqueue = 0;
int serverid = 1;
int errorloginterval = 900;

/*
 * When we send in a "client" message to the server, we get the client configuration
 * back. Save this in a tree so the next time we contact the client, we can provide
 * the configuration for it in the "pullclient" data.
 */
typedef struct clients_t {
	char *hostname;
	time_t nextpoll;	/* When we should contact this host again */
	time_t suggestpoll;	/* When the client msgcache is expected to get the next client message */
	time_t nexterrortxt;	/* When we'll log errors on this client again */
	char *clientdata;	/* This hosts' client configuration data */
	int busy;		/* If set, then we are currently processing this host */
} clients_t;
RbtHandle clients;

typedef enum { C_CLIENT, C_SERVER } conntype_t;
typedef struct conn_t {
	unsigned long seq;
	conntype_t ctype;		/* Talking to a client or a server? */
	int savedata;			/* Save the data to the client data buffer */
	clients_t *client;		/* Which client this refers to. */
	time_t tstamp;			/* When did the connection start. */
	struct sockaddr_in caddr;	/* Destination address */
	int sockfd;			/* Socket */
	enum { C_READING, C_WRITING, C_CLEANUP } action;	/* What are we doing? */
	strbuffer_t *msgbuf;		/* I/O buffer */
	int sentbytes;
	struct conn_t *next;
} conn_t;
conn_t *chead = NULL;			/* Head of current connection queue */
conn_t *ctail = NULL;			/* Tail of current connection queue */
unsigned long connseq = 0;		/* Sequence number, to identify requests in debugging */
int needcleanup = 0;			/* Do we need to perform a cleanup? */

void sigmisc_handler(int signum)
{
	switch (signum) {
	  case SIGTERM:
		errprintf("Caught TERM signal, terminating\n");
		running = 0;
		break;

	  case SIGHUP:
		reloadtime = 0;
		break;

	  case SIGUSR1:
		dumpsessions = 1;
		break;
	}
}

char *addrstring(struct sockaddr_in *addr)
{
	static char res[100];

	sprintf(res, "%s:%d", inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
	return res;
}

void flag_cleanup(conn_t *conn)
{
	/* Called whenever a connection request is complete */
	conn->action = C_CLEANUP;
	needcleanup = 1;
}

void addrequest(conntype_t ctype, char *destip, int portnum, strbuffer_t *req, clients_t *client)
{
	/*
	 * Add a new request to the connection queue.
	 * This sets up all of the connection parameters (IP, port, request type,
	 * i/o buffers), allocates a socket and starts connecting to the peer.
	 * The connection begins by writing data.
	 */

	conn_t *newconn;
	int n;

	connseq++;
	newconn = (conn_t *)calloc(1, sizeof(conn_t));
	newconn->seq = connseq;
	newconn->client = client;
	newconn->msgbuf = req;
	newconn->sentbytes = 0;
	newconn->ctype = ctype;
	newconn->savedata = ((ctype == C_SERVER) && (strncmp(STRBUF(req), "client ", 7) == 0));
	newconn->action = C_WRITING;
	newconn->tstamp = time(NULL);

	/* Setup the address. */
	newconn->caddr.sin_port = htons(portnum);
	newconn->caddr.sin_family = AF_INET;
	if (inet_aton(destip, (struct in_addr *)&newconn->caddr.sin_addr.s_addr) == 0) {
		/* Bad IP. */
		time_t now = time(NULL);
		if (debug || (newconn->client->nexterrortxt < now)) {
			errprintf("Invalid client IP: %s (req %lu)\n", destip, newconn->seq);
			newconn->client->nexterrortxt = now + errorloginterval;
		}
		flag_cleanup(newconn);
		goto done;
	}

	newconn->sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (newconn->sockfd == -1) {
		/* No more sockets available. Try again later. */
		errprintf("Out of sockets (req %lu)\n", newconn->seq);
		flag_cleanup(newconn);
		goto done;
	}
	fcntl(newconn->sockfd, F_SETFL, O_NONBLOCK);

	if (debug) {
		char dbgmsg[100];

		snprintf(dbgmsg, sizeof(dbgmsg), "%s\n", STRBUF(req));
		dbgprintf("Queuing request %lu to %s for %s: '%s'\n", 
			connseq, addrstring(&newconn->caddr), client->hostname, dbgmsg);
	}

	/* All set ... start the connection */
	n = connect(newconn->sockfd, (struct sockaddr *)&newconn->caddr, sizeof(newconn->caddr)); 
	if ((n == -1) && (errno != EINPROGRESS)) {
		/* Immediate connect failure - drop it */
		time_t now = time(NULL);
		if (debug || (newconn->client->nexterrortxt < now)) {
			errprintf("Could not connect to %s (req %lu): %s\n", 
				  addrstring(&newconn->caddr), newconn->seq, strerror(errno));
			newconn->client->nexterrortxt = now + errorloginterval;
		}

		flag_cleanup(newconn);
		goto done;
	}

done:
	/* Add it to our list of active connections */
	if (ctail) {
		ctail->next = newconn;
		ctail = newconn;
	}
	else {
		chead = ctail = newconn;
	}
}


void senddata(conn_t *conn)
{
	/* Write data to a peer connection (client or server) */
	int n, togo;
	char *startp;

	togo = STRBUFLEN(conn->msgbuf) - conn->sentbytes;
	startp = STRBUF(conn->msgbuf) + conn->sentbytes;
	n = write(conn->sockfd, startp, togo);

	if (n == -1) {
		/* Write failure. Also happens if connecting to peer fails */
		time_t now = time(NULL);
		if (debug || (conn->client->nexterrortxt < now)) {
			errprintf("Connection lost during connect/write to %s (req %lu): %s\n", 
				  addrstring(&conn->caddr), conn->seq, strerror(errno));
			conn->client->nexterrortxt = now + errorloginterval;
		}
		flag_cleanup(conn);
	}
	else if (n >= 0) {
		dbgprintf("Sent %d bytes to %s (req %lu)\n", n, addrstring(&conn->caddr), conn->seq);
		conn->sentbytes += n;
		if (conn->sentbytes == STRBUFLEN(conn->msgbuf)) {
			/* Everything has been sent, so switch to READ mode */
			clearstrbuffer(conn->msgbuf);
			shutdown(conn->sockfd, SHUT_WR);
			conn->action = C_READING;
		}
	}
}


void process_clientdata(conn_t *conn)
{
	/* 
	 * Handle data we received while talking to the Hobbit client.
	 * This will be a list of messages we must send to the server.
	 * Each of the messages are pushed to the server through
	 * new C_SERVER requests.
	 */

	char *mptr, *databegin, *msgbegin;
	int portnum = atoi(xgetenv("BBPORT"));

	databegin = strchr(STRBUF(conn->msgbuf), '\n');
	if (!databegin || (STRBUFLEN(conn->msgbuf) == 0)) {
		/* No data - we're done */
		flag_cleanup(conn);
		return;
	}
	*databegin = '\0'; /* End the first line, and point msgbegin at start of data */
	msgbegin = (databegin+1);

	/*
	 * First line of the message is a list of numbers, telling 
	 * us the size of each of the individual messages we got from 
	 * the client, and how long ago they were received.
	 */
	mptr = strtok(STRBUF(conn->msgbuf), " \t");
	while (mptr) {
		int msgbytes, msgago;
		char savech;
		strbuffer_t *req;

		if (sscanf(mptr, "%d:%d", &msgbytes, &msgago) == 2) {
			msgbytes = atoi(mptr);
			if ((msgbytes <= 0) || ((msgbegin + msgbytes) - STRBUF(conn->msgbuf)) > STRBUFLEN(conn->msgbuf)) {
				/* Someone is playing games with us */
				errprintf("Invalid message data from %s (req %lu): Current offset %d, msgbytes %d, msglen %d\n",
					  addrstring(&conn->caddr), conn->seq,
					  (msgbegin - STRBUF(conn->msgbuf)), msgbytes, STRBUFLEN(conn->msgbuf));
				return;
			}

			savech = *(msgbegin + msgbytes);
			*(msgbegin + msgbytes) = '\0';
			req = newstrbuffer(msgbytes+100);
			addtobuffer(req, msgbegin);

			if (strncmp(msgbegin, "client ", 7) == 0) {
				/*
				 * It's a client message. See when it was received in
				 * msgcache, and adjust our next poll time accordingly.
				 */
				char msgcachesection[100];

				conn->client->suggestpoll = time(NULL) - (msgago % 300) + 300 + 10;
				dbgprintf("Client %s (req %lu) received a client message %d secs ago, poll again at %lu\n",
					addrstring(&conn->caddr), conn->seq, msgago,
					conn->client->suggestpoll);

				/* Add a section to the client message with cache delay info */
				snprintf(msgcachesection, sizeof(msgcachesection),
					 "[msgcache]\nCachedelay: %d\n[proxy]\nClientIP:%s", 
					 msgago, addrstring(&conn->caddr));
				addtobuffer(req, msgcachesection);
			}
			else if ( (strncmp(msgbegin, "status", 6) == 0) ||
				  (strncmp(msgbegin, "data", 4) == 0) ) {
				char sourcemsg[100];

				/* Add a line to the message showing where it came from */
				sprintf(sourcemsg, "\nStatus message received from %s\n", 
					addrstring(&conn->caddr));
				addtobuffer(req, sourcemsg);
			}

			addrequest(C_SERVER, serverip, portnum, req, conn->client);

			*(msgbegin + msgbytes) = savech;

			msgbegin += msgbytes;
			mptr = strtok(NULL, " \t");
		}
		else {
			errprintf("Garbled pullclient response from %s (req %lu), token %s\n",
				  addrstring(&conn->caddr), conn->seq, mptr);
			mptr = NULL;
		}
	}
}

void process_serverdata(conn_t *conn)
{
	/*
	 * Handle data we received while talking to the Hobbit server.
	 * We only handle the "client" message response.
	 */

	if (conn->savedata) {
		/*
		 * We just sent a "client" message. So
		 * save the response, which is the client configuration
		 * data that we will provide to the client the next time
		 * we contact him.
		 */

		if (conn->client->clientdata) xfree(conn->client->clientdata);
		conn->client->clientdata = grabstrbuffer(conn->msgbuf);
		conn->msgbuf = NULL;
		dbgprintf("Client data for %s (req %lu): %s\n", conn->client->hostname, conn->seq, 
			(conn->client->clientdata ? conn->client->clientdata : "<Null>"));
	}
}

void grabdata(conn_t *conn)
{
	int n;
	char buf[8192];

	/* Read data from a peer connection (client or server) */
        n = read(conn->sockfd, buf, sizeof(buf));
	if (n == -1) {
		/* Read failure */
		time_t now = time(NULL);
		if (debug || (conn->client->nexterrortxt < now)) {
			errprintf("Connection lost during read from %s (req %lu): %s\n", 
				  addrstring(&conn->caddr), conn->seq, strerror(errno));
			conn->client->nexterrortxt = now + errorloginterval;
		}
		flag_cleanup(conn);
	}
	else if (n > 0) {
		/* Save the data */
		dbgprintf("Got %d bytes of data from %s (req %lu)\n", 
			n, addrstring(&conn->caddr), conn->seq);
		buf[n] = '\0';
		addtobuffer(conn->msgbuf, buf);
	}
	else if (n == 0) {
		/* Done reading. Process the data. */
		dbgprintf("Done reading data from %s (req %lu)\n", 
			addrstring(&conn->caddr), conn->seq);
		shutdown(conn->sockfd, SHUT_RDWR);
		flag_cleanup(conn);

		switch (conn->ctype) {
		  case C_CLIENT:
			process_clientdata(conn);
			break;

		  case C_SERVER:
			process_serverdata(conn);
			break;
		}
	}
}

void set_polltime(clients_t *client)
{
	time_t now = time(NULL);

	if ((client->suggestpoll > now) && (client->suggestpoll < (now + pollinterval))) {
		/*
		 * We have a suggested poll time tuned to the next "client" message,
		 * and it happens within a reasonable time. So use that.
		 */
		client->nextpoll = client->suggestpoll;
		client->suggestpoll = 0;
		dbgprintf("Next poll of %s in %d seconds (for client msg)\n", 
			client->hostname, (client->nextpoll - now));
	}
	else {
		/*
		 * Pick a reasonable next polltime.
		 * We try to avoid doing all polls in one go, by setting
		 * the next poll to "pollinterval" seconds from now, 
		 * +/- 15 seconds.
		 */
		int delay;

		delay = pollinterval + ((random() % 31) - 16);
		client->nextpoll = now + delay;
		dbgprintf("Next poll of %s in %d seconds\n", client->hostname, delay);
	}

	if (whentoqueue > client->nextpoll) {
		whentoqueue = client->nextpoll;
	}
}

int main(int argc, char *argv[])
{
	int argi;
	struct sigaction sa;
	void *hostwalk;
	time_t nexttimeout;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--server=")) {
			char *p = strchr(argv[argi], '=');
			serverip = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--interval=")) {
			char *p = strchr(argv[argi], '=');
			pollinterval = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--log-interval=")) {
			char *p = strchr(argv[argi], '=');
			errorloginterval = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--id=")) {
			char *p = strchr(argv[argi], '=');
			serverid = atoi(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
	}

	setup_signalhandler("hobbitfetch");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigmisc_handler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);	/* SIGUSR1 triggers logging of active requests */

	clients = rbtNew(name_compare);
	nexttimeout = time(NULL) + 60;

	{
		/* Seed the random number generator */
		struct timeval tv;
		struct timezone tz;

		gettimeofday(&tv, &tz);
		srandom(tv.tv_usec);
	}

	do {
		RbtIterator handle;
		conn_t *connwalk, *cprev;
		fd_set fdread, fdwrite;
		int n, maxfd;
		struct timeval tmo;
		time_t now;
		
		now = time(NULL);
		if (now > reloadtime) {
			/* Time to reload the bb-hosts file */
			reloadtime = now + 600;

			load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
			for (hostwalk = first_host(); (hostwalk); hostwalk = next_host(hostwalk, 0)) {
				char *hname;
				clients_t *newclient;

				if (!bbh_item(hostwalk, BBH_FLAG_PULLDATA)) continue;

				hname = bbh_item(hostwalk, BBH_HOSTNAME);
				handle = rbtFind(clients, hname);
				if (handle == rbtEnd(clients)) {
					newclient = (clients_t *)calloc(1, sizeof(clients_t));
					newclient->hostname = strdup(hname);
					rbtInsert(clients, newclient->hostname, newclient);
					whentoqueue = now;
				}
			}
		}

		now = time(NULL);
		if (now > nexttimeout) {
			/* Check for connections that have timed out */
			nexttimeout = now + 60;

			for (connwalk = chead; (connwalk); connwalk = connwalk->next) {
				if ((connwalk->tstamp + 60) < now) {
					if (debug || (connwalk->client->nexterrortxt < now)) {
						errprintf("Timeout while talking to %s (req %lu): Aborting session\n",
							  addrstring(&connwalk->caddr), connwalk->seq);
						connwalk->client->nexterrortxt = now + errorloginterval;
					}
					flag_cleanup(connwalk);
				}
			}
		}

		if (needcleanup) {
			/* Remove any finished requests */
			needcleanup = 0;
			connwalk = chead; cprev = NULL;
			dbgprintf("Doing cleanup\n");

			while (connwalk) {
				conn_t *zombie;

				if ((connwalk->action == C_READING) || (connwalk->action == C_WRITING)) {
					/* Active connection - skip to the next conn_t record */
					cprev = connwalk;
					connwalk = connwalk->next;
					continue;
				}

				if (connwalk->action == C_CLEANUP) {
					if (connwalk->ctype == C_CLIENT) {
						/* 
						 * Finished getting data from a client, 
						 * flag idle and set next poll time.
						 */
						connwalk->client->busy = 0;
						set_polltime(connwalk->client);
					}
					else if (connwalk->ctype == C_SERVER) {
						/* Nothing needed for server cleanups */
					}
				}

				/* Unlink the request from the list of active connections */
				zombie = connwalk;
				if (cprev == NULL) {
					chead = zombie->next;
					connwalk = chead;
					cprev = NULL;
				}
				else {
					cprev->next = zombie->next;
					connwalk = zombie->next;
				}

				/* Purge the zombie */
				dbgprintf("Request completed: req %lu, peer %s, action was %d, type was %d\n", 
					zombie->seq, addrstring(&zombie->caddr), 
					zombie->action, zombie->ctype);
				close(zombie->sockfd);
				freestrbuffer(zombie->msgbuf);
				xfree(zombie);
			}

			/* Set the tail pointer correctly */
			ctail = chead;
			if (ctail) { while (ctail->next) ctail = ctail->next; }
		}

		if (dumpsessions) {
			/* Set by SIGUSR1 - dump the list of active requests */
			dumpsessions = 0;
			for (connwalk = chead; (connwalk); connwalk = connwalk->next) {
				char *ctypestr, *actionstr;
				char timestr[30];

				switch (connwalk->ctype) {
				  case C_CLIENT: ctypestr = "client"; break;
				  case C_SERVER: ctypestr = "server"; break;
				}

				switch (connwalk->action) {
				  case C_READING: actionstr = "reading"; break;
				  case C_WRITING: actionstr = "writing"; break;
				  case C_CLEANUP: actionstr = "cleanup"; break;
				}

				strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S",
					 localtime(&connwalk->tstamp));

				errprintf("Request %lu: state %s/%s, peer %s, started %s (%lu secs ago)\n",
					  connwalk->seq, ctypestr, actionstr, addrstring(&connwalk->caddr),
					  timestr, (now - connwalk->tstamp));
			}
		}

		now = time(NULL);
		if (now >= whentoqueue) {
			/* Scan host-tree for clients we need to contact */
			for (handle = rbtBegin(clients); (handle != rbtEnd(clients)); handle = rbtNext(clients, handle)) {
				clients_t *clientwalk;
				char msgline[100];
				strbuffer_t *request;
				char *pullstr, *ip;
				int port;

				clientwalk = (clients_t *)gettreeitem(clients, handle);
				if (clientwalk->busy) continue;
				if (clientwalk->nextpoll > now) continue;

				/* Deleted hosts stay in our tree - but should disappear from the known hosts */
				hostwalk = hostinfo(clientwalk->hostname); if (!hostwalk) continue;
				pullstr = bbh_item(hostwalk, BBH_FLAG_PULLDATA); if (!pullstr) continue;

				ip = strchr(pullstr, '=');
				port = atoi(xgetenv("BBPORT"));

				if (!ip) {
					ip = strdup(bbh_item(hostwalk, BBH_IP));
				}
				else {
					/* There is an explicit IP setting in the pulldata tag */
					char *p;

					ip++; /* Skip the '=' */
					ip = strdup(ip);
					p = strchr(ip, ':');
					if (p) { *p = '\0'; port = atoi(p+1); }

					if (*ip == '\0') {
						/* No IP given, just a port number */
						xfree(ip);
						ip = strdup(bbh_item(hostwalk, BBH_IP));
					}
				}

				if (strcmp(ip, "0.0.0.0") == 0) {
					struct hostent *hent;

					xfree(ip); ip = NULL;
					hent = gethostbyname(clientwalk->hostname);
					if (hent) {
						struct in_addr addr;

						memcpy(&addr, *(hent->h_addr_list), sizeof(addr));
						ip = strdup(inet_ntoa(addr));
					}
				}

				if (!ip) continue;

				/* 
				 * Build the "pullclient" request, which includes the latest
				 * clientdata config we got from the server. Keep the clientdata
				 * here - we send "pullclient" requests more often that we actually
				 * contact the server, but we should provide the config data always.
				 */
				request = newstrbuffer(0);
				sprintf(msgline, "pullclient %d\n", serverid);
				addtobuffer(request, msgline);
				if (clientwalk->clientdata) addtobuffer(request, clientwalk->clientdata);

				/* Put the request on the connection queue */
				addrequest(C_CLIENT, ip, port, request, clientwalk);
				clientwalk->busy = 1;

				xfree(ip);
			}
		}

		/* Handle request queue */
		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);
		maxfd = -1;
		for (connwalk = chead; (connwalk); connwalk = connwalk->next) {
			switch (connwalk->action) {
			  case C_READING: 
				FD_SET(connwalk->sockfd, &fdread);
				if (connwalk->sockfd > maxfd) maxfd = connwalk->sockfd;
				break;

			  case C_WRITING: 
				FD_SET(connwalk->sockfd, &fdwrite);
				if (connwalk->sockfd > maxfd) maxfd = connwalk->sockfd;
				break;

			  case C_CLEANUP:
				break;
			}
		}

		/* Do select with a 1 second timeout */
		tmo.tv_sec = 1;
		tmo.tv_usec = 0;
		n = select(maxfd+1, &fdread, &fdwrite, NULL, &tmo);

		if (n == -1) {
			if (errno == EINTR) continue;	/* Interrupted, e.g. a SIGHUP */

			/* This is a "cannot-happen" failure. Bail out */
			errprintf("select failure: %s\n", strerror(errno));
			return 0;
		}

		if (n == 0) continue;	/* Timeout */

		for (connwalk = chead; (connwalk); connwalk = connwalk->next) {
			switch (connwalk->action) {
			  case C_READING: 
				if (FD_ISSET(connwalk->sockfd, &fdread)) grabdata(connwalk);
				break;

			  case C_WRITING: 
				if (FD_ISSET(connwalk->sockfd, &fdwrite)) senddata(connwalk);
				break;

			  case C_CLEANUP:
				break;
			}
		}

	} while (running);

	return 0;
}

