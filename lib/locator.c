/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for communicating with the Hobbit locator service.    */
/*                                                                            */
/* Copyright (C) 2006-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: locator.c 5819 2008-09-30 16:37:31Z storner $";

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
#include <fcntl.h>

#include "libbbgen.h"

#include <signal.h>

const char *servicetype_names[] = { "rrd", "client", "alert", "history", "hostdata" };

static struct sockaddr_in myaddr;
static socklen_t myaddrsz = 0;
static int locatorsocket = -1;

#define DEFAULT_CACHETIMEOUT (15*60) 	/* 15 minutes */
static RbtHandle locatorcache[ST_MAX];
static int havecache[ST_MAX] = {0,};
static int cachetimeout[ST_MAX] = {0,};

typedef struct cacheitm_t {
	char *key, *resp;
	time_t tstamp;
} cacheitm_t;


enum locator_servicetype_t get_servicetype(char *typestr)
{
	enum locator_servicetype_t res = 0;

	while ((res < ST_MAX) && (strcmp(typestr, servicetype_names[res]))) res++;

	return res;
}

static int call_locator(char *buf, size_t bufsz)
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
		FD_SET(locatorsocket, &fds);
		tmo.tv_sec = 5;
		tmo.tv_usec = 0;
		n = select(locatorsocket+1, NULL, &fds, NULL, &tmo);

		if (n == 0) {
			errprintf("Send to locator failed: Timeout\n");
			return -1;
		}
		else if (n == -1) {
			errprintf("Send to locator failed: %s\n", strerror(errno));
			return -1;
		}

		n = send(locatorsocket, bufp, bytesleft, 0);
		if (n == -1) {
			errprintf("Send to locator failed: %s\n", strerror(errno));
			return -1;
		}
		else {
			bytesleft -= n;
			bufp += n;
		}
	} while (bytesleft > 0);

	/* Then read the response */
	FD_ZERO(&fds);
	FD_SET(locatorsocket, &fds);
	tmo.tv_sec = 5;
	tmo.tv_usec = 0;
	n = select(locatorsocket+1, &fds, NULL, NULL, &tmo);

	if (n > 0) {
		n = recv(locatorsocket, buf, bufsz-1, 0);

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

static char *locator_querycache(enum locator_servicetype_t svc, char *key)
{
	RbtIterator handle;
	cacheitm_t *itm;

	if (!havecache[svc]) return NULL;

	handle = rbtFind(locatorcache[svc], key);
	if (handle == rbtEnd(locatorcache[svc])) return NULL;

	itm = (cacheitm_t *)gettreeitem(locatorcache[svc], handle);
	return (itm->tstamp + cachetimeout[svc]) > getcurrenttime(NULL) ? itm->resp : NULL;
}


static void locator_updatecache(enum locator_servicetype_t svc, char *key, char *resp)
{
	RbtIterator handle;
	cacheitm_t *newitm;

	if (!havecache[svc]) return;

	handle = rbtFind(locatorcache[svc], key);
	if (handle == rbtEnd(locatorcache[svc])) {
		newitm = (cacheitm_t *)calloc(1, sizeof(cacheitm_t));
		newitm->key = strdup(key);
		newitm->resp = strdup(resp);
		if (rbtInsert(locatorcache[svc], newitm->key, newitm) != RBT_STATUS_OK) {
			xfree(newitm->key);
			xfree(newitm->resp);
			xfree(newitm);
		}
	}
	else {
		newitm = (cacheitm_t *)gettreeitem(locatorcache[svc], handle);
		if (newitm->resp) xfree(newitm->resp);
		newitm->resp = strdup(resp);
		newitm->tstamp = getcurrenttime(NULL);
	}
}


void locator_flushcache(enum locator_servicetype_t svc, char *key)
{
	RbtIterator handle;

	if (!havecache[svc]) return;

	if (key) {
		handle = rbtFind(locatorcache[svc], key);
		if (handle != rbtEnd(locatorcache[svc])) {
			cacheitm_t *itm = (cacheitm_t *)gettreeitem(locatorcache[svc], handle);
			itm->tstamp = 0;
		}
	}
	else {
		for (handle = rbtBegin(locatorcache[svc]); (handle != rbtEnd(locatorcache[svc])); handle = rbtNext(locatorcache[svc], handle)) {
			cacheitm_t *itm = (cacheitm_t *)gettreeitem(locatorcache[svc], handle);
			itm->tstamp = 0;
		}
	}
}


void locator_prepcache(enum locator_servicetype_t svc, int timeout)
{
	if (!havecache[svc]) {
		locatorcache[svc] = rbtNew(name_compare);
		havecache[svc] = 1;
	}
	else {
		locator_flushcache(svc, NULL);
	}

	cachetimeout[svc] = ((timeout>0) ? timeout : DEFAULT_CACHETIMEOUT);
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

	fcntl(locatorsocket, F_SETFL, O_NONBLOCK);

	return (locator_ping() ? 0 : -1);
}


int locator_register_server(char *servername, enum locator_servicetype_t svctype, int weight, enum locator_sticky_t sticky, char *extras)
{
	char *buf;
	int bufsz;
	int res;

	bufsz = strlen(servername) + 100;
	if (extras) bufsz += (strlen(extras) + 1);
	buf = (char *)malloc(bufsz);

	if (sticky == LOC_SINGLESERVER) weight = -1;
	sprintf(buf, "S|%s|%s|%d|%d|%s", servername, servicetype_names[svctype], weight, 
		((sticky == LOC_STICKY) ? 1 : 0), (extras ? extras : ""));

	res = call_locator(buf, bufsz);

	xfree(buf);
	return res;
}

int locator_register_host(char *hostname, enum locator_servicetype_t svctype, char *servername)
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

int locator_rename_host(char *oldhostname, char *newhostname, enum locator_servicetype_t svctype)
{
	char *buf;
	int bufsz;
	int res;

	bufsz = strlen(oldhostname) + strlen(newhostname) + 100;
	buf = (char *)malloc(bufsz);
	sprintf(buf, "M|%s|%s|%s", oldhostname, servicetype_names[svctype], newhostname);

	res = call_locator(buf, bufsz);

	xfree(buf);
	return res;
}

char *locator_query(char *hostname, enum locator_servicetype_t svctype, char **extras)
{
	static char *buf = NULL;
	static int bufsz = 0;
	int res, bufneeded;

	bufneeded = strlen(hostname) + 100;
	if (extras) bufneeded += 1024;
	if (!buf) {
		bufsz = bufneeded;
		buf = (char *)malloc(bufsz);
	}
	else if (bufneeded > bufsz) {
		bufsz = bufneeded;
		buf = (char *)realloc(buf, bufsz);
	}

	if (havecache[svctype] && !extras) {
		char *cachedata = locator_querycache(svctype, hostname);
		if (cachedata) return cachedata;
	}

	sprintf(buf, "Q|%s|%s", servicetype_names[svctype], hostname);
	if (extras) buf[0] = 'X';
	res = call_locator(buf, bufsz);
	if (res == -1) return NULL;

	switch (*buf) {
	  case '!': /* This host is fixed on an available server */
	  case '*': /* Roaming host */
		if (extras) {
			*extras = strchr(buf+2, '|');
			if (**extras == '|') {
				**extras = '\0';
				(*extras)++;
			}
		}
		if (havecache[svctype] && !extras) locator_updatecache(svctype, hostname, buf+2);
		return ((strlen(buf) > 2) ? buf+2 : NULL);

	  case '?': /* No available server to handle the request */
		locator_flushcache(svctype, hostname);
		return NULL;
	}

	return NULL;
}

int locator_serverdown(char *servername, enum locator_servicetype_t svctype)
{
	char *buf;
	int bufsz;
	int res;

	bufsz = strlen(servername) + 100;
	buf = (char *)malloc(bufsz);
	sprintf(buf, "D|%s|%s", servername, servicetype_names[svctype]);

	res = call_locator(buf, bufsz);
	locator_flushcache(svctype, NULL);

	xfree(buf);
	return res;
}

int locator_serverup(char *servername, enum locator_servicetype_t svctype)
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

int locator_serverforget(char *servername, enum locator_servicetype_t svctype)
{
	char *buf;
	int bufsz;
	int res;

	bufsz = strlen(servername) + 100;
	buf = (char *)malloc(bufsz);
	sprintf(buf, "F|%s|%s", servername, servicetype_names[svctype]);

	res = call_locator(buf, bufsz);
	locator_flushcache(svctype, NULL);

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
		char *extras;

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
				enum locator_servicetype_t svc;
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
			extras = NULL;
			res = locator_query(p2, get_servicetype(p3), (*p1 == 'x') ? &extras : NULL);
			if (res) {
				printf("Result: %s\n", res); 
				if (extras) printf("  Extras gave: %s\n", extras);
			}
			else {
				printf("Failed\n");
			}
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

