/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This is part of the hobbitd_alert worker module.                           */
/* This module implements the standard hobbitd alerting function. It loads    */
/* the alert configuration from hobbit-alerts.cfg, and incoming alerts are    */
/* then sent according to the rules defined.                                  */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: do_alert.c,v 1.99 2008-01-02 14:13:24 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <sys/wait.h>

#include <pcre.h>

#include "libbbgen.h"

/*
 * This is the dynamic info stored to keep track of active alerts. We
 * need to keep track of when the next alert is due for each recipient,
 * and this goes on a host+test+recipient basis.
 */
typedef struct repeat_t {
	char *recipid;  /* Essentially hostname|testname|method|address */
	time_t nextalert;
	struct repeat_t *next;
} repeat_t;
static repeat_t *rpthead = NULL;

int include_configid = 0;  /* Whether to include the configuration file linenumber in alerts */
int testonly = 0;	   /* Test mode, dont actually send out alerts */

static int servicecode(char *testname)
{
	/*
	 * The SVCCODES environment is a list of servicecodes:
	 * SVCCODES="disk:100,cpu:200,procs:300,msgs:400,conn:500,http:600,dns:800,smtp:725,telnet:721"
	 * This routine returns the number associated with the service.
	 */
	static char *svccodes = NULL;
	char *tname;
	char *p;

	if (svccodes == NULL) {
		p = xgetenv("SVCCODES");
		if (p == NULL) p = "none";
		svccodes = (char *)malloc(strlen(p)+2);
		sprintf(svccodes, ",%s", p);
	}

	tname = (char *)malloc(strlen(testname)+3);
	sprintf(tname, ",%s:", testname);
	p = strstr(svccodes, tname);
	xfree(tname);

	if (p) {
		p = strchr(p, ':');
		return atoi(p+1);
	}

	return 0;
}

void start_alerts(void)
{
	/* No special pre-alert setup needed */
	return;
}

static repeat_t *find_repeatinfo(activealerts_t *alert, recip_t *recip, int create)
{
	char *id, *method = "unknown";
	repeat_t *walk;

	if (recip->method == M_IGNORE) return NULL;

	switch (recip->method) {
	  case M_MAIL: method = "mail"; break;
	  case M_SCRIPT: method = "script"; break;
	  case M_IGNORE: method = "ignore"; break;
	}

	id = (char *) malloc(strlen(alert->hostname) + strlen(alert->testname) + strlen(method) + strlen(recip->recipient) + 4);
	sprintf(id, "%s|%s|%s|%s", alert->hostname, alert->testname, method, recip->recipient);
	for (walk = rpthead; (walk && strcmp(walk->recipid, id)); walk = walk->next);

	if ((walk == NULL) && create) {
		walk = (repeat_t *)malloc(sizeof(repeat_t));
		walk->recipid = id;
		walk->nextalert = 0;
		walk->next = rpthead;
		rpthead = walk;
	}
	else 
		xfree(id);

	return walk;
}

static char *message_subject(activealerts_t *alert, recip_t *recip)
{
	static char subj[250];
	static char *sevtxt[COL_COUNT] = {
		"is GREEN", 
		"has no data (CLEAR)", 
		"is disabled (BLUE)", 
		"stopped reporting (PURPLE)", 
		"warning (YELLOW)", 
		"CRITICAL (RED)" 
	};
	char *sev = "";
	char *subjfmt = NULL;

	/* Only subjects on ALERTFORM_TEXT and ALERTFORM_PLAIN messages */
	if ((recip->format != ALERTFORM_TEXT) && (recip->format != ALERTFORM_PLAIN)) return NULL;

	MEMDEFINE(subj);

	if ((alert->color >= 0) && (alert->color < COL_COUNT)) sev = sevtxt[alert->color];

	switch (alert->state) {
	  case A_PAGING:
	  case A_ACKED:
		subjfmt = (include_configid ? "Hobbit [%d] %s:%s %s [cfid:%d]" :  "Hobbit [%d] %s:%s %s");
		snprintf(subj, sizeof(subj)-1, subjfmt, 
			 alert->cookie, alert->hostname, alert->testname, sev, recip->cfid);
		break;

	  case A_NOTIFY:
		subjfmt = (include_configid ? "Hobbit %s:%s NOTICE [cfid:%d]" :  "Hobbit %s:%s NOTICE");
		snprintf(subj, sizeof(subj)-1, subjfmt, 
			 alert->hostname, alert->testname, recip->cfid);
		break;

	  case A_RECOVERED:
		subjfmt = (include_configid ? "Hobbit %s:%s recovered [cfid:%d]" :  "Hobbit %s:%s recovered");
		snprintf(subj, sizeof(subj)-1, subjfmt, 
			 alert->hostname, alert->testname, recip->cfid);
		break;

	  case A_NORECIP:
	  case A_DEAD:
		/* Cannot happen */
		break;
	}

	*(subj + sizeof(subj) - 1) = '\0';

	MEMUNDEFINE(subj);
	return subj;
}

static char *message_text(activealerts_t *alert, recip_t *recip)
{
	static strbuffer_t *buf = NULL;
	char *eoln, *bom, *p;
	char info[4096];

	MEMDEFINE(info);

	if (!buf) buf = newstrbuffer(0); else clearstrbuffer(buf);

	if (alert->state == A_NOTIFY) {
		sprintf(info, "%s:%s INFO\n", alert->hostname, alert->testname);
		addtobuffer(buf, info);
		addtobuffer(buf, alert->pagemessage);
		MEMUNDEFINE(info);
		return STRBUF(buf);
	}

	switch (recip->format) {
	  case ALERTFORM_TEXT:
	  case ALERTFORM_PLAIN:
		bom = msg_data(alert->pagemessage);
		eoln = strchr(bom, '\n'); if (eoln) *eoln = '\0';

		/* If there's a "<-- flags:.... -->" then remove it from the message */
		if ((p = strstr(bom, "<!--")) != NULL) {
			/* Add the part of line 1 before the flags ... */
			*p = '\0'; addtobuffer(buf, bom); *p = '<'; 

			/* And the part of line 1 after the flags ... */
			p = strstr(p, "-->"); if (p) addtobuffer(buf, p+3);

			/* And if there is more than line 1, add it as well */
			if (eoln) {
				*eoln = '\n';
				addtobuffer(buf, eoln);
			}
		}
		else {
			if (eoln) *eoln = '\n';
			addtobuffer(buf, bom);
		}

		addtobuffer(buf, "\n");

		if (recip->format == ALERTFORM_TEXT) {
			sprintf(info, "See %s%s\n", 
				xgetenv("BBWEBHOST"), 
				hostsvcurl(alert->hostname, alert->testname, 0));
			addtobuffer(buf, info);
		}

		MEMUNDEFINE(info);
		return STRBUF(buf);

	  case ALERTFORM_SMS:
		/*
		 * Send a report containing a brief alert
		 * and any lines that begin with a "&COLOR"
		 */
		switch (alert->state) {
		  case A_PAGING:
		  case A_ACKED:
			sprintf(info, "%s:%s %s [%d]", 
				alert->hostname, alert->testname, 
				colorname(alert->color), alert->cookie);
			break;

		  case A_RECOVERED:
			sprintf(info, "%s:%s RECOVERED", 
				alert->hostname, alert->testname);
			break;

		  case A_NOTIFY:
			sprintf(info, "%s:%s NOTICE", 
				alert->hostname, alert->testname);
			break;

		  case A_NORECIP:
		  case A_DEAD:
			break;
		}

		addtobuffer(buf, info);
		bom = msg_data(alert->pagemessage);
		eoln = strchr(bom, '\n');
		if (eoln) {
			bom = eoln;
			while ((bom = strstr(bom, "\n&")) != NULL) {
				eoln = strchr(bom+1, '\n'); if (eoln) *eoln = '\0';
				if ((strncmp(bom, "&red", 4) == 0) || (strncmp(bom, "&yellow", 7) == 0)) 
					addtobuffer(buf, bom);
				if (eoln) *eoln = '\n';
				bom = (eoln ? eoln+1 : "");
			}
		}
		MEMUNDEFINE(info);
		return STRBUF(buf);

	  case ALERTFORM_SCRIPT:
		sprintf(info, "%s:%s %s [%d]\n",
			alert->hostname, alert->testname, colorname(alert->color), alert->cookie);
		addtobuffer(buf, info);
		addtobuffer(buf, msg_data(alert->pagemessage));
		addtobuffer(buf, "\n");
		sprintf(info, "See %s%s\n", 
			xgetenv("BBWEBHOST"),
			hostsvcurl(alert->hostname, alert->testname, 0));
		addtobuffer(buf, info);
		MEMUNDEFINE(info);
		return STRBUF(buf);

	  case ALERTFORM_PAGER:
	  case ALERTFORM_NONE:
		MEMUNDEFINE(info);
		return "";
	}

	MEMUNDEFINE(info);
	return alert->pagemessage;
}

void send_alert(activealerts_t *alert, FILE *logfd)
{
	recip_t *recip;
	int first = 1;
	int alertcount = 0;
	time_t now = getcurrenttime(NULL);
	char *alerttxt[A_DEAD+1] = { "Paging", "Acked", "Recovered", "Notify", "Dead" };

	dbgprintf("send_alert %s:%s state %d\n", alert->hostname, alert->testname, (int)alert->state);
	traceprintf("send_alert %s:%s state %s\n", 
		    alert->hostname, alert->testname, alerttxt[alert->state]);

	stoprulefound = 0;

	while (!stoprulefound && ((recip = next_recipient(alert, &first, NULL, NULL)) != NULL)) {
		/* If this is an "UNMATCHED" rule, ignore it if we have already sent out some alert */
		if (recip->unmatchedonly && (alertcount != 0)) {
			traceprintf("Recipient '%s' dropped, not unmatched (count=%d)\n", recip->recipient, alertcount);
			continue;
		}

		if (recip->noalerts && ((alert->state == A_PAGING) || (alert->state == A_RECOVERED))) {
			traceprintf("Recipient '%s' dropped (NOALERT)\n", recip->recipient);
			continue;
		}

		if (recip->method == M_IGNORE) {
			traceprintf("IGNORE rule found\n");
			continue;
		}

		if (alert->state == A_PAGING) {
			repeat_t *rpt = NULL;

			/*
			 * This runs in a child-process context, so the record we
			 * might create here is NOT used later on.
			 */
			rpt = find_repeatinfo(alert, recip, 1);
			if (!rpt) continue;	/* Happens for e.g. M_IGNORE recipients */

			dbgprintf("  repeat %s at %d\n", rpt->recipid, rpt->nextalert);
			if (rpt->nextalert > now) {
				traceprintf("Recipient '%s' dropped, next alert due at %ld > %ld\n",
						rpt->recipid, (long)rpt->nextalert, (long)now);
				continue;
			}
			alertcount++;
		}
		else if (alert->state == A_RECOVERED) {
			/* RECOVERED messages require that we've sent out an alert before */
			repeat_t *rpt = NULL;

			rpt = find_repeatinfo(alert, recip, 0);
			if (!rpt) continue;
			alertcount++;
		}

		dbgprintf("  Alert for %s:%s to %s\n", alert->hostname, alert->testname, recip->recipient);
		switch (recip->method) {
		  case M_IGNORE:
			break;

		  case M_MAIL:
			{
				char cmd[32768];
				char *mailsubj;
				FILE *mailpipe;

				MEMDEFINE(cmd);

				mailsubj = message_subject(alert, recip);

				if (mailsubj) {
					if (xgetenv("MAIL")) 
						sprintf(cmd, "%s \"%s\" ", xgetenv("MAIL"), mailsubj);
					else if (xgetenv("MAILC"))
						sprintf(cmd, "%s -s \"%s\" ", xgetenv("MAILC"), mailsubj);
					else 
						sprintf(cmd, "mail -s \"%s\" ", mailsubj);
				}
				else {
					if (xgetenv("MAILC"))
						sprintf(cmd, "%s ", xgetenv("MAILC"));
					else 
						sprintf(cmd, "mail ");
				}
				strcat(cmd, recip->recipient);

				traceprintf("Mail alert with command '%s'\n", cmd);
				if (testonly) { MEMUNDEFINE(cmd); break; }

				mailpipe = popen(cmd, "w");
				if (mailpipe) {
					fprintf(mailpipe, "%s", message_text(alert, recip));
					pclose(mailpipe);
					if (logfd) {
						init_timestamp();
						fprintf(logfd, "%s %s.%s (%s) %s[%d] %ld %d",
							timestamp, alert->hostname, alert->testname,
							alert->ip, recip->recipient, recip->cfid,
							(long)now, servicecode(alert->testname));
						if (alert->state == A_RECOVERED) {
							fprintf(logfd, " %ld\n", (long)(now - alert->eventstart));
						}
						else {
							fprintf(logfd, "\n");
						}
						fflush(logfd);
					}
				}
				else {
					errprintf("ERROR: Cannot open command pipe for '%s' - alert lost!\n", cmd);
					traceprintf("Mail pipe failed - alert lost\n");
				}

				MEMUNDEFINE(cmd);
			}
			break;

		  case M_SCRIPT:
			{
				pid_t scriptpid;

				traceprintf("Script alert with command '%s' and recipient %s\n", recip->scriptname, recip->recipient);
				if (testonly) break;

				scriptpid = fork();
				if (scriptpid == 0) {
					/* Setup all of the environment for a paging script */
					void *hinfo;
					char *p;
					int ip1=0, ip2=0, ip3=0, ip4=0;
					char *bbalphamsg, *ackcode, *rcpt, *bbhostname, *bbhostsvc, *bbhostsvccommas, *bbnumeric, *machip, *bbsvcname, *bbsvcnum, *bbcolorlevel, *recovered, *downsecs, *eventtstamp, *downsecsmsg, *cfidtxt;
					int msglen;

					cfidtxt = (char *)malloc(strlen("CFID=") + 10);
					sprintf(cfidtxt, "CFID=%d", recip->cfid);
					putenv(cfidtxt);

					p = message_text(alert, recip);
					msglen = strlen(p);
					if (msglen > MAX_ALERTMSG_SCRIPTS) {
						dbgprintf("Cropping large alert message from %d to %d bytes\n", msglen, MAX_ALERTMSG_SCRIPTS);
						msglen = MAX_ALERTMSG_SCRIPTS;
					}
					msglen += strlen("BBALPHAMSG=");
					bbalphamsg = (char *)malloc(msglen + 1);
					snprintf(bbalphamsg, msglen, "BBALPHAMSG=%s", p);
					putenv(bbalphamsg);

					ackcode = (char *)malloc(strlen("ACKCODE=") + 10);
					sprintf(ackcode, "ACKCODE=%d", alert->cookie);
					putenv(ackcode);

					rcpt = (char *)malloc(strlen("RCPT=") + strlen(recip->recipient) + 1);
					sprintf(rcpt, "RCPT=%s", recip->recipient);
					putenv(rcpt);

					bbhostname = (char *)malloc(strlen("BBHOSTNAME=") + strlen(alert->hostname) + 1);
					sprintf(bbhostname, "BBHOSTNAME=%s", alert->hostname);
					putenv(bbhostname);

					bbhostsvc = (char *)malloc(strlen("BBHOSTSVC=") + strlen(alert->hostname) + 1 + strlen(alert->testname) + 1);
					sprintf(bbhostsvc, "BBHOSTSVC=%s.%s", alert->hostname, alert->testname);
					putenv(bbhostsvc);

					bbhostsvccommas = (char *)malloc(strlen("BBHOSTSVCCOMMAS=") + strlen(alert->hostname) + 1 + strlen(alert->testname) + 1);
					sprintf(bbhostsvccommas, "BBHOSTSVCCOMMAS=%s.%s", commafy(alert->hostname), alert->testname);
					putenv(bbhostsvccommas);

					bbnumeric = (char *)malloc(strlen("BBNUMERIC=") + 22 + 1);
					p = bbnumeric;
					p += sprintf(p, "BBNUMERIC=");
					p += sprintf(p, "%03d", servicecode(alert->testname));
					sscanf(alert->ip, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4);
					p += sprintf(p, "%03d%03d%03d%03d", ip1, ip2, ip3, ip4);
					p += sprintf(p, "%d", alert->cookie);
					putenv(bbnumeric);

					machip = (char *)malloc(strlen("MACHIP=") + 13);
					sprintf(machip, "MACHIP=%03d%03d%03d%03d", ip1, ip2, ip3, ip4);
					putenv(machip);

					bbsvcname = (char *)malloc(strlen("BBSVCNAME=") + strlen(alert->testname) + 1);
					sprintf(bbsvcname, "BBSVCNAME=%s", alert->testname);
					putenv(bbsvcname);

					bbsvcnum = (char *)malloc(strlen("BBSVCNUM=") + 10);
					sprintf(bbsvcnum, "BBSVCNUM=%d", servicecode(alert->testname));
					putenv(bbsvcnum);

					bbcolorlevel = (char *)malloc(strlen("BBCOLORLEVEL=") + strlen(colorname(alert->color)) + 1);
					sprintf(bbcolorlevel, "BBCOLORLEVEL=%s", colorname(alert->color));
					putenv(bbcolorlevel);

					recovered = (char *)malloc(strlen("RECOVERED=") + 2);
					sprintf(recovered, "RECOVERED=%d", ((alert->state == A_RECOVERED) ? 1 : 0));
					putenv(recovered);

					downsecs = (char *)malloc(strlen("DOWNSECS=") + 20);
					sprintf(downsecs, "DOWNSECS=%ld", (long)(getcurrenttime(NULL) - alert->eventstart));
					putenv(downsecs);

					eventtstamp = (char *)malloc(strlen("EVENTSTART=") + 20);
					sprintf(eventtstamp, "EVENTSTART=%ld", (long)alert->eventstart);
					putenv(eventtstamp);

					if (alert->state == A_RECOVERED) {
						downsecsmsg = (char *)malloc(strlen("DOWNSECSMSG=Event duration :") + 20);
						sprintf(downsecsmsg, "DOWNSECSMSG=Event duration : %ld", (long)(getcurrenttime(NULL) - alert->eventstart));
					}
					else {
						downsecsmsg = strdup("DOWNSECSMSG=");
					}
					putenv(downsecsmsg);

					hinfo = hostinfo(alert->hostname);
					if (hinfo) {
						enum bbh_item_t walk;
						char *itm, *id, *bbhenv;

						for (walk = 0; (walk < BBH_LAST); walk++) {
							itm = bbh_item(hinfo, walk);
							id = bbh_item_id(walk);
							if (itm && id) {
								bbhenv = (char *)malloc(strlen(id) + strlen(itm) + 2);
								sprintf(bbhenv, "%s=%s", id, itm);
								putenv(bbhenv);
							}
						}
					}

					/* The child starts the script */
					execlp(recip->scriptname, recip->scriptname, NULL);
					errprintf("Could not launch paging script %s: %s\n", 
						  recip->scriptname, strerror(errno));
					exit(0);
				}
				else if (scriptpid > 0) {
					/* Parent waits for child to complete */
					int childstat;

					wait(&childstat);
					if (WIFEXITED(childstat) && (WEXITSTATUS(childstat) != 0)) {
						errprintf("Paging script %s terminated with status %d\n",
							  recip->scriptname, WEXITSTATUS(childstat));
					}
					else if (WIFSIGNALED(childstat)) {
						errprintf("Paging script %s terminated by signal %d\n",
							  recip->scriptname, WTERMSIG(childstat));
					}

					if (logfd) {
						init_timestamp();
						fprintf(logfd, "%s %s.%s (%s) %s %ld %d",
							timestamp, alert->hostname, alert->testname,
							alert->ip, recip->recipient, (long)now, 
							servicecode(alert->testname));
						if (alert->state == A_RECOVERED) {
							fprintf(logfd, " %ld\n", (long)(now - alert->eventstart));
						}
						else {
							fprintf(logfd, "\n");
						}
						fflush(logfd);
					}
				}
				else {
					errprintf("ERROR: Fork failed to launch script '%s' - alert lost\n", recip->scriptname);
					traceprintf("Script fork failed - alert lost\n");
				}
			}
			break;
		}
	}
}

void finish_alerts(void)
{
	/* No special post-alert setup needed */
	return;
}

time_t next_alert(activealerts_t *alert)
{
	time_t now = getcurrenttime(NULL);
	int first = 1;
	int found = 0;
	time_t nexttime = now+(30*86400);	/* 30 days from now */
	recip_t *recip;
	repeat_t *rpt;
	time_t r_next = -1;

	stoprulefound = 0;
	while (!stoprulefound && ((recip = next_recipient(alert, &first, NULL, &r_next)) != NULL)) {
		found = 1;
		/* 
		 * This runs in the parent hobbitd_alert proces, so we must create
		 * a repeat-record here - or all alerts will get repeated every minute.
		 */
		rpt = find_repeatinfo(alert, recip, 1);
		if (rpt) {
			if (rpt->nextalert <= now) rpt->nextalert = (now + recip->interval);
			if (rpt->nextalert < nexttime) nexttime = rpt->nextalert;
		}
		else if (r_next != -1) {
			if (r_next < nexttime) nexttime = r_next;
		}
		else {
			/* 
			 * This can happen, e.g.  if we get an alert, but the minimum 
			 * DURATION has not been met.
			 * This simply means we dropped the alert -for now - for some 
			 * reason, so it should be retried again right away. Put in a
			 * 1 minute delay to prevent run-away alerts from flooding us.
			 */
			if ((now + 60) < nexttime) nexttime = now + 60;
		}
	}

	if (r_next != -1) {
		/*
		 * Waiting for a minimum duration to trigger
		 */
		if (r_next < nexttime) nexttime = r_next;
	}
	else if (!found) {
		/*
		 * There IS a potential recipient (or we would not be here).
		 * And it's not a DURATION waiting to happen.
		 * Probably waiting for a TIME restriction to trigger, so try 
		 * again soon.
		 */
		nexttime = now + 60;
	}

	return nexttime;
}

void cleanup_alert(activealerts_t *alert)
{
	/*
	 * A status has recovered and gone green, or it has been deleted. 
	 * So we clear out all info we have about this alert and it's recipients.
	 */
	char *id;
	repeat_t *rptwalk, *rptprev;

	dbgprintf("cleanup_alert called for host %s, test %s\n", alert->hostname, alert->testname);

	id = (char *)malloc(strlen(alert->hostname)+strlen(alert->testname)+3);
	sprintf(id, "%s|%s|", alert->hostname, alert->testname);
	rptwalk = rpthead; rptprev = NULL;
	while (rptwalk) {
		if (strncmp(rptwalk->recipid, id, strlen(id)) == 0) {
			repeat_t *tmp = rptwalk;

			dbgprintf("cleanup_alert found recipient %s\n", rptwalk->recipid);

			if (rptwalk == rpthead) {
				rptwalk = rpthead = rpthead->next;
			}
			else {
				rptprev->next = rptwalk->next;
				rptwalk = rptwalk->next;
			}

			xfree(tmp->recipid);
			xfree(tmp);
		}
		else {
			rptprev = rptwalk;
			rptwalk = rptwalk->next;
		}
	}

	xfree(id);
}

void clear_interval(activealerts_t *alert)
{
	int first = 1;
	recip_t *recip;
	repeat_t *rpt;

	alert->nextalerttime = 0;
	stoprulefound = 0;
	while (!stoprulefound && ((recip = next_recipient(alert, &first, NULL, NULL)) != NULL)) {
		rpt = find_repeatinfo(alert, recip, 0);
		if (rpt) {
			dbgprintf("Cleared repeat interval for %s\n", rpt->recipid);
			rpt->nextalert = 0;
		}
	}
}

void save_state(char *filename)
{
	FILE *fd = fopen(filename, "w");
	repeat_t *walk;

	if (fd == NULL) return;
	for (walk = rpthead; (walk); walk = walk->next) {
		fprintf(fd, "%ld|%s\n", (long) walk->nextalert, walk->recipid);
	}
	fclose(fd);
}

void load_state(char *filename, char *statusbuf)
{
	FILE *fd = fopen(filename, "r");
	strbuffer_t *inbuf;
	char *p;

	if (fd == NULL) return;

	initfgets(fd);
	inbuf = newstrbuffer(0);
	while (unlimfgets(inbuf, fd)) {
		sanitize_input(inbuf, 0, 0);

		p = strchr(STRBUF(inbuf), '|');
		if (p) {
			repeat_t *newrpt;

			*p = '\0';
			if (atoi(STRBUF(inbuf)) > getcurrenttime(NULL)) {
				char *found = NULL;

				if (statusbuf) {
					char *htend;

					/* statusbuf contains lines with "HOSTNAME|TESTNAME|COLOR" */
					htend = strchr(p+1, '|'); if (htend) htend = strchr(htend+1, '|');
					if (htend) {
						*htend = '\0';
						*p = '\n';
						found = strstr(statusbuf, p);
						if (!found && (strncmp(statusbuf, p+1, strlen(p+1)) == 0)) 
							found = statusbuf;
						*htend = '|';
					}
				}
				if (!found) continue;

				newrpt = (repeat_t *)malloc(sizeof(repeat_t));
				newrpt->recipid = strdup(p+1);
				newrpt->nextalert = atoi(STRBUF(inbuf));
				newrpt->next = rpthead;
				rpthead = newrpt;
			}
		}
	}

	fclose(fd);
	freestrbuffer(inbuf);
}

