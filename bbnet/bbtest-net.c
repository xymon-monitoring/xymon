/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbtest-net.c,v 1.8 2003-04-13 21:11:12 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <netdb.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

typedef struct {
	char *testname;
	int namelen;
	int portnum;
	void *next;
	void *items;
} service_t;

typedef struct {
	char *hostname;
	char ip[16];
	int dialup;
	int testip;
	int dnserror;
	void *next;
} testedhost_t;

typedef struct {
	testedhost_t	*host;
	service_t	*service;
	int		reverse;
	int		dialup;
	int		open;
	void		*next;
} testitem_t;

service_t	*svchead = NULL;
testedhost_t	*testhosthead = NULL;
testitem_t	*testhead = NULL;

service_t *add_service(char *name, int port, int namelen)
{
	service_t *svc;

	svc = malloc(sizeof(service_t));
	svc->portnum = port;
	svc->testname = malloc(strlen(name)+1);
	strcpy(svc->testname, name);
	svc->namelen = namelen;
	svc->items = NULL;
	svc->next = svchead;
	svchead = svc;

	return svc;
}

void load_services(void)
{
	char *netsvcs;
	char *p;
	struct servent *svcinfo;

	netsvcs = malloc(strlen(getenv("BBNETSVCS"))+1);
	strcpy(netsvcs, getenv("BBNETSVCS"));

	p = strtok(netsvcs, " ");
	while (p) {
		svcinfo = getservbyname(p, NULL);
		add_service(p, (svcinfo ? ntohs(svcinfo->s_port) : 0), 0);
		p = strtok(NULL, " ");
	}
	free(netsvcs);
}


void load_tests(void)
{
	FILE 	*bbhosts;
	char 	l[MAX_LINE_LEN];	/* With multiple http tests, we may have long lines */
	int	ip1, ip2, ip3, ip4;
	char	hostname[MAX_LINE_LEN];
	char	*netstring;
	char 	*p;

	bbhosts = fopen(getenv("BBHOSTS"), "r");
	if (bbhosts == NULL) {
		perror("No bb-hosts file found");
		return;
	}

	/* Local hack - each network test tagged with NET:locationname */
	p = getenv("BBLOCATION");
	if (p) {
		netstring = malloc(strlen(p)+5);
		sprintf(netstring, "NET:%s", p);
	}
	else {
		netstring = NULL;
	}

	while (fgets(l, sizeof(l), bbhosts)) {
		p = strchr(l, '\n'); if (p) { *p = '\0'; };

		if ((l[0] == '#') || (strlen(l) == 0)) {
			/* Do nothing - it's a comment */
		}
		else if (sscanf(l, "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {
			char *testspec;

			if (((netstring == NULL) || (strstr(l, netstring) != NULL)) && strchr(l, '#')) {

				testedhost_t *h;
				testitem_t *newtest;
				int anytests;

				h = malloc(sizeof(testedhost_t));
				h->hostname = malloc(strlen(hostname)+1);
				strcpy(h->hostname, hostname);
				h->dialup = (strstr(l, "dialup") != NULL);
				h->testip = (strstr(l, "testip") != NULL);
				h->ip[0] = '\0';
				h->dnserror = 0;

				p = strchr(l, '#'); p++;
				while (isspace((unsigned int) *p)) p++;
				testspec = strtok(p, "\t ");

				anytests = 0;
				while (testspec) {

					service_t *s;
					int dialuptest = 0;
					int reversetest = 0;
					char *option;

					/* Remove any trailing ":s", ":q", ":Q", ":portnumber" */
					option = strchr(testspec, ':'); 
					if (option) { 
						*option = '\0'; 
						option++; 
					}
					if (*testspec == '?') { dialuptest=1; testspec++; }
					if (*testspec == '!') { reversetest=1; testspec++; }

					/* Find the service */
					for (s=svchead; (s && (strcmp(s->testname, testspec) != 0)); s = s->next) ;

					if (option && s) {
						/*
						 * Check if it is a service with an explicit portnumber.
						 * If it is, then create a new service record named
						 * "SERVICE_PORT" so we can merge tests for this service+port
						 * combination for multiple hosts.
						 *
						 * According to BB docs, this type of services must be in
						 * BBNETSVCS - so it is known already.
						 */
						int specialport;
						char *specialname;

						specialport = atoi(option);
						if ((s->portnum == 0) && (specialport > 0)) {
							specialname = malloc(strlen(s->testname)+10);
							sprintf(specialname, "%s_%d", s->testname, specialport);
							s = add_service(specialname, specialport, strlen(s->testname));
							free(specialname);
						}
					}

					if (s) {
						anytests = 1;
						newtest = malloc(sizeof(testitem_t));
						newtest->host = h;
						newtest->service = s;
						newtest->dialup = dialuptest;
						newtest->reverse = reversetest;
						newtest->open = 0;
						newtest->next = s->items;
						s->items = newtest;
					}

					testspec = strtok(NULL, "\t ");
				}

				if (anytests) {
					/* 
					 * Determine the IP address to test. We do it here,
					 * to avoid multiple DNS lookups for each service 
					 * we test on a host.
					 */
					if (h->testip) {
						sprintf(h->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
						if (strcmp(h->ip, "0.0.0.0") == 0) {
							printf("bbtest-net: %s has IP 0.0.0.0 and testip - dropped\n", hostname);
							h->dnserror = 1;
						}
					}
					else {
						struct hostent *hent;

						hent = gethostbyname(hostname);
						if (hent == NULL) {
							/* Cannot resolve hostname */
							h->dnserror = 1;
						}
						else {
							sprintf(h->ip, "%d.%d.%d.%d", 
								(unsigned char) hent->h_addr_list[0][0],
								(unsigned char) hent->h_addr_list[0][1],
								(unsigned char) hent->h_addr_list[0][2],
								(unsigned char) hent->h_addr_list[0][3]);
						}
					}

					if (strcmp(h->ip, "0.0.0.0") == 0) {
						printf("bbtest-net: IP resolver error for host %s\n", hostname);
						h->dnserror = 1;
					}
					h->next = testhosthead;
					testhosthead = h;
				}
				else {
					/* No network tests for this host, so drop it */
					free(h);
				}

			}
		}
		else {
			/* Other bb-hosts line - ignored */
		};
	}

	fclose(bbhosts);
	return;
}


void run_nmap_service(service_t *service)
{
	FILE		*nmapin;
	char		logfn[MAX_PATH];
	char		nmapcmd[MAX_PATH+1024];
	testitem_t	*t;
	FILE		*logfile;
	char		l[MAX_LINE_LEN];
	char 		wantedstatus[80];

	if (service->portnum <= 0) {
		printf("bbtest-net: Attempt to test service %s on port 0\n",
			service->testname);
		return;
	}

	sprintf(logfn, "%s/nmap_%s_%d.out", getenv("BBTMP"), service->testname, service->portnum);

	/*
	 * nmap options:
	 * -sT	: Default TCP connect scan.
	 * -P0	: Dont ping
	 * -n	: Dont do DNS lookups
	 * -iL -: Take IP's from stdin
	 * -oG  : grep'able output format
	 * -p   : portnumber to test
	 */
	sprintf(nmapcmd, "nmap -sT -P0 -n -iL - -oG %s -p %d 2>&1 1>/dev/null", 
		logfn, service->portnum);
	nmapin = popen(nmapcmd, "w");
	if (nmapin == NULL) { perror("Cannot run nmap"); exit(1); }

	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror) fprintf(nmapin, "%s\n", t->host->ip);
	}

	if (pclose(nmapin) != 0) {
		printf("bbtest-net: failed to execute nmap\n");
		exit(1);
	}

	logfile = fopen(logfn, "r");
	if (logfile == NULL) {
		printf("Cannot open logfile %s\n", logfn);
		exit(1);
	}

	sprintf(wantedstatus, "%d/open/tcp/", service->portnum);
	while (fgets(l, sizeof(l), logfile)) {
		int ip1, ip2, ip3, ip4;
		char status[MAX_LINE_LEN];
		char logip[16];

		if ((strncmp(l, "Host:", 5) == 0) && 
		    (sscanf(l, "Host: %3d.%3d.%3d.%3d () Ports: %s Ignored state:", &ip1, &ip2, &ip3, &ip4, status) == 5)) {
			sprintf(logip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
			for (t=service->items; (t && (strcmp(t->host->ip, logip) != 0)); t = t->next) ;
			if (t) {
				t->open = (strncmp(wantedstatus, status, strlen(wantedstatus)) == 0);
			}
			else {
				printf("Weird - tested an IP I didnt know: %s\n", logip);
			}
		}
	}
	fclose(logfile);

	if (!debug) unlink(logfn);
}

void send_results(service_t *service)
{
	testitem_t	*t;
	int		color;
	char		msgline[MAXMSG];
	char		*svcname;

	svcname = malloc(strlen(service->testname)+1);
	strcpy(svcname, service->testname);
	if (service->namelen) svcname[service->namelen] = '\0';

	for (t=service->items; (t); t = t->next) {
		color = COL_GREEN;

		/*
		 * If DNS error, it is red.
		 * If not, then either (open=0,reverse=0) or (open=1,reverse=1) is wrong.
		 */
		if ((t->host->dnserror) || ((t->open + t->reverse) != 1)) {
			color = COL_RED;
		}

		/* Dialup hosts and dialup tests report red as clear */
		if ((color != COL_GREEN) && (t->host->dialup || t->dialup)) color = COL_CLEAR;

		init_status(color);
		sprintf(msgline, "status %s.%s %s %s %s %s\n", 
			commafy(t->host->hostname), svcname, colorname(color), timestamp,
			svcname, ((color == COL_RED) ? "NOT ok" : "ok"));
		addtostatus(msgline);

		if (t->host->dnserror) {
			sprintf(msgline, "\nUnable to resolve hostname %s\n", t->host->hostname);
		}
		else {
			sprintf(msgline, "\n&%s Service %s on %s is %s\n",
				colorname(color), svcname, t->host->hostname,
				(t->open ? "UP" : "DOWN"));
		}
		addtostatus(msgline);
		addtostatus("\n\n");
		finish_status();
	}

}

int main(int argc, char *argv[])
{
	service_t *s;
	testedhost_t *h;
	testitem_t *t;

	if ((argc > 1) && (strcmp(argv[1], "--debug") == 0)) debug = 1;

	init_timestamp();
	load_services();
	for (s = svchead; (s); s = s->next) {
		dprintf("Service %s port %d\n", s->testname, s->portnum);
	}

	load_tests();
	for (h = testhosthead; (h); h = h->next) {
		dprintf("Host %s, dnserror=%d, ip %s, dialup=%d testip=%d\n", 
			h->hostname, h->dnserror, h->ip, h->dialup, h->testip);
	}

	combo_start();
	dprintf("\nTest services\n");
	for (s = svchead; (s); s = s->next) {
		if (s->items) {
			run_nmap_service(s);
			dprintf("Service %s port %d\n", s->testname, s->portnum);
			for (t = s->items; (t); t = t->next) {
				dprintf("\tHost:%s, ip:%s, open:%d, reverse:%d, dialup:%d\n",
					t->host->hostname, t->host->ip, t->open, t->reverse, t->dialup);
			}
			send_results(s);
		}
	}
	combo_end();

	return 0;
}

