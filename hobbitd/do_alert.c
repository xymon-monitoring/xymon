/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* This is part of the bbd_alert worker module.                               */
/* This module implements the standard bbgend alerting function. It loads the */
/* alert configuration from bb-alerts.cfg, and incoming alerts are then sent  */
/* according to the rules defined.                                            */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: do_alert.c,v 1.2 2004-10-17 10:09:50 henrik Exp $";

/*
 * The alert API defines three functions that must be implemented:
 *
 * - void load_alertconfig(char *filename)
 *   Called to load the alert configuration. Will be called multiple
 *   times, and must be capable of handling this without leaking
 *   memory.
 *
 * - void dump_alertconfig(void)
 *   Dump the alert configuration to stdout.
 *
 * - time_t next_alert(activealerts_t *alert)
 *   Must return the time when the next alert is due.
 *
 * - void start_alerts(void)
 *   Called before the first call to send_alert()
 *
 * - void send_alert(activealerts_t *alert)
 *   Called for each alert to send.
 *
 * - void finish_alerts(void)
 *   Called after all calls to send_alert()
 *
 * load_alertconfig() and next_alert() are called in the context
 * of the main bbd_alert worker.
 * send_alert() runs in a sub-proces forked from bbd_alert.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include "bbd_alert.h"
#include "bbgen.h"
#include "util.h"
#include "debug.h"

enum method_t { M_MAIL, M_SCRIPT, M_BBSCRIPT };
enum msgformat_t { FRM_TEXT, FRM_SMS, FRM_PAGER };

typedef struct token_t {
	char *name;
	char *value;
	struct token_t *next;
} token_t;
static token_t *tokhead = NULL;

typedef struct criteria_t {
	char *pagespec;
	char *hostspec;
	char *svcspec;
	int colors;
	char *timespec;
	int minduration, maxduration;
	int sendrecovered;
} criteria_t;

typedef struct recip_t {
	criteria_t *criteria;
	enum method_t method;
	char *recipient;
	enum msgformat_t format;
	time_t interval;
	struct recip_t *next;
} recip_t;

typedef struct rule_t {
	criteria_t *criteria;
	recip_t *recipients;
	struct rule_t *next;
} rule_t;
static rule_t *rulehead = NULL;

static time_t lastload = 0;
static enum { P_NONE, P_RULE, P_RECIP } pstate = P_NONE;

static criteria_t *setup_criteria(rule_t **currule, recip_t **currcp)
{
	criteria_t *crit;

	switch (pstate) {
	  case P_NONE:
		*currule = (rule_t *)calloc(1, sizeof(rule_t));
		pstate = P_RULE;
		/* Fall through */

	  case P_RULE:
		if (!(*currule)->criteria) 
			(*currule)->criteria = (criteria_t *)calloc(1, sizeof(criteria_t));
		crit = (*currule)->criteria;
		*currcp = NULL;
		break;

	  case P_RECIP:
		if (!(*currcp)->criteria)
			(*currcp)->criteria = (criteria_t *)calloc(1, sizeof(criteria_t));
		crit = (*currcp)->criteria;
		break;
	}

	return crit;
}

static char *preprocess(char *buf)
{
	static char *result = NULL;
	static int reslen = 0;
	int n;
	char *inp, *outp, *p;

	if (result == NULL) {
		reslen = 8192;
		result = (char *)malloc(reslen);
	}
	inp = buf;
	outp = result;
	*outp = '\0';

	while (inp) {
		p = strchr(inp, '$');
		if (p == NULL) {
			n = strlen(inp);
			strcat(outp, inp);
			outp += n;
			inp = NULL;
		}
		else {
			token_t *twalk;

			*p = '\0';
			n = strlen(inp);
			strcat(outp, inp);
			outp += n;
			p = (p+1);

			n = strcspn(p, " \t");
			*(p+n) = '\0';
			for (twalk = tokhead; (twalk && strcmp(p, twalk->name)); twalk = twalk->next) ;
			*(p+n) = ' ';

			if (twalk) {
				strcat(outp, twalk->value);
				outp += strlen(twalk->value);
			}
			inp = p+n;
		}
	}
	*outp = '\0';

	return result;
}

void load_alertconfig(char *configfn)
{

	char fn[MAX_PATH];
	struct stat st;
	FILE *fd;
	char l[8192];
	char *p;
	rule_t *currule = NULL, *ruletail = NULL;
	recip_t *currcp = NULL, *rcptail = NULL;

	if (configfn) strcpy(fn, configfn); else sprintf(fn, "%s/etc/bb-alerts.cfg", getenv("BBHOME"));
	if (stat(fn, &st) == -1) return;
	if (st.st_mtime == lastload) return;

	fd = fopen(fn, "r");
	if (!fd) return;

	/* First, cleanout the old rule set */
	while (rulehead) {
		rule_t *trule;

		if (rulehead->criteria) {
			if (rulehead->criteria->pagespec) free(rulehead->criteria->pagespec);
			if (rulehead->criteria->hostspec) free(rulehead->criteria->hostspec);
			if (rulehead->criteria->svcspec)  free(rulehead->criteria->svcspec);
			if (rulehead->criteria->timespec) free(rulehead->criteria->timespec);
			free(rulehead->criteria);
		}

		while (rulehead->recipients) {
			recip_t *trecip = rulehead->recipients;

			if (trecip->criteria) {
				if (trecip->criteria->pagespec) free(trecip->criteria->pagespec);
				if (trecip->criteria->hostspec) free(trecip->criteria->hostspec);
				if (trecip->criteria->svcspec)  free(trecip->criteria->svcspec);
				if (trecip->criteria->timespec) free(trecip->criteria->timespec);
				free(trecip->criteria);
			}
			if (trecip->recipient) free(trecip->recipient);
			rulehead->recipients = rulehead->recipients->next;
			free(trecip);
		}
		trule = rulehead;
		rulehead = rulehead->next;
		free(trule);
	}
	while (tokhead) {
		token_t *ttok;

		if (tokhead->name) free(tokhead->name);
		if (tokhead->value) free(tokhead->value);
		ttok = tokhead;
		tokhead = tokhead->next;
		free(ttok);
	}

	while (fgets(l, sizeof(l), fd)) {
		p = strchr(l, '\n'); if (p) *p = '\0';
		if ((l[0] == '#') || (strlen(l) == 0)) {
			if (currule) {
				currule->next = NULL;

				if (rulehead == NULL) {
					rulehead = ruletail = currule;
				}
				else {
					ruletail->next = currule;
					ruletail = currule;
				}

				currule = NULL;
				currcp = NULL;
				pstate = P_NONE;
			}
			continue;
		}
		else if ((l[0] == '$') && strchr(l, '=')) {
			token_t *newtok = (token_t *) malloc(sizeof(token_t));
			char *delim;

			delim = strchr(l, '=');
			*delim = '\0';
			newtok->name = strdup(l+1);
			newtok->value = strdup(delim+1);
			newtok->next = tokhead;
			tokhead = newtok;
			continue;
		}

		/* Expand shortnames inside the line before parsing */
		p = strtok(preprocess(l), " ");
		while (p) {
			if (strncmp(p, "PAGE=", 5) == 0) {
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->pagespec = strdup(p+5);
			}
			else if (strncmp(p, "HOST=", 5) == 0) {
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->hostspec = strdup(p+5);
			}
			else if (strncmp(p, "SERVICE=", 8) == 0) {
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->svcspec = strdup(p+8);
			}
			else if (strncmp(p, "COLOR=", 6) == 0) {
				criteria_t *crit = setup_criteria(&currule, &currcp);
				char *c1, *c2;
				
				c1 = p+6;
				do {
					c2 = strchr(c1, ',');
					if (c2) *c2 = '\0';
					crit->colors = (crit->colors | (1 << parse_color(c1)));
					if (c2) c1 = (c2+1); else c1 = NULL;
				} while (c1);
			}
			else if (strncmp(p, "TIME=", 5) == 0) {
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->timespec = strdup(p+5);
			}
			else if (strncmp(p, "DURATION", 8) == 0) {
				criteria_t *crit = setup_criteria(&currule, &currcp);
				if (*(p+8) == '>') crit->minduration = 60*atoi(p+9);
				else if (*(p+8) == '<') crit->maxduration = 60*atoi(p+9);
			}
			else if (strncmp(p, "RECOVERED", 9) == 0) {
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->sendrecovered = 1;
			}
			else if (currule && ((strncmp(p, "MAIL ", 5) == 0) || strchr(p, '@')) ) {
				recip_t *newrcp = (recip_t *)malloc(sizeof(recip_t));
				newrcp->method = M_MAIL;
				newrcp->format = FRM_TEXT;
				if (strchr(p, '@') == NULL) p = strtok(NULL, " ");
				if (p) {
					newrcp->recipient = strdup(p);
					newrcp->interval = 300;
					newrcp->next = NULL;
					currcp = newrcp;
					pstate = P_RECIP;

					if (currule->recipients == NULL)
						currule->recipients = rcptail = newrcp;
					else {
						rcptail->next = newrcp;
						rcptail = newrcp;
					}
				}
				else {
					free(newrcp);
				}
			}
			else if (currule && (strncmp(p, "SCRIPT ", 7) == 0)) {
				recip_t *newrcp = (recip_t *)malloc(sizeof(recip_t));
				newrcp->method = M_SCRIPT;
				newrcp->format = FRM_TEXT;
				p = strtok(NULL, " ");
				if (p) {
					newrcp->recipient = strdup(p);
					newrcp->interval = 300;
					newrcp->next = NULL;
					currcp = newrcp;
					pstate = P_RECIP;

					if (currule->recipients == NULL)
						currule->recipients = rcptail = newrcp;
					else {
						rcptail->next = newrcp;
						rcptail = newrcp;
					}
				}
				else {
					free(newrcp);
				}
			}
			else if (currule && (strncmp(p, "BBSCRIPT ", 9) == 0)) {
				recip_t *newrcp = (recip_t *)malloc(sizeof(recip_t));
				newrcp->method = M_BBSCRIPT;
				newrcp->format = FRM_TEXT;
				p = strtok(NULL, " ");
				if (p) {
					newrcp->recipient = strdup(p);
					newrcp->interval = 300;
					newrcp->next = NULL;
					currcp = newrcp;
					pstate = P_RECIP;

					if (currule->recipients == NULL)
						currule->recipients = rcptail = newrcp;
					else {
						rcptail->next = newrcp;
						rcptail = newrcp;
					}
				}
				else {
					free(newrcp);
				}
			}
			else if ((pstate == P_RECIP) && (strncmp(p, "FORMAT=", 7) == 0)) {
				if      (strcmp(p+7, "TEXT") == 0) currcp->format = FRM_TEXT;
				else if (strcmp(p+7, "SMS") == 0) currcp->format = FRM_SMS;
				else if (strcmp(p+7, "PAGER") == 0) currcp->format = FRM_PAGER;
			}
			else if ((pstate == P_RECIP) && (strncmp(p, "REPEAT=", 7) == 0)) {
				currcp->interval = 60*atoi(p+7);
			}

			if (p) p = strtok(NULL, " ");
		}
	}

	fclose(fd);
}

static void dump_criteria(criteria_t *crit)
{
	if (crit->pagespec) printf("PAGE=%s ", crit->pagespec);
	if (crit->hostspec) printf("HOST=%s ", crit->hostspec);
	if (crit->svcspec) printf("SERVICE=%s ", crit->svcspec);
	if (crit->colors) {
		int i;
		static int first = 1;

		printf("COLOR=");
		for (i = 0; (i < COL_COUNT); i++) {
			if ((1 << i) & crit->colors) {
				if (!first) printf(",");
				printf("%s", colorname(i));
				first = 0;
			}
		}
		printf(" ");
	}

	if (crit->timespec) printf("TIME=%s ", crit->timespec);
	if (crit->minduration) printf("DURATION>%d ", (crit->minduration / 60));
	if (crit->maxduration) printf("DURATION<%d ", (crit->maxduration / 60));
	if (crit->sendrecovered) printf("RECOVERED ");
}

void dump_alertconfig(void)
{
	rule_t *rulewalk;
	recip_t *recipwalk;

	for (rulewalk = rulehead; (rulewalk); rulewalk = rulewalk->next) {
		dump_criteria(rulewalk->criteria);
		printf("\n");

		for (recipwalk = rulewalk->recipients; (recipwalk); recipwalk = recipwalk->next) {
			printf("\t");
			switch (recipwalk->method) {
			  case M_MAIL : printf("MAIL "); break;
			  case M_SCRIPT : printf("SCRIPT "); break;
			  case M_BBSCRIPT : printf("BBSCRIPT "); break;
			}
			printf("%s ", recipwalk->recipient);
			switch (recipwalk->format) {
			  case FRM_TEXT : break;
			  case FRM_SMS  : printf("FORMAT=SMS "); break;
			  case FRM_PAGER  : printf("FORMAT=PAGER "); break;
			}
			printf("REPEAT=%d ", (int)(recipwalk->interval / 60));
			if (recipwalk->criteria) dump_criteria(recipwalk->criteria);
			printf("\n");
		}
		printf("\n");
	}
}

void start_alerts(void)
{
}

void send_alert(activealerts_t *alert)
{
	char cmd[4096];
	FILE *mailpipe;

	sprintf(cmd, "%s \"BB alert %s:%s %s\" %s",
		getenv("MAIL"),
		alert->hostname->name, alert->testname->name,
		colorname(alert->color),
		"henrik@hswn.dk");
	mailpipe = popen(cmd, "w");
	if (mailpipe) {
		fprintf(mailpipe, "%s", alert->pagemessage);
		pclose(mailpipe);
	}
}

void finish_alerts(void)
{
}

time_t next_alert(activealerts_t *alert)
{
	time_t result = time(NULL);

	return result+300;
}

