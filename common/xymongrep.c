/*----------------------------------------------------------------------------*/
/* Xymon hosts.cfg file grep'er                                               */
/*                                                                            */
/* This tool will pick out the hosts from a hosts.cfg file that has one of    */
/* the tags given on the command line. This allows an extension script to     */
/* deal with only the relevant parts of the hosts.cfg file, instead of        */
/* having to parse the entire file.                                           */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "version.h"
#include "libxymon.h"

static char *connstatus = NULL;
static char *teststatus = NULL;
static char *conncolumn = "conn";
static char *testcolumn = NULL;


static void load_hoststatus()
{
	int res;
	char msg[1024];
	sendreturn_t *sres;

	sprintf(msg, "xymondboard fields=hostname,testname,color test=%s", conncolumn);
	sres = newsendreturnbuf(1, NULL);
	res = sendmessage(msg, NULL, XYMON_TIMEOUT, sres);
	if (res == XYMONSEND_OK) connstatus = getsendreturnstr(sres, 1);

	if ((res == XYMONSEND_OK) && testcolumn) {
		sprintf(msg, "xymondboard fields=hostname,testname,color test=%s", testcolumn);
		res = sendmessage(msg, NULL, XYMON_TIMEOUT, sres);
		if (res == XYMONSEND_OK) teststatus = getsendreturnstr(sres, 1);
	}

	if (res != XYMONSEND_OK) {
		errprintf("Cannot fetch Xymon status, ignoring --no-down\n");
		connstatus = NULL;
		teststatus = NULL;
	}

	freesendreturnbuf(sres);
}


static int netok(char *netstring, char *curnet, int testuntagged)
{
	return ( (netstring == NULL) || 
		 (curnet && netstring && (strcmp(curnet, netstring) == 0)) || 
		 (testuntagged && (curnet == NULL)) );
}

static int downok(char *hostname, int nodownhosts)
{
	char *mark, *colorstr;
	int color;

	if (!nodownhosts) return 1;

	/* Check if the host is down (i.e. "conn" test is non-green) */
	if (!connstatus) return 1;
	mark = (char *)malloc(strlen(hostname) + strlen(conncolumn) + 4);
	sprintf(mark, "\n%s|%s|", hostname, conncolumn);
	colorstr = strstr(connstatus, mark);
	if (colorstr) {
		colorstr += strlen(mark);	/* Skip to the color data */
	}
	else if (strncmp(connstatus, mark+1, strlen(mark+1)) == 0) {
		colorstr = connstatus + strlen(mark+1);	/* First entry we get */
	}
	xfree(mark);
	color = (colorstr ? parse_color(colorstr) : COL_GREEN);
	if ((color == COL_RED) || (color == COL_BLUE)) return 0;

	/* Check if the test is currently disabled */
	if (!teststatus) return 1;
	mark = (char *)malloc(strlen(hostname) + strlen(testcolumn) + 4);
	sprintf(mark, "\n%s|%s|", hostname, testcolumn);
	colorstr = strstr(teststatus, mark);
	if (colorstr) {
		colorstr += strlen(mark);	/* Skip to the color data */
	}
	else if (strncmp(teststatus, mark+1, strlen(mark+1)) == 0) {
		colorstr = teststatus + strlen(mark+1);	/* First entry we get */
	}
	xfree(mark);
	color = (colorstr ? parse_color(colorstr) : COL_GREEN);
	if ((color == COL_RED) || (color == COL_BLUE)) return 0;

	return 1;
}

int main(int argc, char *argv[])
{ 
	void *hwalk;
	char *hostsfn = NULL;
	char *netstring = NULL;
	char *include2 = NULL;
	int extras = 1;
	int testuntagged = 0;
	int nodownhosts = 0;
	int onlypreferredentry = 0;
	char *p;
	char **lookv;
	int argi, lookc;
	strbuffer_t *wantedtags;

	libxymon_init(argv[0]);

	if ((argc <= 1) || (strcmp(argv[1], "--help") == 0)) {
		printf("Usage:\n%s test1 [test1] [test2] ... \n", argv[0]);
		exit(1);
	}

	lookv = (char **)malloc(argc*sizeof(char *));
	lookc = 0;

	hostsfn = xgetenv("HOSTSCFG");
	conncolumn = xgetenv("PINGCOLUMN");

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--noextras") == 0) {
			extras = 0;
		}
		else if (strcmp(argv[argi], "--test-untagged") == 0) {
			testuntagged = 1;
		}
		else if (argnmatch(argv[argi], "--no-down")) {
			char *p;
			nodownhosts = 1;
			p = strchr(argv[argi], '=');
			if (p) testcolumn = strdup(p+1);
		}
		else if ((strcmp(argv[argi], "--net") == 0) || (strcmp(argv[argi], "--bbnet") == 0)) {
			include2 = "netinclude";
			onlypreferredentry = 0;
		}
		else if ((strcmp(argv[argi], "--web") == 0) || (strcmp(argv[argi], "--bbdisp") == 0)) {
			include2 = "dispinclude";
			onlypreferredentry = 1;
		}
		else if (argnmatch(argv[argi], "--hosts=")) {
			hostsfn = strchr(argv[argi], '=') + 1;
		}
		else if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
		else {
			lookv[lookc] = strdup(argv[argi]);
			lookc++;
		}
	}
	lookv[lookc] = NULL;

	if ((hostsfn == NULL) || (strlen(hostsfn) == 0)) {
		errprintf("Environment variable HOSTSCFG is not set - aborting\n");
		exit(2);
	}

	load_hostnames(hostsfn, include2, get_fqdn());
	if (first_host() == NULL) {
		errprintf("Cannot load %s, or file is empty\n", hostsfn);
		exit(3);
	}

	/* If we must avoid downed or disabled hosts, let's find out what those are */
	if (nodownhosts) load_hoststatus();

	/* Each network test tagged with NET:locationname */
	p = xgetenv("XYMONNETWORK");
	if ((p == NULL) || (strlen(p) == 0)) p = xgetenv("BBLOCATION");
	if (p && strlen(p)) netstring = strdup(p);

	hwalk = first_host();
	wantedtags = newstrbuffer(0);
	while (hwalk) {
		char *hostip = NULL;
		char *curnet = xmh_item(hwalk, XMH_NET);
		char *curname = xmh_item(hwalk, XMH_HOSTNAME);

		/* 
		 * Only look at the hosts whose NET: definition matches the wanted one.
		 * Must also check if the host is currently down (not responding to ping).
		 * And if the host is OK with knownhost(), because it may be time-limited.
		 */
		if (netok(netstring, curnet, testuntagged) && downok(curname, nodownhosts) && knownhost(curname, &hostip, GH_IGNORE)) {
			char *item;

			clearstrbuffer(wantedtags);
			for (item = xmh_item_walk(hwalk); (item); item = xmh_item_walk(NULL)) {
				int i;
				char *realitem = item + strspn(item, "!~?");

				for (i=0; lookv[i]; i++) {
					char *outitem = NULL;

					if (lookv[i][strlen(lookv[i])-1] == '*') {
						if (strncasecmp(realitem, lookv[i], strlen(lookv[i])-1) == 0) {
							outitem = (extras ? item : realitem);
						}
					}
					else if (strcasecmp(realitem, lookv[i]) == 0) {
						outitem = (extras ? item : realitem);
					}

					if (outitem) {
						int needquotes = ((strchr(outitem, ' ') != NULL) || (strchr(outitem, '\t') != NULL));
						addtobuffer(wantedtags, " ");
						if (needquotes) addtobuffer(wantedtags, "\"");
						addtobuffer(wantedtags, outitem);
						if (needquotes) addtobuffer(wantedtags, "\"");
					}
				}
			}

			if (STRBUF(wantedtags) && (*STRBUF(wantedtags) != '\0') && extras) {
				if (xmh_item(hwalk, XMH_FLAG_DIALUP)) addtobuffer(wantedtags, " dialup");
				if (xmh_item(hwalk, XMH_FLAG_TESTIP)) addtobuffer(wantedtags, " testip");
			}

			if (STRBUF(wantedtags) && *STRBUF(wantedtags)) {
				printf("%s %s #%s\n", xmh_item(hwalk, XMH_IP), xmh_item(hwalk, XMH_HOSTNAME), STRBUF(wantedtags));
			}
		}

		do { hwalk = next_host(hwalk, 1); } while (hwalk && onlypreferredentry && (strcmp(curname, xmh_item(hwalk, XMH_HOSTNAME)) == 0));
	}

	return 0;
}

