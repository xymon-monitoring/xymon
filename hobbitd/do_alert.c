/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* This is part of the bbgend_alert worker module.                            */
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

static char rcsid[] = "$Id: do_alert.c,v 1.15 2004-11-25 21:37:00 henrik Exp $";

/*
 * The alert API defines three functions that must be implemented:
 *
 * - void load_alertconfig(char *filename, int defaultcolors)
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
 * - void clear_interval(activealerts_t *alert)
 *   Must clear the repeat-interval for an alert.
 *
 * - void start_alerts(void)
 *   Called before the first call to send_alert()
 *
 * - void send_alert(activealerts_t *alert, FILE *logfd)
 *   Called for each alert to send.
 *
 * - void finish_alerts(void)
 *   Called after all calls to send_alert()
 *
 * load_alertconfig() and next_alert() are called in the context
 * of the main bbgend_alert worker.
 * send_alert() runs in a sub-proces forked from bbgend_alert.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

#include <pcre.h>

#include "libbbgen.h"

#include "bbgend_alert.h"

enum method_t { M_MAIL, M_SCRIPT, M_BBSCRIPT };
enum msgformat_t { FRM_TEXT, FRM_SMS, FRM_PAGER };
enum recovermsg_t { SR_UNKNOWN, SR_NOTWANTED, SR_WANTED };

/* token's are the pre-processor macros we expand while parsing the config file */
typedef struct token_t {
	char *name;
	char *value;
	struct token_t *next;
} token_t;
static token_t *tokhead = NULL;

/* These are the criteria we use when matching an alert. Used both generally for a rule, and for recipients */
typedef struct criteria_t {
	int cfid;
	char *pagespec;		/* Pages to include */
	pcre *pagespecre;
	char *expagespec;	/* Pages to exclude */
	pcre *expagespecre;
	char *hostspec;		/* Hosts to include */
	pcre *hostspecre;
	char *exhostspec;	/* Hosts to exclude */
	pcre *exhostspecre;
	char *svcspec;		/* Services to include */
	pcre *svcspecre;
	char *exsvcspec;	/* Services to exclude */
	pcre *exsvcspecre;
	int colors;
	char *timespec;
	int minduration, maxduration;
	int sendrecovered;
} criteria_t;

/* This defines a recipient. There may be some criteria, and then how we send alerts to him */
typedef struct recip_t {
	int cfid;
	criteria_t *criteria;
	enum method_t method;
	char *recipient;
	enum msgformat_t format;
	time_t interval;
	struct recip_t *next;
} recip_t;

/* This defines a rule. Some general criteria, and a list of recipients. */
typedef struct rule_t {
	int cfid;
	criteria_t *criteria;
	recip_t *recipients;
	struct rule_t *next;
} rule_t;
static rule_t *rulehead = NULL;
static int cfid = 0;

/*
 * This is the dynamic info stored to keep track of active alerts. We
 * need to keep track of when the next alert is due for each recipient,
 * and this goes on a host+test+recipient basis.
 */
typedef struct repeat_t {
	char *recipid;	/* Essentially hostname|testname|address */
	time_t nextalert;
	struct repeat_t *next;
} repeat_t;
static repeat_t *rpthead = NULL;

static time_t lastload = 0;	/* Last time the config file was loaded */
static enum { P_NONE, P_RULE, P_RECIP } pstate = P_NONE;
static int defaultcolors = 0;

static criteria_t *setup_criteria(rule_t **currule, recip_t **currcp)
{
	criteria_t *crit = NULL;

	switch (pstate) {
	  case P_NONE:
		*currule = (rule_t *)calloc(1, sizeof(rule_t));
		(*currule)->cfid = cfid;
		pstate = P_RULE;
		/* Fall through */

	  case P_RULE:
		if (!(*currule)->criteria) 
			(*currule)->criteria = (criteria_t *)calloc(1, sizeof(criteria_t));
		crit = (*currule)->criteria;
		crit->cfid = cfid;
		*currcp = NULL;
		break;

	  case P_RECIP:
		if (!(*currcp)->criteria)
			(*currcp)->criteria = (criteria_t *)calloc(1, sizeof(criteria_t));
		crit = (*currcp)->criteria;
		crit->cfid = cfid;
		crit->colors = (*currule)->criteria->colors;
		break;
	}

	return crit;
}

static char *preprocess(char *buf)
{
	/* Expands config-file macros */
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

pcre *compileregex(char *pattern)
{
	pcre *result;
	const char *errmsg;
	int errofs;

	dprintf("Compiling regex %s\n", pattern);
	result = pcre_compile(pattern, PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (result == NULL) {
		errprintf("pcre compile '%s' failed (offset %d): %s\n", pattern, errofs, errmsg);
		return NULL;
	}

	return result;
}

void load_alertconfig(char *configfn, int defcolors)
{
	/* (Re)load the configuration file without leaking memory */
	char fn[PATH_MAX];
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

	/* First, clean out the old rule set */
	while (rulehead) {
		rule_t *trule;

		if (rulehead->criteria) {
			if (rulehead->criteria->pagespec) free(rulehead->criteria->pagespec);
			if (rulehead->criteria->pagespecre) pcre_free(rulehead->criteria->pagespecre);
			if (rulehead->criteria->expagespec) free(rulehead->criteria->expagespec);
			if (rulehead->criteria->expagespecre) pcre_free(rulehead->criteria->expagespecre);
			if (rulehead->criteria->hostspec) free(rulehead->criteria->hostspec);
			if (rulehead->criteria->hostspecre) pcre_free(rulehead->criteria->hostspecre);
			if (rulehead->criteria->exhostspec) free(rulehead->criteria->exhostspec);
			if (rulehead->criteria->exhostspecre) pcre_free(rulehead->criteria->exhostspecre);
			if (rulehead->criteria->svcspec)  free(rulehead->criteria->svcspec);
			if (rulehead->criteria->svcspecre) pcre_free(rulehead->criteria->svcspecre);
			if (rulehead->criteria->exsvcspec)  free(rulehead->criteria->exsvcspec);
			if (rulehead->criteria->exsvcspecre) pcre_free(rulehead->criteria->exsvcspecre);
			if (rulehead->criteria->timespec) free(rulehead->criteria->timespec);
			free(rulehead->criteria);
		}

		while (rulehead->recipients) {
			recip_t *trecip = rulehead->recipients;

			if (trecip->criteria) {
				if (trecip->criteria->pagespec) free(trecip->criteria->pagespec);
				if (trecip->criteria->pagespecre) pcre_free(trecip->criteria->pagespecre);
				if (trecip->criteria->expagespec) free(trecip->criteria->expagespec);
				if (trecip->criteria->expagespecre) pcre_free(trecip->criteria->expagespecre);
				if (trecip->criteria->hostspec) free(trecip->criteria->hostspec);
				if (trecip->criteria->hostspecre) pcre_free(trecip->criteria->hostspecre);
				if (trecip->criteria->exhostspec) free(trecip->criteria->exhostspec);
				if (trecip->criteria->exhostspecre) pcre_free(trecip->criteria->exhostspecre);
				if (trecip->criteria->svcspec)  free(trecip->criteria->svcspec);
				if (trecip->criteria->svcspecre) pcre_free(trecip->criteria->svcspecre);
				if (trecip->criteria->exsvcspec)  free(trecip->criteria->exsvcspec);
				if (trecip->criteria->exsvcspecre) pcre_free(trecip->criteria->exsvcspecre);
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

	defaultcolors = defcolors;

	cfid = 0;
	while (fgets(l, sizeof(l), fd)) {
		cfid++;
		p = strchr(l, '\n'); if (p) *p = '\0';
		p = l + strspn(l, " \t");
		if (*p == '#') continue;

		if (strlen(p) == 0) {
			/*
			 * Empty line means end of the current rule. So put it into the linked list.
			 */
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
		else if ((*p == '$') && strchr(l, '=')) {
			/* Define a macro */
			token_t *newtok = (token_t *) malloc(sizeof(token_t));
			char *delim;

			delim = strchr(l, '=');
			*delim = '\0';
			newtok->name = strdup(p+1);
			newtok->value = strdup(delim+1);
			newtok->next = tokhead;
			tokhead = newtok;
			continue;
		}

		/* Expand macros inside the line before parsing */
		p = strtok(preprocess(l), " ");
		while (p) {
			if ((strncasecmp(p, "PAGE=", 5) == 0) || (strncasecmp(p, "PAGES=", 6) == 0)) {
				char *val = strchr(p, '=')+1;
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->pagespec = strdup(val);
				if (*(crit->pagespec) == '%') crit->pagespecre = compileregex(crit->pagespec+1);
			}
			else if ((strncasecmp(p, "EXPAGE=", 7) == 0) || (strncasecmp(p, "EXPAGES=", 8) == 0)) {
				char *val = strchr(p, '=')+1;
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->expagespec = strdup(val);
				if (*(crit->expagespec) == '%') crit->expagespecre = compileregex(crit->expagespec+1);
			}
			else if ((strncasecmp(p, "HOST=", 5) == 0) || (strncasecmp(p, "HOSTS=", 6) == 0)) {
				char *val = strchr(p, '=')+1;
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->hostspec = strdup(val);
				if (*(crit->hostspec) == '%') crit->hostspecre = compileregex(crit->hostspec+1);
			}
			else if ((strncasecmp(p, "EXHOST=", 7) == 0) || (strncasecmp(p, "EXHOSTS=", 8) == 0)) {
				char *val = strchr(p, '=')+1;
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->exhostspec = strdup(val);
				if (*(crit->exhostspec) == '%') crit->exhostspecre = compileregex(crit->exhostspec+1);
			}
			else if ((strncasecmp(p, "SERVICE=", 8) == 0) || (strncasecmp(p, "SERVICES=", 9) == 0)) {
				char *val = strchr(p, '=')+1;
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->svcspec = strdup(val);
				if (*(crit->svcspec) == '%') crit->svcspecre = compileregex(crit->svcspec+1);
			}
			else if ((strncasecmp(p, "EXSERVICE=", 10) == 0) || (strncasecmp(p, "EXSERVICES=", 11) == 0)) {
				char *val = strchr(p, '=')+1;
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->exsvcspec = strdup(val);
				if (*(crit->exsvcspec) == '%') crit->exsvcspecre = compileregex(crit->exsvcspec+1);
			}
			else if ((strncasecmp(p, "COLOR=", 6) == 0) || (strncasecmp(p, "COLORS=", 7) == 0)) {
				criteria_t *crit = setup_criteria(&currule, &currcp);
				char *c1, *c2;
				int cval, reverse = 0;

				/* Put a value in crit->colors so we know there is an explicit color setting */
				crit->colors = (1 << 30);
				c1 = strchr(p, '=')+1;

				/*
				 * If the first colorspec is "!color", then apply the default colors and
				 * subtract colors from that.
				 */
				if (*c1 == '!') crit->colors |= defaultcolors;

				do {
					c2 = strchr(c1, ',');
					if (c2) *c2 = '\0';

					if (*c1 == '!') { reverse=1; c1++; }
					cval = (1 << parse_color(c1));

					if (reverse)
						crit->colors &= (~cval);
					else 
						crit->colors |= cval;

					if (c2) c1 = (c2+1); else c1 = NULL;
				} while (c1);
			}
			else if ((strncasecmp(p, "TIME=", 5) == 0) || (strncasecmp(p, "TIMES=", 6) == 0)) {
				char *val = strchr(p, '=')+1;
				criteria_t *crit = setup_criteria(&currule, &currcp);
				crit->timespec = strdup(val);
			}
			else if (strncasecmp(p, "DURATION", 8) == 0) {
				criteria_t *crit = setup_criteria(&currule, &currcp);
				if (*(p+8) == '>') crit->minduration = 60*atoi(p+9);
				else if (*(p+8) == '<') crit->maxduration = 60*atoi(p+9);
			}
			else if (strncasecmp(p, "RECOVERED", 9) == 0) {
				criteria_t *crit = setup_criteria(&currule, &currcp);
				currule->criteria->sendrecovered = SR_WANTED;
				crit->sendrecovered = SR_WANTED;
			}
			else if (currule && ((strncasecmp(p, "MAIL ", 5) == 0) || strchr(p, '@')) ) {
				recip_t *newrcp = (recip_t *)malloc(sizeof(recip_t));
				newrcp->cfid = cfid;
				newrcp->method = M_MAIL;
				newrcp->format = FRM_TEXT;
				newrcp->criteria = NULL;
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
			else if (currule && (strncasecmp(p, "SCRIPT ", 7) == 0)) {
				recip_t *newrcp = (recip_t *)malloc(sizeof(recip_t));
				newrcp->cfid = cfid;
				newrcp->method = M_SCRIPT;
				newrcp->format = FRM_TEXT;
				newrcp->criteria = NULL;
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
			else if (currule && (strncasecmp(p, "BBSCRIPT ", 9) == 0)) {
				recip_t *newrcp = (recip_t *)malloc(sizeof(recip_t));
				newrcp->cfid = cfid;
				newrcp->method = M_BBSCRIPT;
				newrcp->format = FRM_TEXT;
				newrcp->criteria = NULL;
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
			else if ((pstate == P_RECIP) && (strncasecmp(p, "FORMAT=", 7) == 0)) {
				if      (strcmp(p+7, "TEXT") == 0) currcp->format = FRM_TEXT;
				else if (strcmp(p+7, "SMS") == 0) currcp->format = FRM_SMS;
				else if (strcmp(p+7, "PAGER") == 0) currcp->format = FRM_PAGER;
			}
			else if ((pstate == P_RECIP) && (strncasecmp(p, "REPEAT=", 7) == 0)) {
				currcp->interval = 60*atoi(p+7);
			}

			if (p) p = strtok(NULL, " ");
		}
	}

	fclose(fd);
}

static void dump_criteria(criteria_t *crit, int isrecip)
{
	if (crit->pagespec) printf("PAGE=%s ", crit->pagespec);
	if (crit->expagespec) printf("EXPAGE=%s ", crit->expagespec);
	if (crit->hostspec) printf("HOST=%s ", crit->hostspec);
	if (crit->exhostspec) printf("EXHOST=%s ", crit->exhostspec);
	if (crit->svcspec) printf("SERVICE=%s ", crit->svcspec);
	if (crit->exsvcspec) printf("EXSERVICE=%s ", crit->exsvcspec);
	if (crit->colors) {
		int i, first = 1;

		printf("COLOR=");
		for (i = 0; (i < COL_COUNT); i++) {
			if ((1 << i) & crit->colors) {
				dprintf("first=%d, i=%d\n", first, i);
				printf("%s%s", (first ? "" : ","), colorname(i));
				first = 0;
			}
		}
		printf(" ");
	}

	if (crit->timespec) printf("TIME=%s ", crit->timespec);
	if (crit->minduration) printf("DURATION>%d ", (crit->minduration / 60));
	if (crit->maxduration) printf("DURATION<%d ", (crit->maxduration / 60));
	if (isrecip) {
		switch (crit->sendrecovered) {
		  case SR_WANTED: printf("RECOVERED "); break;
		  case SR_NOTWANTED: printf("NORECOVERED "); break;
		}
	}
}

void dump_alertconfig(void)
{
	rule_t *rulewalk;
	recip_t *recipwalk;

	for (rulewalk = rulehead; (rulewalk); rulewalk = rulewalk->next) {
		dump_criteria(rulewalk->criteria, 0);
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
			if (recipwalk->criteria) dump_criteria(recipwalk->criteria, 1);
			printf("\n");
		}
		printf("\n");
	}
}

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
		p = getenv("SVCCODES");
		if (p == NULL) p = "none";
		svccodes = (char *)malloc(strlen(p)+2);
		sprintf(svccodes, ",%s", p);
	}

	tname = (char *)malloc(strlen(testname)+3);
	sprintf(tname, ",%s:", testname);
	p = strstr(svccodes, tname);
	free(tname);

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

static int namematch(char *needle, char *haystack, pcre *pcrecode)
{
	char *xhay;
	char *xneedle;
	char *match;
	int result = 0;

	if (pcrecode) {
		/* Do regex matching. The regex has already been compiled for us. */
		int ovector[30];
		result = pcre_exec(pcrecode, NULL, needle, strlen(needle), 0, 0, ovector, (sizeof(ovector)/sizeof(int)));
		dprintf("pcre_exec returned %d\n", result);
		return (result >= 0);
	}

	/* Implement a simple, no-wildcard match */
	xhay = malloc(strlen(haystack) + 3);
	sprintf(xhay, ",%s,", haystack);
	xneedle = malloc(strlen(needle)+2);
	sprintf(xneedle, "%s,", needle);

	match = strstr(xhay, xneedle);
	if (match) {
		if (*(match-1) == '!') {
			/* Matched, but was a negative rule. */
			result = 0;
		}
		else if (*(match-1) == ',') {
			/* Matched */
			result = 1;
		}
		else {
			/* Matched a partial string. Fail. */
			result = 0;
		}
	}
	else {
		/* 
		 * It is not in the list. If the list is exclusively negative matches,
		 * we must return a positive result for "no match".
		 */
		char *p;

		/* Find the first name in the list that does not begin with a '!' */
		p = xhay+1;
		while (p && (*p == '!')) {
			p = strchr(p, ','); if (p) p++;
		}
		if (*p == '\0') result = 1;
	}

	free(xhay);
	free(xneedle);
	return result;
}

static int timematch(char *tspec)
{
	return within_sla(tspec, "", 0);
}

static int criteriamatch(activealerts_t *alert, criteria_t *crit)
{
	/*
	 * See if the "crit" matches the "alert".
	 * Match on pagespec, hostspec, svcspec, colors, timespec, minduration, maxduration, sendrecovered
	 */

	time_t duration;
	
	if (crit == NULL) return 1;	/* Only happens for recipient-list criteria */

	dprintf("criteriamatch %s:%s %s:%s:%s\n", alert->hostname->name, alert->testname->name, 
		textornull(crit->hostspec), textornull(crit->pagespec), textornull(crit->svcspec));

	duration = (time(NULL) - alert->eventstart);
	if (crit->minduration && (duration < crit->minduration)) { 
		dprintf("failed minduration %d<%d\n", duration, crit->minduration); 
		return 0; 
	}

	if (crit->maxduration && (duration > crit->maxduration)) { 
		dprintf("failed maxduration\n"); 
		return 0; 
	}

	if (crit->pagespec && !namematch(alert->location->name, crit->pagespec, crit->pagespecre)) { 
		dprintf("failed pagespec\n"); 
		return 0; 
	}
	if (crit->expagespec && namematch(alert->location->name, crit->expagespec, crit->expagespecre)) { 
		dprintf("matched expagespec, so drop it\n"); 
		return 0; 
	}

	if (crit->hostspec && !namematch(alert->hostname->name, crit->hostspec, crit->hostspecre)) { 
		dprintf("failed hostspec\n"); 
		return 0; 
	}
	if (crit->exhostspec && namematch(alert->hostname->name, crit->exhostspec, crit->exhostspecre)) { 
		dprintf("matched exhostspec, so drop it\n"); 
		return 0; 
	}

	if (crit->svcspec && !namematch(alert->testname->name, crit->svcspec, crit->svcspecre))  { 
		dprintf("failed svcspec\n"); 
		return 0; 
	}
	if (crit->exsvcspec && namematch(alert->testname->name, crit->exsvcspec, crit->exsvcspecre))  { 
		dprintf("matched exsvcspec, so drop it\n"); 
		return 0; 
	}

	if (crit->timespec && !timematch(crit->timespec)) { 
		dprintf("failed timespec\n"); 
		return 0; 
	}

	if (alert->state == A_RECOVERED) {
		dprintf("Checking for recovered setting %d\n", crit->sendrecovered);
		if (crit->sendrecovered) {
			return (crit->sendrecovered == SR_WANTED);
		}
	}

	/* We now know that the state is not A_RECOVERED, so final check is to match against the colors. */
	if (crit->colors) {
		int result = (((1 << alert->color) & crit->colors) != 0);
		dprintf("Checking explicit color setting %o against %o gives %d\n", crit->colors, alert->color, result);
		return result;
	}
	else {
		int result = (((1 << alert->color) & defaultcolors) != 0);
		dprintf("Checking default color setting %o against %o gives %d\n", defaultcolors, alert->color, result);
		return result;
	}
}

static recip_t *next_recipient(activealerts_t *alert, int *first)
{
	static rule_t *rulewalk = NULL;
	static recip_t *recipwalk = NULL;

	do {
		if (*first) {
			/* Start at beginning of rules-list and find the first matching rule. */
			*first = 0;
			rulewalk = rulehead;
			while (rulewalk && !criteriamatch(alert, rulewalk->criteria)) rulewalk = rulewalk->next;
			if (rulewalk) {
				/* Point recipwalk at the list of possible candidates */
				dprintf("Found a first matching rule\n");
				recipwalk = rulewalk->recipients; 
			}
			else {
				/* No matching rules */
				dprintf("Found no first matching rule\n");
				recipwalk = NULL;
			}
		}
		else {
			if (recipwalk->next) {
				/* Check the next recipient in the current rule */
				recipwalk = recipwalk->next;
			}
			else {
				/* End of recipients in current rule. Go to the next matching rule */
				do {
					rulewalk = rulewalk->next;
				} while (rulewalk && !criteriamatch(alert, rulewalk->criteria));

				if (rulewalk) {
					/* Point recipwalk at the list of possible candidates */
					dprintf("Found a secondary matching rule\n");
					recipwalk = rulewalk->recipients; 
				}
				else {
					/* No matching rules */
					dprintf("No more secondary matching rule\n");
					recipwalk = NULL;
				}
			}
		}
	} while (rulewalk && recipwalk && !criteriamatch(alert, recipwalk->criteria));

	return recipwalk;
}

static repeat_t *find_repeatinfo(activealerts_t *alert, recip_t *recip, int create)
{
	char *id;
	repeat_t *walk;

	id = (char *) malloc(strlen(alert->hostname->name) + strlen(alert->testname->name) + strlen(recip->recipient) + 3);
	sprintf(id, "%s|%s|%s", alert->hostname->name, alert->testname->name, recip->recipient);
	for (walk = rpthead; (walk && strcmp(walk->recipid, id)); walk = walk->next);

	if ((walk == NULL) && create) {
		walk = (repeat_t *)malloc(sizeof(repeat_t));
		walk->recipid = id;
		walk->nextalert = 0;
		walk->next = rpthead;
		rpthead = walk;
	}
	else 
		free(id);

	return walk;
}

static char *message_subject(activealerts_t *alert, recip_t *recip)
{
	static char subj[150];
	char *sev = "";

	switch (alert->color) {
	  case COL_RED:
		  sev = "is RED";
		  break;
	  case COL_YELLOW:
		  sev = "is YELLOW";
		  break;
	  case COL_CLEAR:
		  sev = "has no data";
		  break;
	  case COL_PURPLE:
		  sev = "stopped reporting to BB";
		  break;
	  case COL_GREEN:
		  sev = "recovered";
		  break;
	  case COL_BLUE:
		  sev = "is disabled";
		  break;
	}

	switch (recip->format) {
	  case FRM_TEXT:
		if (include_configid) {
			snprintf(subj, sizeof(subj)-1, "BB [%d] %s:%s %s [cfid:%d]",
				 alert->cookie, alert->hostname->name, alert->testname->name, sev, recip->cfid);
		}
		else {
			snprintf(subj, sizeof(subj)-1, "BB [%d] %s:%s %s",
				 alert->cookie, alert->hostname->name, alert->testname->name, sev);
		}
		return subj;

	 case FRM_SMS:
		return NULL;

	 case FRM_PAGER:
		return NULL;
	}

	return NULL;
}

static char *message_text(activealerts_t *alert, recip_t *recip)
{
	static char *buf = NULL;
	static int buflen = 0;
	char *eoln, *bom;
	char info[100];

	switch (recip->format) {
	  case FRM_TEXT:
		return msg_data(alert->pagemessage);

	  case FRM_SMS:
		/*
		 * Send a report containing a brief alert
		 * and any lines that begin with a "&COLOR"
		 */
		sprintf(info, "%s:%s %s [%d]", 
			alert->hostname->name, alert->testname->name, 
			colorname(alert->color), alert->cookie);
		addtobuffer(&buf, &buflen, info);
		bom = msg_data(alert->pagemessage);
		eoln = strchr(bom, '\n');
		if (eoln) {
			bom = eoln;
			while ((bom = strstr(bom, "\n&")) != NULL) {
				eoln = strchr(bom+1, '\n'); if (eoln) *eoln = '\0';
				if ((strncmp(bom, "&red", 4) == 0) || (strncmp(bom, "&yellow", 7) == 0)) 
					addtobuffer(&buf, &buflen, bom);
				if (eoln) *eoln = '\n';
				bom = (eoln ? eoln+1 : "");
			}
		}
		return buf;

	  case FRM_PAGER:
		return "";
	}

	return alert->pagemessage;
}

void send_alert(activealerts_t *alert, FILE *logfd)
{
	recip_t *recip;
	repeat_t *rpt;
	int first = 1;
	time_t now = time(NULL);

	dprintf("send_alert %s:%s state %d\n", alert->hostname->name, alert->testname->name, (int)alert->state);

	while ((recip = next_recipient(alert, &first)) != NULL) {
		rpt = find_repeatinfo(alert, recip, 1);
		dprintf("  repeat %s at %d\n", rpt->recipid, rpt->nextalert);
		if (rpt->nextalert > now) continue;

		dprintf("  Alert for %s:%s to %s\n", alert->hostname->name, alert->testname->name, recip->recipient);
		switch (recip->method) {
		  case M_MAIL:
			{
				char cmd[32768];
				char *mailsubj;
				FILE *mailpipe;

				mailsubj = message_subject(alert, recip);

				if (mailsubj) {
					if (getenv("MAIL")) 
						sprintf(cmd, "%s \"%s\" ", getenv("MAIL"), mailsubj);
					else if (getenv("MAILC"))
						sprintf(cmd, "%s -s \"%s\" ", getenv("MAILC"), mailsubj);
					else 
						sprintf(cmd, "mail -s \"%s\" ", mailsubj);
				}
				else {
					if (getenv("MAILC"))
						sprintf(cmd, "%s ", getenv("MAILC"));
					else 
						sprintf(cmd, "mail ");
				}
				strcat(cmd, recip->recipient);

				mailpipe = popen(cmd, "w");
				if (mailpipe) {
					fprintf(mailpipe, "%s", message_text(alert, recip));
					pclose(mailpipe);
					if (logfd) {
						init_timestamp();
						fprintf(logfd, "%s %s.%s (%s) %s %d %d",
							timestamp, alert->hostname->name, alert->testname->name,
							alert->ip, recip->recipient, (int)now, 
							servicecode(alert->testname->name));
						if (alert->state == A_RECOVERED) {
							fprintf(logfd, " %d\n", (int)(now - alert->eventstart));
						}
						else {
							fprintf(logfd, "\n");
						}
						fflush(logfd);
					}
				}
			}
			break;

		  case M_SCRIPT:
			break;

		  case M_BBSCRIPT:
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
	time_t now = time(NULL);
	int first = 1;
	time_t nexttime = now+86400;
	recip_t *recip;
	repeat_t *rpt;

	while ((recip = next_recipient(alert, &first)) != NULL) {
		rpt = find_repeatinfo(alert, recip, 1);
		if (rpt->nextalert <= now) rpt->nextalert = (now + recip->interval);
		if (rpt->nextalert < nexttime) nexttime = rpt->nextalert;
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

	id = (char *)malloc(strlen(alert->hostname->name)+strlen(alert->testname->name)+3);
	sprintf(id, "%s|%s|", alert->hostname->name, alert->testname->name);
	rptwalk = rpthead; rptprev = NULL;
	while (rptwalk) {
		if (strncmp(rptwalk->recipid, id, strlen(id)) == 0) {
			repeat_t *tmp = rptwalk;

			if (rptwalk == rpthead) {
				rptwalk = rpthead = rpthead->next;
			}
			else {
				rptprev->next = rptwalk->next;
				rptwalk = rptwalk->next;
			}

			free(tmp->recipid);
			free(tmp);
		}
		else {
			rptprev = rptwalk;
			rptwalk = rptwalk->next;
		}
	}
}

void clear_interval(activealerts_t *alert)
{
	int first = 1;
	recip_t *recip;
	repeat_t *rpt;

	alert->nextalerttime = 0;
	while ((recip = next_recipient(alert, &first)) != NULL) {
		rpt = find_repeatinfo(alert, recip, 0);
		if (rpt) {
			dprintf("Cleared repeat interval for %s\n", rpt->recipid);
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
		fprintf(fd, "%d|%s\n", (int) walk->nextalert, walk->recipid);
	}
	fclose(fd);
}

void load_state(char *filename)
{
	FILE *fd = fopen(filename, "r");
	char l[8192];
	char *p;

	if (fd == NULL) return;
	while (fgets(l, sizeof(l), fd)) {
		p = strchr(l, '\n'); if (p) *p = '\0';

		p = strchr(l, '|');
		if (p) {
			repeat_t *newrpt;

			*p = '\0';
			if (atoi(l) > time(NULL)) {
				newrpt = (repeat_t *)malloc(sizeof(repeat_t));
				newrpt->recipid = strdup(p+1);
				newrpt->nextalert = atoi(l);
				newrpt->next = rpthead;
				rpthead = newrpt;
			}
		}
	}

	fclose(fd);
}

