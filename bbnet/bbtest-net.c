/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbtest-net.c,v 1.1 2003-04-13 12:00:24 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <netdb.h>

#define MAX_LINE_LEN 4096

typedef struct {
	char *testname;
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
	void		*next;
} testitem_t;

service_t	*svchead = NULL;
testedhost_t	*hosthead = NULL;
testitem_t	*testhead = NULL;

service_t *add_service(char *name, int port)
{
	service_t *svc;

	svc = malloc(sizeof(service_t));
	svc->portnum = port;
	svc->testname = malloc(strlen(name)+1);
	strcpy(svc->testname, name);
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
		add_service(p, (svcinfo ? ntohs(svcinfo->s_port) : 0));
		p = strtok(NULL, " ");
	}
	free(netsvcs);
}


void load_tests(int pinginfo)
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

					for (s=svchead; (s && (strcmp(s->testname, testspec) != 0)); s = s->next) ;

					if (option && s) {
						int specialport;

						specialport = atoi(option);
						if ((s->portnum == 0) && (specialport > 0)) {
							char *specialname;

							specialname = malloc(strlen(s->testname)+10);
							sprintf(specialname, "%s_%d", s->testname, specialport);
							s = add_service(specialname, specialport);
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
						newtest->next = s->items;
						s->items = newtest;
					}

					testspec = strtok(NULL, "\t ");
				}

				if (anytests) {
					if (h->testip) {
						sprintf(h->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
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
								(unsigned char) hent->h_addr_list[0][2],
								(unsigned char) hent->h_addr_list[0][2],
								(unsigned char) hent->h_addr_list[0][3]);
						}
					}

					h->next = hosthead;
					hosthead = h;
				}
				else free(h);

			}
		}
		else {
			/* Other bb-hosts line - ignored */
		};
	}

	fclose(bbhosts);
	return;
}


int main(int argc, char *argv[])
{
	service_t *s;
	testedhost_t *h;
	testitem_t *t;

	load_services();
	for (s = svchead; (s); s = s->next) {
		printf("Service %s port %d\n", s->testname, s->portnum);
	}

	load_tests(0);
	for (h = hosthead; (h); h = h->next) {
		printf("Host %s, dnserror=%d, ip %s, dialup=%d testip=%d\n", 
			h->hostname, h->dnserror, h->ip, h->dialup, h->testip);
	}

	printf("\nTest services\n");
	for (s = svchead; (s); s = s->next) {
		printf("Service %s port %d\n", s->testname, s->portnum);
		for (t = s->items; (t); t = t->next) {
			printf("\tHost:%s, reverse:%d, dialup:%d\n",
				t->host->hostname, t->reverse, t->dialup);
		}
	}

	return 0;
}

