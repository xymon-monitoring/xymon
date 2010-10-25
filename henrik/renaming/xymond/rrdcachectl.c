/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This utility is used to manually control the RRD cache.                    */
/* It is a debugging tool.                                                    */
/*                                                                            */
/* Copyright (C) 2007-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/time.h>
#include <sys/types.h>

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <sys/socket.h>
#include <sys/un.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

#define errprintf printf
#define xgetenv getenv

static struct sockaddr_un myaddr;
static socklen_t myaddrsz = 0;
static int ctlsocket = -1;


int init_svc(char *sockfn)
{
	ctlsocket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (ctlsocket == -1) {
		errprintf("Cannot get socket: %s\n", strerror(errno));
		return -1;
	}

	memset(&myaddr, 0, sizeof(myaddr));
	myaddr.sun_family = AF_UNIX;
	sprintf(myaddr.sun_path, "%s/%s", xgetenv("BBTMP"), sockfn);
	myaddrsz = sizeof(myaddr);

	if (connect(ctlsocket, (struct sockaddr *)&myaddr, myaddrsz) == -1) {
		errprintf("Cannot set target address: %s (%s)\n", sockfn, strerror(errno));
		close(ctlsocket);
		ctlsocket = -1;
		return -1;
	}

	fcntl(ctlsocket, F_SETFL, O_NONBLOCK);
	return 0;
}


static int call_svc(char *buf)
{
	int n, bytesleft;
	fd_set fds;
	struct timeval tmo;
	char *bufp;

	/* First, send the request */
	bufp = buf;
	bytesleft = strlen(buf)+1;
	do {
		FD_ZERO(&fds);
		FD_SET(ctlsocket, &fds);
		tmo.tv_sec = 5;
		tmo.tv_usec = 0;
		n = select(ctlsocket+1, NULL, &fds, NULL, &tmo);

		if (n == 0) {
			errprintf("Send failed: Timeout\n");
			return -1;
		}
		else if (n == -1) {
			errprintf("Send failed: %s\n", strerror(errno));
			return -1;
		}

		n = send(ctlsocket, bufp, bytesleft, 0);
		if (n == -1) {
			errprintf("Send failed: %s\n", strerror(errno));
			return -1;
		}
		else {
			bytesleft -= n;
			bufp += n;
		}
	} while (bytesleft > 0);

	errprintf("Request sent\n");
	return 0;
}



int main(int argc, char *argv[])
{
	char buf[1024];
	int done = 0;

	if (argc < 2) {
		printf("Usage: %s SOCKETFILE\n", argv[0]);
		return 1;
	}

	if (init_svc(argv[1]) == -1) {
		printf("Cannot get socket\n");
		return 1;
	}

	printf("Enter the hostname whose RRD cache should be flushed. One host per line\n");
	while (!done) {
		done = (fgets(buf, sizeof(buf), stdin) == NULL); if (done) continue;
		done = (call_svc(buf) != 0);
	}

	return 0;
}

