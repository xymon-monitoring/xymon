/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for communicating with the Hobbit locator service.    */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: locator.c,v 1.1 2006-11-14 11:56:02 henrik Exp $";

#include <sys/time.h>
#include <sys/types.h>

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "libbbgen.h"

const char *servicetype_names[] = { "rrd", "client", "alert", "history", "hostdata" };

static struct sockaddr_in myaddr;
static socklen_t myaddrsz = 0;
static int locatorsocket = -1;

enum servicetype_t get_servicetype(char *typestr)
{
	enum servicetype_t res = 0;

	while ((res < ST_MAX) && (strcmp(typestr, servicetype_names[res]))) res++;

	return res;
}

static int call_locator(char *buf, size_t bufsz)
{
	int n;
	fd_set fds;
	struct timeval tmo;

	n = send(locatorsocket, buf, strlen(buf)+1, 0);
	if (n == -1) {
		errprintf("Send to locator failed: %s\n", strerror(errno));
		return -1;
	}

	FD_ZERO(&fds);
	FD_SET(locatorsocket, &fds);
	tmo.tv_sec = 5;
	tmo.tv_usec = 0;
	n = select(locatorsocket+1, &fds, NULL, NULL, &tmo);

	if (n > 0) {
		n = recv(locatorsocket, buf, bufsz-1, MSG_DONTWAIT);

		if (n == -1) {
			errprintf("call_locator() recv() failed: %s\n", strerror(errno));
			return -1;
		}
		buf[n] = '\0';
	}
	else {
		errprintf("call_locator() comm failure: %s\n",
			  (n == 0) ? "Timeout" : strerror(errno));
		return -1;
	}

	return 0;
}


char *locator_cmd(char *cmd)
{
	static char pingbuf[512];
	int res;

	strcpy(pingbuf, cmd);
	res = call_locator(pingbuf, sizeof(pingbuf));

	return (res == 0) ? pingbuf : NULL;
}

char *locator_ping(void)
{
	return locator_cmd("p");
}

int locator_init(char *ipport)
{
	char *ip, *p;
	int portnum;

	if (locatorsocket >= 0) {
		close(locatorsocket);
		locatorsocket = -1;
	}

	locatorsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (locatorsocket == -1) {
		errprintf("Cannot get socket: %s\n", strerror(errno));
		return -1;
	}

	ip = strdup(ipport);
	p = strchr(ip, ':'); 
	if (p) {
		*p = '\0';
		portnum = atoi(p+1);
	}
	else {
		portnum = atoi(xgetenv("BBPORT"));
	}

	memset(&myaddr, 0, sizeof(myaddr));
	inet_aton(ip, (struct in_addr *) &myaddr.sin_addr.s_addr);
	myaddr.sin_port = htons(portnum);
	myaddr.sin_family = AF_INET;
	myaddrsz = sizeof(myaddr);

	if (connect(locatorsocket, (struct sockaddr *)&myaddr, myaddrsz) == -1) {
		errprintf("Cannot set target address for locator: %s:%d (%s)\n",
			ip, portnum, strerror(errno));
		close(locatorsocket);
		locatorsocket = -1;
		return -1;
	}

	return (locator_ping() ? 0 : -1);
}


int locator_register_server(char *servername, enum servicetype_t svctype, int weight, enum locator_sticky_t sticky, char *extras)
{
	char *buf;
	int bufsz;
	int res;

	bufsz = strlen(servername) + 100;
	if (extras) bufsz += (strlen(extras) + 1);
	buf = (char *)malloc(bufsz);
	sprintf(buf, "S|%s|%s|%d|%d|%s", servername, servicetype_names[svctype], weight, 
		((sticky == LOC_STICKY) ? 1 : 0), (extras ? extras : ""));

	res = call_locator(buf, bufsz);

	xfree(buf);
	return res;
}

int locator_register_host(char *hostname, enum servicetype_t svctype, char *servername)
{
	char *buf;
	int bufsz;
	int res;

	bufsz = strlen(servername) + strlen(hostname) + 100;
	buf = (char *)malloc(bufsz);
	sprintf(buf, "H|%s|%s|%s", hostname, servicetype_names[svctype], servername);

	res = call_locator(buf, bufsz);

	xfree(buf);
	return res;
}

char *locator_query(char *hostname, enum servicetype_t svctype, int extras)
{
	static char *buf = NULL;
	static int bufsz = 0;
	int res, bufneeded;

	bufneeded = strlen(hostname) + 100;
	if (!buf) {
		bufsz = bufneeded;
		buf = (char *)malloc(bufsz);
	}
	else if (bufneeded > bufsz) {
		bufsz = bufneeded;
		buf = (char *)realloc(buf, bufsz);
	}
	sprintf(buf, "Q|%s|%s", servicetype_names[svctype], hostname);
	if (extras) buf[0] = 'X';

	res = call_locator(buf, bufsz);
	if (res == -1) return NULL;

	return buf+2;
}

int locator_serverdown(char *servername, enum servicetype_t svctype)
{
	char *buf;
	int bufsz;
	int res;

	bufsz = strlen(servername) + 100;
	buf = (char *)malloc(bufsz);
	sprintf(buf, "D|%s|%s", servername, servicetype_names[svctype]);

	res = call_locator(buf, bufsz);

	xfree(buf);
	return res;
}

int locator_serverup(char *servername, enum servicetype_t svctype)
{
	char *buf;
	int bufsz;
	int res;

	bufsz = strlen(servername) + 100;
	buf = (char *)malloc(bufsz);
	sprintf(buf, "U|%s|%s", servername, servicetype_names[svctype]);

	res = call_locator(buf, bufsz);

	xfree(buf);
	return res;
}

int locator_serverforget(char *servername, enum servicetype_t svctype)
{
	char *buf;
	int bufsz;
	int res;

	bufsz = strlen(servername) + 100;
	buf = (char *)malloc(bufsz);
	sprintf(buf, "F|%s|%s", servername, servicetype_names[svctype]);

	res = call_locator(buf, bufsz);

	xfree(buf);
	return res;
}

#ifdef STANDALONE

int main(int argc, char *argv[])
{
	char buf[1024];
	int done = 0;
	char *res;

	if (argc < 2) {
		printf("Usage: %s IP:PORT\n", argv[0]);
		return 1;
	}

	if (locator_init(argv[1]) == -1) {
		printf("Locator ping failed\n");
		return 1;
	}
	else {
		printf("Locator is available\n");
	}

	while (!done) {
		char *p, *p1, *p2, *p3, *p4, *p5, *p6, *p7;

		printf("Commands:\n");
		printf("  r(egister) s servername type weight sticky\n");
		printf("  r(egister) h servername type hostname\n");
		printf("  d(own)       servername type\n");
		printf("  u(p)         servername type\n");
		printf("  f(orget)     servername type\n");
		printf("  q(uery)      hostname type\n");
		printf("  x(query)     hostname type\n");
		printf("  p(ing)\n");
		printf("  s(ave state)\n");
		printf(">"); fflush(stdout);
		done = (fgets(buf, sizeof(buf), stdin) == NULL); if (done) continue;

		p = strchr(buf, '\n'); if (p) *p = '\0';
		p1 = p2 = p3 = p4 = p5 = p6 = p7 = NULL;

		p1 = strtok(buf, " ");
		if (p1) p2 = strtok(NULL, " ");
		if (p2) p3 = strtok(NULL, " ");
		if (p3) p4 = strtok(NULL, " ");
		if (p4) p5 = strtok(NULL, " ");
		if (p5) p6 = strtok(NULL, " ");
		if (p6) p7 = strtok(NULL, "\r\n");

		switch (*p1) {
		  case 'R': case 'r':
			if (*p2 == 's') {
				enum servicetype_t svc;
				enum locator_sticky_t sticky;
				int weight;

				svc = get_servicetype(p4);
				weight = (p5 ? atoi(p5) : 1);
				sticky = ((p6 && (atoi(p6) == 1)) ? LOC_STICKY : LOC_ROAMING);

				printf("%s\n", locator_register_server(p3, svc, weight, sticky, p7) ? "Failed" : "OK");
			}
			else if (*p2 == 'h') {
				printf("%s\n", locator_register_host(p5, get_servicetype(p4), p3) ? "Failed" : "OK");
			}
			break;

		  case 'D': case 'd':
			printf("%s\n", locator_serverdown(p2, get_servicetype(p3)) ? "Failed" : "OK");
			break;

		  case 'U': case 'u':
			printf("%s\n", locator_serverup(p2, get_servicetype(p3)) ? "Failed" : "OK");
			break;

		  case 'F': case 'f':
			printf("%s\n", locator_serverforget(p2, get_servicetype(p3)) ? "Failed" : "OK");
			break;

		  case 'Q': case 'q':
		  case 'X': case 'x':
			res = locator_query(p2, get_servicetype(p3), (*p1 == 'x'));
			if (res) printf("Result: %s\n", res); else printf("Failed\n");
			break;

		  case 'P': case 'p':
			p = locator_cmd("p");
			if (p == NULL) printf("Failed\n"); else printf("OK: %s\n", p);
			break;

		  case 'S': case 's':
			p = locator_cmd("@");
			if (p == NULL) printf("Failed\n"); else printf("OK: %s\n", p);
			break;
		}
	}

	return 0;
}

#endif

