/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for xymonnet collector                               */
/*                                                                            */
/* Copyright (C) 2009-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char netcollect_rcsid[] = "$Id: snmpcollect.c 6712 2011-07-31 21:01:52Z storner $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>

#include "libxymon.h"
#include "xymond_worker.h"
#include "sections.h"

typedef struct connectresult_t {
	char *testspec;
	enum { NETCOLLECT_UNKNOWN, NETCOLLECT_OK, NETCOLLECT_FAILED, NETCOLLECT_TIMEOUT, NETCOLLECT_RESOLVERROR, NETCOLLECT_SSLERROR, NETCOLLECT_BADDATA } status;
	float elapsedms;
	char *sslsubject;
	time_t sslexpires;
	char *plainlog, *httpheaders, *httpbody;
	int httpstatus;
	int ntpstratum;
	float ntpoffset;
	int interval;
} connectresult_t;

typedef struct hostresults_t {
	char *hostname;
	connectresult_t *ping;
	void *results; /* Tree of connectresult_t records, keyed by testspec */
} hostresults_t;

static void *hostresults = NULL; /* Tree of hostresults_t records, keyed by hostname. So a tree of trees. */

static char *sizedstr(char *sbegin, char *eoln)
{
	char *result;
	int sz;

	sz = atoi(strchr(sbegin, ':') + 1);

	result = (char *)malloc(sz + 1);
	memcpy(result, eoln+1, sz);
	*(result+sz) = '\0';
	return result;
}

void handle_netcollect_client(char *hostname, char *clienttype, enum ostype_t os, 
				void *hinfo, char *sender, time_t timestamp,
				char *clientdata)
{
	void *ns1var, *ns1sects;
	char *onetest, *testspec;

	if (!hostresults) hostresults = xtreeNew(strcasecmp);

	onetest = nextsection_r(clientdata, &testspec, &ns1var, &ns1sects);
	while (onetest) {
		xtreePos_t handle;
		hostresults_t *hrec;
		connectresult_t *rec;
		char *bol, *eoln;

		handle = xtreeFind(hostresults, hostname);
		if (handle == xtreeEnd(hostresults)) {
			hrec = (hostresults_t *)calloc(1, sizeof(hostresults_t));
			hrec->hostname = strdup(hostname);
			hrec->results = xtreeNew(strcmp);
			xtreeAdd(hostresults, hrec->hostname, hrec);
		}
		else {
			hrec = (hostresults_t *)xtreeData(hostresults, handle);
		}

		handle = xtreeFind(hrec->results, testspec);
		if (handle == xtreeEnd(hrec->result)) {
			rec = (connectresult_t *)calloc(1, sizeof(connectresult_t));
			rec->testspec = strdup(testspec);
			xtreeAdd(hrec->results, rec->testspec, rec);
		}
		else {
			rec = (connectresult_t *)xtreeData(hrec->results, handle);
		}

		if (strcmp(testspec, "ping") == 0) hrec->ping = rec;

		rec->status = NETCOLLECT_UNKNOWN;
		if (rec->sslsubject) xfree(rec->sslsubject);
		if (rec->plainlog) xfree(rec->plainlog);
		if (rec->httpheaders) xfree(rec->httpheaders);
		if (rec->httpbody) xfree(rec->httpbody);
		rec->elapsedms = 0.0; rec->sslexpires = 0; rec->httpstatus = 0; rec->ntpstratum = 0; rec->ntpoffset = 0.0; rec->interval = 0;

		bol = onetest;
		while (bol) {
			eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
			if (argnmatch(bol, "Status: ")) {
				char *s = strchr(bol, ':') + 2;

				if (strcmp(s, "OK") == 0) rec->status = NETCOLLECT_OK;
				else if (strcmp(s, "CONN_FAILED") == 0) rec->status = NETCOLLECT_FAILED;
				else if (strcmp(s, "CONN_TIMEOUT") == 0) rec->status = NETCOLLECT_TIMEOUT;
				else if (strcmp(s, "BADSSLHANDSHAKE") == 0) rec->status = NETCOLLECT_SSLERROR;
				else if (strcmp(s, "CANNOT_RESOLVE") == 0) rec->status = NETCOLLECT_RESOLVERROR;
				else if (strcmp(s, "BADDATA") == 0) rec->status = NETCOLLECT_BADDATA;
				else if (strcmp(s, "INTERRUPTED") == 0) rec->status = NETCOLLECT_FAILED;
				else if (strcmp(s, "MODULE_FAILED") == 0) rec->status = NETCOLLECT_FAILED;
			}
			else if (argnmatch(bol, "PeerCertificateSubject: ")) rec->sslsubject = strdup(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "PLAINlog: ")) rec->plainlog = sizedstr(bol, eoln);
			else if (argnmatch(bol, "HTTPheaders: ")) rec->httpheaders = sizedstr(bol, eoln);
			else if (argnmatch(bol, "HTTPbody: ")) rec->httpbody = sizedstr(bol, eoln);
			else if (argnmatch(bol, "HTTPstatus: ")) rec->httpstatus = atoi(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "ElapsedMS: ")) rec->elapsedms = atof(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "IntervalMS: ")) rec->interval = atoi(strchr(bol, ':') + 2) / 1000;
			else if (argnmatch(bol, "PeerCertificateExpiry: ")) rec->sslexpires = atoi(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "NTPstratum: ")) rec->ntpstratum = atoi(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "NTPoffset: ")) rec->ntpoffset = atof(strchr(bol, ':') + 2);

			if (eoln) *eoln = '\n';
			bol = (eoln ? eoln+1 : NULL);
		}

sectiondone:
		onetest = nextsection_r(NULL, &testspec, &ns1var, &ns1sects);
	}
	nextsection_r_done(ns1sects);
}


int decide_color(connectresult_t *crec, int ispingtest, int noping, int pingisdown, int isdialup, int isreverse, int isforced, int failgoesclear, char *cause)
{
	int color = COL_GREEN;

	*cause = '\0';

	if (ispingtest) {
		if (noping) {
			/* Ping test disabled - go "clear". End of story. */
			strcpy(cause, "Ping test disabled (noping)");
			return COL_CLEAR; 
		}

		/* Red if (STATUS=OK, reverse=1) or (status=FAILED, reverse=0) */
		switch (crec->status) {
		  case NETCOLLECT_OK:
			if (isreverse) {
				sprintf(cause, "Host does respond to ping");
				color = COL_RED;
			}
			break;

		  case NETCOLLECT_FAILED:
		  case NETCOLLECT_TIMEOUT:
		  case NETCOLLECT_BADDATA:
			if (!isreverse) {
				sprintf(cause, "Host does not respond to ping");
				color = COL_RED;
			}
			break;

		  case NETCOLLECT_RESOLVERROR:
			strcpy(cause, "DNS lookup failure");
			color = COL_RED;
			break;

		  default:
			strcpy(cause, "Xymon pingtest failed");
			color = COL_CLEAR;
			break;
		}
#if 0
			if (test->host->extrapings) {
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
						  sprintf(cause, "Host does not respond to ping on any of %d IP's", 
							  totalcount);
					  }
					  break;
				  case MULTIPING_WORST:
					  if (okcount < totalcount) {
						  color = COL_RED;
						  sprintf(cause, "Host responds to ping on %d of %d IP's",
							  okcount, totalcount);
					  }
					  break;
				}
			}
#endif
	}
	else {
		/* TCP test */
		if (isreverse) {
			/*
			 * Reverse tests go RED when open.
			 * If not open, they may go CLEAR if the ping test failed
			 */

			if (crec->status == NETCOLLECT_OK) {
				strcpy(cause, "Service responds when it should not");
				color = COL_RED;
			}
			else if (failgoesclear && pingisdown && !isforced) {
				strcpy(cause, "Host appears to be down");
				color = COL_CLEAR;
			}
		}
		else {
			if (crec->status != NETCOLLECT_OK) {
				if (failgoesclear && pingisdown && !isforced) {
					strcpy(cause, "Host appears to be down");
					color = COL_CLEAR;
				}
				else {
					strcpy(cause, "Service unavailable");
					color = COL_RED;
				}

				switch (crec->status) {
				  case NETCOLLECT_TIMEOUT: strcat(cause, " (connect timeout)"); break;
				  case NETCOLLECT_FAILED: strcat(cause, " (connection failed)"); break;
		  		  case NETCOLLECT_RESOLVERROR: strcat(cause, " (DNS error)"); break;
				  case NETCOLLECT_SSLERROR: strcat(cause, " (SSL error)"); break;
		  		  case NETCOLLECT_BADDATA: strcpy(cause, "Unexpected service response"); break;
				  case NETCOLLECT_UNKNOWN: strcat(cause, " (Xymon error)"); break;
				  case NETCOLLECT_OK: break;
				}
			}
		}
	}


	/* Dialup hosts and dialup tests report red as clear */
	if ( ((color == COL_RED) || (color == COL_YELLOW)) && isdialup && !isreverse) { 
		strcat(cause, "\nDialup host or service");
		color = COL_CLEAR;
	}

	return color;
}


void netcollect_generate_updates(void)
{
	void *hwalk;
	char *pingcolumn;
	int failgoesclear = 1;
	char *failtext = "not OK";
	char causetext[1024];

	pingcolumn = xgetenv("PINGCOLUMN"); if (!pingcolumn) pingcolumn = "conn";
	if (xgetenv("IPTEST_2_CLEAR_ON_FAILED_CONN")) failgoesclear = (strcmp(xgetenv("IPTEST_2_CLEAR_ON_FAILED_CONN"), "TRUE") == 0);
	if (xgetenv("NETFAILTEXT")) failtext = xgetenv("NETFAILTEXT");

	init_timestamp();

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		xtreePos_t handle;
		hostresults_t *hrec;
		char *tag;

		handle = xtreeFind(hostresults, xmh_item(hwalk, XMH_HOSTNAME));
		if (handle == xtreeEnd(hostresults)) continue;

		hrec = xtreeData(hostresults, handle);

		for (tag = xmh_item_walk(hwalk); (tag); tag = xmh_item_walk(NULL)) {
			connectresult_t *crec;
			char *testspec;
			int isdialup, isreverse, isforced;
			char *p;
			int color;
			char msgline[4096], msgtext[4096];

			handle = xtreeFind(hrec->results, tag);
			if (handle == xtreeEnd(hrec->results)) continue;

			crec = (connectresult_t *)xtreeData(hrec->results, handle);

			testspec = NULL;
			isdialup = (xmh_item(hwalk, XMH_FLAG_DIALUP) != NULL);
			isreverse = isforced = 0;
			for (p = crec->testspec; (!testspec); p++) {
				switch (*p) {
				  case '?' : isdialup = 1; break;
				  case '!' : isreverse = 1; break;
				  case '~' : isforced = 1; break;
				  default: testspec = p; break;
				}
			}

			if (argnmatch(testspec, "ldap://") || argnmatch(testspec, "ldaps://") ||argnmatch(testspec, "ldaptls://")) {
				/* LDAP */
			}
			else if ( argnmatch(testspec, "http")         ||
				  argnmatch(testspec, "content=http") ||
				  argnmatch(testspec, "cont;http")    ||
				  argnmatch(testspec, "cont=")        ||
				  argnmatch(testspec, "nocont;http")  ||
				  argnmatch(testspec, "nocont=")      ||
				  argnmatch(testspec, "post;http")    ||
				  argnmatch(testspec, "post=")        ||
				  argnmatch(testspec, "nopost;http")  ||
				  argnmatch(testspec, "nopost=")      ||
				  argnmatch(testspec, "soap;http")    ||
				  argnmatch(testspec, "soap=")        ||
				  argnmatch(testspec, "nosoap;http")  ||
				  argnmatch(testspec, "nosoap=")      ||
				  argnmatch(testspec, "type;http")    ||
				  argnmatch(testspec, "type=")        )      {
				/* HTTP */
			}
			else if (argnmatch(testspec, "apache") || argnmatch(testspec, "apache=")) {
				/* Apache data report */
			}
			else {
				/* Standard net test */
				char flags[5];

				flags[0] = (isdialup ? 'D' : 'd');
				flags[1] = (isreverse ? 'R' : 'r');
				flags[2] = '\0';

				color = decide_color(crec,
							(hrec->ping == crec),
							(xmh_item(hwalk, XMH_FLAG_NOPING) != NULL),
							(hrec->ping && (hrec->ping->status == NETCOLLECT_FAILED)),
							isdialup,
							isreverse,
							isforced,
							failgoesclear, causetext);

				init_status(color);
				sprintf(msgline, "status+%d %s.%s %s <!-- [flags:%s] --> %s %s %s ", 
					crec->interval/10, xmh_item(hwalk, XMH_HOSTNAME), testspec, colorname(color), 
					flags, timestamp, testspec, 
					( ((color == COL_RED) || (color == COL_YELLOW)) ? "NOT ok" : "ok"));

				if (crec->status == NETCOLLECT_RESOLVERROR) {
					strcat(msgline, ": DNS lookup failed");
					sprintf(msgtext, "\nUnable to resolve hostname %s\n\n", xmh_item(hwalk, XMH_HOSTNAME));
				}
				else {
					sprintf(msgtext, "\nService %s on %s is ", testspec, xmh_item(hwalk, XMH_HOSTNAME));
					switch (color) {
					  case COL_GREEN: 
						strcat(msgtext, "OK ");
						strcat(msgtext, (isreverse ? "(down)" : "(up)"));
						strcat(msgtext, "\n");
						break;
					  case COL_RED:
					  case COL_YELLOW:
						sprintf(msgtext+strlen(msgtext), "%s : %s\n", failtext, causetext);
						break;
					  case COL_CLEAR:
						strcat(msgtext, "OK\n");
						if (crec == hrec->ping) {
							if (xmh_item(hwalk, XMH_FLAG_NOPING)) {
								strcat(msgline, ": Disabled");
								strcat(msgtext, "Ping check disabled (noping)\n");
							}
							else if (isdialup) {
								strcat(msgline, ": Disabled (dialup host)");
								strcat(msgtext, "Dialup host\n");
							}
						}
						else {
							/* Non-ping test clear: Dialup test or failed ping */
							strcat(msgline, ": Ping failed, or dialup host/service");
							strcat(msgtext, "Dialup host/service, or test depends on another failed test\n");
							strcat(msgtext, causetext);
						}
						break;
					}
				}

				strcat(msgline, "\n");
				addtostatus(msgline);
				addtostatus(msgtext);

				if (crec->plainlog) {
					addtostatus("\n"); addtostatus(crec->plainlog); addtostatus("\n");
				}

				sprintf(msgtext, "\nSeconds: %.2f\n", crec->elapsedms / 1000);
				addtostatus(msgtext);

				addtostatus("\n\n");

				finish_status();
			}
		}
	}
}

#define MAX_META 20	/* The maximum number of meta-data items in a message */


int main(int argc, char *argv[])
{
	char *msg;
	int running;
	int argi, seq;
	struct timespec timeout = { 10, 0 };
	time_t nextconfigload = 0;

	/* Handle program options. */
	for (argi = 1; (argi < argc); argi++) {
		if (standardoption(argv[0], argv[argi])) {
			if (showhelp) return 0;
		}
	}

	save_errbuf = 0;

	running = 1;
	while (running) {
		char *eoln, *restofmsg, *p;
		char *metadata[MAX_META+1];
		int metacount;

		if (gettimer() > nextconfigload) {
			load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
			nextconfigload = gettimer() + 600;
		}

		msg = get_xymond_message(C_LAST, argv[0], &seq, &timeout);
		if (msg == NULL) {
			running = 0;
			continue;
		}

 		eoln = strchr(msg, '\n');
		if (eoln) {
			*eoln = '\0';
			restofmsg = eoln+1;
		}
		else {
			restofmsg = "";
		}

		metacount = 0; 
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < MAX_META)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;

		/*
		 * A "shutdown" message is sent when the master daemon
		 * terminates. The child workers should shutdown also.
		 */
		if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			printf("Shutting down\n");
			running = 0;
			continue;
		}

		/*
		 * A "logrotate" message is sent when the Xymon logs are
		 * rotated. The child workers must re-open their logfiles,
		 * typically stdin and stderr - the filename is always
		 * provided in the XYMONCHANNEL_LOGFILENAME environment.
		 */
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("XYMONCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				freopen(fn, "a", stdout);
				freopen(fn, "a", stderr);
			}
			continue;
		}

		/*
		 * "reload" means the hosts.cfg file has changed.
		 */
		else if (strncmp(metadata[0], "@@reload", 8) == 0) {
			nextconfigload = 0;
			continue;
		}

		/*
		 * An "idle" message appears when get_xymond_message() 
		 * exceeds the timeout setting (ie. you passed a timeout
		 * value). This allows your worker module to perform
		 * some internal processing even though no messages arrive.
		 */
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			netcollect_generate_updates();
		}

		/*
		 * The "drophost", "droptest", "renamehost" and "renametst"
		 * indicates that a host/test was deleted or renamed. If the
		 * worker module maintains some internal storage (in memory
		 * or persistent file-storage), it should act on these
		 * messages to maintain data consistency.
		 */
		else if ((metacount > 3) && (strncmp(metadata[0], "@@drophost", 10) == 0)) {
			// printf("Got a 'drophost' message for host '%s'\n", metadata[3]);
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@droptest", 10) == 0)) {
			// printf("Got a 'droptest' message for host '%s' test '%s'\n", metadata[3], metadata[4]);
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@renamehost", 12) == 0)) {
			// printf("Got a 'renamehost' message for host '%s' -> '%s'\n", metadata[3], metadata[4]);
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			// printf("Got a 'renametest' message for host '%s' test '%s' -> '%s'\n", 
			// 	metadata[3], metadata[4], metadata[5]);
		}
		else if ((metacount > 4) && (strncmp(metadata[0], "@@client", 8) == 0)) {
			time_t timestamp = atoi(metadata[1]);
			char *sender = metadata[2];
			char *hostname = metadata[3];
			char *clientos = metadata[4];
			char *clientclass = metadata[5];
			char *collectorid = metadata[6];
			enum ostype_t os;
			void *hinfo = NULL;

			dbgprintf("Client report from host %s\n", (hostname ? hostname : "<unknown>"));

			/* Check if we are running a collector module for this type of client */
			if (!collectorid || (strcmp(collectorid, "xymonnet2") != 0)) continue;

			hinfo = hostinfo(hostname); if (!hinfo) continue;
			os = get_ostype(clientos);

			/* Default clientclass to the OS name */
			if (!clientclass || (*clientclass == '\0')) clientclass = clientos;

			handle_netcollect_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
		}
	}

	return 0;
}

