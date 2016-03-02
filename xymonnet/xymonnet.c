/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
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
#include <fcntl.h>
#include <errno.h>

#include "libxymon.h"

#ifdef HAVE_RPCENT_H
#include <rpc/rpcent.h>
#endif

#ifdef BROKEN_HPUX_NETDB
/* 
 * Some HP-UX include files fail to define RPC functions 
 * and structs that are purely standard. At the same time,
 * their own docs claim that the DO define them. Go figure ...
 */
struct rpcent {
	char    *r_name;        /* name of server for this rpc program */
	char    **r_aliases;    /* alias list */
	int     r_number;       /* rpc program number */
};
extern struct rpcent *getrpcbyname(char *);
#endif

#include "libxymon.h"
#include "version.h"

#include "xymonnet.h"
#include "dns.h"
#include "contest.h"
#include "httptest.h"
#include "httpresult.h"
#include "httpcookies.h"
#include "ldaptest.h"

#define DEFAULT_PING_CHILD_COUNT 1

char *reqenv[] = {
	"NONETPAGE",
	"HOSTSCFG",
	"XYMONTMP",
	"XYMONHOME",
	NULL
};

void *	svctree;			/* All known services, has service_t records */
service_t	*pingtest = NULL;		/* Identifies the pingtest within svctree list */
int		pingcount = 0;
service_t	*dnstest = NULL;		/* Identifies the dnstest within svctree list */
service_t	*httptest = NULL;		/* Identifies the httptest within svctree list */
service_t	*ldaptest = NULL;		/* Identifies the ldaptest within svctree list */
service_t	*rpctest = NULL;		/* Identifies the rpctest within svctree list */
void *       testhosttree;			/* All tested hosts, has testedhost_t records */
char		*nonetpage = NULL;		/* The "NONETPAGE" env. variable */
int		dnsmethod = DNS_THEN_IP;	/* How to do DNS lookups */
int 		timeout=10;			/* The timeout (seconds) for all TCP-tests */
char		*contenttestname = "content";   /* Name of the content checks column */
char		*ssltestname = "sslcert";       /* Name of the SSL certificate checks column */
char		*failtext = "not OK";
int             sslwarndays = 30;		/* If cert expires in fewer days, SSL cert column = yellow */
int             sslalarmdays = 10;		/* If cert expires in fewer days, SSL cert column = red */
int             mincipherbits = 0;		/* If weakest cipher is weaker than this # of buts, SSL cert column = red */
int		validity = 30;
int		pingchildcount = DEFAULT_PING_CHILD_COUNT;	/* How many ping processes to start */
int		hostcount = 0;
int		testcount = 0;
int		notesthostcount = 0;
char		**selectedhosts;
int		selectedcount = 0;
time_t		frequenttestlimit = 1800;	/* Interval (seconds) when failing hosts are retried frequently */
int		checktcpresponse = 0;
int		dotraceroute = 0;
int		fqdn = 1;
int		dosendflags = 1;
int		dosavecookies = 1;
int		dousecookies = 1;
char		*pingcmd = NULL;
char		pinglog[PATH_MAX];
char		pingerrlog[PATH_MAX];
pid_t		*pingpids;
int		respcheck_color = COL_YELLOW;
httpstatuscolor_t *httpstatusoverrides = NULL;
int		extcmdtimeout = 30;
int		bigfailure = 0;
char		*defaultsourceip = NULL;
int		loadhostsfromxymond = 0;
int		sslminkeysize = 0;

void dump_hostlist(void)
{
	xtreePos_t handle;
	testedhost_t *walk;

	for (handle = xtreeFirst(testhosttree); (handle != xtreeEnd(testhosttree)); handle = xtreeNext(testhosttree, handle)) {
		walk = (testedhost_t *)xtreeData(testhosttree, handle);
		printf("Hostname: %s\n", textornull(walk->hostname));
		printf("\tIP           : %s\n", textornull(walk->ip));
		printf("\tHosttype     : %s\n", textornull(walk->hosttype));

		printf("\tFlags        :");
		if (walk->testip) printf(" testip");
		if (walk->dialup) printf(" dialup");
		if (walk->nosslcert) printf(" nosslcert");
		if (walk->dodns) printf(" dodns");
		if (walk->dnserror) printf(" dnserror");
		if (walk->repeattest) printf(" repeattest");
		if (walk->noconn) printf(" noconn");
		if (walk->noping) printf(" noping");
		if (walk->dotrace) printf(" dotrace");
		printf("\n");

		printf("\tbadconn      : %d:%d:%d\n", walk->badconn[0], walk->badconn[1], walk->badconn[2]);
		printf("\tdowncount    : %d started %s", walk->downcount, ctime(&walk->downstart));
		printf("\trouterdeps   : %s\n", textornull(walk->routerdeps));
		printf("\tdeprouterdown: %s\n", (walk->deprouterdown ? textornull(walk->deprouterdown->hostname) : ""));
		printf("\tdeptests     : %s\n", textornull(walk->deptests));
		printf("\tldapauth     : '%s' '%s'\n", textornull(walk->ldapuser), textornull(walk->ldappasswd));
		printf("\tSSL alerts   : %d:%d\n", walk->sslwarndays, walk->sslalarmdays);
		printf("\n");
	}
}
void dump_testitems(void)
{
	xtreePos_t handle;
	service_t *swalk;
	testitem_t *iwalk;

	for (handle = xtreeFirst(svctree); handle != xtreeEnd(svctree); handle = xtreeNext(svctree, handle)) {
		swalk = (service_t *)xtreeData(svctree, handle);

		printf("Service %s, port %d, toolid %d\n", swalk->testname, swalk->portnum, swalk->toolid);

		for (iwalk = swalk->items; (iwalk); iwalk = iwalk->next) {
			printf("\tHost        : %s\n", textornull(iwalk->host->hostname));
			printf("\ttestspec    : %s\n", textornull(iwalk->testspec));
			printf("\tFlags       :");
			if (iwalk->dialup) printf(" dialup");
			if (iwalk->reverse) printf(" reverse");
			if (iwalk->silenttest) printf(" silenttest");
			if (iwalk->alwaystrue) printf(" alwaystrue");
			printf("\n");
			printf("\tOpen        : %d\n", iwalk->open);
			printf("\tBanner      : %s\n", textornull(STRBUF(iwalk->banner)));
			printf("\tcertinfo    : %s\n", textornull(iwalk->certinfo));
			printf("\tDuration    : %ld.%06ld\n", (long int)iwalk->duration.tv_sec, (long int)iwalk->duration.tv_nsec / 1000);
			printf("\tbadtest     : %d:%d:%d\n", iwalk->badtest[0], iwalk->badtest[1], iwalk->badtest[2]);
			printf("\tdowncount    : %d started %s", iwalk->downcount, ctime(&iwalk->downstart));
			printf("\n");
		}

		printf("\n");
	}
}

testitem_t *find_test(char *hostname, char *testname)
{
	xtreePos_t handle;
	testedhost_t *h;
	service_t *s;
	testitem_t *t;

	handle = xtreeFind(svctree, testname);
	if (handle == xtreeEnd(svctree)) return NULL;
	s = (service_t *)xtreeData(svctree, handle);

	handle = xtreeFind(testhosttree, hostname);
	if (handle == xtreeEnd(testhosttree)) return NULL;
	h = (testedhost_t *)xtreeData(testhosttree, handle);

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

	depcopy = strdup(host->deptests);
	sprintf(depitem, "(%s:", testname);
	p = strstr(depcopy, depitem);
	if (p == NULL) { xfree(depcopy); return NULL; }

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

	xfree(depcopy);
	if (strlen(result)) strcat(result, "\n\n");

	return (strlen(result) ? result : NULL);
}


service_t *add_service(char *name, int port, int namelen, int toolid)
{
	xtreePos_t handle;
	service_t *svc;

	/* Avoid duplicates */
	handle = xtreeFind(svctree, name);
	if (handle != xtreeEnd(svctree)) {
		svc = (service_t *)xtreeData(svctree, handle);
		return svc;
	}

	svc = (service_t *) malloc(sizeof(service_t));
	svc->portnum = port;
	svc->testname = strdup(name); 
	svc->toolid = toolid;
	svc->namelen = namelen;
	svc->items = NULL;
	xtreeAdd(svctree, svc->testname, svc);

	return svc;
}

int getportnumber(char *svcname)
{
	struct servent *svcinfo;
	int result = 0;

	result = default_tcp_port(svcname);
	if (result == 0) {
		svcinfo = getservbyname(svcname, NULL);
		if (svcinfo) result = ntohs(svcinfo->s_port);
	}

	return result;
}

void load_services(void)
{
	char *netsvcs;
	char *p;

	netsvcs = init_tcp_services();

	/* Keep the services db open. Don't close since we use it later in 
	 * SSL handshake checking and URL parsing...
	 */
	setservent(1);
	p = strtok(netsvcs, " ");
	while (p) {
		add_service(p, getportnumber(p), 0, TOOL_CONTEST);
		p = strtok(NULL, " ");
	}
	xfree(netsvcs);

	/* Save NONETPAGE env. var in ",test1,test2," format for easy and safe grepping */
	nonetpage = (char *) malloc(strlen(xgetenv("NONETPAGE"))+3);
	sprintf(nonetpage, ",%s,", xgetenv("NONETPAGE"));
	for (p=nonetpage; (*p); p++) if (*p == ' ') *p = ',';
}


testedhost_t *init_testedhost(char *hostname)
{
	testedhost_t *newhost;

	hostcount++;
	newhost = (testedhost_t *) calloc(1, sizeof(testedhost_t));
	newhost->hostname = strdup(hostname);
	newhost->dotrace = dotraceroute;
	newhost->sslwarndays = sslwarndays;
	newhost->sslalarmdays = sslalarmdays;
	newhost->mincipherbits = mincipherbits;

	return newhost;
}

testitem_t *init_testitem(testedhost_t *host, service_t *service, char *srcip, char *testspec, 
                          int dialuptest, int reversetest, int alwaystruetest, int silenttest,
			  int sendasdata, int sendasclient)
{
	testitem_t *newtest;

	testcount++;
	newtest = (testitem_t *) calloc(1, sizeof(testitem_t));
	newtest->host = host;
	newtest->service = service;
	newtest->dialup = dialuptest;
	newtest->reverse = reversetest;
	newtest->alwaystrue = alwaystruetest;
	newtest->silenttest = silenttest;
	newtest->senddata = sendasdata;
	newtest->sendclient = sendasclient;
	newtest->testspec = (testspec ? strdup(testspec) : NULL);
	if (srcip)
		newtest->srcip = strdup(srcip);
	else if (defaultsourceip)
		newtest->srcip = defaultsourceip;
	else
		newtest->srcip = NULL;
	newtest->privdata = NULL;
	newtest->open = 0;
	newtest->banner = newstrbuffer(0);
	newtest->certinfo = NULL;
	newtest->certissuer = NULL;
	newtest->certexpires = 0;
	newtest->certkeysz = 0;
	newtest->mincipherbits = 0;
	newtest->duration.tv_sec = newtest->duration.tv_nsec = -1;
	newtest->downcount = 0;
	newtest->badtest[0] = newtest->badtest[1] = newtest->badtest[2] = 0;
	newtest->internal = 0;
	newtest->next = NULL;

	return newtest;
}


int wanted_host(void *host)
{
	char *netlocation = xmh_item(host, XMH_NET);

	if (selectedcount == 0)
		return oklocation_host(netlocation);
	else {
		/* User provided an explicit list of hosts to test */
		int i;

		for (i=0; (i < selectedcount); i++) {
			if (strcmp(selectedhosts[i], xmh_item(host, XMH_HOSTNAME)) == 0) return 1;
		}
	}

	return 0;
}


void load_tests(void)
{
	char *p, *routestring = NULL;
	void *hwalk;
	testedhost_t *h;
	int badtagsused = 0;

	if (loadhostsfromxymond) {
		if (load_hostnames("@", NULL, fqdn) != 0) {
			errprintf("Cannot load host configuration from xymond\n");
			return;
		}
	}
	else {
		if (load_hostnames(xgetenv("HOSTSCFG"), "netinclude", fqdn) != 0) {
			errprintf("Cannot load host configuration from %s\n", xgetenv("HOSTSCFG"));
			return;
		}
	}

	if (first_host() == NULL) {
		errprintf("Empty configuration from %s\n", (loadhostsfromxymond ? "xymond" : xgetenv("HOSTSCFG")));
		return;
	}

	/* Each network test tagged with NET:locationname */
	if (location) {
		routestring = (char *) malloc(strlen(location)+strlen("route_:")+1);
		sprintf(routestring, "route_%s:", location);
	}

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		int anytests = 0;
		int ping_dialuptest = 0, ping_reversetest = 0;
		char *testspec;

		if (!wanted_host(hwalk)) continue;
		else dbgprintf(" - adding wanted host\n");

		h = init_testedhost(xmh_item(hwalk, XMH_HOSTNAME));

		p = xmh_custom_item(hwalk, "badconn:");
		if (p) {
			sscanf(p+strlen("badconn:"), "%d:%d:%d", &h->badconn[0], &h->badconn[1], &h->badconn[2]);
			badtagsused = 1;
		}

		p = xmh_custom_item(hwalk, "route:");
		if (p) h->routerdeps = p + strlen("route:");
		if (routestring) {
			p = xmh_custom_item(hwalk, routestring);
			if (p) h->routerdeps = p + strlen(routestring);
		}

		if (xmh_item(hwalk, XMH_FLAG_NOCONN)) h->noconn = 1;
		if (xmh_item(hwalk, XMH_FLAG_NOPING)) h->noping = 1;
		if (xmh_item(hwalk, XMH_FLAG_TRACE)) h->dotrace = 1;
		if (xmh_item(hwalk, XMH_FLAG_NOTRACE)) h->dotrace = 0;
		if (xmh_item(hwalk, XMH_FLAG_TESTIP)) h->testip = 1;
		if (xmh_item(hwalk, XMH_FLAG_DIALUP)) h->dialup = 1;
		if (xmh_item(hwalk, XMH_FLAG_NOSSLCERT)) h->nosslcert = 1;
		if (xmh_item(hwalk, XMH_FLAG_LDAPFAILYELLOW)) h->ldapsearchfailyellow = 1;
		if (xmh_item(hwalk, XMH_FLAG_HIDEHTTP)) h->hidehttp = 1;

		p = xmh_item(hwalk, XMH_SSLDAYS);
		if (p) sscanf(p, "%d:%d", &h->sslwarndays, &h->sslalarmdays);

		p = xmh_item(hwalk, XMH_SSLMINBITS);
		if (p) h->mincipherbits = atoi(p);

		p = xmh_item(hwalk, XMH_DEPENDS);
		if (p) h->deptests = p;

		p = xmh_item(hwalk, XMH_LDAPLOGIN);
		if (p) {
			h->ldapuser = strdup(p);
			h->ldappasswd = (strchr(h->ldapuser, ':'));
			if (h->ldappasswd) {
				*h->ldappasswd = '\0';
				h->ldappasswd++;
			}
		}

		p = xmh_item(hwalk, XMH_DESCRIPTION);
		if (p) {
			h->hosttype = strdup(p);
			p = strchr(h->hosttype, ':');
			if (p) *p = '\0';
		}

		testspec = xmh_item_walk(hwalk);
		while (testspec) {
			service_t *s = NULL;
			int dialuptest = 0, reversetest = 0, silenttest = 0, sendasdata = 0, sendasclient = 0;
			char *srcip = NULL;
			int alwaystruetest = (xmh_item(hwalk, XMH_FLAG_NOCLEAR) != NULL);

			if (xmh_item_idx(testspec) == -1) {

				/* Test prefixes:
				 * - '?' denotes dialup test, i.e. report failures as clear.
				 * - '|' denotes reverse test, i.e. service should be DOWN.
				 * - '~' denotes test that ignores ping result (normally,
				 *       TCP tests are reported CLEAR if ping check fails;
				 *       with this flag report their true status)
				 */
				if (*testspec == '?') { dialuptest=1;     testspec++; }
				if (*testspec == '!') { reversetest=1;    testspec++; }
				if (*testspec == '~') { alwaystruetest=1; testspec++; }

				if (pingtest && argnmatch(testspec, pingtest->testname)) {
					char *p;

					/*
					 * Ping/conn test. Save any modifier flags for later use.
					 */
					ping_dialuptest = dialuptest;
					ping_reversetest = reversetest;
					p = strchr(testspec, '=');
					if (p) {
						char *ips;

						/* Extra ping tests - save them for later */
						h->extrapings = (extraping_t *)malloc(sizeof(extraping_t));
						h->extrapings->iplist = NULL;
						if (argnmatch(p, "=worst,")) {
							h->extrapings->matchtype = MULTIPING_WORST;
							ips = strdup(p+7);
						}
						else if (argnmatch(p, "=best,")) {
							h->extrapings->matchtype = MULTIPING_BEST;
							ips = strdup(p+6);
						}
						else {
							h->extrapings->matchtype = MULTIPING_BEST;
							ips = strdup(p+1);
						}

						do {
							ipping_t *newping = (ipping_t *)malloc(sizeof(ipping_t));

							newping->ip = ips;
							newping->open = 0;
							newping->banner = newstrbuffer(0);
							newping->next = h->extrapings->iplist;
							h->extrapings->iplist = newping;
							ips = strchr(ips, ',');
							if (ips) { *ips = '\0'; ips++; }
						} while (ips && (*ips));
					}
					s = NULL; /* Don't add the test now - ping is special (enabled by default) */
				}
				else if ((argnmatch(testspec, "ldap://")) || (argnmatch(testspec, "ldaps://"))) {
					/*
					 * LDAP test. This uses ':' a lot, so save it here.
					 */
#ifdef HAVE_LDAP
					s = ldaptest;
					add_url_to_dns_queue(testspec);
#else
					errprintf("Host %s: ldap test requested, but xymonnet was built with no ldap support\n", xmh_item(hwalk, XMH_HOSTNAME));
#endif
				}
				else if ((strcmp(testspec, "http") == 0) || (strcmp(testspec, "https") == 0)) {
					errprintf("Host %s: http/https tests requires a full URL\n", xmh_item(hwalk, XMH_HOSTNAME));
				}
				else if ( argnmatch(testspec, "http")         ||
					  argnmatch(testspec, "content=http") ||
					  argnmatch(testspec, "cont;http")    ||
					  argnmatch(testspec, "cont=")        ||
					  argnmatch(testspec, "data;http")    ||
					  argnmatch(testspec, "data=")        ||
					  argnmatch(testspec, "dataonly;http") ||
					  argnmatch(testspec, "dataonly=")     ||
					  argnmatch(testspec, "datasvc;http")  ||
					  argnmatch(testspec, "datasvc=")      ||
					  argnmatch(testspec, "clienthttp;http")     ||
					  argnmatch(testspec, "clienthttp=")         ||
					  argnmatch(testspec, "clienthttponly;http") ||
					  argnmatch(testspec, "clienthttponly=")     ||
					  argnmatch(testspec, "clienthttpsvc;http")  ||
					  argnmatch(testspec, "clienthttpsvc=")      ||
					  argnmatch(testspec, "nocont;http")  ||
					  argnmatch(testspec, "nocont=")      ||
					  argnmatch(testspec, "post;http")    ||
					  argnmatch(testspec, "post=")        ||
					  argnmatch(testspec, "nopost;http")  ||
					  argnmatch(testspec, "nopost=")      ||
					  argnmatch(testspec, "soap;http")    ||
					  argnmatch(testspec, "soap=")        ||
					  argnmatch(testspec, "nosoap;http")    ||
					  argnmatch(testspec, "nosoap=")        ||
					  argnmatch(testspec, "type;http")    ||
					  argnmatch(testspec, "type=")        )      {

					/* HTTP test. */
					weburl_t url;

					decode_url(testspec, &url);
					if (url.desturl->parseerror || (url.proxyurl && url.proxyurl->parseerror)) {
						s = NULL;
						errprintf("Host %s: Invalid URL for http test - ignored: %s\n", 
							  xmh_item(hwalk, XMH_HOSTNAME), testspec);
					}
					else {
						s = httptest;
						if (!url.desturl->ip)
							add_url_to_dns_queue(testspec);
						if (argnmatch(testspec, "dataonly;http") || argnmatch(testspec, "dataonly="))
							sendasdata = 1;
						else if (argnmatch(testspec, "data;http") || argnmatch(testspec, "data="))
							sendasdata = 2;
						else if (argnmatch(testspec, "datasvc;http") || argnmatch(testspec, "datasvc="))
							sendasdata = 3;
						else if (argnmatch(testspec, "clienthttponly;http") || argnmatch(testspec, "clienthttponly="))
							sendasclient = 1;
						else if (argnmatch(testspec, "clienthttp;http") || argnmatch(testspec, "clienthttp="))
							sendasclient = 2;
						else if (argnmatch(testspec, "clienthttpsvc;http") || argnmatch(testspec, "clienthttpsvc="))
							sendasclient = 3;
					}
				}
				else if (argnmatch(testspec, "apache") || argnmatch(testspec, "apache=")) {
					char *userfmt = "cont=apache;%s;.";
					char *deffmt = "cont=apache;http://%s/server-status?auto;.";
					static char *statusurl = NULL;
					char *userurl;

					if (statusurl != NULL) xfree(statusurl);

					userurl = strchr(testspec, '='); 
					if (userurl) {
						weburl_t url;
						userurl++;

						decode_url(userurl, &url);
						if (url.desturl->parseerror || (url.proxyurl && url.proxyurl->parseerror)) {
							s = NULL;
							errprintf("Host %s: Invalid URL for apache test - ignored: %s\n", xmh_item(hwalk, XMH_HOSTNAME), testspec);
						}
						else {
							statusurl = (char *)malloc(strlen(userurl) + strlen(userfmt) + 1);
							sprintf(statusurl, userfmt, userurl);
							s = httptest;
						}
					}
					else {
						char *ip = xmh_item(hwalk, XMH_IP);
						statusurl = (char *)malloc(strlen(deffmt) + strlen(ip) + 1);
						sprintf(statusurl, deffmt, ip);
						s = httptest;
					}

					if (s) {
						testspec = statusurl;
						add_url_to_dns_queue(testspec);
						sendasdata = 1;
					}
				}
				else if (argnmatch(testspec, "rpc")) {
					/*
					 * rpc check via rpcinfo
					 */
					s = rpctest;
				}
				else if (argnmatch(testspec, "dns=")) {
					s = dnstest;
				}
				else if (argnmatch(testspec, "dig=")) {
					s = dnstest;
				}
				else {
					/* 
					 * Simple TCP connect test. 
					 */
					char *option;
					xtreePos_t handle;

					/* See if there's a source IP */
					srcip = strchr(testspec, '@');
					if (srcip) {
						*srcip = '\0';
						srcip++;
					}

					/* Remove any trailing ":s", ":q", ":Q", ":portnumber" */
					option = strchr(testspec, ':'); 
					if (option) { 
						*option = '\0'; 
						option++; 
					}
	
					/* Find the service */
					handle = xtreeFind(svctree, testspec);
					s = ((handle == xtreeEnd(svctree)) ? NULL : (service_t *)xtreeData(svctree, handle));
					if (option && s) {
						/*
						 * Check if it is a service with an explicit portnumber.
						 * If it is, then create a new service record named
						 * "SERVICE_PORT" so we can merge tests for this service+port
						 * combination for multiple hosts.
						 *
						 * According to Xymon docs, this type of services must be in
						 * XYMONNETSVCS - so it is known already.
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
							xfree(specialname);
						}
					}

					if (s) h->dodns = 1;
					if (option) *(option-1) = ':';
				}

				if (s) {
					testitem_t *newtest;

					anytests = 1;
					newtest = init_testitem(h, s, srcip, testspec, dialuptest, reversetest, alwaystruetest, silenttest, sendasdata, sendasclient);
					newtest->next = s->items;
					s->items = newtest;

					/*
					 * TODO: In the case of external libraries (like ldap)
					 * basic TCP pre-testing needs to take into account port details
					 * and other edge cases.
					 */
					if (s == httptest) h->firsthttp = newtest;
					else if (s == ldaptest) h->firstldap = newtest;
				}
			}

			testspec = xmh_item_walk(NULL);
		}

		if (pingtest && !h->noconn) {
			/* Add the ping check */
			testitem_t *newtest;

			anytests = 1;
			newtest = init_testitem(h, pingtest, NULL, NULL, ping_dialuptest, ping_reversetest, 1, 0, 0, 0);
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
		testspec = xmh_item_walk(hwalk);
		while (testspec) {
			char *testname, *timespec, *badcounts;
			int badclear, badyellow, badred;
			int inscope;
			testitem_t *twalk;
			service_t *swalk;

			if (strncmp(testspec, "bad", 3) != 0) {
				/* Not a bad* tag - skip it */
				testspec = xmh_item_walk(NULL);
				continue;
			}


			badtagsused = 1;
			badclear = badyellow = badred = 0;
			inscope = 1;

			testname = testspec+strlen("bad");
			badcounts = strchr(testspec, ':');
			if (badcounts) {
				if (sscanf(badcounts, ":%d:%d:%d", &badclear, &badyellow, &badred) != 3) {
					errprintf("Host %s: Incorrect 'bad' counts: '%s'\n", xmh_item(hwalk, XMH_HOSTNAME), badcounts);
					badcounts = NULL;
				}
			}
			timespec = strchr(testspec, '-');
			if (timespec) inscope = periodcoversnow(timespec);

			if (strlen(testname) && badcounts && inscope) {
				char *p;
				xtreePos_t handle;
				twalk = NULL;

				p = strchr(testname, ':'); if (p) *p = '\0';
				handle = xtreeFind(svctree, testname);
				swalk = ((handle == xtreeEnd(svctree)) ? NULL : (service_t *)xtreeData(svctree, handle));
				if (p) *p = ':';
				if (swalk) {
					if (swalk == httptest) twalk = h->firsthttp;
					else if (swalk == ldaptest) twalk = h->firstldap;
					else for (twalk = swalk->items; (twalk && (twalk->host != h)); twalk = twalk->next) ;
				}

				if (twalk) {
					twalk->badtest[0] = badclear;
					twalk->badtest[1] = badyellow;
					twalk->badtest[2] = badred;
				}
				else {
					dbgprintf("No test for badtest spec host=%s, test=%s\n",
						h->hostname, testname);
				}
			}

			testspec = xmh_item_walk(NULL);
		}


		if (anytests) {
			xtreeStatus_t res;

			/* 
			 * Check for a duplicate host def. Causes all sorts of funny problems.
			 * However, don't drop the second definition - to do this, we will have
			 * to clean up the testitem lists as well, or we get crashes when 
			 * tests belong to a non-existing host.
			 */

			res = xtreeAdd(testhosttree, h->hostname, h);
			if (res == XTREE_STATUS_DUPLICATE_KEY) {
				errprintf("Host %s appears twice in hosts.cfg! This may cause strange results\n", h->hostname);
			}
	
			h->ip = strdup(xmh_item(hwalk, XMH_IP));
			if (!h->testip && (dnsmethod != IP_ONLY)) add_host_to_dns_queue(h->hostname);
		}
		else {
			/* No network tests for this host, so ignore it */
			dbgprintf("Did not find any network tests for host %s\n", h->hostname);
			xfree(h);
			notesthostcount++;
		}

	}

	if (badtagsused) {
		errprintf("WARNING: The 'bad<TESTNAME>' syntax has been deprecated, please convert to 'delayred' and/or 'delayyellow' tags\n");
	}

	return;
}

char *ip_to_test(testedhost_t *h)
{
	char *dnsresult;
	int nullip = (h->ip && conn_null_ip(h->ip));

	if (!nullip && (h->testip || (dnsmethod == IP_ONLY))) {
		/* Already have the IP setup */
	}
	else if (h->dodns) {
		dnsresult = dnsresolve(h->hostname);

		if (dnsresult) {
			h->ip = strdup(dnsresult);
		}
		else if ((dnsmethod == DNS_THEN_IP) && !nullip) {
			/* Already have the IP setup */
		}
		else {
			/* Cannot resolve hostname */
			h->dnserror = 1;
			logprintf("xymonnet: Cannot resolve IP for host %s\n", h->hostname);
		}
	}

	return h->ip;
}


void load_ping_status(void)
{
	FILE *statusfd;
	char statusfn[PATH_MAX];
	char l[MAX_LINE_LEN];
	char host[MAX_LINE_LEN];
	int  downcount;
	time_t downstart;
	xtreePos_t handle;
	testedhost_t *h;

	sprintf(statusfn, "%s/ping.%s.status", xgetenv("XYMONTMP"), location);
	statusfd = fopen(statusfn, "r");
	if (statusfd == NULL) return;

	while (fgets(l, sizeof(l), statusfd)) {
		unsigned int uidownstart;
		if (sscanf(l, "%s %d %u", host, &downcount, &uidownstart) == 3) {
			downstart = uidownstart;
			handle = xtreeFind(testhosttree, host);
			if (handle != xtreeEnd(testhosttree)) {
				h = (testedhost_t *)xtreeData(testhosttree, handle);
				if (!h->noping && !h->noconn) {
					h->downcount = downcount;
					h->downstart = downstart;
				}
			}
		}
	}

	fclose(statusfd);
}

void save_ping_status(void)
{
	FILE *statusfd;
	char statusfn[PATH_MAX];
	testitem_t *t;
	int didany = 0;

	sprintf(statusfn, "%s/ping.%s.status", xgetenv("XYMONTMP"), location);
	statusfd = fopen(statusfn, "w");
	if (statusfd == NULL) return;

	for (t=pingtest->items; (t); t = t->next) {
		if (t->host->downcount) {
			fprintf(statusfd, "%s %d %u\n", t->host->hostname, t->host->downcount, (unsigned int)t->host->downstart);
			didany = 1;
			t->host->repeattest = ((getcurrenttime(NULL) - t->host->downstart) < frequenttestlimit);
		}
	}

	fclose(statusfd);
	if (!didany) unlink(statusfn);
}

void load_test_status(service_t *test)
{
	FILE *statusfd;
	char statusfn[PATH_MAX];
	char l[MAX_LINE_LEN];
	char host[MAX_LINE_LEN];
	int  downcount;
	time_t downstart;
	xtreePos_t handle;
	testedhost_t *h;
	testitem_t *walk;

	sprintf(statusfn, "%s/%s.%s.status", xgetenv("XYMONTMP"), test->testname, location);
	statusfd = fopen(statusfn, "r");
	if (statusfd == NULL) return;

	while (fgets(l, sizeof(l), statusfd)) {
		unsigned int uidownstart;
		if (sscanf(l, "%s %d %u", host, &downcount, &uidownstart) == 3) {
			downstart = uidownstart;
			handle = xtreeFind(testhosttree, host);
			if (handle != xtreeEnd(testhosttree)) {
				h = (testedhost_t *)xtreeData(testhosttree, handle);
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
	char statusfn[PATH_MAX];
	testitem_t *t;
	int didany = 0;

	sprintf(statusfn, "%s/%s.%s.status", xgetenv("XYMONTMP"), test->testname, location);
	statusfd = fopen(statusfn, "w");
	if (statusfd == NULL) return;

	for (t=test->items; (t); t = t->next) {
		if (t->downcount) {
			fprintf(statusfd, "%s %d %u\n", t->host->hostname, t->downcount, (unsigned int)t->downstart);
			didany = 1;
			t->host->repeattest = ((getcurrenttime(NULL) - t->downstart) < frequenttestlimit);
		}
	}

	fclose(statusfd);
	if (!didany) unlink(statusfn);
}


void save_frequenttestlist(int argc, char *argv[])
{
	FILE *fd;
	char fn[PATH_MAX];
	xtreePos_t handle;
	testedhost_t *h;
	int didany = 0;
	int i;

	sprintf(fn, "%s/frequenttests.%s", xgetenv("XYMONTMP"), location);
	fd = fopen(fn, "w");
	if (fd == NULL) return;

	for (i=1; (i<argc); i++) {
		if (!argnmatch(argv[i], "--report")) fprintf(fd, "%s ", argv[i]);
	}
	for (handle = xtreeFirst(testhosttree); (handle != xtreeEnd(testhosttree)); handle = xtreeNext(testhosttree, handle)) {
		h = (testedhost_t *)xtreeData(testhosttree, handle);
		if (h->repeattest) {
			fprintf(fd, "%s ", h->hostname);
			didany = 1;
		}
	}

	fclose(fd);
	if (!didany) unlink(fn);
}


void run_nslookup_service(service_t *service)
{
	testitem_t	*t;
	char		*lookup;

	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror) {
			if (t->testspec && (lookup = strchr(t->testspec, '='))) {
				lookup++; 
			}
			else {
				lookup = t->host->hostname;
			}

			t->open = (dns_test_server(ip_to_test(t->host), lookup, t->banner) == 0);
		}
	}
}

void run_ntp_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[PATH_MAX];
	int		use_sntp = 0;

	p = getenv("SNTP");	/* Plain "getenv" as we want to know if it's unset */
	use_sntp = (p != NULL);

	strcpy(cmdpath, (use_sntp ? xgetenv("SNTP") : xgetenv("NTPDATE")) );

	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror) {
			if (use_sntp) {
				sprintf(cmd, "%s %s -d %d %s 2>&1", cmdpath, xgetenv("SNTPOPTS"), extcmdtimeout-1, ip_to_test(t->host));
			}
			else {
				sprintf(cmd, "%s %s %s 2>&1", cmdpath, xgetenv("NTPDATEOPTS"), ip_to_test(t->host));
			}

			t->open = (run_command(cmd, "no server suitable for synchronization", t->banner, 1, extcmdtimeout) == 0);
		}
	}
}


void run_rpcinfo_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[PATH_MAX];

	p = xgetenv("RPCINFO");
	strcpy(cmdpath, (p ? p : "rpcinfo"));
	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror && (t->host->downcount == 0)) {
			sprintf(cmd, "%s -p %s 2>&1", cmdpath, ip_to_test(t->host));
			t->open = (run_command(cmd, NULL, t->banner, 1, extcmdtimeout) == 0);
		}
	}
}


int start_ping_service(service_t *service)
{
	testitem_t *t;
	char *cmd;
	char **cmdargs;
	int pfd[2];
	int i;
	void *iptree;
	xtreePos_t handle;
	
	/* We build a tree of the IP's to test, so we only test each IP once */
	iptree = xtreeNew(strcmp);
	for (t=service->items; (t); t = t->next) {
		char *rec;
		char *ip;

		if (t->host->dnserror || t->host->noping) continue;

		ip = strdup(ip_to_test(t->host));
		handle = xtreeFind(iptree, ip);
		if (handle == xtreeEnd(iptree)) {
			rec = ip;
			xtreeAdd(iptree, rec, rec);
		}

		if (t->host->extrapings) {
			ipping_t *walk;

			for (walk = t->host->extrapings->iplist; (walk); walk = walk->next) {
				handle = xtreeFind(iptree, walk->ip);
				if (handle == xtreeEnd(iptree)) {
					rec = strdup(walk->ip);
					xtreeAdd(iptree, rec, rec);
				}
			}
		}
	}

	/*
	 * The idea here is to run ping in a separate process, in parallel
	 * with some other time-consuming task (the TCP network tests).
	 * We cannot use the simple "popen()/pclose()" interface, because
	 *   a) ping doesn't start the tests until EOF is reached on stdin
	 *   b) EOF on stdin happens with pclose(), but it will also wait
	 *      for the process to finish.
	 *
	 * Therefore this slightly more complex solution, which in essence
	 * forks a new process running "xymonping 2>&1 1>$XYMONTMP/ping.$$"
	 * The output is then picked up by the finish_ping_service().
	 */

	pingcount = 0;
	pingpids = calloc(pingchildcount, sizeof(pid_t));
	pingcmd = malloc(strlen(xgetenv("FPING")) + strlen(xgetenv("FPINGOPTS")) + 2);
	sprintf(pingcmd, "%s %s", xgetenv("FPING"), xgetenv("FPINGOPTS"));

	sprintf(pinglog, "%s/ping-stdout.%lu", xgetenv("XYMONTMP"), (unsigned long)getpid());
	sprintf(pingerrlog, "%s/ping-stderr.%lu", xgetenv("XYMONTMP"), (unsigned long)getpid());

	/* Setup command line and arguments */
	cmdargs = setup_commandargs(pingcmd, &cmd);

	for (i=0; (i < pingchildcount); i++) {
		/* Get a pipe FD */
		if (pipe(pfd) == -1) {
			errprintf("Could not create pipe for xymonping\n");
			return -1;
		}

		/* Now fork off the ping child-process */
		pingpids[i] = fork();

		if (pingpids[i] < 0) {
			errprintf("Could not fork() the ping child\n");
			return -1;
		}
		else if (pingpids[i] == 0) {
			/*
			 * child must have
			 *  - stdin fed from the parent
			 *  - stdout going to a file
			 *  - stderr going to another file. This is important, as
			 *    putting it together with stdout will wreak havoc when 
			 *    we start parsing the output later on. We could just 
			 *    dump it to /dev/null, but it might be useful to see
			 *    what went wrong.
			 */
			int outfile, errfile;

			sprintf(pinglog+strlen(pinglog), ".%02d", i);
			sprintf(pingerrlog+strlen(pingerrlog), ".%02d", i);

			outfile = open(pinglog, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR);
			if (outfile == -1) errprintf("Cannot create file %s : %s\n", pinglog, strerror(errno));
			errfile = open(pingerrlog, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR);
			if (errfile == -1) errprintf("Cannot create file %s : %s\n", pingerrlog, strerror(errno));

			if ((outfile == -1) || (errfile == -1)) {
				/* Ouch - cannot create our output files. Abort. */
				exit(98);
			}

			dup2(pfd[0], STDIN_FILENO);
			dup2(outfile, STDOUT_FILENO);
			dup2(errfile, STDERR_FILENO);
			close(pfd[0]); close(pfd[1]); close(outfile); close(errfile);

			execvp(cmd, cmdargs);

			/* Should never go here ... just kill the child */
			fprintf(stderr, "Command '%s' failed: %s\n", cmd, strerror(errno));
			exit(99);
		}
		else {
			/* parent */
			char *ip;
			int hnum, feederror = 0;

			close(pfd[0]);

			/* Feed the IP's to test to the child */
			for (handle = xtreeFirst(iptree), hnum = 0; ((feederror == 0) && (handle != xtreeEnd(iptree))); handle = xtreeNext(iptree, handle), hnum++) {
				if ((hnum % pingchildcount) != i) continue;

				ip = xtreeKey(iptree, handle);
				if ((write(pfd[1], ip, strlen(ip)) != strlen(ip)) || (write(pfd[1], "\n", 1) != 1)) {
					errprintf("Cannot feed IP to ping tool: %s\n", strerror(errno));
					feederror = 1;
					continue;
				}
				pingcount++;
			}

			close(pfd[1]);	/* This is when ping starts doing tests */
		}
	}

	for (handle = xtreeFirst(iptree); handle != xtreeEnd(iptree); handle = xtreeNext(iptree, handle)) {
		char *rec = xtreeKey(iptree, handle);
		xfree(rec);
	}
	xtreeDestroy(iptree);

	return 0;
}


int finish_ping_service(service_t *service)
{
	testitem_t	*t;
	FILE		*logfd;
	char 		*p;
	char		l[MAX_LINE_LEN];
	char		pingip[MAX_LINE_LEN];
	int		ip1, ip2, ip3, ip4;
	int		pingstatus, failed = 0, i;
	char		fn[PATH_MAX];

	/* Load status of previously failed tests */
	load_ping_status();

	/* 
	 * Wait for the ping child to finish.
	 * If we're lucky, it will be done already since it has run
	 * while we were doing tcp tests.
	 */
	for (i = 0; (i < pingchildcount); i++) {
		waitpid(pingpids[i], &pingstatus, 0);
		switch (WEXITSTATUS(pingstatus)) {
			case 0: /* All hosts reachable */
			case 1: /* Some hosts unreachable */
			case 2: /* Some IP's not found (should not happen) */
				break;

			case 3: /* Bad command-line args, or not suid-root */
				failed = 1;
				errprintf("Execution of '%s' failed - program not suid root?\n", pingcmd);
				break;

			case 98:
				failed = 1;
				errprintf("xymonping child could not create outputfiles in %s\n", xgetenv("XYMONTMP"));
				break;

			case 99:
				failed = 1;
				errprintf("Could not run the command '%s' (exec failed)\n", pingcmd);
				break;

			default:
				failed = 1;
				errprintf("Execution of '%s' failed with error-code %d\n", 
						pingcmd, WEXITSTATUS(pingstatus));
		}

		/* Open the new ping result file */
		sprintf(fn, "%s.%02d", pinglog, i);
		logfd = fopen(fn, "r");
		if (logfd == NULL) { 
			failed = 1;
			errprintf("Cannot open ping output file %s\n", fn);
		}
		if (!debug) unlink(fn);	/* We have an open filehandle, so it's ok to delete the file now */

		/* Copy error messages to the Xymon logfile */
		sprintf(fn, "%s.%02d", pingerrlog, i);
		if (failed) {
			FILE *errfd;
			char buf[1024];

			errfd = fopen(fn, "r");
			if (errfd && fgets(buf, sizeof(buf), errfd)) {
				errprintf("%s", buf);
			}
			if (errfd) fclose(errfd);
		}
		if (!debug) unlink(fn);

		if (failed) {
			/* Flag all ping tests as "undecided" */
			bigfailure = 1;
			for (t=service->items; (t); t = t->next) t->open = -1;
		}
		else {
			/* The test did run, and we have a result-file. Look at it. */
			while (fgets(l, sizeof(l), logfd)) {
				p = strchr(l, '\n'); if (p) *p = '\0';
				if (sscanf(l, "%d.%d.%d.%d ", &ip1, &ip2, &ip3, &ip4) == 4) {

					sprintf(pingip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

					/*
					 * Need to loop through all testitems - there may be multiple entries for
					 * the same IP-address.
					 */
					for (t=service->items; (t); t = t->next) {
						if (strcmp(t->host->ip, pingip) == 0) {
							if (t->open) dbgprintf("More than one ping result for %s\n", pingip);
							t->open = (strstr(l, "is alive") != NULL);
							t->banner = dupstrbuffer(l);
						}

						if (t->host->extrapings) {
							ipping_t *walk;
							for (walk = t->host->extrapings->iplist; (walk); walk = walk->next) {
								if (strcmp(walk->ip, pingip) == 0) {
									if (t->open) dbgprintf("More than one ping result for %s\n", pingip);
									walk->open = (strstr(l, "is alive") != NULL);
									walk->banner = dupstrbuffer(l);
								}
							}
						}
					}
				}
			}
		}

		if (logfd) fclose(logfd);
	}

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


int decide_color(service_t *service, char *svcname, testitem_t *test, int failgoesclear, char *cause)
{
	int color = COL_GREEN;
	int countasdown = 0;
	char *deptest = NULL;

	*cause = '\0';
	if (service == pingtest) {
		/*
		 * "noconn" is handled elsewhere.
		 * "noping" always sends back a status "clear".
		 * If DNS error, return red and count as down.
		 */
		if (test->open == -1) {
			/* Failed to run the ping utility. */
			strcpy(cause, "Xymon system failure");
			return COL_CLEAR;
		}
		else if (test->host->noping) { 
			/* Ping test disabled - go "clear". End of story. */
			strcpy(cause, "Ping test disabled (noping)");
			return COL_CLEAR; 
		}
		else if (test->host->dnserror) { 
			strcpy(cause, "DNS lookup failure");
			color = COL_RED; countasdown = 1; 
		}
		else {
			if (test->host->extrapings == NULL) {
				/* Red if (open=0, reverse=0) or (open=1, reverse=1) */
				if ((test->open + test->reverse) != 1) { 
					sprintf(cause, "Host %s respond to ping", (test->open ? "does" : "does not"));
					color = COL_RED; countasdown = 1; 
				}
			}
			else {
				/* Host with many pings */
				int totalcount = 1;
				int okcount = test->open;
				ipping_t *walk;

				for (walk = test->host->extrapings->iplist; (walk); walk = walk->next) {
					if (walk->open) okcount++;
					totalcount++;
				}

				switch (test->host->extrapings->matchtype) {
				  case MULTIPING_BEST:
					  if (okcount == 0) {
						  color = COL_RED;
						  countasdown = 1;
						  sprintf(cause, "Host does not respond to ping on any of %d IP's", 
							  totalcount);
					  }
					  break;
				  case MULTIPING_WORST:
					  if (okcount < totalcount) {
						  color = COL_RED;
						  countasdown = 1;
						  sprintf(cause, "Host responds to ping on %d of %d IP's",
							  okcount, totalcount);
					  }
					  break;
				}
			}
		}

		/* Handle the "route" tag dependencies. */
		if ((color == COL_RED) && test->host->deprouterdown) { 
			char *routertext;

			routertext = test->host->deprouterdown->hosttype;
			if (routertext == NULL) routertext = xgetenv("XYMONROUTERTEXT");
			if (routertext == NULL) routertext = "router";

			strcat(cause, "\nIntermediate ");
			strcat(cause, routertext);
			strcat(cause, " down ");
			color = COL_YELLOW; 
		}

		/* Handle "badconn" */
		if ((color == COL_RED) && (test->host->downcount < test->host->badconn[2])) {
			if      (test->host->downcount >= test->host->badconn[1]) color = COL_YELLOW;
			else if (test->host->downcount >= test->host->badconn[0]) color = COL_CLEAR;
			else                                                      color = COL_GREEN;
		}

		/* Run traceroute , but not on dialup or reverse-test hosts */
		if ((color == COL_RED) && test->host->dotrace && !test->host->dialup && !test->reverse && !test->dialup) {
			char cmd[PATH_MAX];

			if (getenv("TRACEROUTEOPTS")) {
				/* post 4.3.21 */
				sprintf(cmd, "%s %s %s 2>&1", xgetenv("TRACEROUTE"), xgetenv("TRACEROUTEOPTS"), test->host->ip);
			}
			else {
				sprintf(cmd, "%s %s 2>&1", xgetenv("TRACEROUTE"), test->host->ip);
			}
			test->host->traceroute = newstrbuffer(0);
			run_command(cmd, NULL, test->host->traceroute, 0, extcmdtimeout);
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
						tcptest_t *tcptest = (tcptest_t *)test->privdata;

						strcpy(cause, "Service unavailable");
						if (tcptest) {
							switch (tcptest->errcode) {
							  case CONTEST_ETIMEOUT: 
								strcat(cause, " (connect timeout)"); 
								break;
							  case CONTEST_ENOCONN : 
								strcat(cause, " (");
								strcat(cause, strerror(tcptest->connres));
								strcat(cause, ")");
								break;
							  case CONTEST_EDNS    : 
								strcat(cause, " (DNS error)"); 
								break;
							  case CONTEST_EIO     : 
								strcat(cause, " (I/O error)"); 
								break;
							  case CONTEST_ESSL    : 
								strcat(cause, " (SSL error)"); 
								break;
							}
						}
						color = COL_RED; countasdown = 1;
					}
				}
				else {
					tcptest_t *tcptest = (tcptest_t *)test->privdata;

					/* Check if we got the expected data */
					if (checktcpresponse && (service->toolid == TOOL_CONTEST) && !tcp_got_expected((tcptest_t *)test->privdata)) {
						strcpy(cause, "Unexpected service response");
						color = respcheck_color; countasdown = 1;
					}

					/* Check that other transport issues didn't occur (like SSL) */
					if (tcptest && (tcptest->errcode != CONTEST_ENOERROR)) {
						switch (tcptest->errcode) {
						  case CONTEST_ESSL    : 
							strcpy(cause, "Service listening but unavailable (SSL error)"); 
							color = COL_RED; countasdown = 1;
							break;
						  default		:
							errprintf("TCPtest error %d seen on open connection for %s.%s\n", tcptest->errcode, test->host, test->service->testname); 
							// color = COL_RED; countasdown = 1;
							break;
						}
						// color = COL_RED; countasdown = 1;
					}

				}
			}
		}

		/* Handle test dependencies */
		if ( failgoesclear && (color == COL_RED) && !test->alwaystrue && (deptest = deptest_failed(test->host, test->service->testname)) ) {
			strcpy(cause, deptest);
			color = COL_CLEAR;
		}

		/* Handle the "badtest" stuff for other tests */
		if ((color == COL_RED) && (test->downcount < test->badtest[2])) {
			if      (test->downcount >= test->badtest[1]) color = COL_YELLOW;
			else if (test->downcount >= test->badtest[0]) color = COL_CLEAR;
			else                                          color = COL_GREEN;
		}
	}


	/* Dialup hosts and dialup tests report red as clear */
	if ( ((color == COL_RED) || (color == COL_YELLOW)) && (test->host->dialup || test->dialup) && !test->reverse) { 
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
		xfree(nopagename);
	}

	if (service == pingtest) {
		if (countasdown) {
			test->host->downcount++; 
			if (test->host->downcount == 1) test->host->downstart = getcurrenttime(NULL);
		}
		else test->host->downcount = 0;
	}
	else {
		if (countasdown) {
			test->downcount++; 
			if (test->downcount == 1) test->downstart = getcurrenttime(NULL);
		}
		else test->downcount = 0;
	}
	return color;
}


void send_results(service_t *service, int failgoesclear)
{
	testitem_t	*t;
	int		color;
	char		msgline[4096];
	char		msgtext[4096];
	char		causetext[1024];
	char		*svcname;

	svcname = strdup(service->testname);
	if (service->namelen) svcname[service->namelen] = '\0';

	dbgprintf("Sending results for service %s\n", svcname);

	for (t=service->items; (t); t = t->next) {
		char flags[10];
		int i;

		if (t->internal) continue;

		i = 0;
		flags[i++] = (t->open ? 'O' : 'o');
		flags[i++] = (t->reverse ? 'R' : 'r');
		flags[i++] = ((t->dialup || t->host->dialup) ? 'D' : 'd');
		flags[i++] = (t->alwaystrue ? 'A' : 'a');
		flags[i++] = (t->silenttest ? 'S' : 's');
		flags[i++] = (t->host->testip ? 'T' : 't');
		flags[i++] = (t->host->dodns ? 'L' : 'l');
		flags[i++] = (t->host->dnserror ? 'E' : 'e');
		flags[i++] = '\0';

		color = decide_color(service, svcname, t, failgoesclear, causetext);

		init_status(color);
		if (dosendflags) 
			sprintf(msgline, "status+%d %s.%s %s <!-- [flags:%s] --> %s %s %s ", 
				validity, commafy(t->host->hostname), svcname, colorname(color), 
				flags, timestamp, 
				svcname, ( ((color == COL_RED) || (color == COL_YELLOW)) ? "NOT ok" : "ok"));
		else
			sprintf(msgline, "status %s.%s %s %s %s %s ", 
				commafy(t->host->hostname), svcname, colorname(color), 
				timestamp, 
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
					char *routertext;

					routertext = t->host->deprouterdown->hosttype;
					if (routertext == NULL) routertext = xgetenv("XYMONROUTERTEXT");
					if (routertext == NULL) routertext = "router";

					strcat(msgline, ": Intermediate ");
					strcat(msgline, routertext);
					strcat(msgline, " down");

					sprintf(msgtext+strlen(msgtext), 
						"%s.\nThe %s %s (IP:%s) is not reachable, causing this host to be unreachable.\n",
						failtext, routertext, 
						((testedhost_t *)t->host->deprouterdown)->hostname,
						((testedhost_t *)t->host->deprouterdown)->ip);
				  }
				  else {
					sprintf(msgtext+strlen(msgtext), "%s : %s\n", failtext, causetext);
				  }
				  break;

			  case COL_CLEAR:
				  strcat(msgtext, "OK\n");
				  if (service == pingtest) {
					  if (t->host->deprouterdown) {
						char *routertext;

						routertext = t->host->deprouterdown->hosttype;
						if (routertext == NULL) routertext = xgetenv("XYMONROUTERTEXT");
						if (routertext == NULL) routertext = "router";

						strcat(msgline, ": Intermediate ");
						strcat(msgline, routertext);
						strcat(msgline, " down");

						strcat(msgtext, "\nThe ");
						strcat(msgtext, routertext); strcat(msgtext, " ");
						strcat(msgtext, ((testedhost_t *)t->host->deprouterdown)->hostname);
						strcat(msgtext, " (IP:");
						strcat(msgtext, ((testedhost_t *)t->host->deprouterdown)->ip);
						strcat(msgtext, ") is not reachable, causing this host to be unreachable.\n");
					  }
					  else if (t->host->noping) {
						  strcat(msgline, ": Disabled");
						  strcat(msgtext, "Ping check disabled (noping)\n");
					  }
					  else if (t->host->dialup) {
						  strcat(msgline, ": Disabled (dialup host)");
						  strcat(msgtext, "Dialup host\n");
					  }
					  else if (t->open == -1) {
						  strcat(msgline, ": System failure of the ping test");
						  strcat(msgtext, "Xymon system error\n");
					  }
					  /* "clear" due to badconn: no extra text */
				  }
				  else {
					  /* Non-ping test clear: Dialup test or failed ping */
					  strcat(msgline, ": Ping failed, or dialup host/service");
					  strcat(msgtext, "Dialup host/service, or test depends on another failed test\n");
					  strcat(msgtext, causetext);
				  }
				  break;
			}
			strcat(msgtext, "\n");
		}
		strcat(msgline, "\n");
		addtostatus(msgline);
		addtostatus(msgtext);

		if ((service == pingtest) && t->host->downcount) {
			sprintf(msgtext, "\nSystem unreachable for %d poll periods (%u seconds)\n",
				t->host->downcount, (unsigned int)(getcurrenttime(NULL) - t->host->downstart));
			addtostatus(msgtext);
		}

		if (STRBUFLEN(t->banner)) {
			if (service == pingtest) {
				sprintf(msgtext, "\n&%s %s\n", colorname(t->open ? COL_GREEN : COL_RED), STRBUF(t->banner));
				addtostatus(msgtext);
				if (t->host->extrapings) {
					ipping_t *walk;
					for (walk = t->host->extrapings->iplist; (walk); walk = walk->next) {
						if (STRBUFLEN(walk->banner)) {
							sprintf(msgtext, "&%s %s\n", 
								colorname(walk->open ? COL_GREEN : COL_RED), STRBUF(walk->banner));
							addtostatus(msgtext);
						}
					}
				}
			}
			else {
				addtostatus("\n"); addtostrstatus(t->banner); addtostatus("\n");
			}
		}

		if ((service == pingtest) && t->host->traceroute && (STRBUFLEN(t->host->traceroute) > 0)) {
			addtostatus("Traceroute results:\n");
			addtostrstatus(t->host->traceroute);
			addtostatus("\n");
		}

		if (t->duration.tv_sec != -1) {
			sprintf(msgtext, "\nSeconds: %u.%.9ld\n", 
				(unsigned int)t->duration.tv_sec, t->duration.tv_nsec);
			addtostatus(msgtext);
		}
		addtostatus("\n\n");
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
		if (p) wantedrpcsvcs = strdup(p+1);

		if ((color == COL_GREEN) && STRBUFLEN(t->banner) && wantedrpcsvcs) {
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
				aline = STRBUF(t->banner); 
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

		if (wantedrpcsvcs) xfree(wantedrpcsvcs);

		init_status(color);
		sprintf(msgline, "status+%d %s.%s %s %s %s %s, %s\n\n", 
			validity, commafy(t->host->hostname), service->testname, colorname(color), timestamp, 
			service->testname, 
			( ((color == COL_RED) || (color == COL_YELLOW)) ? "NOT ok" : "ok"),
			causetext);
		addtostatus(msgline);

		/* The summary of wanted RPC services */
		addtostatus(msgbuf);

		/* rpcinfo output */
		if (t->open) {
			if (STRBUFLEN(t->banner)) {
				addtostatus("\n\n");
				addtostrstatus(t->banner);
			}
			else {
				sprintf(msgline, "\n\nNo output from rpcinfo -p %s\n", t->host->ip);
				addtostatus(msgline);
			}
		}
		else {
			addtostatus("\n\nCould not connect to the portmapper service\n");
			if (STRBUFLEN(t->banner)) addtostrstatus(t->banner);
		}
		finish_status();
	}

	xfree(msgbuf);
}


void send_sslcert_status(testedhost_t *host)
{
	int color = -1;
	xtreePos_t handle;
	service_t *s;
	testitem_t *t;
	char msgline[1024];
	strbuffer_t *sslmsg;
	time_t now = getcurrenttime(NULL);
	char *certowner;

	sslmsg = newstrbuffer(0);

	for (handle = xtreeFirst(svctree); handle != xtreeEnd(svctree); handle = xtreeNext(svctree, handle)) {
		s = (service_t *)xtreeData(svctree, handle);
		certowner = s->testname;

		for (t=s->items; (t); t=t->next) {
			if ((t->host == host) && t->certinfo && (t->certexpires > 0)) {
				int sslcolor = COL_GREEN;
				int ciphercolor = COL_GREEN;
				int keycolor = COL_GREEN;

				if (s == httptest) certowner = ((http_data_t *)t->privdata)->url;
				else if (s == ldaptest) certowner = t->testspec;

				if (t->certexpires < (now+host->sslwarndays*86400)) sslcolor = COL_YELLOW;
				if (t->certexpires < (now+host->sslalarmdays*86400)) sslcolor = COL_RED;
				if (sslcolor > color) color = sslcolor;

				if (host->mincipherbits && (t->mincipherbits < host->mincipherbits)) ciphercolor = COL_RED;
				if (ciphercolor > color) color = ciphercolor;

				if (sslminkeysize > 0) {
					if ((t->certkeysz > 0) && (t->certkeysz < sslminkeysize)) keycolor = COL_YELLOW;
					if (keycolor > color) color = keycolor;
				}

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
				addtobuffer(sslmsg, msgline);

				if (host->mincipherbits) {
					sprintf(msgline, "&%s Minimum available SSL encryption is %d bits (should be %d)\n",
						colorname(ciphercolor), t->mincipherbits, host->mincipherbits);
					addtobuffer(sslmsg, msgline);
				}

				if (keycolor != COL_GREEN) {
					sprintf(msgline, "&%s Certificate public key size is less than %d bits\n", colorname(keycolor), sslminkeysize);
					addtobuffer(sslmsg, msgline);
				}
				addtobuffer(sslmsg, "\n");

				addtobuffer(sslmsg, t->certinfo);
			}
		}
	}

	if (color != -1) {
		/* Send off the sslcert status report */
		init_status(color);
		sprintf(msgline, "status+%d %s.%s %s %s\n", 
			validity, commafy(host->hostname), ssltestname, colorname(color), timestamp);
		addtostatus(msgline);
		addtostrstatus(sslmsg);
		addtostatus("\n\n");
		finish_status();
	}

	freestrbuffer(sslmsg);
}

int main(int argc, char *argv[])
{
	xtreePos_t handle;
	service_t *s;
	testedhost_t *h;
	testitem_t *t;
	int argi;
	int concurrency = 0;
	char *pingcolumn = "";
	char *egocolumn = NULL;
	int failgoesclear = 0;		/* IPTEST_2_CLEAR_ON_FAILED_CONN */
	int dumpdata = 0;
	int runtimewarn;		/* 300 = default TASKSLEEP setting */
	int servicedumponly = 0;
	int pingrunning = 0;
	int usebackfeedqueue = 0;
	int force_backfeedqueue = 0;

#ifdef HAVE_LZ4
        /* xymonnet sends a lot of data; decrease the load on xymond */
        defaultcompression = strdup("lz4");
#endif

	libxymon_init(argv[0]);

	if (init_ldap_library() != 0) {
		errprintf("Failed to initialize ldap library\n");
		return 1;
	}

	if (xgetenv("CONNTEST") && (strcmp(xgetenv("CONNTEST"), "FALSE") == 0)) pingcolumn = NULL;
	runtimewarn = (xgetenv("TASKSLEEP") ? atol(xgetenv("TASKSLEEP")) : 300);

	for (argi=1; (argi < argc); argi++) {
		if      (argnmatch(argv[argi], "--timeout=")) {
			char *p = strchr(argv[argi], '=');
			p++; timeout = atoi(p);
		}
		else if (argnmatch(argv[argi], "--conntimeout=")) {
			int newtimeout;
			char *p = strchr(argv[argi], '=');
			p++; newtimeout = atoi(p);
			if (newtimeout > timeout) timeout = newtimeout;
			errprintf("Deprecated option '--conntimeout' should not be used\n");
		}
		else if (argnmatch(argv[argi], "--cmdtimeout=")) {
			char *p = strchr(argv[argi], '=');
			p++; extcmdtimeout = atoi(p);
		}
		else if (argnmatch(argv[argi], "--concurrency=")) {
			char *p = strchr(argv[argi], '=');
			p++; concurrency = atoi(p);
		}
		else if (argnmatch(argv[argi], "--dns-timeout=") || argnmatch(argv[argi], "--dns-max-all=")) {
			char *p = strchr(argv[argi], '=');
			p++; dnstimeout = atoi(p);
		}
		else if (argnmatch(argv[argi], "--dns=")) {
			char *p = strchr(argv[argi], '=');
			p++;
			if (strcmp(p, "only") == 0)      dnsmethod = DNS_ONLY;
			else if (strcmp(p, "ip") == 0)   dnsmethod = IP_ONLY;
			else                             dnsmethod = DNS_THEN_IP;
		}
		else if (strcmp(argv[argi], "--no-ares") == 0) {
			use_ares_lookup = 0;
		}
		else if (strcmp(argv[argi], "--bfq") == 0) {
			force_backfeedqueue = 1;
		}
		else if (strcmp(argv[argi], "--no-bfq") == 0) {
			force_backfeedqueue = -1;
		}
		else if (argnmatch(argv[argi], "--maxdnsqueue=")) {
			char *p = strchr(argv[argi], '=');
			max_dns_per_run = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--dnslog=")) {
			char *fn = strchr(argv[argi], '=');
			dnsfaillog = fopen(fn+1, "w");
		}
		else if (argnmatch(argv[argi], "--report=") || (strcmp(argv[argi], "--report") == 0)) {
			char *p = strchr(argv[argi], '=');
			if (p) {
				egocolumn = strdup(p+1);
			}
			else egocolumn = "xymonnet";
			timing = 1;
		}
		else if (strcmp(argv[argi], "--test-untagged") == 0) {
			testuntagged = 1;
		}
		else if (argnmatch(argv[argi], "--frequenttestlimit=")) {
			char *p = strchr(argv[argi], '=');
			p++; frequenttestlimit = atoi(p);
		}
		else if (strcmp(argv[argi], "--timelimit=") == 0) {
			char *p = strchr(argv[argi], '=');
			p++; runtimewarn = atol(p);
		}
		else if (strcmp(argv[argi], "--huge=") == 0) {
			char *p = strchr(argv[argi], '=');
			p++; warnbytesread = atoi(p);
		}
		else if (strcmp(argv[argi], "--loadhostsfromxymond") == 0) {
			loadhostsfromxymond = 1;
		}

		/* Options for TCP tests */
		else if (strcmp(argv[argi], "--checkresponse") == 0) {
			checktcpresponse = 1;
		}
		else if (argnmatch(argv[argi], "--checkresponse=")) {
			char *p = strchr(argv[argi], '=');
			checktcpresponse = 1;
			respcheck_color = parse_color(p+1);
			if (respcheck_color == -1) {
				errprintf("Invalid colorname in '%s' - using yellow\n", argv[argi]);
				respcheck_color = COL_YELLOW;
			}
		}
		else if (strcmp(argv[argi], "--no-flags") == 0) {
			dosendflags = 0;
		}
		else if (strcmp(argv[argi], "--no-save-cookies") == 0) {
			dosavecookies = 0;
		}
		else if (strcmp(argv[argi], "--no-cookies") == 0) {
			dousecookies = 0;
		}
		else if (strcmp(argv[argi], "--shuffle") == 0) {
			shuffletests = 1;
		}
		else if (argnmatch(argv[argi], "--source-ip=")) {
			char *p = strchr(argv[argi], '=');
			struct in_addr aa;
			p++;
			if (inet_aton(p, &aa))
				defaultsourceip = strdup(p);
			else
				errprintf("Invalid source ip address '%s'\n", argv[argi]);
		}

		/* Options for PING tests */
		else if (argnmatch(argv[argi], "--ping-tasks=")) {
			/* Note: must check for this before checking "--ping" option */
			char *p = strchr(argv[argi], '=');
			pingchildcount = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--ping")) {
			char *p = strchr(argv[argi], '=');
			if (p) {
				p++; pingcolumn = p;
			}
			else pingcolumn = "";
		}
		else if (strcmp(argv[argi], "--noping") == 0) {
			pingcolumn = NULL;
		}
		else if (strcmp(argv[argi], "--trace") == 0) {
			dotraceroute = 1;
		}
		else if (strcmp(argv[argi], "--notrace") == 0) {
			dotraceroute = 0;
		}

		/* Options for HTTP tests */
		else if (argnmatch(argv[argi], "--content=")) {
			char *p = strchr(argv[argi], '=');
			contenttestname = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--bb-proxy-syntax") == 0) {
			/* Obey the Big Brother format for http proxy listed as part of the URL */
			obeybbproxysyntax = 1;
		}

		/* Options for SSL certificates */
		else if (argnmatch(argv[argi], "--ssl=")) {
			char *p = strchr(argv[argi], '=');
			ssltestname = strdup(p+1);
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
		else if (argnmatch(argv[argi], "--sslbits=")) {
			char *p = strchr(argv[argi], '=');
			p++; mincipherbits = atoi(p);
		}
		else if (argnmatch(argv[argi], "--validity=")) {
			char *p = strchr(argv[argi], '=');
			p++; validity = atoi(p);
		}
		else if (argnmatch(argv[argi], "--sslkeysize=")) {
			char *p = strchr(argv[argi], '=');
			p++; sslminkeysize = atoi(p);
		}
		else if (argnmatch(argv[argi], "--sni=")) {
			char *p = strchr(argv[argi], '=');
			p++; snienabled = ( (strcasecmp(p, "yes") == 0) || (strcasecmp(p, "on") == 0) || (strcasecmp(p, "enabled") == 0) || (strcasecmp(p, "true") == 0) || (strcasecmp(p, "1") == 0) );
		}
		else if (strcmp(argv[argi], "--no-cipherlist") == 0) {
			sslincludecipherlist = 0;
		}
		else if (strcmp(argv[argi], "--showallciphers") == 0) {
			sslshowallciphers = 1;
		}

		/* Debugging options */
		else if (argnmatch(argv[argi], "--dump")) {
			char *p = strchr(argv[argi], '=');

			if (p) {
				if (strcmp(p, "=before") == 0) dumpdata = 1;
				else if (strcmp(p, "=after") == 0) dumpdata = 2;
				else dumpdata = 3;
			}
			else dumpdata = 2;

			debug = 1;
		}
		else if (strcmp(argv[argi], "--no-update") == 0) {
			dontsendmessages = 1;
		}
		else if (strcmp(argv[argi], "--timing") == 0) {
			timing = 1;
		}

		/* Informational options */
		else if (strcmp(argv[argi], "--services") == 0) {
			servicedumponly = 1;
		}
		else if (strcmp(argv[argi], "--version") == 0) {
			printf("xymonnet version %s\n", VERSION);
			if (ssl_library_version) printf("SSL library : %s\n", ssl_library_version);
			if (ldap_library_version) printf("LDAP library: %s\n", ldap_library_version);
			printf("\n");
			return 0;
		}
		else if (standardoption(argv[argi])) {
			if (showhelp) {
				printf("Usage: %s [options] [host1 host2 host3 ...]\n", argv[0]);
				printf("General options:\n");
				printf("    --timeout=N                 : Timeout (in seconds) for service tests\n");
				printf("    --concurrency=N             : Number of tests run in parallel\n");
				printf("    --dns-timeout=N             : DNS lookups timeout and fail after N seconds [30]\n");
				printf("    --dns=[only|ip|standard]    : How IP's are decided\n");
				printf("    --no-ares                   : Use the system resolver library for hostname lookups\n");
				printf("    --dnslog=FILENAME           : Log failed hostname lookups to file FILENAME\n");
				printf("    --report[=COLUMNNAME]       : Send a status report about the running of xymonnet\n");
				printf("    --test-untagged             : Include hosts without a NET: tag in the test\n");
				printf("    --frequenttestlimit=N       : Seconds after detecting failures in which we poll frequently\n");
				printf("    --timelimit=N               : Warns if the complete test run takes longer than N seconds [TASKSLEEP]\n");
				printf("\nOptions for simple TCP service tests:\n");
				printf("    --checkresponse             : Check response from known services\n");
				printf("    --no-flags                  : Don't send extra xymonnet test flags\n");
				printf("\nOptions for PING (connectivity) tests:\n");
				printf("    --ping[=COLUMNNAME]         : Enable ping checking, default columname is \"conn\"\n");
				printf("    --noping                    : Disable ping checking\n");
				printf("    --trace                     : Run traceroute on all hosts where ping fails\n");
				printf("    --notrace                   : Disable traceroute when ping fails (default)\n");
				printf("    --ping-tasks=N              : Run N ping tasks in parallel (default N=1)\n");
				printf("\nOptions for HTTP/HTTPS (Web) tests:\n");
				printf("    --content=COLUMNNAME        : Define default columnname for CONTENT checks (content)\n");
				printf("    --no-cookies                : Disable reading and writing of cookies for any tests\n");
				printf("    --no-save-cookies           : Disable writing of per-session cookies received\n");
				printf("\nOptions for SSL certificate tests:\n");
				printf("    --ssl=COLUMNNAME            : Define columnname for SSL certificate checks (sslcert)\n");
				printf("    --no-ssl                    : Disable SSL certificate check\n");
				printf("    --sslwarn=N                 : Go yellow if certificate expires in less than N days (default:30)\n");
				printf("    --sslalarm=N                : Go red if certificate expires in less than N days (default:10)\n");
				printf("    --no-cipherlist             : Do not display SSL cipher data in the SSL certificate check\n");
				printf("    --showallciphers            : List all available ciphers supported by the local SSL library\n");
				printf("\nDebugging options:\n");
				printf("    --no-update                 : Send status messages to stdout instead of to Xymon\n");
				printf("    --timing                    : Trace the amount of time spent on each series of tests\n");
				printf("    --debug                     : Output debugging information\n");
				printf("    --dump[=before|=after|=all] : Dump internal memory structures before/after tests run\n");
				printf("    --maxdnsqueue=N             : Only queue N DNS lookups at a time\n");
				printf("\nInformational options:\n");
				printf("    --services                  : Dump list of known services and exit\n");
				printf("    --version                   : Show program version and exit\n");
				printf("    --help                      : Show help text and exit\n");

				return 0;
			}
		}
		else if (strncmp(argv[argi], "-", 1) == 0) {
			errprintf("Unknown option %s - try --help\n", argv[argi]);
		}
		else {
			/* Must be a hostname */
			if (selectedcount == 0) selectedhosts = (char **) malloc(argc*sizeof(char *));
			selectedhosts[selectedcount++] = strdup(argv[argi]);
		}
	}

	if (!dousecookies) dosavecookies=0;

	svctree = xtreeNew(strcasecmp);
	testhosttree = xtreeNew(strcasecmp);
	if (dousecookies) cookietree = xtreeNew(strcmp);
	init_timestamp();
	envcheck(reqenv);
	fqdn = get_fqdn();

	/* Setup SEGV handler */
	setup_signalhandler(egocolumn ? egocolumn : "xymonnet");

	/* Setup network filters for NET:locationname hosts */
	load_locations();

	if (pingcolumn && (strlen(pingcolumn) == 0)) pingcolumn = xgetenv("PINGCOLUMN");
	if (pingcolumn && xgetenv("IPTEST_2_CLEAR_ON_FAILED_CONN")) {
		failgoesclear = (strcmp(xgetenv("IPTEST_2_CLEAR_ON_FAILED_CONN"), "TRUE") == 0);
	}
	if (xgetenv("NETFAILTEXT")) failtext = strdup(xgetenv("NETFAILTEXT"));

	if (debug) {
		int i;
		printf("Command: xymonnet");
		for (i=1; (i<argc); i++) printf(" '%s'", argv[i]);
		printf("\n");
		printf("Environment BBLOCATION='%s'\n", textornull(xgetenv("BBLOCATION")));
		printf("Environment XYMONNETWORK='%s'\n", textornull(xgetenv("XYMONNETWORK")));
		printf("Environment XYMONNETWORKS='%s'\n", textornull(xgetenv("XYMONNETWORKS")));
		printf("Environment XYMONEXNETWORKS='%s'\n", textornull(xgetenv("XYMONEXNETWORKS")));
		printf("Environment CONNTEST='%s'\n", textornull(xgetenv("CONNTEST")));
		printf("Environment IPTEST_2_CLEAR_ON_FAILED_CONN='%s'\n", textornull(xgetenv("IPTEST_2_CLEAR_ON_FAILED_CONN")));
		printf("\n");
	}

	add_timestamp("xymonnet startup");

	load_services();
	if (servicedumponly) {
		dump_tcp_services();
		return 0;
	}

	dnstest = add_service("dns", getportnumber("domain"), 0, TOOL_DNS);
	add_service("ntp", getportnumber("ntp"),    0, TOOL_NTP);
	rpctest  = add_service("rpc", getportnumber("sunrpc"), 0, TOOL_RPCINFO);
	httptest = add_service("http", getportnumber("http"),  0, TOOL_HTTP);
	ldaptest = add_service("ldapurl", getportnumber("ldap"), strlen("ldap"), TOOL_LDAP);
	if (pingcolumn) pingtest = add_service(pingcolumn, 0, 0, TOOL_FPING);
	add_timestamp("Service definitions loaded");

	usebackfeedqueue = ((force_backfeedqueue >= 0) ? (sendmessage_init_local() > 0) : 0);
	if (force_backfeedqueue == 1 && usebackfeedqueue <= 0) {
		errprintf("Unable to set up backfeed queue when --bfq given, aborting run\n");
		return 0;
	}


	load_tests();
        if (loadhostsfromxymond && first_host() == NULL) {
                errprintf("Failed to load hostlist from xymond, aborting run\n");
                return 0;
        }
	add_timestamp(use_ares_lookup ? "Tests loaded" : "Tests loaded, hostname lookups done");

	flush_dnsqueue();
	if (use_ares_lookup) add_timestamp("DNS lookups completed");

	if (dumpdata & 1) { dump_hostlist(); dump_testitems(); }

	/* Ping checks first */
	if (pingtest && pingtest->items) pingrunning = (start_ping_service(pingtest) == 0);

	/* Load current status files */
	for (handle = xtreeFirst(svctree); handle != xtreeEnd(svctree); handle = xtreeNext(svctree, handle)) {
		s = (service_t *)xtreeData(svctree, handle);
		if (s != pingtest) load_test_status(s);
	}

	/* First run the TCP/IP and HTTP tests */
	for (handle = xtreeFirst(svctree); handle != xtreeEnd(svctree); handle = xtreeNext(svctree, handle)) {
		s = (service_t *)xtreeData(svctree, handle);
		if ((s->items) && (s->toolid == TOOL_CONTEST)) {
			char tname[128];

			for (t = s->items; (t); t = t->next) {
				if (!t->host->dnserror) {
					strcpy(tname, s->testname);
					if (s->namelen) tname[s->namelen] = '\0';
					t->privdata = (void *)add_tcp_test(ip_to_test(t->host), s->portnum, tname, NULL,
									   t->srcip,
									   NULL, t->silenttest, NULL, 
									   NULL, NULL, NULL);
				}
			}
		}
	}
	for (t = httptest->items; (t); t = t->next) add_http_test(t, dousecookies);
	add_timestamp("Test engine setup completed");

	do_tcp_tests(timeout, concurrency);
	add_timestamp("TCP tests completed");

	if (pingrunning) {
		char msg[512];

		finish_ping_service(pingtest); 
		sprintf(msg, "PING test completed (%d hosts)", pingcount);
		add_timestamp(msg);

		if (usebackfeedqueue)
			combo_start_local();
		else
			combo_start();

		send_results(pingtest, failgoesclear);
		if (selectedhosts == 0) save_ping_status();
		combo_end();
		add_timestamp("PING test results sent");
	}

	if (debug) {
		show_tcp_test_results();
		show_http_test_results(httptest);
	}

	for (handle = xtreeFirst(svctree); handle != xtreeEnd(svctree); handle = xtreeNext(svctree, handle)) {
		s = (service_t *)xtreeData(svctree, handle);
		if ((s->items) && (s->toolid == TOOL_CONTEST)) {
			for (t = s->items; (t); t = t->next) { 
				/*
				 * If the test fails due to DNS error, t->privdata is NULL
				 */
				if (t->privdata) {
					char *p;
					int i;
					tcptest_t *testresult = (tcptest_t *)t->privdata;

					t->open = testresult->open;
					t->banner = dupstrbuffer(testresult->banner);
					t->certinfo = testresult->certinfo;
					t->certissuer = testresult->certissuer;
					t->certexpires = testresult->certexpires;
					t->certkeysz = testresult->certkeysz;
					t->mincipherbits = testresult->mincipherbits;
					t->duration.tv_sec = testresult->duration.tv_sec;
					t->duration.tv_nsec = testresult->duration.tv_nsec;

					/* Binary data in banner ... */
					for (i=0, p=STRBUF(t->banner); (i < STRBUFLEN(t->banner)); i++, p++) {
						if (!isprint((int)*p) && !isspace((int)*p)) *p = '.';
					}
				}
			}
		}
	}
	for (t = httptest->items; (t); t = t->next) {
		if (t->privdata) {
			http_data_t *testresult = (http_data_t *)t->privdata;

			t->certinfo = testresult->tcptest->certinfo;
			t->certissuer = testresult->tcptest->certissuer;
			t->certexpires = testresult->tcptest->certexpires;
			t->certkeysz = testresult->tcptest->certkeysz;
			t->mincipherbits = testresult->tcptest->mincipherbits;
		}
	}

	add_timestamp("Test result collection completed");


	/* Run the ldap tests */
	for (t = ldaptest->items; (t); t = t->next) add_ldap_test(t);
	add_timestamp("LDAP test engine setup completed");

	run_ldap_tests(ldaptest, (ssltestname != NULL), timeout);
	add_timestamp("LDAP tests executed");

	if (debug) show_ldap_test_results(ldaptest);
	for (t = ldaptest->items; (t); t = t->next) {
		if (t->privdata) {
			ldap_data_t *testresult = (ldap_data_t *)t->privdata;

			t->certinfo = testresult->certinfo;
			t->certissuer = testresult->certissuer;
			t->mincipherbits = testresult->mincipherbits;
			t->certexpires = testresult->certexpires;
			t->certkeysz = testresult->certkeysz;
		}
	}
	add_timestamp("LDAP tests result collection completed");


	/* dns, ntp tests */
	for (handle = xtreeFirst(svctree); handle != xtreeEnd(svctree); handle = xtreeNext(svctree, handle)) {
		s = (service_t *)xtreeData(svctree, handle);
		if (s->items) {
			switch(s->toolid) {
				case TOOL_DNS:
					run_nslookup_service(s); 
					add_timestamp("DNS tests executed");
					break;
				case TOOL_NTP:
					run_ntp_service(s); 
					add_timestamp("NTP tests executed");
					break;
				case TOOL_RPCINFO:
					run_rpcinfo_service(s); 
					add_timestamp("RPC tests executed");
					break;
				default:
					break;
			}
		}
	}

	if (usebackfeedqueue)
		combo_start_local();
	else
		combo_start();

	for (handle = xtreeFirst(svctree); handle != xtreeEnd(svctree); handle = xtreeNext(svctree, handle)) {
		s = (service_t *)xtreeData(svctree, handle);
		switch (s->toolid) {
			case TOOL_CONTEST:
			case TOOL_DNS:
			case TOOL_NTP:
				send_results(s, failgoesclear);
				break;

			case TOOL_FPING:
			case TOOL_HTTP:
			case TOOL_LDAP:
				/* These handle result-transmission internally */
				break;

			case TOOL_RPCINFO:
				send_rpcinfo_results(s, failgoesclear);
				break;
		}
	}
	for (handle = xtreeFirst(testhosttree); (handle != xtreeEnd(testhosttree)); handle = xtreeNext(testhosttree, handle)) {
		h = (testedhost_t *)xtreeData(testhosttree, handle);
		send_http_results(httptest, h, h->firsthttp, nonetpage, failgoesclear, usebackfeedqueue, dosavecookies);
		send_content_results(httptest, h, nonetpage, contenttestname, failgoesclear);
		send_ldap_results(ldaptest, h, nonetpage, failgoesclear);
		if (ssltestname && !h->nosslcert) send_sslcert_status(h);
	}

	combo_end();

	add_timestamp("Test results transmitted");

	/*
	 * The list of hosts to test frequently because of a failure must
	 * be saved - it is then picked up by the frequent-test ext script
	 * that runs xymonnet again with the frequent-test hosts as
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
		for (handle = xtreeFirst(svctree); handle != xtreeEnd(svctree); handle = xtreeNext(svctree, handle)) {
			s = (service_t *)xtreeData(svctree, handle);
			if (s != pingtest) save_test_status(s);
		}
		/* Save frequent-test list */
		save_frequenttestlist(argc, argv);
	}

	/* Save session cookies - every time */
	if (dosavecookies) save_session_cookies();

	shutdown_ldap_library();
	add_timestamp("xymonnet completed");

	if (dumpdata & 2) { dump_hostlist(); dump_testitems(); }

	/* Tell about us */
	if (egocolumn) {
		char msgline[4096];
		char *timestamps;
		int color;

		/* Go yellow if it runs for too long */
		if ((runtimewarn > 0) && (total_runtime() > runtimewarn)) {
			errprintf("WARNING: Runtime %ld longer than time limit (%ld)\n", total_runtime(), runtimewarn);
		}
		color = (errbuf ? COL_YELLOW : COL_GREEN);
		if (bigfailure) color = COL_RED;

		if (usebackfeedqueue) combo_start_local(); else combo_start();
		init_status(color);
		sprintf(msgline, "status+%d %s.%s %s %s - xymonnet completed in %lds\n\n", validity, xgetenv("MACHINE"), egocolumn, colorname(color), timestamp, total_runtime());
		addtostatus(msgline);

		sprintf(msgline, "xymonnet version %s\n", VERSION);
		addtostatus(msgline);
		if (ssl_library_version) {
			sprintf(msgline, "SSL library : %s\n", ssl_library_version);
			addtostatus(msgline);
		}
		if (ldap_library_version) {
			sprintf(msgline, "LDAP library: %s\n", ldap_library_version);
			addtostatus(msgline);
		}

		sprintf(msgline, "\nStatistics:\n Hosts total           : %8d\n Hosts with no tests   : %8d\n Total test count      : %8d\n", 
			hostcount, notesthostcount, testcount);
		addtostatus(msgline);
		sprintf(msgline, "\nDNS statistics:\n # hostnames resolved  : %8d\n # successful          : %8d\n # failed              : %8d\n # calls to dnsresolve : %8d\n",
			dns_stats_total, dns_stats_success, dns_stats_failed, dns_stats_lookups);
		addtostatus(msgline);
		sprintf(msgline, "\nTCP test statistics:\n # TCP tests total     : %8d\n # HTTP tests          : %8d\n # Simple TCP tests    : %8d\n # Connection attempts : %8d\n # bytes written       : %8ld\n # bytes read          : %8ld\n",
			tcp_stats_total, tcp_stats_http, tcp_stats_plain, tcp_stats_connects, 
			tcp_stats_written, tcp_stats_read);
		addtostatus(msgline);

		if (errbuf) {
			addtostatus("\n\nError output:\n");
			addtostatus(prehtmlquoted(errbuf));
		}

		show_timestamps(&timestamps);
		addtostatus(timestamps);

		finish_status();
		combo_end();
	}
	else show_timestamps(NULL);

	if (dnsfaillog) fclose(dnsfaillog);

	if (usebackfeedqueue) sendmessage_finish_local();

	return 0;
}

