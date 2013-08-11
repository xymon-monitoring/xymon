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

#define MINIMUM_UPDATE_INTERVAL 10

typedef struct connectresult_t {
	char *testspec;
	char *targetip;
	int targetport;
	enum { NC_STATUS_UNKNOWN, NC_STATUS_OK, NC_STATUS_FAILED, NC_STATUS_TIMEOUT, NC_STATUS_RESOLVERROR, NC_STATUS_SSLERROR, NC_STATUS_BADDATA } status;
	enum { NC_HANDLER_PING, NC_HANDLER_PLAIN, NC_HANDLER_HTTP, NC_HANDLER_DNS, NC_HANDLER_NTP, NC_HANDLER_LDAP, NC_HANDLER_RPC, NC_HANDLER_APACHE } handler;
	float elapsedms;
	char *sslsubject, *sslissuer, *ssldetails;
	time_t sslstart, sslexpires;
	int sslkeysize;
	char *plainlog, *httpheaders, *httpbody;
	int httpstatus;
	int ntpstratum;
	float ntpoffset;
	int interval;
	time_t sent;
} connectresult_t;

typedef struct hostresults_t {
	char *hostname;
	connectresult_t *ping;
	void *results; /* Tree of connectresult_t records, keyed by testspec */
} hostresults_t;

static void *hostresults = NULL; /* Tree of hostresults_t records, keyed by hostname. So a tree of trees. */
static int anychanges = 0;
static time_t lastupdatesent = 0;


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
	xtreePos_t handle;
	hostresults_t *hrec;
	void *ns1var, *ns1sects;
	char *onetest, *testspec;

	if (!hostresults) hostresults = xtreeNew(strcasecmp);

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

	onetest = nextsection_r(clientdata, &testspec, &ns1var, &ns1sects);
	while (onetest) {
		connectresult_t *rec;
		char *bol, *eoln;

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

		rec->status = NC_STATUS_UNKNOWN;
		if (rec->sslsubject) xfree(rec->sslsubject);
		if (rec->plainlog) xfree(rec->plainlog);
		if (rec->httpheaders) xfree(rec->httpheaders);
		if (rec->httpbody) xfree(rec->httpbody);
		rec->elapsedms = 0.0; rec->sslexpires = 0; rec->httpstatus = 0; rec->ntpstratum = 0; rec->ntpoffset = 0.0; rec->interval = 0;
		rec->sent = 0;

		bol = onetest;
		while (bol) {
			eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
			if (argnmatch(bol, "Status: ")) {
				char *s = strchr(bol, ':') + 2;

				if (strcmp(s, "OK") == 0) rec->status = NC_STATUS_OK;
				else if (strcmp(s, "CONN_FAILED") == 0) rec->status = NC_STATUS_FAILED;
				else if (strcmp(s, "CONN_TIMEOUT") == 0) rec->status = NC_STATUS_TIMEOUT;
				else if (strcmp(s, "BADSSLHANDSHAKE") == 0) rec->status = NC_STATUS_SSLERROR;
				else if (strcmp(s, "CANNOT_RESOLVE") == 0) rec->status = NC_STATUS_RESOLVERROR;
				else if (strcmp(s, "BADDATA") == 0) rec->status = NC_STATUS_BADDATA;
				else if (strcmp(s, "INTERRUPTED") == 0) rec->status = NC_STATUS_FAILED;
				else if (strcmp(s, "MODULE_FAILED") == 0) rec->status = NC_STATUS_FAILED;
			}
			else if (argnmatch(bol, "Handler: ")) {
				char *s = strchr(bol, ':') + 2;

				if      (strcmp(s, "plain") == 0) rec->handler = NC_HANDLER_PLAIN;
				else if (strcmp(s, "ntp") == 0)   rec->handler = NC_HANDLER_NTP;
				else if (strcmp(s, "dns") == 0)   rec->handler = NC_HANDLER_DNS;
				else if (strcmp(s, "ping") == 0)  rec->handler = NC_HANDLER_PING;
				else if (strcmp(s, "ldap") == 0)  rec->handler = NC_HANDLER_LDAP;
				else if (strcmp(s, "http") == 0)  rec->handler = ((strncmp(testspec, "apache", 6) == 0) ? NC_HANDLER_APACHE : NC_HANDLER_HTTP);
			}
			else if (argnmatch(bol, "PeerCertificateSubject: ")) rec->sslsubject = strdup(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "PeerCertificateIssuer: ")) rec->sslissuer = strdup(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "PeerCertificateStart: ")) rec->sslstart = atoi(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "PeerCertificateExpiry: ")) rec->sslexpires = atoi(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "PeerCertificateKeysize: ")) rec->sslkeysize = atoi(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "PeerCertificateDetails: ")) rec->ssldetails = sizedstr(bol, eoln);
			else if (argnmatch(bol, "PLAINlog: ")) rec->plainlog = sizedstr(bol, eoln);
			else if (argnmatch(bol, "HTTPheaders: ")) rec->httpheaders = sizedstr(bol, eoln);
			else if (argnmatch(bol, "HTTPbody: ")) rec->httpbody = sizedstr(bol, eoln);
			else if (argnmatch(bol, "HTTPstatus: ")) rec->httpstatus = atoi(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "ElapsedMS: ")) rec->elapsedms = atof(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "IntervalMS: ")) rec->interval = atoi(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "TargetIP: ")) rec->targetip = strdup(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "TargetPort: ")) rec->targetport = atoi(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "NTPstratum: ")) rec->ntpstratum = atoi(strchr(bol, ':') + 2);
			else if (argnmatch(bol, "NTPoffset: ")) rec->ntpoffset = atof(strchr(bol, ':') + 2);

			if (eoln) *eoln = '\n';
			bol = (eoln ? eoln+1 : NULL);
		}

sectiondone:
		onetest = nextsection_r(NULL, &testspec, &ns1var, &ns1sects);
	}

	if (debug) {
		dbgprintf("***********************\n");
		dbgprintf("Results for %s\n", hrec->hostname);
		for (handle = xtreeFirst(hrec->results); (handle != xtreeEnd(hrec->results)); handle = xtreeNext(hrec->results, handle)) {
			connectresult_t *rec;
			rec = (connectresult_t *)xtreeData(hrec->results, handle);
			dbgprintf("    %s\n", rec->testspec);
			dbgprintf("\tStatus:%d, elapsed:%.3f, interval:%d\n", rec->status, rec->elapsedms, rec->interval);
			dbgprintf("\thttpstatus:%d, ntpstratum:%d, ntpoffset=%.6f\n", rec->httpstatus, rec->ntpstratum, rec->ntpoffset);
		}
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
		  case NC_STATUS_OK:
			if (isreverse) {
				sprintf(cause, "Host does respond to ping");
				color = COL_RED;
			}
			break;

		  case NC_STATUS_FAILED:
		  case NC_STATUS_TIMEOUT:
		  case NC_STATUS_BADDATA:
			if (!isreverse) {
				sprintf(cause, "Host does not respond to ping");
				color = COL_RED;
			}
			break;

		  case NC_STATUS_RESOLVERROR:
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

			if (crec->status == NC_STATUS_OK) {
				strcpy(cause, "Service responds when it should not");
				color = COL_RED;
			}
			else if (failgoesclear && pingisdown && !isforced) {
				strcpy(cause, "Host appears to be down");
				color = COL_CLEAR;
			}
		}
		else {
			if (crec->status != NC_STATUS_OK) {
				if (failgoesclear && pingisdown && !isforced) {
					strcpy(cause, "Host appears to be down");
					color = COL_CLEAR;
				}
				else {
					strcpy(cause, "Service unavailable");
					color = COL_RED;
				}

				switch (crec->status) {
				  case NC_STATUS_TIMEOUT: strcat(cause, " (connect timeout)"); break;
				  case NC_STATUS_FAILED: strcat(cause, " (connection failed)"); break;
		  		  case NC_STATUS_RESOLVERROR: strcat(cause, " (DNS error)"); break;
				  case NC_STATUS_SSLERROR: strcat(cause, " (SSL error)"); break;
		  		  case NC_STATUS_BADDATA: strcpy(cause, "Unexpected service response"); break;
				  case NC_STATUS_UNKNOWN: strcat(cause, " (Xymon error)"); break;
				  case NC_STATUS_OK: break;
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


void netcollect_generate_updates(int usebackfeedqueue)
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
	if (usebackfeedqueue) combo_start_local(); else combo_start();

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		xtreePos_t handle;
		hostresults_t *hrec;
		char msgline[4096], msgtext[4096];
		multistatus_t *mhead = NULL;

		handle = xtreeFind(hostresults, xmh_item(hwalk, XMH_HOSTNAME));
		if (handle == xtreeEnd(hostresults)) continue;

		hrec = xtreeData(hostresults, handle);

		for (handle = xtreeFirst(hrec->results); (handle != xtreeEnd(hrec->results)); handle = xtreeNext(hrec->results, handle)) {
			connectresult_t *crec;
			char *testspec;
			int isdialup, isreverse, isforced;
			char *p;

			crec = (connectresult_t *)xtreeData(hrec->results, handle);
			if (crec->sent) continue;

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

			if (crec->sslsubject) {
				char *dupsubj = strdup(crec->sslsubject);
				char *cn, *tokr;
				time_t now;
				char timestr[30];
				multistatus_t *multiitem;
				int sslcolor = COL_GREEN;

				multiitem = init_multi(&mhead, "sslcert", crec->interval, "SSL certificate(s) OK", "SSL certificate(s) about to expire", "SSL certificate(s) expire immediately");

				cn = strtok_r(dupsubj, "/", &tokr);
				while (cn && (strncmp(cn, "CN=", 3) != 0)) cn = strtok_r(NULL, "/", &tokr);
				now = getcurrenttime(NULL);

				if ((crec->sslexpires - now) >= 30*24*60*60) {
					sslcolor = COL_GREEN;

				}
				else if ((crec->sslexpires - now) >= 10*24*60*60) {
					sslcolor = COL_YELLOW;
				}
				else {
					sslcolor = COL_RED;
				}

				strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S UTC", gmtime((time_t *)&crec->sslexpires));
				if (crec->sslexpires >= now)
					sprintf(msgtext, "SSL certificate for %s expires in %d days (%s)\n", cn, (int)((crec->sslexpires - now) / (24*60*60)), timestr);
				else
					sprintf(msgtext, "SSL certificate for %s expired %d days ago (%s)\n", cn, (int)((now - crec->sslexpires) / (24*60*60)), timestr);
				add_multi_item(multiitem, sslcolor, msgtext);

				sprintf(msgtext, "Server certificate for %s\n", testspec);
				addtobuffer(multiitem->detailtext, msgtext);
				sprintf(msgtext, "\tSubject: %s\n", crec->sslsubject);
				addtobuffer(multiitem->detailtext, msgtext);
				sprintf(msgtext, "\tIssuer: %s\n", crec->sslissuer);
				addtobuffer(multiitem->detailtext, msgtext);
				strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S UTC", gmtime((time_t *)&crec->sslstart));
				sprintf(msgtext, "\tValid from: %s\n", timestr);
				addtobuffer(multiitem->detailtext, msgtext);
				strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S UTC", gmtime((time_t *)&crec->sslexpires));
				sprintf(msgtext, "\tValid until: %s\n", timestr);
				addtobuffer(multiitem->detailtext, msgtext);
				sprintf(msgtext, "\tKeysize: %d\n", crec->sslkeysize);
				addtobuffer(multiitem->detailtext, msgtext);
				addtobuffer(multiitem->detailtext, "\n");

				xfree(dupsubj);
			}

			switch (crec->handler) {
			  case NC_HANDLER_HTTP:
			  case NC_HANDLER_LDAP:
			  case NC_HANDLER_DNS:
				{
					char *testname;
					multistatus_t *multiitem = NULL;
					int color = COL_GREEN;


					switch (crec->handler) {
					  case NC_HANDLER_HTTP:
						multiitem = init_multi(&mhead, "http", crec->interval, "Web check OK", "Web check warning", "Web check failed");
						break;
					  case NC_HANDLER_LDAP:
						multiitem = init_multi(&mhead, "ldap", crec->interval, "LDAP Web check OK", "LDAP Web check warning", "LDAP check failed");
						break;
					  case NC_HANDLER_DNS:
						multiitem = init_multi(&mhead, "dns", crec->interval, "DNS Lookup OK", "DNS lookup warning", "DNS lookup failed");
						break;
					  default:
						break;
					}

					color = decide_color(crec,
							(hrec->ping == crec),
							(xmh_item(hwalk, XMH_FLAG_NOPING) != NULL),
							(hrec->ping && (hrec->ping->status == NC_STATUS_FAILED)),
							isdialup,
							isreverse,
							isforced,
							failgoesclear, causetext);

					if ((crec->handler == NC_HANDLER_LDAP) && (color == COL_RED) && (xmh_item(hwalk, XMH_FLAG_LDAPFAILYELLOW))) color = COL_YELLOW;

					sprintf(msgline, "%s - %s\n", testspec, ((color != COL_GREEN) ? "failed" : "OK"));
					add_multi_item(multiitem, color, msgline);

					switch (crec->handler) {
					  case NC_HANDLER_HTTP:
						sprintf(msgline, "&%s %s\n\n", colorname(color), testspec);
						addtobuffer(multiitem->detailtext, msgline);
						if (crec->httpheaders) addtobuffer(multiitem->detailtext, crec->httpheaders);
						// if (crec->httpbody) addtostatus(crec->httpbody);
						break;

					  default:
						break;
					}

					if (crec->plainlog) addtobuffer(multiitem->detailtext, crec->plainlog);

					sprintf(msgtext, "\nTarget : %s port %d\n", crec->targetip, crec->targetport);
					addtobuffer(multiitem->detailtext, msgtext);

					sprintf(msgtext, "\nSeconds: %.3f\n", crec->elapsedms / 1000);
					addtobuffer(multiitem->detailtext, msgtext);

					crec->sent = 1;
				}
				break;

			  case NC_HANDLER_APACHE:
				{
					strbuffer_t *datamsg = newstrbuffer(0);

					sprintf(msgline, "data %s.%s\n", xmh_item(hwalk, XMH_HOSTNAME), "apache");
					addtobuffer(datamsg, msgline);
					addtobuffer(datamsg, crec->httpbody);
					if (usebackfeedqueue) sendmessage_local(STRBUF(datamsg)); else sendmessage(STRBUF(datamsg), NULL, XYMON_TIMEOUT, NULL);
					freestrbuffer(datamsg);
					crec->sent = 1;
				}
				break;

			  default:
				{
					int color = COL_GREEN;
					char flags[5];

					flags[0] = (isdialup ? 'D' : 'd');
					flags[1] = (isreverse ? 'R' : 'r');
					flags[2] = '\0';

					color = decide_color(crec,
							(hrec->ping == crec),
							(xmh_item(hwalk, XMH_FLAG_NOPING) != NULL),
							(hrec->ping && (hrec->ping->status == NC_STATUS_FAILED)),
							isdialup,
							isreverse,
							isforced,
							failgoesclear, causetext);

					/* Validity = 6*(interval in minutes) = 6*(interval/(1000*60)) = interval/10000 */
					init_status(color);
					sprintf(msgline, "status+%d %s.%s %s <!-- [flags:%s] --> %s %s %s ", 
						crec->interval/10000, xmh_item(hwalk, XMH_HOSTNAME), testspec, colorname(color), 
						flags, timestamp, testspec, 
						(((color == COL_RED) || (color == COL_YELLOW)) ? "NOT ok" : "ok") );

					if (crec->status == NC_STATUS_RESOLVERROR) {
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

					sprintf(msgtext, "\nTarget : %s", crec->targetip);
					if (hrec->ping != crec) sprintf(msgtext+strlen(msgtext), " port %d", crec->targetport);
					strcat(msgtext, "\n");
					addtostatus(msgtext);

					sprintf(msgtext, "\nSeconds: %.3f\n", crec->elapsedms / 1000);
					addtostatus(msgtext);

					addtostatus("\n");

					finish_status();
					crec->sent = 1;
				}
				break;
			}
		}

		finish_multi(mhead, xmh_item(hwalk, XMH_HOSTNAME));
	}

	combo_end();

	anychanges = 0;
	lastupdatesent = gettimer();
}

#define MAX_META 20	/* The maximum number of meta-data items in a message */


int main(int argc, char *argv[])
{
	char *msg;
	int running;
	int argi, seq;
	struct timespec timeout;
	time_t nextconfigload = 0;
	int usebackfeedqueue = 0, alwaysflush = 0;

	libxymon_init(argv[0]);

	/* Handle program options. */
	for (argi = 1; (argi < argc); argi++) {
		if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
		else if (strcmp(argv[argi], "--flush") == 0) {
			alwaysflush = 1;
		}
	}

	usebackfeedqueue = (sendmessage_init_local() > 0);
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

		timeout.tv_sec = MINIMUM_UPDATE_INTERVAL; timeout.tv_nsec = 0;
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
			if (anychanges) {
				netcollect_generate_updates(usebackfeedqueue);
			}
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
			if (!collectorid || ((strcmp(collectorid, "xymonnet2") != 0) && (strcmp(collectorid, "netmodule") != 0))) continue;

			hinfo = hostinfo(hostname); if (!hinfo) continue;
			os = get_ostype(clientos);

			/* Default clientclass to the OS name */
			if (!clientclass || (*clientclass == '\0')) clientclass = clientos;

			handle_netcollect_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
			anychanges = 1;

			if (alwaysflush || ((lastupdatesent + MINIMUM_UPDATE_INTERVAL) < gettimer())) {
				netcollect_generate_updates(usebackfeedqueue);
			}
		}
	}

	if (debug) netcollect_generate_updates(usebackfeedqueue);
	if (usebackfeedqueue) sendmessage_finish_local();

	return 0;
}

