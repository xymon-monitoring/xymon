/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbtest-net.c,v 1.109 2003-09-05 16:51:11 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/wait.h>
#include <rpc/rpc.h>

#ifdef HAVE_RPCENT
#include <rpc/rpcent.h>
#endif

#include "bbgen.h"
#include "util.h"
#include "sendmsg.h"
#include "debug.h"
#include "bbtest-net.h"
#include "contest.h"
#include "httptest.h"
#include "ldaptest.h"

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

char *reqenv[] = {
	"BBNETSVCS",
	"NONETPAGE",
	"BBHOSTS",
	"BBTMP",
	"BBHOME",
	"BB",
	"BBDISP",
	"MACHINE",
	NULL
};

/* toolid values */
#define TOOL_CONTEST	0
#define TOOL_NSLOOKUP	1
#define TOOL_DIG	2
#define TOOL_NTP        3
#define TOOL_FPING      4
#define TOOL_CURL       5
#define TOOL_MODEMBANK  6
#define TOOL_LDAP	7
#define TOOL_RPCINFO	8

/* dnslookup values */
#define DNS_THEN_IP     0	/* Try DNS - if it fails, use IP from bb-hosts */
#define DNS_ONLY        1	/* DNS only - if it fails, report service down */
#define IP_ONLY         2	/* IP only - dont do DNS lookups */

service_t	*svchead = NULL;		/* Head of all known services */
service_t	*pingtest = NULL;		/* Identifies the pingtest within svchead list */
service_t	*httptest = NULL;		/* Identifies the httptest within svchead list */
service_t	*ldaptest = NULL;		/* Identifies the ldaptest within svchead list */
service_t	*rpctest = NULL;		/* Identifies the rpctest within svchead list */
service_t	*modembanktest = NULL;		/* Identifies the modembank test within svchead list */
testedhost_t	*testhosthead = NULL;		/* Head of all hosts */
char		*nonetpage = NULL;		/* The "NONETPAGE" env. variable */
int		dnsmethod = DNS_THEN_IP;	/* How to do DNS lookups */
int 		timeout=0;
int 		conntimeout=0;
long		followlocations = 0;		/* Follow Location: redirects in HTTP? */
char		*contenttestname = "content";   /* Name of the content checks column */
char		*ssltestname = "sslcert";       /* Name of the SSL certificate checks column */
int             sslwarndays = 30;		/* If cert expires in fewer days, SSL cert column = yellow */
int             sslalarmdays = 10;		/* If cert expires in fewer days, SSL cert column = red */
char		*location = "";			/* BBLOCATION value */
char		*logfile = NULL;
int		hostcount = 0;
int		testcount = 0;
int		notesthostcount = 0;
char		**selectedhosts;
int		selectedcount = 0;
int		testuntagged = 0;
time_t		frequenttestlimit = 1800;	/* Interval (seconds) when failing hosts are retried frequently */
int		checktcpresponse = 0;
int		fqdn = 1;

testitem_t *find_test(char *hostname, char *testname)
{
	testedhost_t *h;
	service_t *s;
	testitem_t *t;

	for (s=svchead; (s && (strcmp(s->testname, testname) != 0)); s = s->next);
	if (s == NULL) return NULL;

	for (h=testhosthead; (h && (strcmp(h->hostname, hostname) != 0)); h = h->next) ;
	if (h == NULL) return NULL;

	for (t=s->items; (t && (t->host != h)); t = t->next) ;

	return t;
}


char *deptest_failed(testedhost_t *host, char *testname)
{
	static char result[1024];

	char *depcopy;
	char depitem[MAX_LINE_LEN];
	char *p, *q;
	char *dephostname, *deptestname, *nextdep;
	testitem_t *t;

	if (host->deptests == NULL) return NULL;

	depcopy = malcop(host->deptests);
	sprintf(depitem, "(%s:", testname);
	p = strstr(depcopy, depitem);
	if (p == NULL) { free(depcopy); return NULL; }

	result[0] = '\0';
	dephostname = p+strlen(depitem);
	q = strchr(dephostname, ')');
	if (q) *q = '\0';

	/* dephostname now points to a list of "host1/test1,host2/test2" dependent tests. */
	while (dephostname) {
		p = strchr(dephostname, '/');
		if (p) {
			*p = '\0';
			deptestname = (p+1); 
		}
		else deptestname = "";

		p = strchr(deptestname, ',');
		if (p) {
			*p = '\0';
			nextdep = (p+1);
		}
		else nextdep = NULL;

		t = find_test(dephostname, deptestname);
		if (t && !t->open) {
			if (strlen(result) == 0) {
				strcpy(result, "\nThis test depends on the following test(s) that failed:\n\n");
			}

			if ((strlen(result) + strlen(dephostname) + strlen(deptestname) + 2) < sizeof(result)) {
				strcat(result, dephostname);
				strcat(result, "/");
				strcat(result, deptestname);
				strcat(result, "\n");
			}
		}

		dephostname = nextdep;
	}

	free(depcopy);
	if (strlen(result)) strcat(result, "\n\n");

	return (strlen(result) ? result : NULL);
}


service_t *add_service(char *name, int port, int namelen, int toolid)
{
	service_t *svc;

	svc = (service_t *) malloc(sizeof(service_t));
	svc->portnum = port;
	svc->testname = malcop(name); 
	svc->toolid = toolid;
	svc->namelen = namelen;
	svc->items = NULL;
	svc->next = svchead;
	svchead = svc;

	return svc;
}

int getportnumber(char *svcname)
{
	struct servent *svcinfo;

	svcinfo = getservbyname(svcname, NULL);
	return (svcinfo ? ntohs(svcinfo->s_port) : 0);
}

void load_services(void)
{
	char *netsvcs;
	char *p;

	netsvcs = malcop(getenv("BBNETSVCS"));

	p = strtok(netsvcs, " ");
	while (p) {
		add_service(p, getportnumber(p), 0, TOOL_CONTEST);
		p = strtok(NULL, " ");
	}
	free(netsvcs);

	/* Save NONETPAGE env. var in ",test1,test2," format for easy and safe grepping */
	nonetpage = (char *) malloc(strlen(getenv("NONETPAGE"))+3);
	sprintf(nonetpage, ",%s,", getenv("NONETPAGE"));
	for (p=nonetpage; (*p); p++) if (*p == ' ') *p = ',';
}


testedhost_t *init_testedhost(char *hostname, int timeout, int conntimeout, int okexpected)
{
	testedhost_t *newhost;

	hostcount++;
	newhost = (testedhost_t *) malloc(sizeof(testedhost_t));
	newhost->hostname = malcop(hostname);
	newhost->ip[0] = '\0';
	newhost->conntimeout = conntimeout;
	newhost->timeout = timeout;

	newhost->dialup = 0;
	newhost->testip = 0;
	newhost->nosslcert = 0;
	newhost->dnserror = 0;
	newhost->dodns = 0;
	newhost->okexpected = okexpected;
	newhost->repeattest = 0;

	newhost->noconn = 0;
	newhost->noping = 0;
	newhost->badconn[0] = newhost->badconn[1] = newhost->badconn[2] = 0;
	newhost->downcount = 0;
	newhost->downstart = 0;
	newhost->routerdeps = NULL;
	newhost->deprouterdown = NULL;

	newhost->firsthttp = NULL;

	newhost->firstldap = NULL;
	newhost->ldapuser = NULL;
	newhost->ldappasswd = NULL;

	newhost->sslwarndays = sslwarndays;
	newhost->sslalarmdays = sslwarndays;

	newhost->deptests = NULL;

	newhost->next = NULL;

	return newhost;
}

testitem_t *init_testitem(testedhost_t *host, service_t *service, char *testspec, 
                          int dialuptest, int reversetest, int alwaystruetest, int silenttest)
{
	testitem_t *newtest;

	testcount++;
	newtest = (testitem_t *) malloc(sizeof(testitem_t));
	newtest->host = host;
	newtest->service = service;
	newtest->dialup = dialuptest;
	newtest->reverse = reversetest;
	newtest->alwaystrue = alwaystruetest;
	newtest->silenttest = silenttest;
	newtest->testspec = testspec;
	newtest->privdata = NULL;
	newtest->open = 0;
	newtest->banner = NULL;
	newtest->certinfo = NULL;
	newtest->certexpires = 0;
	newtest->duration.tv_sec = newtest->duration.tv_usec = -1;
	newtest->downcount = 0;
	newtest->badtest[0] = newtest->badtest[1] = newtest->badtest[2] = 0;
	newtest->next = NULL;

	return newtest;
}


int wanted_host(char *l, char *netstring, char *hostname)
{
	if (selectedcount == 0)
		return ((netstring == NULL) || 				/* No BBLOCATION = do all */
			(strstr(l, netstring) != NULL) ||		/* BBLOCATION && matching NET: tag */
			(testuntagged && (strstr(l, "NET:") == NULL))); /* No NET: tag for this host */
	else {
		/* User provided an explicit list of hosts to test */
		int i;

		for (i=0; (i < selectedcount); i++) {
			if (strcmp(selectedhosts[i], hostname) == 0) return 1;
		}
	}

	return 0;
}


void load_tests(void)
{
	FILE 	*bbhosts;
	char 	l[MAX_LINE_LEN];	/* With multiple http tests, we may have long lines */
	char	hostname[MAX_LINE_LEN];
	int	ip1, ip2, ip3, ip4, banksize;
	char	*netstring, *routestring;
	char 	*p;

	bbhosts = stackfopen(getenv("BBHOSTS"), "r");
	if (bbhosts == NULL) {
		errprintf("No bb-hosts file found");
		return;
	}

	/* Each network test tagged with NET:locationname */
	p = getenv("BBLOCATION");
	if (p && (strlen(p) > 0)) {
		netstring = (char *) malloc(strlen(p)+strlen("NET:")+1);
		sprintf(netstring, "NET:%s", p);
		routestring = (char *) malloc(strlen(p)+strlen("route_:")+1);
		sprintf(routestring, "route_%s:", p);
	}
	else {
		netstring = NULL;
		routestring = NULL;
	}

	while (stackfgets(l, sizeof(l), "include")) {
		p = strchr(l, '\n'); if (p) { *p = '\0'; };
		for (p=l; (*p && isspace((int) *p)); p++) ;

		if ((*p == '#') || (*p == '\0')) {
			/* Do nothing - it's a comment or empty line */
		}
		else if (sscanf(l, "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {

			if (!fqdn) {
				/* FQDN=OFF means strip the domain part of the hostname */
				char *p = strchr(hostname, '.');

				if (p) {
					*p = '\0';
				}
			}

			if (wanted_host(l, netstring, hostname)) {

				char *testspec;
				char *badsaves;
				testedhost_t *h;
				testitem_t *newtest;
				int anytests = 0;
				int ping_dialuptest = 0;
				int ping_reversetest = 0;

				p = strchr(l, '#'); 
				if (p) {
					p++;
					while (isspace((unsigned int) *p)) p++;
				}
				else {
					/* There is just an IP and a hostname - handle as if no tests */
					p = "";
				}

				h = init_testedhost(hostname, timeout, conntimeout, 
						    (strstr(p, "SLA=") ? within_sla(p, "SLA", 1) : !within_sla(p, "DOWNTIME", 0)) );
				anytests = 0;
				badsaves = (char *) malloc(strlen(p)+1); *badsaves = '\0';

				testspec = strtok(p, "\t ");
				while (testspec) {

					service_t *s = NULL;
					int dialuptest = 0;
					int reversetest = 0;
					int alwaystruetest = 0;
					int silenttest = 0;
					int specialtag = 0;
					char *savedspec = NULL;

					if ((strncmp(testspec, "badconn", 7) == 0) && periodcoversnow(testspec+7)) {
						char *p =strchr(testspec, ':');

						specialtag = 1;
						if (p) sscanf(p, ":%d:%d:%d", &h->badconn[0], &h->badconn[1], &h->badconn[2]);
					}
					else if (strncmp(testspec, "bad", 3) == 0) {
						if (strlen(badsaves)) strcat(badsaves, " ");
						strcat(badsaves, testspec);
						specialtag = 1;
					}
					else if (strncmp(testspec, "route:", 6) == 0) {
						specialtag = 1;
						h->routerdeps = malcop(testspec+6);
					}
					else if (routestring && (strncasecmp(testspec, routestring, strlen(routestring)) == 0)) {
						specialtag = 1;
						h->routerdeps = malcop(testspec+strlen(routestring));
					}
					else if (strncmp(testspec, "TIMEOUT:", 8) == 0) {
						specialtag = 1;

						if (sscanf(testspec, "TIMEOUT:%d:%d", &h->conntimeout, &h->timeout) != 2) {
							h->timeout = timeout;
							h->conntimeout = 0;
						}
					}
					else if (strcmp(testspec, "noconn") == 0) { specialtag = 1; h->noconn = 1; }
					else if (strcmp(testspec, "noping") == 0) { specialtag = 1; h->noping = 1; }
					else if (strcmp(testspec, "testip") == 0) { specialtag = 1; h->testip = 1; }
					else if (strcmp(testspec, "dialup") == 0) { specialtag = 1; h->dialup = 1; }
					else if (strcmp(testspec, "nosslcert") == 0) { specialtag = 1; h->nosslcert = 1; }
					else if (strncmp(testspec, "ssldays=", 8) == 0) {
						int warndays, alarmdays;

						if (sscanf(testspec, "ssldays=%d:%d", &warndays, &alarmdays) == 2) {
							h->sslwarndays = warndays;
							h->sslalarmdays = alarmdays;
						}
					}
					else if (strncmp(testspec, "depends=", 8) == 0) {
						specialtag = 1;
						h->deptests = malcop(testspec+8);
					}
					else if (strncmp(testspec, "ldaplogin=", 10) == 0) {
						char *username, *password;
						
						username = password = NULL;
						username = (strchr(testspec, '='));
						if (username) {
							username++;
							password = (strchr(username, ':'));
							if (password) {
								*password = '\0';
								password++;
							}
						}

						specialtag = 1;
						if (username) h->ldapuser = malcop(username);
						if (password) h->ldappasswd = malcop(password);
					}

					if (!specialtag) {
						/* Test prefixes:
						 * - '?' denotes dialup test, i.e. report failures as clear.
						 * - '|' denotes reverse test, i.e. service should be DOWN.
						 * - '~' denotes test that ignores ping result (normally,
						 *       TCP tests are reported CLEAR if ping check fails;
						 *       with this flag report their true status)
						 */
						dialuptest = reversetest = alwaystruetest = 0;
						if (*testspec == '?') { dialuptest=1;     testspec++; }
						if (*testspec == '!') { reversetest=1;    testspec++; }
						if (*testspec == '~') { alwaystruetest=1; testspec++; }
					}

					if (specialtag) {
						s = NULL;
					}
					else if (pingtest && (strcmp(testspec, pingtest->testname) == 0)) {
						/*
						 * Ping/conn test. Save any modifier flags for later use.
						 */
						ping_dialuptest = dialuptest;
						ping_reversetest = reversetest;
						s = NULL; /* Dont add the test now - ping is special (enabled by default) */
					}
					else if ((strncmp(testspec, "ldap:", 5) == 0) ||
						 (strncmp(testspec, "ldaps:", 6) == 0)) {
						/*
						 * LDAP test. This uses ':' a lot, so save it here.
						 */
#ifdef BBGEN_LDAP
						s = ldaptest;
						savedspec = malcop(testspec);
#else
						errprintf("ldap test requested, but bbgen was built with no ldap support\n");
#endif
					}
					else if (strncmp(testspec, "http", 4) == 0) {
						/*
						 * HTTP test. This uses ':' a lot, so save it here.
						 */
						s = httptest;
						savedspec = malcop(testspec);
					}
					else if (strncmp(testspec, "content=", 8) == 0) {
						/*
						 * Content check. Like http above.
						 */
						s = httptest;
						savedspec = malcop(testspec);
					}
					else if (strncmp(testspec, "cont;", 5) == 0) {
						/*
						 * Content check, "cont.sh" style. Like http above.
						 */
						s = httptest;
						savedspec = malcop(testspec);
					}
					else if (strncmp(testspec, "post;", 5) == 0) {
						/*
						 * POST with content check, "cont.sh" style. Like http above.
						 */
						s = httptest;
						savedspec = malcop(testspec);
					}
					else if (strncmp(testspec, "rpc", 3) == 0) {
						/*
						 * rpc check via rpcinfo
						 */
						s = rpctest;
						savedspec = malcop(testspec);
					}
					else {
						/* 
						 * Simple TCP connect test. 
						 */
						char *option;

						/* Remove any trailing ":s", ":q", ":Q", ":portnumber" */
						option = strchr(testspec, ':'); 
						if (option) { 
							*option = '\0'; 
							option++; 
						}
	
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
							int specialport = 0;
							char *specialname;
							char *opt2 = strrchr(option, ':');

							if (opt2) {
								if (strcmp(opt2, ":s") == 0) {
									/* option = "portnumber:s" */
									silenttest = 1;
									*opt2 = '\0';
									specialport = atoi(option);
									*opt2 = ':';
								}
							}
							else if (strcmp(option, "s") == 0) {
								/* option = "s" */
								silenttest = 1;
								specialport = 0;
							}
							else {
								/* option = "portnumber" */
								specialport = atoi(option);
							}

							if (specialport) {
								specialname = (char *) malloc(strlen(s->testname)+10);
								sprintf(specialname, "%s_%d", s->testname, specialport);
								s = add_service(specialname, specialport, strlen(s->testname), TOOL_CONTEST);
								free(specialname);
							}
						}

						if (s) h->dodns = 1;
						if (option) *(option-1) = ':';
					}

					if (s) {
						anytests = 1;
						newtest = init_testitem(h, s, savedspec, dialuptest, reversetest, alwaystruetest, silenttest);
						newtest->next = s->items;
						s->items = newtest;

						if (s == httptest) h->firsthttp = newtest;
						else if (s == ldaptest) h->firstldap = newtest;
					}

					testspec = strtok(NULL, "\t ");
				}

				if (pingtest && !h->noconn) {
					/* Add the ping check */
					anytests = 1;
					newtest = init_testitem(h, pingtest, NULL, ping_dialuptest, ping_reversetest, 1, 0);
					newtest->next = pingtest->items;
					pingtest->items = newtest;
					h->dodns = 1;
				}

				/* 
				 * Setup badXXX values.
				 *
				 * We need to do this last, because the testitem_t records do
				 * not exist until the test has been created.
				 *
				 * So after parsing the badFOO tag, we must find the testitem_t
				 * record created earlier for this test (it may not exist).
				 */
				testspec = strtok(badsaves, " ");
				while (testspec) {
					char *testname, *timespec, *badcounts;
					int badclear, badyellow, badred;
					int inscope;
					testitem_t *twalk;
					service_t *swalk;

					badclear = badyellow = badred = 0;
					inscope = 1;

					testname = testspec+strlen("bad");
					badcounts = strchr(testspec, ':');
					if (badcounts) {
						*badcounts = '\0';
						badcounts++;
						if (sscanf(badcounts, "%d:%d:%d", &badclear, &badyellow, &badred) != 3) {
							errprintf("Incorrect 'bad' counts: '%s'\n", badcounts);
							badcounts = NULL;
						}
					}
					timespec = strchr(testspec, '-');
					if (timespec) inscope = periodcoversnow(timespec);

					if (strlen(testname) && badcounts && inscope) {
						twalk = NULL;

						for (swalk=svchead; (swalk && (strcmp(swalk->testname, testname) != 0)); swalk = swalk->next) ;
						if (swalk) {
							if (swalk == httptest) twalk = h->firsthttp;
							else if (swalk == ldaptest) twalk = h->firstldap;
							else {
								for (twalk = swalk->items; (twalk && (twalk->host != h)); twalk = twalk->next) ;
							}
						}

						if (twalk) {
							twalk->badtest[0] = badclear;
							twalk->badtest[1] = badyellow;
							twalk->badtest[2] = badred;
						}
						else {
							dprintf("No test for badtest spec host=%s, test=%s\n",
								h->hostname, testname);
						}
					}

					testspec = strtok(NULL, " ");
				}
				free(badsaves);

				if (anytests) {
					sprintf(h->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
					h->next = testhosthead;
					testhosthead = h;
				}
				else {
					/* No network tests for this host, so ignore it */
					dprintf("Did not find any network tests for host %s\n", h->hostname);
					free(h);
					notesthostcount++;
				}
			}
		}
		else if ((sscanf(l, "dialup %s %3d.%3d.%3d.%3d %d", hostname, &ip1, &ip2, &ip3, &ip4, &banksize) == 6) && (banksize > 0)) {
			/* Modembank entry: "dialup displayname startIP count" */

			if (wanted_host (l, netstring, hostname)) {
				testitem_t *newtest;
				modembank_t *newentry;
				int i;

				newtest = init_testitem(NULL, modembanktest, NULL, 0, 0, 0, 0);
				newtest->next = modembanktest->items;
				modembanktest->items = newtest;

				newtest->privdata = (void *)malloc(sizeof(modembank_t));
				newentry = (modembank_t *)newtest->privdata;

				newentry->hostname = malcop(hostname);
				newentry->startip = IPtou32(ip1, ip2, ip3, ip4);
				newentry->banksize = banksize;
				newentry->responses = (int *) malloc(banksize * sizeof(int));
				for (i=0; i<banksize; i++) newentry->responses[i] = 0;
			}
		}
		else {
			/* Other bb-hosts line - ignored */
		};
	}

	stackfclose(bbhosts);
	return;
}


void dns_resolve(void)
{
	testedhost_t	*h;

	for (h=testhosthead; (h); h=h->next) {
		/* 
		 * Determine the IP address to test. We do it here,
		 * to avoid multiple DNS lookups for each service 
		 * we test on a host.
		 */
		if (h->testip || (dnsmethod == IP_ONLY)) {
			if (strcmp(h->ip, "0.0.0.0") == 0) {
				errprintf("bbtest-net: %s has IP 0.0.0.0 and testip - dropped\n", h->hostname);
				h->dnserror = 1;
			}
		}
		else if (h->dodns) {
			struct hostent *hent;

			hent = gethostbyname(h->hostname);
			if (hent) {
				sprintf(h->ip, "%d.%d.%d.%d", 
					(unsigned char) hent->h_addr_list[0][0],
					(unsigned char) hent->h_addr_list[0][1],
					(unsigned char) hent->h_addr_list[0][2],
					(unsigned char) hent->h_addr_list[0][3]);
			}
			else if (dnsmethod == DNS_THEN_IP) {
				/* Already have the IP setup */
			}
			else {
				/* Cannot resolve hostname */
				h->dnserror = 1;
			}

			if (strcmp(h->ip, "0.0.0.0") == 0) {
				errprintf("bbtest-net: IP resolver error for host %s\n", h->hostname);
				h->dnserror = 1;
			}
		}
	}
}


void load_fping_status(void)
{
	FILE *statusfd;
	char statusfn[MAX_PATH];
	char l[MAX_LINE_LEN];
	char host[MAX_LINE_LEN];
	int  downcount;
	time_t downstart;
	testedhost_t *h;

	sprintf(statusfn, "%s/fping.%s.status", getenv("BBTMP"), location);
	statusfd = fopen(statusfn, "r");
	if (statusfd == NULL) return;

	while (fgets(l, sizeof(l), statusfd)) {
		if (sscanf(l, "%s %d %u", host, &downcount, (unsigned int *)&downstart) == 3) {
			for (h=testhosthead; (h && (strcmp(h->hostname, host) != 0)); h = h->next) ;
			if (h && !h->noping && !h->noconn) {
				h->downcount = downcount;
				h->downstart = downstart;
			}
		}
	}

	fclose(statusfd);
}

void save_fping_status(void)
{
	FILE *statusfd;
	char statusfn[MAX_PATH];
	testitem_t *t;
	int didany = 0;

	sprintf(statusfn, "%s/fping.%s.status", getenv("BBTMP"), location);
	statusfd = fopen(statusfn, "w");
	if (statusfd == NULL) return;

	for (t=pingtest->items; (t); t = t->next) {
		if (t->host->downcount) {
			fprintf(statusfd, "%s %d %u\n", t->host->hostname, t->host->downcount, (unsigned int)t->host->downstart);
			didany = 1;
			t->host->repeattest = ((time(NULL) - t->host->downstart) < frequenttestlimit);
		}
	}

	fclose(statusfd);
	if (!didany) unlink(statusfn);
}

void load_test_status(service_t *test)
{
	FILE *statusfd;
	char statusfn[MAX_PATH];
	char l[MAX_LINE_LEN];
	char host[MAX_LINE_LEN];
	int  downcount;
	time_t downstart;
	testedhost_t *h;
	testitem_t *walk;

	sprintf(statusfn, "%s/%s.%s.status", getenv("BBTMP"), test->testname, location);
	statusfd = fopen(statusfn, "r");
	if (statusfd == NULL) return;

	while (fgets(l, sizeof(l), statusfd)) {
		if (sscanf(l, "%s %d %u", host, &downcount, (unsigned int *) &downstart) == 3) {
			for (h=testhosthead; (h && (strcmp(h->hostname, host) != 0)); h = h->next) ;
			if (h) {
				if (test == httptest) walk = h->firsthttp;
				else if (test == ldaptest) walk = h->firstldap;
				else for (walk = test->items; (walk && (walk->host != h)); walk = walk->next) ;

				if (walk) {
					walk->downcount = downcount;
					walk->downstart = downstart;
				}
			}
		}
	}

	fclose(statusfd);
}

void save_test_status(service_t *test)
{
	FILE *statusfd;
	char statusfn[MAX_PATH];
	testitem_t *t;
	int didany = 0;

	sprintf(statusfn, "%s/%s.%s.status", getenv("BBTMP"), test->testname, location);
	statusfd = fopen(statusfn, "w");
	if (statusfd == NULL) return;

	for (t=test->items; (t); t = t->next) {
		if (t->downcount) {
			fprintf(statusfd, "%s %d %u\n", t->host->hostname, t->downcount, (unsigned int)t->downstart);
			didany = 1;
			t->host->repeattest = ((time(NULL) - t->downstart) < frequenttestlimit);
		}
	}

	fclose(statusfd);
	if (!didany) unlink(statusfn);
}


void save_frequenttestlist(int argc, char *argv[])
{
	FILE *fd;
	char fn[MAX_PATH];
	testedhost_t *h;
	int didany = 0;
	int i;

	sprintf(fn, "%s/frequenttests.%s", getenv("BBTMP"), location);
	fd = fopen(fn, "w");
	if (fd == NULL) return;

	for (i=1; (i<argc); i++) {
		if (!argnmatch(argv[i], "--report")) fprintf(fd, "\"%s\" ", argv[i]);
	}
	for (h = testhosthead; (h); h = h->next) {
		if (h->repeattest) {
			fprintf(fd, "%s ", h->hostname);
			didany = 1;
		}
	}

	fclose(fd);
	if (!didany) unlink(fn);
}


int run_command(char *cmd, char *errortext, char **banner)
{
	FILE	*cmdpipe;
	char	l[1024];
	int	result;
	int	piperes;
	int	bannersize = 0;

	result = 0;
	if (banner) { 
		bannersize = 4096;
		*banner = (char *) malloc(bannersize); 
		sprintf(*banner, "Command: %s\n\n", cmd); 
	}
	cmdpipe = popen(cmd, "r");
	if (cmdpipe == NULL) {
		errprintf("Could not open pipe for command %s\n", cmd);
		if (banner) strcat(*banner, "popen() failed to run command\n");
		return -1;
	}

	while (fgets(l, sizeof(l), cmdpipe)) {
		if (banner) {
			if ((strlen(l) + strlen(*banner)) > bannersize) {
				bannersize += strlen(l) + 4096;
				*banner = (char *) realloc(*banner, bannersize);
			}
			strcat(*banner, l);
		}
		if (errortext && (strstr(l, errortext) != NULL)) result = 1;
	}
	piperes = pclose(cmdpipe);
	if (!WIFEXITED(piperes) || (WEXITSTATUS(piperes) != 0)) {
		/* Something failed */
		result = 1;
	}

	return result;
}

void run_nslookup_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[MAX_PATH];

	p = getenv("NSLOOKUP");
	strcpy(cmdpath, (p ? p : "nslookup"));
	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror) {
			sprintf(cmd, "%s %s %s 2>&1", 
				cmdpath, t->host->hostname, t->host->ip);
			t->open = (run_command(cmd, "can't find", &t->banner) == 0);
		}
	}
}

void run_dig_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[MAX_PATH];

	p = getenv("DIG");
	strcpy(cmdpath, (p ? p : "dig"));
	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror) {
			sprintf(cmd, "%s @%s %s 2>&1", 
				cmdpath, t->host->ip, t->host->hostname);
			t->open = (run_command(cmd, "Bad server", &t->banner) == 0);
		}
	}
}

void run_ntp_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[MAX_PATH];

	p = getenv("NTPDATE");
	strcpy(cmdpath, (p ? p : "ntpdate"));
	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror) {
			sprintf(cmd, "%s -u -q -p 2 %s 2>&1", cmdpath, t->host->ip);
			t->open = (run_command(cmd, "no server suitable for synchronization", &t->banner) == 0);
		}
	}
}


void run_rpcinfo_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[MAX_PATH];

	p = getenv("RPCINFO");
	strcpy(cmdpath, (p ? p : "rpcinfo"));
	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror) {
			sprintf(cmd, "%s -p %s 2>&1", cmdpath, t->host->ip);
			t->open = (run_command(cmd, NULL, &t->banner) == 0);
		}
	}
}


int run_fping_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[MAX_PATH];
	char		logfn[MAX_PATH];
	FILE		*cmdpipe;
	FILE		*logfd;
	char		l[MAX_LINE_LEN];
	char		hostname[MAX_LINE_LEN];
	int		ip1, ip2, ip3, ip4;

	/* Run "fping -Ae 2>/dev/null" and feed it all IP's to test */
	p = getenv("FPING");
	strcpy(cmdpath, (p ? p : "fping"));
	sprintf(logfn, "%s/fping.%lu", getenv("BBTMP"), (unsigned long)getpid());
	sprintf(cmd, "%s -Ae 2>/dev/null 1>%s", cmdpath, logfn);

	cmdpipe = popen(cmd, "w");
	if (cmdpipe == NULL) {
		errprintf("Could not run the fping command %s\n", cmd);
		return -1;
	}
	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror && !t->host->noping) {
			fprintf(cmdpipe, "%s\n", t->host->ip);
		}
	}
	pclose(cmdpipe);

	/* Load status of previously failed tests */
	load_fping_status();

	logfd = fopen(logfn, "r");
	if (logfd == NULL) { errprintf("Cannot open fping output file!\n"); return -1; }
	while (fgets(l, sizeof(l), logfd)) {
		if (sscanf(l, "%d.%d.%d.%d ", &ip1, &ip2, &ip3, &ip4) == 4) {
			p = strchr(l, ' ');
			if (p) *p = '\0';
			strcpy(hostname, l);
			if (p) *p = ' ';

			/*
			 * Need to loop through all testitems - there may be multiple entries for
			 * the same IP-address.
			 */
			for (t=service->items; (t); t = t->next) {
				if (strcmp(t->host->ip, hostname) == 0) {
					t->open = (strstr(l, "is alive") != NULL);
					t->banner = malcop(l);
				}
			}
		}
	}
	fclose(logfd);
	if (!debug) unlink(logfn);

	/* 
	 * Handle the router dependency stuff. I.e. for all hosts
	 * where the ping test failed, go through the list of router
	 * dependencies and if one of the dependent hosts also has 
	 * a failed ping test, point the dependency there.
	 */
	for (t=service->items; (t); t = t->next) {
		if (!t->open && t->host->routerdeps) {
			testitem_t *router;

			strcpy(l, t->host->routerdeps);
			p = strtok(l, ",");
			while (p && (t->host->deprouterdown == NULL)) {
				for (router=service->items; 
					(router && (strcmp(p, router->host->hostname) != 0)); 
					router = router->next) ;

				if (router && !router->open) t->host->deprouterdown = router->host;

				p = strtok(NULL, ",");
			}
		}
	}

	return 0;
}

void run_modembank_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		startip[16], endip[16];
	char		*p;
	char		cmdpath[MAX_PATH];
	FILE		*cmdpipe;
	char		l[MAX_LINE_LEN];
	int		ip1, ip2, ip3, ip4;

	for (t=service->items; (t); t = t->next) {
		modembank_t *req = (modembank_t *)t->privdata;

		p = getenv("FPING");
		strcpy(cmdpath, (p ? p : "fping"));
		strcpy(startip, u32toIP(req->startip));
		strcpy(endip, u32toIP(req->startip + req->banksize - 1));
		sprintf(cmd, "%s -g -Ae %s %s 2>/dev/null", cmdpath, startip, endip);

		dprintf("Running command: '%s'\n", cmd);
		cmdpipe = popen(cmd, "r");
		if (cmdpipe == NULL) {
			errprintf("Could not run the fping command %s\n", cmd);
			return;
		}

		while (fgets(l, sizeof(l), cmdpipe)) {
			dprintf("modembank response: %s", l);

			if (sscanf(l, "%d.%d.%d.%d ", &ip1, &ip2, &ip3, &ip4) == 4) {
				unsigned int idx = IPtou32(ip1, ip2, ip3, ip4) - req->startip;

				if (idx >= req->banksize) {
					errprintf("Unexpected response for IP not in bank - %d.%d.%d.%d", 
						  ip1, ip2, ip3, ip4);
				}
				else {
					req->responses[idx] = (strstr(l, "is alive") != NULL);
				}
			}
		}
		pclose(cmdpipe);

		if (debug) {
			int i;

			dprintf("Results for modembank start=%s, length %d\n", u32toIP(req->startip), req->banksize);
			for (i=0; (i<req->banksize); i++)
				dprintf("\t%s is %d\n", u32toIP(req->startip+i), req->responses[i]);
		}
	}
}


int decide_color(service_t *service, char *svcname, testitem_t *test, int failgoesclear, char *cause)
{
	int color = COL_GREEN;
	int countasdown = 0;

	*cause = '\0';
	if (service == pingtest) {
		/*
		 * "noconn" is handled elsewhere.
		 * "noping" always sends back a status "clear".
		 * If DNS error, return red and count as down.
		 */
		if (test->host->noping) { 
			/* Ping test disabled - go "clear". End of story. */
			strcpy(cause, "Ping test disabled (noping)");
			return COL_CLEAR; 
		}
		else if (test->host->dnserror) { 
			strcpy(cause, "DNS lookup failure");
			color = COL_RED; countasdown = 1; 
		}
		else {
			/* Red if (open=0, reverse=0) or (open=1, reverse=1) */
			if ((test->open + test->reverse) != 1) { 
				sprintf(cause, "Host %s respond to ping", (test->open ? "does" : "does not"));
				color = COL_RED; countasdown = 1; 
			}
		}

		/* Handle the "route" tag dependencies. */
		if ((color == COL_RED) && test->host->deprouterdown) { 
			strcat(cause, "\nIntermediate ");
			strcat(cause, (getenv("BBROUTERTEXT") ? getenv("BBROUTERTEXT") : "router"));
			strcat(cause, " down ");
			color = COL_YELLOW; 
		}

		/* Handle "badconn" */
		if ((color == COL_RED) && (test->host->downcount < test->host->badconn[2])) {
			if      (test->host->downcount >= test->host->badconn[1]) color = COL_YELLOW;
			else if (test->host->downcount >= test->host->badconn[0]) color = COL_CLEAR;
			else                                                      color = COL_GREEN;
		}
	}
	else {
		/* TCP test */
		if (test->host->dnserror) { 
			strcpy(cause, "DNS lookup failure");
			color = COL_RED; countasdown = 1; 
		}
		else {
			if (test->reverse) {
				/*
				 * Reverse tests go RED when open.
				 * If not open, they may go CLEAR if the ping test failed
				 */

				if (test->open) { 
					strcpy(cause, "Service responds when it should not");
					color = COL_RED; countasdown = 1; 
				}
				else if (failgoesclear && (test->host->downcount != 0) && !test->alwaystrue) {
					strcpy(cause, "Host appears to be down");
					color = COL_CLEAR; countasdown = 0;
				}
			}
			else {
				if (!test->open) {
					if (failgoesclear && (test->host->downcount != 0) && !test->alwaystrue) {
						strcpy(cause, "Host appears to be down");
						color = COL_CLEAR; countasdown = 0;
					}
					else {
						strcpy(cause, "Service does not respond");
						color = COL_RED; countasdown = 1;
					}
				}
				else {
					/* Check if we got the expected data */
					if (checktcpresponse && !tcp_got_expected((test_t *)test->privdata)) {
						strcpy(cause, "Unexpected service response");
						color = COL_YELLOW; countasdown = 1;
					}
				}
			}
		}

		/* Handle test dependencies */
		if ( failgoesclear && (color == COL_RED) && !test->alwaystrue && deptest_failed(test->host, test->service->testname) ) {
			strcat(cause, "\nProblem appears to be with another service or host");
			color = COL_CLEAR;
		}

		/* Handle the "badtest" stuff for other tests */
		if ((color == COL_RED) && (test->downcount < test->badtest[2])) {
			if      (test->downcount >= test->badtest[1]) color = COL_YELLOW;
			else if (test->downcount >= test->badtest[0]) color = COL_CLEAR;
			else                                          color = COL_GREEN;
		}
	}


	/* If non-green and it was not expected to be up, report as BLUE */
	if ((color != COL_GREEN) && !test->host->okexpected) {
		strcat(cause, "\nHost or service was not expected to be up");
		color = COL_BLUE;
	}

	/* Dialup hosts and dialup tests report red as clear */
	if ( ((color == COL_RED) || (color == COL_YELLOW)) && (test->host->dialup || test->dialup) ) { 
		strcat(cause, "\nDialup host or service");
		color = COL_CLEAR; countasdown = 0; 
	}

	/* If a NOPAGENET service, downgrade RED to YELLOW */
	if (color == COL_RED) {
		char *nopagename;

		/* Check if this service is a NOPAGENET service. */
		nopagename = (char *) malloc(strlen(svcname)+3);
		sprintf(nopagename, ",%s,", svcname);
		if (strstr(nonetpage, svcname) != NULL) color = COL_YELLOW;
		free(nopagename);
	}

	if (service == pingtest) {
		if (countasdown) {
			test->host->downcount++; 
			if (test->host->downcount == 1) test->host->downstart = time(NULL);
		}
		else test->host->downcount = 0;
	}
	else {
		if (countasdown) {
			test->downcount++; 
			if (test->downcount == 1) test->downstart = time(NULL);
		}
		else test->downcount = 0;
	}
	return color;
}


void send_results(service_t *service, int failgoesclear)
{
	testitem_t	*t;
	int		color;
	char		msgline[MAXMSG];
	char		msgtext[MAXMSG];
	char		causetext[1024];
	char		*svcname;

	svcname = malcop(service->testname);
	if (service->namelen) svcname[service->namelen] = '\0';

	dprintf("Sending results for service %s\n", svcname);

	for (t=service->items; (t); t = t->next) {
		char flags[10];
		int i;

		i = 0;
		flags[i++] = (t->open ? 'O' : 'o');
		flags[i++] = (t->reverse ? 'R' : 'r');
		flags[i++] = ((t->dialup || t->host->dialup) ? 'D' : 'd');
		flags[i++] = (t->alwaystrue ? 'A' : 'a');
		flags[i++] = (t->silenttest ? 'S' : 's');
		flags[i++] = (t->host->testip ? 'T' : 't');
		flags[i++] = (t->host->okexpected ? 'I' : 'i');
		flags[i++] = (t->host->dodns ? 'L' : 'l');
		flags[i++] = (t->host->dnserror ? 'E' : 'e');
		flags[i++] = '\0';

		color = decide_color(service, svcname, t, failgoesclear, causetext);

		init_status(color);
		sprintf(msgline, "status %s.%s %s <!-- [flags:%s] --> %s %s %s ", 
			commafy(t->host->hostname), svcname, colorname(color), 
			flags, timestamp, 
			svcname, ( ((color == COL_RED) || (color == COL_YELLOW)) ? "NOT ok" : "ok"));

		if (t->host->dnserror) {
			strcat(msgline, ": DNS lookup failed");
			sprintf(msgtext, "\nUnable to resolve hostname %s\n\n", t->host->hostname);
		}
		else {
			sprintf(msgtext, "\nService %s on %s is ", svcname, t->host->hostname);
			switch (color) {
			  case COL_GREEN: 
				  strcat(msgtext, "OK ");
				  strcat(msgtext, (t->reverse ? "(down)" : "(up)"));
				  strcat(msgtext, "\n");
				  break;

			  case COL_RED:
			  case COL_YELLOW:
				  if ((service == pingtest) && t->host->deprouterdown) {
					strcat(msgline, ": Intermediate ");
					strcat(msgline, (getenv("BBROUTERTEXT") ? getenv("BBROUTERTEXT") : "router"));
					strcat(msgline, " down");

					strcat(msgtext, "not OK.\n");
					strcat(msgtext, "The device ");
					strcat(msgtext, ((testedhost_t *)t->host->deprouterdown)->hostname);
					strcat(msgtext, " (IP:");
					strcat(msgtext, ((testedhost_t *)t->host->deprouterdown)->ip);
					strcat(msgtext, ") ");
					strcat(msgtext, "is not reachable, causing this host to be unreachable.");
					strcat(msgtext, "\n");
				  }
				  else {
				  	strcat(msgtext, "not OK : ");
				  	strcat(msgtext, causetext);
				  	strcat(msgtext, "\n");
				  }
				  break;

			  case COL_CLEAR:
				  strcat(msgtext, "OK\n");
				  if (service == pingtest) {
					  if (t->host->deprouterdown) {
						strcat(msgline, ": Intermediate ");
						strcat(msgline, (getenv("BBROUTERTEXT") ? getenv("BBROUTERTEXT") : "router"));
						strcat(msgline, " down");

						strcat(msgtext, "The device ");
						strcat(msgtext, ((testedhost_t *)t->host->deprouterdown)->hostname);
						strcat(msgtext, " (IP:");
						strcat(msgtext, ((testedhost_t *)t->host->deprouterdown)->ip);
						strcat(msgtext, ") ");
						strcat(msgtext, "is not reachable, causing this host to be unreachable.");
						strcat(msgtext, "\n");
					  }
					  else if (t->host->noping) {
						  strcat(msgline, ": Disabled");
						  strcat(msgtext, "Ping check disabled (noping)\n");
					  }
					  else if (t->host->dialup) {
						  strcat(msgline, ": Disabled (dialup host)");
						  strcat(msgtext, "Dialup host\n");
					  }
					  /* "clear" due to badconn: no extra text */
				  }
				  else {
					  /* Non-ping test clear: Dialup test or failed ping */
					  strcat(msgline, ": Failure ignored due to failure of another test");
					  strcat(msgtext, "Dialup host/service, or test depends on another failed test\n");
				  }
				  break;

			  case COL_BLUE:
				  strcat(msgline, ": Temporarily disabled");
				  strcat(msgtext, "OK\n");
				  strcat(msgtext, "Host currently not monitored due to DOWNTIME setting.\n");
				  break;
			}
			strcat(msgtext, "\n");
		}
		strcat(msgline, "\n");
		addtostatus(msgline);
		addtostatus(msgtext);

		if ((service == pingtest) && t->host->downcount) {
			sprintf(msgtext, "\nSystem unreachable for %d poll periods (%u seconds)\n",
				t->host->downcount, (unsigned int)(time(NULL) - t->host->downstart));
			addtostatus(msgtext);
		}

		if (t->banner) {
			addtostatus("\n"); addtostatus(t->banner); addtostatus("\n");
		}
		if (t->duration.tv_sec != -1) {
			sprintf(msgtext, "\nSeconds: %ld.%03ld\n", 
				t->duration.tv_sec, t->duration.tv_usec / 1000);
			addtostatus(msgtext);
		}
		addtostatus("\n\n");
		finish_status();
	}
}


void send_modembank_results(service_t *service)
{
	testitem_t	*t;
	char		msgline[1024];
	int		i, color, inuse;
	char		startip[16], endip[16];

	inuse = 0;
	for (t=service->items; (t); t = t->next) {
		modembank_t *req = (modembank_t *)t->privdata;

		strcpy(startip, u32toIP(req->startip));
		strcpy(endip, u32toIP(req->startip + req->banksize - 1));

		init_status(COL_GREEN);		/* Modembanks are always green */
		sprintf(msgline, "status dialup.%s %s %s FROM %s TO %s DATA ", 
			commafy(req->hostname), colorname(COL_GREEN), timestamp, startip, endip);
		addtostatus(msgline);
		for (i=0; i<req->banksize; i++) {
			if (req->responses[i]) {
				color = COL_GREEN;
				inuse++;
			}
			else {
				color = COL_CLEAR;
			}

			sprintf(msgline, "%s ", colorname(color));
			addtostatus(msgline);
		}

		sprintf(msgline, "\n\nUsage: %d of %d (%d%%)\n", inuse, req->banksize, ((inuse * 100) / req->banksize));
		addtostatus(msgline);
		finish_status();
	}
}


void send_rpcinfo_results(service_t *service, int failgoesclear)
{
	testitem_t	*t;
	int		color;
	char		msgline[1024];
	char		*msgbuf;
	char		causetext[1024];

	msgbuf = (char *)malloc(4096);

	for (t=service->items; (t); t = t->next) {
		char *wantedrpcsvcs = NULL;
		char *p;

		/* First see if the rpcinfo command succeeded */
		*msgbuf = '\0';

		color = decide_color(service, service->testname, t, failgoesclear, causetext);
		p = strchr(t->testspec, '=');
		if (p) wantedrpcsvcs = malcop(p+1);

		if ((color == COL_GREEN) && t->banner && wantedrpcsvcs) {
			char *rpcsvc, *aline;

			rpcsvc = strtok(wantedrpcsvcs, ",");
			while (rpcsvc) {
				struct rpcent *rpcinfo;
				int  svcfound = 0;
				int  aprogram;
				int  aversion;
				char aprotocol[10];
				int  aport;

				rpcinfo = getrpcbyname(rpcsvc);
				aline = t->banner; 
				while ((!svcfound) && rpcinfo && aline && (*aline != '\0')) {
					p = strchr(aline, '\n');
					if (p) *p = '\0';

					if (sscanf(aline, "%d %d %s %d", &aprogram, &aversion, aprotocol, &aport) == 4) {
						svcfound = (aprogram == rpcinfo->r_number);
					}

					aline = p;
					if (p) {
						*p = '\n';
						aline++;
					}
				}

				if (svcfound) {
					sprintf(msgline, "&%s Service %s (ID: %d) found on port %d\n", 
						colorname(COL_GREEN), rpcsvc, rpcinfo->r_number, aport);
				}
				else if (rpcinfo) {
					color = COL_RED;
					sprintf(msgline, "&%s Service %s (ID: %d) NOT found\n", 
						colorname(COL_RED), rpcsvc, rpcinfo->r_number);
				}
				else {
					color = COL_RED;
					sprintf(msgline, "&%s Unknown RPC service %s\n",
						colorname(COL_RED), rpcsvc);
				}
				strcat(msgbuf, msgline);

				rpcsvc = strtok(NULL, ",");
			}
		}

		if (wantedrpcsvcs) free(wantedrpcsvcs);

		init_status(color);
		sprintf(msgline, "status %s.%s %s %s %s %s, %s\n\n", 
			commafy(t->host->hostname), service->testname, colorname(color), timestamp, 
			service->testname, 
			( ((color == COL_RED) || (color == COL_YELLOW)) ? "NOT ok" : "ok"),
			causetext);
		addtostatus(msgline);

		/* The summary of wanted RPC services */
		addtostatus(msgbuf);

		/* rpcinfo output */
		if (t->open) {
			if (t->banner) {
				addtostatus("\n\n");
				addtostatus(t->banner);
			}
			else {
				sprintf(msgline, "\n\nNo output from rpcinfo -p %s\n", t->host->ip);
				addtostatus(msgline);
			}
		}
		else {
			addtostatus("\n\nCould not connect to the portmapper service\n");
			if (t->banner) addtostatus(t->banner);
		}
		finish_status();
	}

	free(msgbuf);
}


void send_sslcert_status(testedhost_t *host)
{
	int color = -1;
	service_t *s;
	testitem_t *t;
	char msgline[1024];
	char *sslmsg;
	int sslmsgsize;
	time_t now = time(NULL);
	char *certowner;

	sslmsgsize = 4096;
	sslmsg = (char *)malloc(sslmsgsize);
	*sslmsg = '\0';

	for (s=svchead; (s); s = s->next) {
		certowner = s->testname;

		for (t=s->items; (t); t=t->next) {
			if ((t->host == host) && t->certinfo && (t->certexpires > 0)) {
				int sslcolor = COL_GREEN;

				if (s == httptest) certowner = ((http_data_t *)t->privdata)->url;
				else if (s == ldaptest) certowner = t->testspec;

				if (t->certexpires < (now+host->sslwarndays*86400)) sslcolor = COL_YELLOW;
				if (t->certexpires < (now+host->sslalarmdays*86400)) sslcolor = COL_RED;
				if (sslcolor > color) color = sslcolor;

				if (t->certexpires > now) {
					sprintf(msgline, "\n&%s SSL certificate for %s expires in %u days\n\n", 
						colorname(sslcolor), certowner,
						(unsigned int)((t->certexpires - now) / 86400));
				}
				else {
					sprintf(msgline, "\n&%s SSL certificate for %s expired %u days ago\n\n", 
						colorname(sslcolor), certowner,
						(unsigned int)((now - t->certexpires) / 86400));
				}

				if ((strlen(msgline)+strlen(sslmsg) + strlen(t->certinfo)) > sslmsgsize) {
					sslmsgsize += (4096 + strlen(t->certinfo) + strlen(msgline));
					sslmsg = (char *)realloc(sslmsg, sslmsgsize);
				}
				strcat(sslmsg, msgline);
				strcat(sslmsg, t->certinfo);
			}
		}
	}

	if (color != -1) {
		/* Send off the sslcert status report */
		init_status(color);
		sprintf(msgline, "status %s.%s %s %s\n", 
			commafy(host->hostname), ssltestname, colorname(color), timestamp);
		addtostatus(msgline);
		addtostatus(sslmsg);
		addtostatus("\n\n");
		finish_status();
	}

	free(sslmsg);
}

int main(int argc, char *argv[])
{
	service_t *s;
	testedhost_t *h;
	testitem_t *t;
	int argi;
	int concurrency = 0;
	char *pingcolumn = "conn";
	char *egocolumn = NULL;
	int failgoesclear = 0;		/* IPTEST_2_CLEAR_ON_FAILED_CONN */

	/* Setup SEGV handler */
	setup_signalhandler("bbtest");

	if (init_http_library() != 0) {
		errprintf("Failed to initialize http library\n");
		return 1;
	}

	if (init_ldap_library() != 0) {
		errprintf("Failed to initialize ldap library\n");
		return 1;
	}

	if (getenv("CONNTEST") && (strcmp(getenv("CONNTEST"), "FALSE") == 0)) pingcolumn = NULL;

	for (argi=1; (argi < argc); argi++) {
		if      (argnmatch(argv[argi], "--timeout=")) {
			char *p = strchr(argv[argi], '=');
			p++; timeout = atoi(p);
		}
		else if (argnmatch(argv[argi], "--dns=")) {
			char *p = strchr(argv[argi], '=');
			p++;
			if (strcmp(p, "only") == 0)      dnsmethod = DNS_ONLY;
			else if (strcmp(p, "ip") == 0)   dnsmethod = IP_ONLY;
			else                             dnsmethod = DNS_THEN_IP;
		}
		else if (strcmp(argv[argi], "--test-untagged") == 0) {
			testuntagged = 1;
		}

		/* Debugging options */
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[argi], "--no-update") == 0) {
			dontsendmessages = 1;
		}
		else if (strcmp(argv[argi], "--timing") == 0) {
			timing = 1;
		}
		else if (argnmatch(argv[argi], "--report=") || (strcmp(argv[argi], "--report") == 0)) {
			char *p = strchr(argv[argi], '=');
			if (p) {
				egocolumn = malcop(p+1);
			}
			else egocolumn = "bbtest";
			timing = 1;
		}
		else if (argnmatch(argv[argi], "--frequenttestlimit=")) {
			char *p = strchr(argv[argi], '=');
			p++; frequenttestlimit = atoi(p);
		}

		/* Options for TCP tests */
		else if (argnmatch(argv[argi], "--concurrency=")) {
			char *p = strchr(argv[argi], '=');
			p++; concurrency = atoi(p);
		}
		else if (strcmp(argv[argi], "--checkresponse") == 0) {
			checktcpresponse = 1;
		}

		/* Options for PING tests */
		else if (argnmatch(argv[argi], "--ping")) {
			char *p = strchr(argv[argi], '=');
			if (p) {
				p++; pingcolumn = p;
			}
			else pingcolumn = "conn";
		}
		else if (strcmp(argv[argi], "--noping") == 0) {
			pingcolumn = NULL;
		}

		/* Options for HTTP tests */
		else if (argnmatch(argv[argi], "--conntimeout=")) {
			char *p = strchr(argv[argi], '=');
			p++; conntimeout = atoi(p);
		}
		else if (argnmatch(argv[argi], "--content=")) {
			char *p = strchr(argv[argi], '=');
			contenttestname = malcop(p+1);
		}
		else if (argnmatch(argv[argi], "--ssl=")) {
			char *p = strchr(argv[argi], '=');
			ssltestname = malcop(p+1);
		}
		else if (strcmp(argv[argi], "--no-ssl") == 0) {
			ssltestname = NULL;
		}
		else if (argnmatch(argv[argi], "--sslwarn=")) {
			char *p = strchr(argv[argi], '=');
			p++; sslwarndays = atoi(p);
		}
		else if (argnmatch(argv[argi], "--sslalarm=")) {
			char *p = strchr(argv[argi], '=');
			p++; sslalarmdays = atoi(p);
		}
		else if (argnmatch(argv[argi], "--follow=") || (strcmp(argv[argi], "--follow") == 0)) {
			char *p = strchr(argv[argi], '=');

			if (p) followlocations = atoi(p+1);
			else followlocations = 3;
		}
		else if (argnmatch(argv[argi], "--log=")) {
			char *p = strchr(argv[argi], '=');

			logfile = malcop(p+1);
		}

		/* Informational options */
		else if (strcmp(argv[argi], "--version") == 0) {
			printf("bbtest-net version %s\n", VERSION);
			if (http_library_version) printf("HTTP library: %s\n", http_library_version);
			if (ldap_library_version) printf("LDAP library: %s\n", ldap_library_version);
			return 0;
		}
		else if ((strcmp(argv[argi], "--help") == 0) || (strcmp(argv[argi], "-?") == 0)) {
			printf("bbtest-net version %s\n\n", VERSION);
			printf("Usage: %s [options] [host1 host2 host3 ...]\n", argv[0]);
			printf("Options:\n");
			printf("    --timeout=N                 : Timeout (in seconds) for service tests\n");
			printf("    --dns=[only|ip|standard]    : How IP's are decided\n");
			printf("    --test-untagged             : Include hosts without a NET: tag in the test\n");
			printf("    --report[=COLUMNNAME]       : Send a status report about the running of bbtest-net\n");
			printf("    --frequenttestlimit=N       : Seconds after detecting failures in which we poll frequently\n");
			printf("\nOptions for services in BBNETSVCS (tcp tests):\n");
			printf("    --concurrency=N             : Number of tests run in parallel\n");
			printf("    --checkresponse             : Check response from known services\n");
			printf("\nOptions for PING (connectivity) tests:\n");
			printf("    --ping[=COLUMNNAME]         : Enable ping checking, default columname is \"conn\"\n");
			printf("    --noping                    : Disable ping checking\n");
			printf("\nOptions for HTTP/HTTPS (Web) tests:\n");
			printf("    --conntimeout=N             : Timeout for the connection to the server to succeed\n");
			printf("    --content=COLUMNNAME        : Define columnname for CONTENT checks (content)\n");
			printf("    --ssl=COLUMNNAME            : Define columnname for SSL certificate checks (sslcert)\n");
			printf("    --sslwarn=N                 : Go yellow if certificate expires in less than N days (default:30\n");
			printf("    --sslalarm=N                : Go red if certificate expires in less than N days (default:10\n");
			printf("    --no-ssl                    : Disable SSL certificate check\n");
			printf("    --follow[=N]                : Follow redirects for N levels (default: N=3).\n");
			printf("\nDebugging options:\n");
			printf("    --debug                     : Output debugging information\n");
			printf("    --no-update                 : Send status messages to stdout instead of to bbd\n");
			printf("    --log=FILENAME              : Output trace of HTTP tests to a file.\n");
			printf("    --timing                    : Trace the amount of time spent on each series of tests\n");

			return 0;
		}
		else if (strncmp(argv[argi], "-", 1) == 0) {
			errprintf("Unknown option %s - try --help\n", argv[argi]);
		}
		else {
			/* Must be a hostname */
			if (selectedcount == 0) selectedhosts = (char **) malloc(argc*sizeof(char *));
			selectedhosts[selectedcount++] = malcop(argv[argi]);
		}
	}

	/* In case they changed the name of our column ... */
	if (egocolumn) setup_signalhandler(egocolumn);

	init_timestamp();
	envcheck(reqenv);
	fqdn = get_fqdn();
	if (getenv("BBLOCATION")) location = malcop(getenv("BBLOCATION"));
	if (pingcolumn && getenv("IPTEST_2_CLEAR_ON_FAILED_CONN")) {
		failgoesclear = (strcmp(getenv("IPTEST_2_CLEAR_ON_FAILED_CONN"), "TRUE") == 0);
	}

	add_timestamp("bbtest-net startup");

	load_services();

	/* bbd uses 1984 - may not be in /etc/services */
	add_service("bbd", (getportnumber("bbd") ? getportnumber("bbd") : 1984), 0, TOOL_CONTEST);

	add_service("dns", getportnumber("domain"), 0, TOOL_NSLOOKUP);
	add_service("dig", getportnumber("domain"), 0, TOOL_DIG);
	add_service("ntp", getportnumber("ntp"),    0, TOOL_NTP);
	rpctest  = add_service("rpc", getportnumber("sunrpc"), 0, TOOL_RPCINFO);
	httptest = add_service("http", getportnumber("http"),  0, TOOL_CURL);
	ldaptest = add_service("ldap", getportnumber("ldap"),  0, TOOL_LDAP);
	if (pingcolumn) pingtest = add_service(pingcolumn, 0, 0, TOOL_FPING);
	modembanktest = add_service("dialup", 0, 0, TOOL_MODEMBANK);
	add_timestamp("Service definitions loaded");

	load_tests();
	add_timestamp("Test definitions loaded");

	dns_resolve();
	add_timestamp("DNS lookups completed");

	/* Ping checks first */
	if (pingtest && pingtest->items) {
		run_fping_service(pingtest); 
		add_timestamp("PING test completed");
		combo_start();
		send_results(pingtest, failgoesclear);
		if (selectedhosts == 0) save_fping_status();
		combo_end();
	}


	/* Load current status files */
	for (s = svchead; (s); s = s->next) { if (s != pingtest) load_test_status(s); }

	/* First run the standard TCP/IP tests */
	for (s = svchead; (s); s = s->next) {
		if ((s->items) && (s->toolid == TOOL_CONTEST)) {
			for (t = s->items; (t); t = t->next) {
				if (!t->host->dnserror) {
					t->privdata = (void *)add_tcp_test(t->host->ip, s->portnum, s->testname, t->silenttest);
				}
			}
		}
	}
	add_timestamp("TCP test engine setup completed");

	do_tcp_tests(timeout, concurrency);
	add_timestamp("TCP tests executed");

	if (debug) show_tcp_test_results();
	for (s = svchead; (s); s = s->next) {
		if ((s->items) && (s->toolid == TOOL_CONTEST)) {
			for (t = s->items; (t); t = t->next) { 
				/*
				 * If the test fails due to DNS error, t->privdata is NULL
				 */
				if (t->privdata) {
					test_t *testresult = (test_t *)t->privdata;

					t->open = testresult->open;
					t->banner = testresult->banner;
					t->certinfo = testresult->certinfo;
					t->certexpires = testresult->certexpires;
					t->duration.tv_sec = testresult->duration.tv_sec;
					t->duration.tv_usec = testresult->duration.tv_usec;
				}
			}
		}
	}
	add_timestamp("TCP tests result collection completed");

	/* Run the http tests */
	for (t = httptest->items; (t); t = t->next) add_http_test(t);
	add_timestamp("HTTP test engine setup completed");

	run_http_tests(httptest, followlocations, logfile, (ssltestname != NULL));
	add_timestamp("HTTP tests executed");

	if (debug) show_http_test_results(httptest);
	for (t = httptest->items; (t); t = t->next) {
		if (t->privdata) {
			http_data_t *testresult = (http_data_t *)t->privdata;

			t->certinfo = testresult->certinfo;
			t->certexpires = testresult->certexpires;
		}
	}
	add_timestamp("HTTP tests result collection completed");

	/* Run the ldap tests */
	for (t = ldaptest->items; (t); t = t->next) add_ldap_test(t);
	add_timestamp("LDAP test engine setup completed");

	run_ldap_tests(ldaptest, (ssltestname != NULL));
	add_timestamp("LDAP tests executed");

	if (debug) show_ldap_test_results(ldaptest);
	for (t = ldaptest->items; (t); t = t->next) {
		if (t->privdata) {
			ldap_data_t *testresult = (ldap_data_t *)t->privdata;

			t->certinfo = testresult->certinfo;
			t->certexpires = testresult->certexpires;
		}
	}
	add_timestamp("LDAP tests result collection completed");


	/* dns, dig, ntp tests */
	for (s = svchead; (s); s = s->next) {
		if (s->items) {
			switch(s->toolid) {
				case TOOL_NSLOOKUP:
					run_nslookup_service(s); 
					add_timestamp("NSLOOKUP tests executed");
					break;
				case TOOL_DIG:
					run_dig_service(s); 
					add_timestamp("DIG tests executed");
					break;
				case TOOL_NTP:
					run_ntp_service(s); 
					add_timestamp("NTP tests executed");
					break;
				case TOOL_RPCINFO:
					run_rpcinfo_service(s); 
					add_timestamp("RPC tests executed");
					break;
				case TOOL_MODEMBANK:
					run_modembank_service(s); 
					add_timestamp("Modembank tests executed");
					break;
			}
		}
	}

	combo_start();
	for (s = svchead; (s); s = s->next) {
		switch (s->toolid) {
			case TOOL_CONTEST:
			case TOOL_NSLOOKUP:
			case TOOL_DIG:
			case TOOL_NTP:
				send_results(s, failgoesclear);
				break;

			case TOOL_FPING:
			case TOOL_CURL:
			case TOOL_LDAP:
				/* These handle result-transmission internally */
				break;

			case TOOL_MODEMBANK:
				send_modembank_results(s);
				break;

			case TOOL_RPCINFO:
				send_rpcinfo_results(s, failgoesclear);
				break;
		}
	}
	for (h=testhosthead; (h); h = h->next) {
		send_http_results(httptest, h, nonetpage, contenttestname, failgoesclear);
		send_ldap_results(ldaptest, h, nonetpage, failgoesclear);
		if (ssltestname && !h->nosslcert) send_sslcert_status(h);
	}

	combo_end();
	add_timestamp("Test results transmitted");

	/*
	 * The list of hosts to test frequently because of a failure must
	 * be saved - it is then picked up by the frequent-test ext script
	 * that runs bbtest-net again with the frequent-test hosts as
	 * parameter.
	 *
	 * Should the retest itself update the frequent-test file ? It
	 * would allow us to kick hosts from the frequent-test file sooner.
	 * However, it is simpler (no races) if we just let the normal
	 * test-engine be alone in updating the file. 
	 * At the worst, we'll re-test a host going up a couple of times
	 * too much.
	 *
	 * So for now update the list only if we ran with no host-parameters.
	 */
	if (selectedcount == 0) {
		/* Save current status files */
		for (s = svchead; (s); s = s->next) { if (s != pingtest) save_test_status(s); }
		/* Save frequent-test list */
		save_frequenttestlist(argc, argv);
	}

	shutdown_ldap_library();
	shutdown_http_library();
	add_timestamp("bbtest-net completed");

	/* Tell about us */
	if (egocolumn) {
		char msgline[MAXMSG];
		char *timestamps;
		long bbsleep = (getenv("BBSLEEP") ? atol(getenv("BBSLEEP")) : 300);
		int color;

		/* Go yellow if it runs for too long */
		if (total_runtime() > bbsleep) {
			errprintf("WARNING: Runtime %ld longer than BBSLEEP (%ld)\n", total_runtime(), bbsleep);
		}
		color = (errbuf ? COL_YELLOW : COL_GREEN);

		combo_start();
		init_status(color);
		sprintf(msgline, "status %s.%s %s %s\n\n", getenv("MACHINE"), egocolumn, colorname(color), timestamp);
		addtostatus(msgline);

		sprintf(msgline, "bbtest-net version %s\n", VERSION);
		addtostatus(msgline);
		if (http_library_version) {
			sprintf(msgline, "HTTP library: %s\n", http_library_version);
			addtostatus(msgline);
		}
		if (ldap_library_version) {
			sprintf(msgline, "LDAP library: %s\n", ldap_library_version);
			addtostatus(msgline);
		}

		sprintf(msgline, "\nStatistics:\n Hosts total         : %5d\n Hosts with no tests : %5d\n Total test count    : %5d\n Status messages     : %5d\n Alert status msgs   : %5d\n Transmissions       : %5d\n", 
			hostcount, notesthostcount, testcount, bbstatuscount, bbnocombocount, bbmsgcount);
		addtostatus(msgline);

		if (errbuf) {
			addtostatus("\n\nError output:\n");
			addtostatus(errbuf);
		}

		show_timestamps(&timestamps);
		addtostatus(timestamps);

		finish_status();
		combo_end();
	}
	else show_timestamps(NULL);

	return 0;
}

