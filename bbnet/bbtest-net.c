/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbtest-net.c,v 1.54 2003-05-23 11:53:55 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/wait.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "bbtest-net.h"
#include "contest.h"
#include "httptest.h"

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
	NULL
};

/* toolid values */
#define TOOL_CONTEST	0
#define TOOL_NSLOOKUP	1
#define TOOL_DIG	2
#define TOOL_NTP        3
#define TOOL_FPING      4
#define TOOL_CURL       5

/* dnslookup values */
#define DNS_THEN_IP     0	/* Try DNS - if it fails, use IP from bb-hosts */
#define DNS_ONLY        1	/* DNS only - if it fails, report service down */
#define IP_ONLY         2	/* IP only - dont do DNS lookups */

service_t	*svchead = NULL;		/* Head of all known services */
service_t	*pingtest = NULL;		/* Identifies the pingtest within svchead list */
service_t	*httptest = NULL;		/* Identifies the httptest within svchead list */
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

	svc = malloc(sizeof(service_t));
	svc->portnum = port;
	svc->testname = malloc(strlen(name)+1);
	strcpy(svc->testname, name);
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

	netsvcs = malloc(strlen(getenv("BBNETSVCS"))+1);
	strcpy(netsvcs, getenv("BBNETSVCS"));

	p = strtok(netsvcs, " ");
	while (p) {
		add_service(p, getportnumber(p), 0, TOOL_CONTEST);
		p = strtok(NULL, " ");
	}
	free(netsvcs);

	/* Save NONETPAGE env. var in ",test1,test2," format for easy and safe grepping */
	nonetpage = malloc(strlen(getenv("NONETPAGE"))+3);
	sprintf(nonetpage, ",%s,", getenv("NONETPAGE"));
	for (p=nonetpage; (*p); p++) if (*p == ' ') *p = ',';
}


testedhost_t *init_testedhost(char *hostname, int timeout, int conntimeout, int in_sla)
{
	testedhost_t *newhost;

	hostcount++;
	newhost = malloc(sizeof(testedhost_t));
	newhost->hostname = malcop(hostname);
	newhost->ip[0] = '\0';
	newhost->conntimeout = conntimeout;
	newhost->timeout = timeout;

	newhost->dialup = 0;
	newhost->testip = 0;
	newhost->dnserror = 0;
	newhost->in_sla = in_sla;

	newhost->noconn = 0;
	newhost->noping = 0;
	newhost->badconn[0] = newhost->badconn[1] = newhost->badconn[2] = 0;
	newhost->downcount = 0;
	newhost->downstart = 0;
	newhost->routerdeps = NULL;
	newhost->deprouterdown = NULL;

	newhost->firsthttp = NULL;

	newhost->deptests = NULL;

	newhost->next = NULL;

	return newhost;
}

testitem_t *init_testitem(testedhost_t *host, service_t *service, char *testspec, 
                          int dialuptest, int reversetest, int alwaystruetest, int silenttest)
{
	testitem_t *newtest;

	testcount++;
	newtest = malloc(sizeof(testitem_t));
	newtest->host = host;
	newtest->service = service;
	newtest->dialup = dialuptest;
	newtest->reverse = reversetest;
	newtest->alwaystrue = alwaystruetest;
	newtest->silenttest = silenttest;
	newtest->testspec = testspec;
	newtest->private = NULL;
	newtest->open = 0;
	newtest->testresult = NULL;
	newtest->banner = NULL;
	newtest->next = NULL;

	return newtest;
}


void load_tests(void)
{
	FILE 	*bbhosts;
	char 	l[MAX_LINE_LEN];	/* With multiple http tests, we may have long lines */
	char	hostname[MAX_LINE_LEN];
	int	ip1, ip2, ip3, ip4;
	char	*netstring;
	char 	*p;

	bbhosts = stackfopen(getenv("BBHOSTS"), "r");
	if (bbhosts == NULL) {
		errprintf("No bb-hosts file found");
		return;
	}

	/* Each network test tagged with NET:locationname */
	p = getenv("BBLOCATION");
	if (p) {
		netstring = malloc(strlen(p)+5);
		sprintf(netstring, "NET:%s", p);
	}
	else {
		netstring = NULL;
	}

	while (stackfgets(l, sizeof(l), "include")) {
		p = strchr(l, '\n'); if (p) { *p = '\0'; };
		for (p=l; (*p && isspace((int) *p)); p++) ;

		if ((*p == '#') || (*p == '\0')) {
			/* Do nothing - it's a comment or empty line */
		}
		else if (sscanf(l, "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {
			char *testspec;

			if (((netstring == NULL) || (strstr(l, netstring) != NULL)) && strchr(l, '#')) {

				testedhost_t *h;
				testitem_t *newtest;
				int anytests = 0;
				int ping_dialuptest = 0;
				int ping_reversetest = 0;

				p = strchr(l, '#'); p++;
				while (isspace((unsigned int) *p)) p++;

				h = init_testedhost(hostname, timeout, conntimeout, within_sla(p));
				anytests = 0;

				testspec = strtok(p, "\t ");
				while (testspec) {

					service_t *s = NULL;
					int dialuptest = 0;
					int reversetest = 0;
					int alwaystruetest = 0;
					int specialtag = 0;
					char *savedspec = NULL;

					if ((strncmp(testspec, "badconn", 7) == 0) && periodcoversnow(testspec+7)) {
						char *p =strchr(testspec, ':');

						specialtag = 1;
						if (p) sscanf(p, ":%d:%d:%d", &h->badconn[0], &h->badconn[1], &h->badconn[2]);
					}
					else if (strncmp(testspec, "route:", 6) == 0) {
						specialtag = 1;
						h->routerdeps = malcop(testspec+6);
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
					else if (strncmp(testspec, "depends=", 8) == 0) {
						specialtag = 1;
						h->deptests = malcop(testspec+8);
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
							int specialport;
							char *specialname;

							specialport = atoi(option);
							if ((s->portnum == 0) && (specialport > 0)) {
								specialname = malloc(strlen(s->testname)+10);
								sprintf(specialname, "%s_%d", s->testname, specialport);
								s = add_service(specialname, specialport, strlen(s->testname), TOOL_CONTEST);
								free(specialname);
							}
						}

						if (option) *(option-1) = ':';
					}

					if (s) {
						anytests = 1;
						newtest = init_testitem(h, s, savedspec, dialuptest, reversetest, alwaystruetest, 0);
						newtest->next = s->items;
						s->items = newtest;

						if (s == httptest) h->firsthttp = newtest;
					}

					testspec = strtok(NULL, "\t ");
				}

				if (pingtest && !h->noconn) {
					/* Add the ping check */
					anytests = 1;
					newtest = init_testitem(h, pingtest, NULL, ping_dialuptest, ping_reversetest, 1, 0);
					newtest->next = pingtest->items;
					pingtest->items = newtest;
				}

				if (anytests) {
					sprintf(h->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
					h->next = testhosthead;
					testhosthead = h;
				}
				else {
					/* No network tests for this host, so ignore it */
					free(h);
					notesthostcount++;
				}
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
		else {
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
		if (sscanf(l, "%s %d %lu", host, &downcount, &downstart) == 3) {
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

	sprintf(statusfn, "%s/fping.%s.status", getenv("BBTMP"), location);
	statusfd = fopen(statusfn, "w");
	if (statusfd == NULL) return;

	for (t=pingtest->items; (t); t = t->next) {
		if (!t->open) {
			t->host->downcount++;
			if (t->host->downcount == 1) t->host->downstart = time(NULL);
			fprintf(statusfd, "%s %d %lu\n", t->host->hostname, t->host->downcount, t->host->downstart);
		}
		else t->host->downcount = 0;
	}

	fclose(statusfd);
}


int run_command(char *cmd, char *errortext, char **banner)
{
	FILE	*cmdpipe;
	char	l[1024];
	int	result;
	int	piperes;

	result = 0;
	if (banner) { *banner = malloc(1024); sprintf(*banner, "Command: %s\n\n", cmd); }
	cmdpipe = popen(cmd, "r");
	if (cmdpipe == NULL) {
		errprintf("Could not open pipe for command %s\n", cmd);
		if (banner) strcat(*banner, "popen() failed to run command\n");
		return -1;
	}

	while (fgets(l, sizeof(l), cmdpipe)) {
		if (banner && ((strlen(l) + strlen(*banner)) < 1024)) strcat(*banner, l);
		if (strstr(l, errortext) != NULL) result = 1;
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
					t->banner = malloc(strlen(l)+1);
					strcpy(t->banner, l);
				}
			}
		}
	}
	fclose(logfd);
	if (!debug) unlink(logfn);

	save_fping_status();

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


void send_results(service_t *service)
{
	testitem_t	*t;
	int		color;
	char		msgline[MAXMSG];
	char		*svcname;
	char		*nopagename;
	int		nopage = 0;

	svcname = malloc(strlen(service->testname)+1);
	strcpy(svcname, service->testname);
	if (service->namelen) svcname[service->namelen] = '\0';

	/* Check if this service is a NOPAGENET service. */
	nopagename = malloc(strlen(svcname)+3);
	sprintf(nopagename, ",%s,", svcname);
	nopage = (strstr(nonetpage, svcname) != NULL);
	free(nopagename);

	for (t=service->items; (t); t = t->next) {
		if ((service == pingtest) && t->host->noping) {
			color = COL_CLEAR;
		}
		else {
			color = COL_GREEN;

			/*
			 * If DNS error, it is red.
			 * If not, then either (open=0,reverse=0) or (open=1,reverse=1) is wrong.
			 */
			if ((t->host->dnserror) || ((t->open + t->reverse) != 1)) color = COL_RED;

			/* Dialup hosts and dialup tests report red as clear */
			if ((color != COL_GREEN) && (t->host->dialup || t->dialup)) color = COL_CLEAR;

			/* If not inside SLA and non-green, report as BLUE */
			if (!t->host->in_sla && (color != COL_GREEN)) color = COL_BLUE;
		}

		/* Handle the "route" tag dependencies. */
		if ((service == pingtest) && (color == COL_RED) && t->host->deprouterdown) {
			color = COL_YELLOW;
		}

		/* Handle the "badconn" stuff for ping checks */
		if ((service == pingtest) && (color == COL_RED) && (t->host->downcount < t->host->badconn[2])) {
			if      (t->host->downcount >= t->host->badconn[1]) color = COL_YELLOW;
			else if (t->host->downcount >= t->host->badconn[0]) color = COL_CLEAR;
			else                                                color = COL_GREEN;

		}

		/* If host ping fails, report failed TCP tests as clear unless "alwaystrue" flag set */
		if ( ((color == COL_RED) || (color == COL_YELLOW)) && /* Test failed */
		     (service != pingtest)                         && /* It's not a ping test */
		     (!t->host->noping && !t->host->noconn)        && /* We are doing a ping test */
		     (t->host->downcount > 0)                      && /* The ping check did fail */
		     (!t->alwaystrue)                              )  /* No "~testname" flag */ {
			color = COL_CLEAR;
		}

		/* Handle test dependencies */
		if ( ((color == COL_RED) || (color == COL_YELLOW)) && /* Test failed */
		     deptest_failed(t->host, t->service->testname) &&
		     (!t->alwaystrue)                              )  /* No "~testname" flag */ {
			color = COL_CLEAR;
		}

		/* NOPAGENET services that are down are reported as yellow */
		if (nopage && (color == COL_RED)) color = COL_YELLOW;

		init_status(color);
		sprintf(msgline, "status %s.%s %s %s %s %s\n", 
			commafy(t->host->hostname), svcname, colorname(color), timestamp,
			svcname, ( ((color == COL_RED) || (color == COL_YELLOW)) ? "NOT ok" : "ok"));
		addtostatus(msgline);

		if (t->host->dnserror) {
			sprintf(msgline, "\nUnable to resolve hostname %s\n\n", t->host->hostname);
		}
		else {
			sprintf(msgline, "\nService %s on %s is ", svcname, t->host->hostname);
			switch (color) {
			  case COL_GREEN: 
				  strcat(msgline, "OK ");
				  strcat(msgline, (t->reverse ? "(down)" : "(up)"));
				  strcat(msgline, "\n");
				  break;

			  case COL_RED:
			  case COL_YELLOW:
				  if ((service == pingtest) && t->host->deprouterdown) {
					strcat(msgline, "not OK.\n");
					strcat(msgline, "The gateway ");
					strcat(msgline, ((testedhost_t *)t->host->deprouterdown)->hostname);
					strcat(msgline, " (IP:");
					strcat(msgline, ((testedhost_t *)t->host->deprouterdown)->ip);
					strcat(msgline, ") ");
					strcat(msgline, "is not reachable, causing this host to be unreachable.");
					strcat(msgline, "\n");
				  }
				  else {
				  	strcat(msgline, "not OK ");
				  	strcat(msgline, (t->reverse ? "(up)" : "(down)"));
				  	strcat(msgline, "\n");
				  }
				  break;

			  case COL_CLEAR:
				  strcat(msgline, "OK\n");
				  if (service == pingtest) {
					  if (t->host->deprouterdown) {
						  strcat(msgline, "The gateway ");
						  strcat(msgline, ((testedhost_t *)t->host->deprouterdown)->hostname);
						  strcat(msgline, " (IP:");
						  strcat(msgline, ((testedhost_t *)t->host->deprouterdown)->ip);
						  strcat(msgline, ") ");
						  strcat(msgline, "is not reachable, causing this host to be unreachable.");
						  strcat(msgline, "\n");
					  }
					  else if (t->host->noping) {
						  strcat(msgline, "Ping check disabled (noping)\n");
					  }
					  else if (t->host->dialup) {
						  strcat(msgline, "Dialup host\n");
					  }
					  /* "clear" due to badconn: no extra text */
				  }
				  else {
					  /* Non-ping test clear: Dialup test or failed ping */
					  strcat(msgline, "Dialup host/service, or test depends on another failed test\n");
				  }
				  break;

			  case COL_BLUE:
				  strcat(msgline, "OK\n");
				  strcat(msgline, "Host currently not monitored due to SLA setting.\n");
				  break;
			}
			strcat(msgline, "\n");
		}
		addtostatus(msgline);

		if ((service == pingtest) && t->host->downcount) {
			sprintf(msgline, "\n<p>System unreachable for %d poll periods (%lu seconds)\n</p>",
				t->host->downcount, (time(NULL) - t->host->downstart));
			addtostatus(msgline);
		}

		if (t->banner) {
			addtostatus("\n"); addtostatus(t->banner); addtostatus("\n");
		}
		if (t->testresult) {
			sprintf(msgline, "\nSeconds: %ld.%03ld\n", 
				t->testresult->duration.tv_sec, t->testresult->duration.tv_usec / 1000);
			addtostatus(msgline);
		}
		addtostatus("\n\n");
		finish_status();
	}
}

int main(int argc, char *argv[])
{
	service_t *s;
	testedhost_t *h;
	testitem_t *t;
	int argi;
	int concurrency = 0;
	char *pingcolumn = NULL;
	char *egocolumn = NULL;

	if (init_http_library() != 0) {
		errprintf("Failed to initialize http library\n");
		return 1;
	}

	for (argi=1; (argi < argc); argi++) {
		if      (strncmp(argv[argi], "--timeout=", 10) == 0) {
			char *p = strchr(argv[argi], '=');
			p++; timeout = atoi(p);
		}
		else if (strncmp(argv[argi], "--dns=", 6) == 0) {
			char *p = strchr(argv[argi], '=');
			p++;
			if (strcmp(p, "only") == 0)      dnsmethod = DNS_ONLY;
			else if (strcmp(p, "ip") == 0)   dnsmethod = IP_ONLY;
			else                             dnsmethod = DNS_THEN_IP;
		}

		/* Debugging options */
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[argi], "--timing") == 0) {
			timing = 1;
		}
		else if (strcmp(argv[argi], "--report") == 0) {
			char *p = strchr(argv[argi], '=');
			if (p) {
				egocolumn = malcop(p+1);
			}
			else egocolumn = "bbtest";
			timing = 1;
		}

		/* Options for TCP tests */
		else if (strncmp(argv[argi], "--concurrency=", 14) == 0) {
			char *p = strchr(argv[argi], '=');
			p++; concurrency = atoi(p);
		}

		/* Options for PING tests */
		else if (strncmp(argv[argi], "--ping", 6) == 0) {
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
		else if (strncmp(argv[argi], "--conntimeout=", 14) == 0) {
			char *p = strchr(argv[argi], '=');
			p++; conntimeout = atoi(p);
		}
		else if (strncmp(argv[argi], "--content=", 10) == 0) {
			char *p = strchr(argv[argi], '=');
			contenttestname = malcop(p+1);
		}
		else if (strncmp(argv[argi], "--ssl=", 6) == 0) {
			char *p = strchr(argv[argi], '=');
			ssltestname = malcop(p+1);
		}
		else if (strncmp(argv[argi], "--no-ssl", 8) == 0) {
			ssltestname = NULL;
		}
		else if (strncmp(argv[argi], "--sslwarn=", 10) == 0) {
			char *p = strchr(argv[argi], '=');
			p++; sslwarndays = atoi(p);
		}
		else if (strncmp(argv[argi], "--sslalarm=", 11) == 0) {
			char *p = strchr(argv[argi], '=');
			p++; sslalarmdays = atoi(p);
		}
		else if (strcmp(argv[argi], "--follow") == 0) {
			char *p = strchr(argv[argi], '=');

			if (p) followlocations = atoi(p+1);
			else followlocations = 3;
		}
		else if (strncmp(argv[argi], "--log=", 6) == 0) {
			char *p = strchr(argv[argi], '=');

			logfile = malcop(p+1);
		}

		/* Informational options */
		else if (strcmp(argv[argi], "--version") == 0) {
			printf("bbtest-net version %s\n", VERSION);
			printf("HTTP library: %s\n", http_library_version);
			return 0;
		}
		else if ((strcmp(argv[argi], "--help") == 0) || (strcmp(argv[argi], "-?") == 0)) {
			printf("bbtest-net version %s\n\n", VERSION);
			printf("Usage: %s [options]\n", argv[0]);
			printf("Options:\n");
			printf("    --timeout=N                 : Timeout (in seconds) for service tests\n");
			printf("    --dns=[only|ip|standard]    : How IP's are decided\n");
			printf("\nOptions for services in BBNETSVCS (tcp tests):\n");
			printf("    --concurrency=N             : Number of tests run in parallel\n");
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
			printf("    --log=FILENAME              : Output trace of HTTP tests to a file.\n");
			printf("    --timing                    : Trace the amount of time spent on each series of tests\n");
			printf("    --report[=COLUMNNAME]       : Send a status report about the running of bbtest-net\n");

			return 0;
		}
		else {
			errprintf("Unknown option %s - try --help\n", argv[argi]);
		}
	}

	init_timestamp();
	envcheck(reqenv);
	if (getenv("BBLOCATION")) location = malcop(getenv("BBLOCATION"));

	add_timestamp("bbtest-net startup");

	load_services();
	add_service("dns", getportnumber("domain"), 0, TOOL_NSLOOKUP);
	add_service("dig", getportnumber("domain"), 0, TOOL_DIG);
	add_service("ntp", getportnumber("ntp"),    0, TOOL_NTP);
	httptest = add_service("http", getportnumber("http"),  0, TOOL_CURL);
	if (pingcolumn) pingtest = add_service(pingcolumn, 0, 0, TOOL_FPING);
	add_timestamp("Service definitions loaded");

	load_tests();
	add_timestamp("Test definitions loaded");

	dns_resolve();
	add_timestamp("DNS lookups completed");

	/* Ping checks first */
	if (pingtest && pingtest->items) {
		run_fping_service(pingtest); 
		add_timestamp("PING test completed");
	}


	/* First run the standard TCP/IP tests */
	for (s = svchead; (s); s = s->next) {
		if ((s->items) && (s->toolid == TOOL_CONTEST)) {
			for (t = s->items; (t); t = t->next) {
				if (!t->host->dnserror) {
					t->testresult = add_tcp_test(t->host->ip, s->portnum, s->testname, t->silenttest);
				}
				else {
					t->testresult = NULL;
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
				 * If the test fails due to DNS error, t->testresult is NULL
				 */
				if (t->testresult) {
					t->open = t->testresult->open;
					t->banner = t->testresult->banner;
				}
				else {
					t->open = 0;
					t->banner = NULL;
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
	add_timestamp("HTTP tests result collection completed");


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
			}
		}
	}

	combo_start();
	for (s = svchead; (s); s = s->next) {
		switch (s->toolid) {
			case TOOL_FPING:
			case TOOL_CONTEST:
			case TOOL_NSLOOKUP:
			case TOOL_DIG:
			case TOOL_NTP:
				send_results(s);
				break;

			case TOOL_CURL:
				break;
		}
	}
	for (h=testhosthead; (h); h = h->next) {
		send_http_results(httptest, h, nonetpage, contenttestname, ssltestname, sslwarndays, sslalarmdays);
	}

	combo_end();
	add_timestamp("Test results transmitted");

	shutdown_http_library();
	add_timestamp("bbtest-net completed");

	/* Tell about us */
	if (egocolumn) {
		char msgline[MAXMSG];
		char *timestamps;
		int color = (errbuf ? COL_YELLOW : COL_GREEN);

		combo_start();
		init_status(color);
		sprintf(msgline, "status %s.%s %s %s\n\n", getenv("MACHINE"), egocolumn, colorname(color), timestamp);
		addtostatus(msgline);

		sprintf(msgline, "bbtest-net version %s\n", VERSION);
		addtostatus(msgline);
		sprintf(msgline, "HTTP library: %s\n", http_library_version);
		addtostatus(msgline);

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

