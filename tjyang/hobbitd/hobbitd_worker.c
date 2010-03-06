/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This is a small library for hobbitd worker modules, to read a new message  */
/* from the hobbitd_channel process, and also do the decoding of messages     */
/* that are passed on the "meta-data" first line of such a message.           */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_worker.c 6125 2009-02-12 13:09:34Z storner $";

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>         /* Someday I'll move to GNU Autoconf for this ..  . */
#endif

#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "libbbgen.h"

#include "hobbitd_ipc.h"
#include "hobbitd_worker.h"

#include <signal.h>


static int running = 1;
static int inputfd = STDIN_FILENO;

#define EXTRABUFSPACE 4095

static char *locatorlocation = NULL;
static char *locatorid = NULL;
static enum locator_servicetype_t locatorsvc = ST_MAX;
static int  locatorweight = 1;
static char *locatorextra = NULL;
static char *listenipport = NULL;
static time_t locatorhb = 0;


static void netinp_sighandler(int signum)
{
	switch (signum) {
	  case SIGINT:
	  case SIGTERM:
		running = 0;
		break;
	}
}

static void net_worker_heartbeat(void)
{
	time_t now;

	if (!locatorid || (locatorsvc == ST_MAX)) return;

	now = gettimer();
	if (now > locatorhb) {
		locator_serverup(locatorid, locatorsvc);
		locatorhb = now + 60;
	}
}

static int net_worker_listener(char *ipport)
{
	/*
	 * Setup a listener socket on IP+port. When a connection arrives,
	 * pick it up, fork() and let the rest of the input go via the
	 * network socket.
	 */

	char *listenip, *p;
	int listenport = 0;
	int lsocket = -1;
	struct sockaddr_in laddr;
	struct sigaction sa;
	int opt;

	listenip = ipport;
	p = strchr(listenip, ':');
	if (p) {
		*p = '\0';
		listenport = atoi(p+1);
	}

	if (listenport == 0) {
		errprintf("Must include PORT number in --listen=IP:PORT option\n");
		return -1;
	}

        /* Set up a socket to listen for new connections */
	errprintf("Setting up network listener on %s:%d\n", listenip, listenport);
	memset(&laddr, 0, sizeof(laddr));
	if ((strlen(listenip) == 0) || (strcmp(listenip, "0.0.0.0") == 0)) {
		listenip = "0.0.0.0";
		laddr.sin_addr.s_addr = INADDR_ANY;
	}
	else if (inet_aton(listenip, (struct in_addr *) &laddr.sin_addr.s_addr) == 0) {
		/* Not an IP */
		errprintf("Listener IP must be an IP-address, not hostname\n");
		return -1;
	}
	laddr.sin_port = htons(listenport);
	laddr.sin_family = AF_INET;
	lsocket = socket(AF_INET, SOCK_STREAM, 0);
	if (lsocket == -1) {
		errprintf("Cannot create listen socket (%s)\n", strerror(errno));
		return -1;
	}

	opt = 1;
	setsockopt(lsocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	if (bind(lsocket, (struct sockaddr *)&laddr, sizeof(laddr)) == -1) {
		errprintf("Cannot bind to listen socket (%s)\n", strerror(errno));
		return -1;
	}
	if (listen(lsocket, 5) == -1) {
		errprintf("Cannot listen (%s)\n", strerror(errno));
		return -1;
	}

	/* Make listener socket non-blocking, so we can send keep-alive's while waiting for connections */
	fcntl(lsocket, F_SETFL, O_NONBLOCK);

	/* Catch some signals */
	setup_signalhandler("hobbitd_rrd-listener");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = netinp_sighandler;
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	while (running) {
		struct sockaddr_in netaddr;
		int addrsz, sock;
		int childstat;
		pid_t childpid;
		fd_set fdread;
		struct timeval tmo;
		int n;

		FD_ZERO(&fdread);
		FD_SET(lsocket, &fdread);
		tmo.tv_sec = 60; tmo.tv_usec = 0;
		n = select(lsocket+1, &fdread, NULL, NULL, &tmo);
		if (n == -1) {
			if (errno != EINTR) {
				errprintf("select() failed while waiting for connection : %s\n", strerror(errno));
				running = 0;
			}
		}
		else if (n == 0) {
			/* Timeout */
			net_worker_heartbeat();
		}
		else {
			/* We have a connection ready */
			addrsz = sizeof(netaddr);
			sock = accept(lsocket, (struct sockaddr *)&netaddr, &addrsz);

			if (sock >= 0) {
				/* Got a new connection */

				childpid = fork();
				if (childpid == 0) {
					/* Child takes input from the new socket, and starts working */
					close(lsocket);	/* Close the listener socket */
					inputfd = sock;
					return 0;
				}
				else if (childpid > 0) {
					/* Parent closes the new socket (child has it) */
					close(sock);
					continue;
				}
				else {
					errprintf("Error forking worker for new connection: %s\n", strerror(errno));
					running = 0;
					continue;
				}
			}
			else {
				/* Error while waiting for accept() to complete */
				if (errno != EINTR) {
					errprintf("accept() failed: %s\n", strerror(errno));
					running = 0;
				}
			}
		}

		/* Pickup failed children */
		while ((childpid = wait3(&childstat, WNOHANG, NULL)) > 0);
	}

	/* Close the listener socket */
	close(lsocket);

	/* Kill any children that are still around */
	kill(0, SIGTERM);

	return 1;
}


int net_worker_option(char *arg)
{
	int res = 1;

	if (argnmatch(arg, "--locator=")) {
		char *p = strchr(arg, '=');
		locatorlocation = strdup(p+1);
	}
	else if (argnmatch(arg, "--locatorid=")) {
		char *p = strchr(arg, '=');
		locatorid = strdup(p+1);
	}
	else if (argnmatch(arg, "--locatorweight=")) {
		char *p = strchr(arg, '=');
		locatorweight = atoi(p+1);
	}
	else if (argnmatch(arg, "--locatorextra=")) {
		char *p = strchr(arg, '=');
		locatorextra = strdup(p+1);
	}
	else if (argnmatch(arg, "--listen=")) {
		char *p = strchr(arg, '=');
		listenipport = strdup(p+1);
	}
	else {
		res = 0;
	}

	return res;
}


int net_worker_locatorbased(void)
{
	return ((locatorsvc != ST_MAX) && listenipport && locatorlocation);
}

void net_worker_run(enum locator_servicetype_t svc, enum locator_sticky_t sticky, update_fn_t *updfunc)
{
	locatorsvc = svc;

	if (listenipport) {
		char *p;
		struct in_addr dummy;

		if (!locatorid) locatorid = strdup(listenipport);

		p = strchr(locatorid, ':');
		if (p == NULL) {
			errprintf("Locator ID must be IP:PORT matching the listener address\n");
			exit(1);
		}
		*p = '\0'; 
		if (inet_aton(locatorid, &dummy) == 0) {
			errprintf("Locator ID must be IP:PORT matching the listener address\n");
			exit(1);
		}
		*p = ':';
	}

	if (listenipport && locatorlocation) {
		int res;
		int delay = 10;

		/* Tell the world we're here */
		while (locator_init(locatorlocation) != 0) {
			errprintf("Locator unavailable, waiting for it to be ready\n");
			sleep(delay);
			if (delay < 240) delay *= 2;
		}

		locator_register_server(locatorid, svc, locatorweight, sticky, locatorextra);
		if (updfunc) (*updfunc)(locatorid);

		/* Launch the network listener and wait for incoming connections */
		res = net_worker_listener(listenipport);

		/*
		 * Return value is:
		 * -1 : Error in setup. Abort.
		 *  0 : New connection arrived, and this is now a forked worker process. Continue.
		 *  1 : Listener terminates. Exit normally.
		 */
		if (res == -1) {
			errprintf("Listener setup failed, aborting\n");
			locator_serverdown(locatorid, svc);
			exit(1);
		}
		else if (res == 1) {
			errprintf("hobbitd_rrd network listener terminated\n");
			locator_serverdown(locatorid, svc);
			exit(0);
		}
		else {
			/* Worker process started. Return from here causes worker to start. */
		}
	}
	else if (listenipport || locatorlocation || locatorid) {
		errprintf("Must specify all of --listen, --locator and --locatorid\n");
		exit(1);
	}
}


unsigned char *get_hobbitd_message(enum msgchannels_t chnid, char *id, int *seq, struct timespec *timeout)
{
	static unsigned int seqnum = 0;
	static char *idlemsg = NULL;
	static char *buf = NULL;
	static size_t bufsz = 0;
	static size_t maxmsgsize = 0;
	static int ioerror = 0;

	static char *startpos;	/* Where our unused data starts */
	static char *endpos;	/* Where the first message ends */
	static char *fillpos;	/* Where our unused data ends (the \0 byte) */

	struct timespec cutoff;
	int maymove, needmoredata;
	char *endsrch;		/* Where in the buffer do we start looking for the end-message marker */
	char *result;

	/*
	 * The way this works is to read data from stdin into a
	 * buffer. Each read fetches as much data as possible,
	 * i.e. all that is available up to the amount of 
	 * buffer space we have.
	 *
	 * When the buffer contains a complete message,
	 * we return a pointer to the message.
	 *
	 * Since a read into the buffer can potentially
	 * fetch multiple messages, we need to keep track of
	 * the start/end positions of the next message, and
	 * where in the buffer new data should be read in.
	 * As long as there is a complete message available
	 * in the buffer, we just return that message - only
	 * when there is no complete message do we read data
	 * from stdin.
	 *
	 * A message is normally NOT copied, we just return
	 * a pointer to our input buffer. The only time we 
	 * need to shuffle data around is if the buffer
	 * does not have room left to hold a complete message.
	 */

	if (buf == NULL) {
		/*
		 * Initial setup of the buffers.
		 * We allocate a buffer large enough for the largest message
		 * that can arrive on this channel, and add 4KB extra room.
		 * The EXTRABUFSPACE is to allow the memmove() that will be
		 * needed occasionally some room to work optimally.
		 */
		maxmsgsize = 1024*shbufsz(chnid);
		bufsz = maxmsgsize + EXTRABUFSPACE;
		buf = (char *)malloc(bufsz+1);
		*buf = '\0';
		startpos = fillpos = buf;
		endpos = NULL;

		/* idlemsg is used to return the idle message in case of timeouts. */
		idlemsg = strdup("@@idle\n");

		/* We dont want to block when reading data. */
		fcntl(inputfd, F_SETFL, O_NONBLOCK);
	}

	/*
	 * If the start of the next message doesn't begin with "@" then 
	 * there's something rotten.
	 */
	if (*startpos && (*startpos != '@')) {
		errprintf("Bad data in channel, skipping it\n");
		startpos = strstr(startpos, "\n@@");
		endpos = (startpos ? strstr(startpos, "\n@@\n") : NULL);
		if (startpos && (startpos == endpos)) {
			startpos = endpos + 4;
			endpos = strstr(startpos, "\n@@\n");
		}

		if (!startpos) {
			/* We're lost - flush the buffer and try to recover */
			errprintf("Buffer sync lost, flushing data\n");
			*buf = '\0';
			startpos = fillpos = buf;
			endpos = NULL;
		}

		seqnum = 0; /* After skipping, we dont know what to expect */
	}

startagain:
	if (ioerror) {
		errprintf("get_hobbitd_message: Returning NULL due to previous i/o error\n");
		return NULL;
	}

	if (timeout) {
		/* Calculate when the read should timeout. */
		getntimer(&cutoff);
		cutoff.tv_sec += timeout->tv_sec;
		cutoff.tv_nsec += timeout->tv_nsec;
		if (cutoff.tv_nsec > 1000000000) {
			cutoff.tv_sec += 1;
			cutoff.tv_nsec -= 1000000000;
		}
	}

	/*
	 * Start looking for the end-of-message marker at the beginning of
	 * the message. The next scans will only look at the new data we've
	 * got when reading data in.
	 */
	endsrch = startpos;

	/*
	 * See if the current available buffer space is enough to hold a full message.
	 * If not, then flag that we may do a memmove() of the buffer data.
	 */
	maymove = ((startpos + maxmsgsize) >= (buf + bufsz));

	/* We only need to read data, if we do not have an end-of-message marker */
	needmoredata = (endpos == NULL);
	while (needmoredata) {
		/* Fill buffer with more data until we get an end-of-message marker */
		struct timespec now, tmo;
		struct timeval selecttmo;
		fd_set fdread;
		int res;
		size_t bufleft = bufsz - (fillpos - buf);
		size_t usedbytes = (fillpos - startpos);

		dbgprintf("Want msg %d, startpos %ld, fillpos %ld, endpos %ld, usedbytes=%ld, bufleft=%ld\n",
			  (seqnum+1), (startpos-buf), (fillpos-buf), (endpos ? (endpos-buf) : -1), usedbytes, bufleft);

		if (usedbytes >= maxmsgsize) {
			/* Over-size message. Truncate it. */
			errprintf("Got over-size message, truncating at %d bytes (max: %d)\n", usedbytes, maxmsgsize);
			endpos = startpos + maxmsgsize;
			memcpy(endpos, "\n@@\n", 4);	/* Simulate end-of-message and flush data */
			needmoredata = 0;
			continue;
		}

		if (maymove && (bufleft < EXTRABUFSPACE)) {
			/* Buffer is almost full - move data to accomodate a large message. */
			dbgprintf("Moving %d bytes to start of buffer\n", usedbytes);
			memmove(buf, startpos, usedbytes);
			startpos = buf;
			fillpos = startpos + usedbytes;
			*fillpos = '\0';
			endsrch = (usedbytes >= 4) ? (fillpos - 4) : startpos;
			maymove = 0;
			bufleft = bufsz - (fillpos - buf);
		}

		if (timeout) {
			/* How long time until the timeout ? */

			getntimer(&now);
			selecttmo.tv_sec = cutoff.tv_sec - now.tv_sec;
			selecttmo.tv_usec = (cutoff.tv_nsec - now.tv_nsec) / 1000;
			if (selecttmo.tv_usec < 0) {
				selecttmo.tv_sec--;
				selecttmo.tv_usec += 1000000;
			}
		}

		FD_ZERO(&fdread);
		FD_SET(inputfd, &fdread);

		res = select(inputfd+1, &fdread, NULL, NULL, (timeout ? &selecttmo : NULL));

		if (res < 0) {
			if (errno == EAGAIN) continue;

			if (errno == EINTR) {
				dbgprintf("get_hobbitd_message: Interrupted\n");
				*seq = 0;
				return idlemsg;
			}

			/* Some error happened */
			ioerror = 1;
			dbgprintf("get_hobbitd_message: Returning NULL due to select error %s\n",
				  strerror(errno));
			return NULL;
		}
		else if (res == 0) {
			/* 
			 * Timeout - return the "idle" message.
			 * NB: If select() was not passed a timeout parameter, this cannot trigger
			 */
			*seq = 0;
			return idlemsg;
		}
		else if (FD_ISSET(inputfd, &fdread)) {
			res = read(inputfd, fillpos, bufleft);
			if (res < 0) {
				if ((errno == EAGAIN) || (errno == EINTR)) continue;

				ioerror = 1;
				dbgprintf("get_hobbitd_message: Returning NULL due to read error %s\n",
					  strerror(errno));
				return NULL;
			}
			else if (res == 0) {
				/* read() returns 0 --> End-of-file */
				ioerror = 1;
				dbgprintf("get_hobbitd_message: Returning NULL due to EOF\n");
				return NULL;
			}
			else {
				/* 
				 * Got data - null-terminate it, and update fillpos
				 */
				*(fillpos+res) = '\0';
				fillpos += res;

				/* Did we get an end-of-message marker ? Then we're done. */
				endpos = strstr(endsrch, "\n@@\n");
				needmoredata = (endpos == NULL);

				/*
				 * If not done, update endsrch. We need to look at the
				 * last 3 bytes of input we got - they could be "\n@@" so
				 * all that is missing is the final "\n".
				 */
				if (needmoredata && (res >= 3)) endsrch = fillpos-3;
			}
		}
	}

	/* We have a complete message between startpos and endpos */
	result = startpos;
	*endpos = '\0';
	startpos = endpos+4; /* +4 because we skip the "\n@@\n" end-marker from the previous message */
	endpos = strstr(startpos, "\n@@\n");	/* To see if we already have a full message loaded */
	/* fillpos stays where it is */

	/* Check that it really is a message, and not just some garbled data */
	if (strncmp(result, "@@", 2) != 0) {
		errprintf("Dropping (more) garbled data\n");
		goto startagain;
	}

	if (!locatorid) {
		/* 
		 * Get and check the message sequence number.
		 * We dont do this for network based workers, since the
		 * sequence number is globally generated (by hobbitd)
		 * but a network-based worker may only see some of the
		 * messages (those that are not handled by other network-based
		 * worker modules).
		 */
		char *p = result + strcspn(result, "0123456789|");
		if (isdigit((int)*p)) {
			*seq = atoi(p);

			if (debug) {
				p = strchr(result, '\n'); if (p) *p = '\0';
				dbgprintf("%s: Got message %u %s\n", id, *seq, result);
				if (p) *p = '\n';
			}

			if ((seqnum == 0) || (*seq == (seqnum + 1))) {
				/* First message, or the correct sequence # */
				seqnum = *seq;
			}
			else if (*seq == seqnum) {
				/* Duplicate message - drop it */
				errprintf("%s: Duplicate message %d dropped\n", id, *seq);
				goto startagain;
			}
			else {
				/* Out-of-sequence message. Cant do much except accept it */
				errprintf("%s: Got message %u, expected %u\n", id, *seq, seqnum+1);
				seqnum = *seq;
			}

			if (seqnum == 999999) seqnum = 0;
		}
	}

	dbgprintf("startpos %ld, fillpos %ld, endpos %ld\n",
		  (startpos-buf), (fillpos-buf), (endpos ? (endpos-buf) : -1));

	return result;
}

