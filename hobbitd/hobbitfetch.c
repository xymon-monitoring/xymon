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

static char rcsid[] = "$Id: hobbitfetch.c,v 1.2 2006-06-27 12:39:51 henrik Exp $";

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
#include <netdb.h>
#include <ctype.h>
#include <signal.h>

#include "libbbgen.h"

volatile int running = 1;
volatile time_t reloadtime = 0;
char *serverip = "127.0.0.1";
int pollinterval = 60; /* Seconds */

/*
 * When we send in a "client" message to the server, we get the client configuration
 * back. Save this in a tree so the next time we contact the client, we can provide
 * the configuration for it in the "pullclient" data.
 */
typedef struct clients_t {
	char *hostname;
	time_t nextpoll;	/* When we should contact this host again */
	char *clientdata;	/* This hosts' client configuration data */
	int busy;		/* If set, then we are currently processing this host */
} clients_t;
RbtHandle clients;

typedef enum { C_CLIENT, C_SERVER } conntype_t;
typedef struct conn_t {
	conntype_t ctype;		/* Talking to a client or a server? */
	int savedata;			/* Save the data to the client data buffer */
	clients_t *client;		/* Which client this refers to. */
	time_t tstamp;			/* When did the connection start. */
	struct sockaddr_in caddr;	/* Destination address */
	int sockfd;			/* Socket */
	enum { C_READING, C_WRITING, C_CLEANUP, C_DONE } action;	/* What are we doing? */
	strbuffer_t *msgbuf;		/* I/O buffer */
	int sentbytes;
	struct conn_t *next;
} conn_t;
conn_t *chead = NULL;
conn_t *ctail = NULL;


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
	}
}


void addrequest(conntype_t ctype, char *destip, strbuffer_t *req, clients_t *client)
{
	conn_t *newconn;
	int n;

	if (debug) {
		char dbgmsg[100];

		snprintf(dbgmsg, sizeof(dbgmsg), "%s\n", STRBUF(req));
		dprintf("Queuing request to %s for %s: '%s'\n", destip, client->hostname, dbgmsg);
	}

	newconn = (conn_t *)calloc(1, sizeof(conn_t));
	newconn->client = client;
	newconn->msgbuf = req;
	newconn->sentbytes = 0;
	newconn->ctype = ctype;
	newconn->savedata = ((ctype == C_SERVER) && (strncmp(STRBUF(req), "client ", 7) == 0));
	newconn->action = C_WRITING;
	newconn->tstamp = time(NULL);

	/* Setup the address. FIXME: Handle non-standard ports. */
	newconn->caddr.sin_port = htons(1984);
	newconn->caddr.sin_family = AF_INET;
	if (inet_aton(destip, (struct in_addr *)&newconn->caddr.sin_addr.s_addr) == 0) {
		/* Bad IP. */
		errprintf("Invalid client IP: %s\n", destip);
		freestrbuffer(req);
		xfree(newconn);
		return;
	}

	newconn->sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (newconn->sockfd == -1) {
		/* No more sockets available. Try again later. */
		errprintf("Out of sockets\n");
		freestrbuffer(req);
		xfree(newconn);
		return;
	}
	fcntl(newconn->sockfd, F_SETFL, O_NONBLOCK);

	/* All set ... start the connection */
	n = connect(newconn->sockfd, (struct sockaddr *)&newconn->caddr, sizeof(newconn->caddr)); 
	if ((n == -1) && (errno != EINPROGRESS)) {
		/* Immediate connect failure - drop it */
		errprintf("Could not connect to %s\n", destip);
		close(newconn->sockfd);
		freestrbuffer(req);
		xfree(newconn);
		return;
	}

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
	/* Send data on the connection socket */
	int n, togo;
	char *startp;

	togo = STRBUFLEN(conn->msgbuf) - conn->sentbytes;
	startp = STRBUF(conn->msgbuf) + conn->sentbytes;
	n = write(conn->sockfd, startp, togo);

	if (n == -1) {
		/* Write failure */
		errprintf("Connection lost during write\n");
		conn->action = C_CLEANUP;
	}
	else if (n >= 0) {
		conn->sentbytes += n;
		if (conn->sentbytes == STRBUFLEN(conn->msgbuf)) {
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
	 */

	char *mptr, *databegin, *msgbegin;

	databegin = strchr(STRBUF(conn->msgbuf), '\n');
	if (!databegin || (STRBUFLEN(conn->msgbuf) == 0)) {
		conn->client->busy = 0;
		conn->client->nextpoll = time(NULL) + poll_interval;
		return;
	}

	*databegin = '\0';
	msgbegin = (databegin+1);
	mptr = strtok(STRBUF(conn->msgbuf), " \t");
	while (mptr) {
		int msgbytes;
		char savech;
		strbuffer_t *req;

		msgbytes = atoi(mptr);
		savech = *(msgbegin + msgbytes);
		*(msgbegin + msgbytes) = '\0';
		req = newstrbuffer(msgbytes+1);
		addtobuffer(req, msgbegin);
		addrequest(C_SERVER, serverip, req, conn->client);

		*(msgbegin + msgbytes) = savech;
		msgbegin += msgbytes;

		mptr = strtok(NULL, " \t");
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
		dprintf("Client data for %s: %s\n", conn->client->hostname, 
			(conn->client->clientdata ? conn->client->clientdata : "<Null>"));
	}
}

void grabdata(conn_t *conn)
{
	int n;
	char buf[8192];

        n = read(conn->sockfd, buf, sizeof(buf));
	if (n == -1) {
		/* Read failure */
		errprintf("Connection lost during read\n");
		conn->action = C_CLEANUP;
	}
	else if (n > 0) {
		/* Save the data */
		buf[n] = '\0';
		addtobuffer(conn->msgbuf, buf);
	}
	else if (n == 0) {
		/* Done reading. Process the data. */
		shutdown(conn->sockfd, SHUT_RDWR);
		conn->action = C_DONE;

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


int main(int argc, char *argv[])
{
	int argi;
	struct sigaction sa;
	namelist_t *hostwalk;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--server=")) {
			char *p = strchr(argv[argi], '=');
			serverip = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--interval=")) {
			char *p = strchr(argv[argi], '=');
			poll_interval = atoi(p+1);
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

	clients = rbtNew(name_compare);

	do {
		RbtIterator handle;
		conn_t *connwalk, *cprev;
		fd_set fdread, fdwrite;
		int n, maxfd;
		struct timeval tmo;
		time_t now;
		
		now = time(NULL);

		if (now > reloadtime) {
			reloadtime = now + 600;

			load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
			for (hostwalk = first_host(); (hostwalk); hostwalk = hostwalk->next) {
				char *hname;
				clients_t *newclient;

				if (!bbh_item(hostwalk, BBH_FLAG_PULLDATA)) continue;

				hname = bbh_item(hostwalk, BBH_HOSTNAME);
				handle = rbtFind(clients, hname);
				if (handle == rbtEnd(clients)) {
					newclient = (clients_t *)calloc(1, sizeof(clients_t));
					newclient->hostname = strdup(hname);
					rbtInsert(clients, newclient->hostname, newclient);
				}
			}
		}

		/* Remove any finished connections */
		connwalk = chead; cprev = NULL;
		while (connwalk) {
			conn_t *zombie;

			if ((connwalk->action != C_DONE) && (connwalk->action != C_CLEANUP)) {
				cprev = connwalk;
				connwalk = connwalk->next;
				continue;
			}

			if ((connwalk->action == C_CLEANUP) || (connwalk->ctype == C_SERVER)) {
				/*
				 * We've talked to both the client and the server, so
				 * this host is now idle. Call it again after a while.
				 */
				connwalk->client->busy = 0;
				connwalk->client->nextpoll = time(NULL) + poll_interval;
			}

			/* Close the socket */
			close(connwalk->sockfd);

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

			freestrbuffer(zombie->msgbuf);
			xfree(zombie);
		}
		if (!chead) ctail = NULL;


		/* List the clients we should contact now */
		for (handle = rbtBegin(clients); (handle != rbtEnd(clients)); handle = rbtNext(clients, handle)) {
			clients_t *clientwalk;
			strbuffer_t *request;

			clientwalk = (clients_t *)gettreeitem(clients, handle);
			if (clientwalk->busy) continue;
			if (clientwalk->nextpoll > now) continue;

			/* Deleted hosts stay in our tree - but should disappear from the known hosts */
			hostwalk = hostinfo(clientwalk->hostname); if (!hostwalk) continue;

			/* 
			 * Build the "pullclient" request, which includes the latest
			 * clientdata config we got from the server. Keep the clientdata
			 * here - we send "pullclient" requests more often that we actually
			 * contact the server, but we should provide the config data always.
			 */
			request = newstrbuffer(0);
			addtobuffer(request, "pullclient\n");
			if (clientwalk->clientdata) addtobuffer(request, clientwalk->clientdata);

			/* Put the request on the connection queue */
			addrequest(C_CLIENT, bbh_item(hostwalk, BBH_IP), request, clientwalk);
			clientwalk->busy = 1;
		}

		/* Handle connection queue */
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
			  case C_DONE:
				break;
			}
		}

		tmo.tv_sec = 10;
		tmo.tv_usec = 0;
		n = select(maxfd+1, &fdread, &fdwrite, NULL, &tmo);

		if (n == -1) {
			errprintf("select failure: %s\n", strerror(errno));
			return 0;
		}

		if (n == 0) continue;

		for (connwalk = chead; (connwalk); connwalk = connwalk->next) {
			switch (connwalk->action) {
			  case C_READING: 
				if (FD_ISSET(connwalk->sockfd, &fdread)) grabdata(connwalk);
				break;

			  case C_WRITING: 
				if (FD_ISSET(connwalk->sockfd, &fdwrite)) senddata(connwalk);
				break;

			  case C_CLEANUP:
			  case C_DONE:
				break;
			}
		}

	} while (running);

	return 0;
}

