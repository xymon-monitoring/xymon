/*----------------------------------------------------------------------------*/
/* Hobbit monitor network test tool.                                          */
/*                                                                            */
/* Copyright (C) 2003-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __BBTEST_NET_H__
#define __BBTEST_NET_H__

#include <sys/time.h>

#define STATUS_CONTENTMATCH_NOFILE 901
#define STATUS_CONTENTMATCH_FAILED 902
#define STATUS_CONTENTMATCH_BADREGEX 903

#define MAX_CONTENT_DATA (1024*1024)	/* 1 MB should be enough for most */

/*
 * Structure of the bbtest-net in-memory records
 *
 *  +->service_t
 *  |      testname
 *  |      namelen
 *  |      portnum
 *  |      toolid
 *  |      items --------------> testitem_t  <----------------------------------------------+
 *  |      next             +------- service                                                |
 *  |                       |        host ---------------+
 *  +-----------------------+        testspec            |                                  |
 *                                   dialup              +-->testedhost_t <------------+    |
 *                                   reverse                     hostname              |    |
 *                                   silenttest                  ip                    |    |
 *                                   alwaystrue                  dialup                |    |
 *                                   open                        testip                |    |
 *                                   banner                      nosslcert             |    |
 *                                   certinfo                    dodns                 |    |
 *                                   duration                    dnserror              |    |
 *                                   badtest                     //////////            |    |
 *                                   downcount                   repeattest            |    |
 *                                   downstart                   noconn                |    |
 *                                   privdata ----+              noping                |    |
 *                                   next         |              badconn               |    |
 *                                                |              downcount             |    |
 *                                                |              downstart             |    |
 *                                                |              routerdeps            |    |
 *                                                |              deprouterdown --------+    |
 *                                                |              firsthttp -----------------+
 *                                                |              firstldap -----------------+
 *                                                |              ldapuser
 *                                                |              ldappasswd
 *                                                |              sslwarndays
 *                                                |              sslalarmdays
 *                                                |
 *                                                +---------><test private struct>
 */

typedef struct service_t {
	char *testname;		/* Name of the test = column name in Hobbit report */
	int namelen;		/* Length of name - "testname:port" has this as strlen(testname), others 0 */
	int portnum;		/* Port number this service runs on */
	int toolid;		/* Which tool to use */
	struct testitem_t *items; /* testitem_t linked list of tests for this service */
} service_t;

#define MULTIPING_BEST 0
#define MULTIPING_WORST 1
typedef struct ipping_t {
	char *ip;
	int  open;
	strbuffer_t *banner;
	struct ipping_t *next;
} ipping_t;

typedef struct extraping_t {
	int   matchtype;
	ipping_t *iplist;
} extraping_t;

typedef struct testedhost_t {
	char *hostname;
	char ip[IP_ADDR_STRLEN];
	int dialup;		/* dialup flag (if set, failed tests report as clear) */
	int testip;		/* testip flag (dont do dns lookups on hostname) */
	int nosslcert;		/* nosslcert flag */
	int hidehttp;		/* hidehttp flag */
	int dodns;              /* set while loading tests if we need to do a DNS lookup */
	int dnserror;		/* set internally if we cannot find the host's IP */
	int repeattest;         /* Set if this host goes on the quick poll list */
	char *hosttype;         /* For the "Intermediate HOSTTYPE is down" message */

	/* The following are for the connectivity test */
	int noconn;		/* noconn flag (dont report "conn" at all */
	int noping;		/* noping flag (report "conn" as clear=disabled */
	int badconn[3];		/* badconn:x:y:z flag */
	int downcount;		/* number of successive failed conn tests */
	time_t downstart;	/* time() of first conn failure */
	char *routerdeps;       /* Hosts from the "router:" tag */
	struct testedhost_t *deprouterdown;    /* Set if dependant router is down */
	int dotrace;		/* Run traceroute for this host */
	strbuffer_t *traceroute;/* traceroute results */
	struct extraping_t *extrapings;

	/* The following is for the HTTP/FTP URL tests */
	struct testitem_t *firsthttp;	/* First HTTP testitem in testitem list */

	/* The following is for the LDAP tests */
	struct testitem_t *firstldap;	/* First LDAP testitem in testitem list */
	char   *ldapuser;		/* Username */
	char   *ldappasswd;		/* Password */
	int    ldapsearchfailyellow;    /* Go red or yellow on failed search */

	/* The following is for the SSL certificate checks */
	int  sslwarndays;
	int  sslalarmdays;

	/* For storing the test dependency tag. */
	char *deptests;
} testedhost_t;

typedef struct testitem_t {
	struct testedhost_t *host;	/* Pointer to testedhost_t record for this host */
	struct service_t *service;	/* Pointer to service_t record for the service to test */

	char		*testspec;      /* Pointer to the raw testspec in bb-hosts */
	int		reverse;	/* "!testname" flag */
	int		dialup;		/* "?testname" flag */
	int		alwaystrue;	/* "~testname" flag */
	int		silenttest;	/* "testname:s" flag */
	int             senddata;       /* For tests that merely generate a "data" report */

	/* These data may be filled in from the test engine private data */
	int		open;		/* Is the service open ? NB: Shows true state of service, ignores flags */
	strbuffer_t	*banner;
	char		*certinfo;
	time_t		certexpires;
	struct timeval	duration;

	/* For badTEST handling: Need to track downtime duration and poll count */
	int		badtest[3];
	time_t		downstart;
	int		downcount;	/* Number of polls when down. */

	/* Each test engine has its own data */
	void		*privdata;	/* Private data use by test tool */

	struct testitem_t *next;
} testitem_t;

typedef struct modembank_t {
	char		*hostname;
	unsigned int 	startip;	/* Saved as 32-bit binary */
	int		banksize;
	int		*responses;
} modembank_t;

typedef struct dnstest_t {
	int	testcount;
	int	okcount;
} dnstest_t;

extern char *deptest_failed(testedhost_t *host, char *testname);

#endif

