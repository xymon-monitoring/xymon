/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This is part of the hobbitd_alert worker module.                           */
/* This module implements the standard hobbitd alerting function. It loads    */
/* the alert configuration from hobbit-alerts.cfg, and incoming alerts are    */
/* then sent according to the rules defined.                                  */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: do_alert.c,v 1.65 2005-05-22 07:31:22 henrik Exp $";

/*
 * The alert API defines three functions that must be implemented:
 *
 * - void load_alertconfig(char *filename, int defaultcolors, int defaultinterval)
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
 * of the main hobbitd_alert worker.
 * send_alert() runs in a sub-proces forked from hobbitd_alert.
 *
 */

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

#include "hobbitd_alert.h"

int include_configid = 0;  /* Whether to include the configuration file linenumber in alerts */
int testonly = 0;	   /* Test mode, dont actually send out alerts */

enum method_t { M_MAIL, M_SCRIPT };
enum msgformat_t { FRM_TEXT, FRM_PLAIN, FRM_SMS, FRM_PAGER, FRM_SCRIPT };
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
	char *cfline;
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
	int minduration, maxduration;	/* In seconds */
	enum recovermsg_t sendrecovered, sendnotice;
} criteria_t;

/* This defines a recipient. There may be some criteria, and then how we send alerts to him */
typedef struct recip_t {
	int cfid;
	criteria_t *criteria;
	enum method_t method;
	char *recipient;
	char *scriptname;
	enum msgformat_t format;
	time_t interval;		/* In seconds */
	int stoprule, unmatchedonly, noalerts;
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
static rule_t *ruletail = NULL;
static int cfid = 0;
static char cfline[256];
static int stoprulefound = 0;
static int printmode = 0;
static rule_t *printrule = NULL;

/*
 * This is the dynamic info stored to keep track of active alerts. We
 * need to keep track of when the next alert is due for each recipient,
 * and this goes on a host+test+recipient basis.
 */
typedef struct repeat_t {
	char *recipid;	/* Essentially hostname|testname|method|address */
	time_t nextalert;
	struct repeat_t *next;
} repeat_t;
static repeat_t *rpthead = NULL;

static enum { P_NONE, P_RULE, P_RECIP } pstate = P_NONE;
static int defaultcolors = 0;

static criteria_t *setup_criteria(rule_t **currule, recip_t **currcp)
{
	criteria_t *crit = NULL;

	MEMDEFINE(cfline);

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
		if (crit->cfline == NULL) crit->cfline = strdup(cfline);
		*currcp = NULL;
		break;

	  case P_RECIP:
		if (!(*currcp)->criteria) {
			recip_t *rwalk;

			(*currcp)->criteria = (criteria_t *)calloc(1, sizeof(criteria_t));

			/* Make sure other recipients on the same rule also get these criteria */
			for (rwalk = (*currule)->recipients; (rwalk); rwalk = rwalk->next) {
				if (rwalk->cfid == cfid) rwalk->criteria = (*currcp)->criteria;
			}
		}
		crit = (*currcp)->criteria;
		crit->cfid = cfid;
		if (crit->cfline == NULL) crit->cfline = strdup(cfline);
		break;
	}

	MEMUNDEFINE(cfline);
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
			char savech;

			*p = '\0';
			n = strlen(inp);
			strcat(outp, inp);
			outp += n;
			p = (p+1);

			n = strcspn(p, "\t $.,|%!()[]{}+?/&@:;*");
			savech = *(p+n);
			*(p+n) = '\0';
			for (twalk = tokhead; (twalk && strcmp(p, twalk->name)); twalk = twalk->next) ;
			*(p+n) = savech;

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

static pcre *compileregex(char *pattern)
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

static void flush_rule(rule_t *currule)
{
	if (currule == NULL) return;

	currule->next = NULL;

	if (rulehead == NULL) {
		rulehead = ruletail = currule;
	}
	else {
		ruletail->next = currule;
		ruletail = currule;
	}
}

static void free_criteria(criteria_t *crit)
{
	if (crit->cfline)       xfree(crit->cfline);
	if (crit->pagespec)     xfree(crit->pagespec);
	if (crit->pagespecre)   pcre_free(crit->pagespecre);
	if (crit->expagespec)   xfree(crit->expagespec);
	if (crit->expagespecre) pcre_free(crit->expagespecre);
	if (crit->hostspec)     xfree(crit->hostspec);
	if (crit->hostspecre)   pcre_free(crit->hostspecre);
	if (crit->exhostspec)   xfree(crit->exhostspec);
	if (crit->exhostspecre) pcre_free(crit->exhostspecre);
	if (crit->svcspec)      xfree(crit->svcspec);
	if (crit->svcspecre)    pcre_free(crit->svcspecre);
	if (crit->exsvcspec)    xfree(crit->exsvcspec);
	if (crit->exsvcspecre)  pcre_free(crit->exsvcspecre);
	if (crit->timespec)     xfree(crit->timespec);
}

void load_alertconfig(char *configfn, int defcolors, int defaultinterval)
{
	/* (Re)load the configuration file without leaking memory */
	static time_t lastload = 0;	/* Last time the config file was loaded */
	char fn[PATH_MAX];
	struct stat st;
	FILE *fd;
	char l[8192];
	char *p;
	rule_t *currule = NULL;
	recip_t *currcp = NULL, *rcptail = NULL;

	MEMDEFINE(fn);

	if (configfn) strcpy(fn, configfn); else sprintf(fn, "%s/etc/hobbit-alerts.cfg", xgetenv("BBHOME"));
	if (stat(fn, &st) == -1) { MEMUNDEFINE(fn); return; }
	if (st.st_mtime == lastload) { MEMUNDEFINE(fn); return; }
	lastload = st.st_mtime;

	fd = fopen(fn, "r");
	if (!fd) { MEMUNDEFINE(fn); return; }

	/* First, clean out the old rule set */
	while (rulehead) {
		rule_t *trule;

		if (rulehead->criteria) {
			free_criteria(rulehead->criteria);
			xfree(rulehead->criteria);
		}

		while (rulehead->recipients) {
			recip_t *trecip = rulehead->recipients;

			if (trecip->criteria) {
				recip_t *rwalk;

				/* Clear out the duplicate criteria that may exist, to avoid double-free'ing them */
				for (rwalk = trecip->next; (rwalk); rwalk = rwalk->next) {
					if (rwalk->criteria == trecip->criteria) rwalk->criteria = NULL;
				}

				free_criteria(trecip->criteria);
				xfree(trecip->criteria);
			}

			if (trecip->recipient)  xfree(trecip->recipient);
			if (trecip->scriptname) xfree(trecip->scriptname);
			rulehead->recipients = rulehead->recipients->next;
			xfree(trecip);
		}
		trule = rulehead;
		rulehead = rulehead->next;
		xfree(trule);
	}

	while (tokhead) {
		token_t *ttok;

		if (tokhead->name)  xfree(tokhead->name);
		if (tokhead->value) xfree(tokhead->value);
		ttok = tokhead;
		tokhead = tokhead->next;
		xfree(ttok);
	}

	defaultcolors = defcolors;

	MEMDEFINE(cfline);

	cfid = 0;
	while (fgets(l, sizeof(l), fd)) {
		int firsttoken = 1;
		int mailcmdactive = 0, scriptcmdactive = 0;
		recip_t *curlinerecips = NULL;

		cfid++;
		grok_input(l);

		/* Skip empty lines */
		if (strlen(l) == 0) continue;

		if ((*l == '$') && strchr(l, '=')) {
			/* Define a macro */
			token_t *newtok = (token_t *) malloc(sizeof(token_t));
			char *delim;

			delim = strchr(l, '=');
			*delim = '\0';
			newtok->name = strdup(l+1);	/* Skip the '$' */
			newtok->value = strdup(delim+1);
			newtok->next = tokhead;
			tokhead = newtok;
			continue;
		}

		strncpy(cfline, l, (sizeof(cfline)-1));
		cfline[sizeof(cfline)-1] = '\0';

		/* Expand macros inside the line before parsing */
		p = strtok(preprocess(l), " \t");
		while (p) {
			if ((strncasecmp(p, "PAGE=", 5) == 0) || (strncasecmp(p, "PAGES=", 6) == 0)) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->pagespec = strdup(val);
				if (*(crit->pagespec) == '%') crit->pagespecre = compileregex(crit->pagespec+1);
				firsttoken = 0;
			}
			else if ((strncasecmp(p, "EXPAGE=", 7) == 0) || (strncasecmp(p, "EXPAGES=", 8) == 0)) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->expagespec = strdup(val);
				if (*(crit->expagespec) == '%') crit->expagespecre = compileregex(crit->expagespec+1);
				firsttoken = 0;
			}
			else if ((strncasecmp(p, "HOST=", 5) == 0) || (strncasecmp(p, "HOSTS=", 6) == 0)) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->hostspec = strdup(val);
				if (*(crit->hostspec) == '%') crit->hostspecre = compileregex(crit->hostspec+1);
				firsttoken = 0;
			}
			else if ((strncasecmp(p, "EXHOST=", 7) == 0) || (strncasecmp(p, "EXHOSTS=", 8) == 0)) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->exhostspec = strdup(val);
				if (*(crit->exhostspec) == '%') crit->exhostspecre = compileregex(crit->exhostspec+1);
				firsttoken = 0;
			}
			else if ((strncasecmp(p, "SERVICE=", 8) == 0) || (strncasecmp(p, "SERVICES=", 9) == 0)) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->svcspec = strdup(val);
				if (*(crit->svcspec) == '%') crit->svcspecre = compileregex(crit->svcspec+1);
				firsttoken = 0;
			}
			else if ((strncasecmp(p, "EXSERVICE=", 10) == 0) || (strncasecmp(p, "EXSERVICES=", 11) == 0)) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->exsvcspec = strdup(val);
				if (*(crit->exsvcspec) == '%') crit->exsvcspecre = compileregex(crit->exsvcspec+1);
				firsttoken = 0;
			}
			else if ((strncasecmp(p, "COLOR=", 6) == 0) || (strncasecmp(p, "COLORS=", 7) == 0)) {
				criteria_t *crit;
				char *c1, *c2;
				int cval, reverse = 0;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				crit = setup_criteria(&currule, &currcp);

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
				firsttoken = 0;
			}
			else if ((strncasecmp(p, "TIME=", 5) == 0) || (strncasecmp(p, "TIMES=", 6) == 0)) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->timespec = strdup(val);
				firsttoken = 0;
			}
			else if (strncasecmp(p, "DURATION", 8) == 0) {
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				crit = setup_criteria(&currule, &currcp);
				if (*(p+8) == '>') crit->minduration = 60*durationvalue(p+9);
				else if (*(p+8) == '<') crit->maxduration = 60*durationvalue(p+9);
				else errprintf("Ignoring invalid DURATION at line %d: %s\n",cfid, p);
				firsttoken = 0;
			}
			else if (strncasecmp(p, "RECOVERED", 9) == 0) {
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				crit = setup_criteria(&currule, &currcp);
				crit->sendrecovered = SR_WANTED;
				firsttoken = 0;
			}
			else if (strncasecmp(p, "NORECOVERED", 11) == 0) {
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				crit = setup_criteria(&currule, &currcp);
				crit->sendrecovered = SR_NOTWANTED;
				firsttoken = 0;
			}
			else if (strncasecmp(p, "NOTICE", 6) == 0) {
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				crit = setup_criteria(&currule, &currcp);
				crit->sendnotice = SR_WANTED;
				firsttoken = 0;
			}
			else if (strncasecmp(p, "NONOTICE", 8) == 0) {
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				crit = setup_criteria(&currule, &currcp);
				crit->sendnotice = SR_NOTWANTED;
				firsttoken = 0;
			}
			else if ((pstate == P_RECIP) && (strncasecmp(p, "FORMAT=", 7) == 0)) {
				if (!currcp) errprintf("FORMAT used without a recipient (line %d), ignored\n", cfid);
				else if (strcasecmp(p+7, "TEXT") == 0) currcp->format = FRM_TEXT;
				else if (strcasecmp(p+7, "PLAIN") == 0) currcp->format = FRM_PLAIN;
				else if (strcasecmp(p+7, "SMS") == 0) currcp->format = FRM_SMS;
				else if (strcasecmp(p+7, "PAGER") == 0) currcp->format = FRM_PAGER;
				else if (strcasecmp(p+7, "SCRIPT") == 0) currcp->format = FRM_SCRIPT;
				else errprintf("Unknown FORMAT setting '%s' ignored\n", p);
				firsttoken = 0;
			}
			else if ((pstate == P_RECIP) && (strncasecmp(p, "REPEAT=", 7) == 0)) {
				if (!currcp) errprintf("REPEAT used without a recipient (line %d), ignored\n", cfid);
				else currcp->interval = 60*durationvalue(p+7);
				firsttoken = 0;
			}
			else if ((pstate == P_RECIP) && (strcasecmp(p, "STOP") == 0)) {
				if (!currcp) errprintf("STOP used without a recipient (line %d), ignored\n", cfid);
				else currcp->stoprule = 1;
				firsttoken = 0;
			}
			else if ((pstate == P_RECIP) && (strcasecmp(p, "UNMATCHED") == 0)) {
				if (!currcp) errprintf("UNMATCHED used without a recipient (line %d), ignored\n", cfid);
				else currcp->unmatchedonly = 1;
				firsttoken = 0;
			}
			else if ((pstate == P_RECIP) && (strncasecmp(p, "NOALERT", 7) == 0)) {
				if (!currcp) errprintf("NOALERT used without a recipient (line %d), ignored\n", cfid);
				else currcp->noalerts = 1;
				firsttoken = 0;
			}
			else if (currule && ((strncasecmp(p, "MAIL", 4) == 0) || mailcmdactive) ) {
				recip_t *newrcp;

				mailcmdactive = 1;
				newrcp = (recip_t *)malloc(sizeof(recip_t));
				newrcp->cfid = cfid;
				newrcp->method = M_MAIL;
				newrcp->format = FRM_TEXT;
				newrcp->criteria = NULL;
				newrcp->recipient = NULL;
				newrcp->scriptname = NULL;

				if (strncasecmp(p, "MAIL=", 5) == 0) {
					p += 5;
				}
				else if (strcasecmp(p, "MAIL") == 0) {
					p = strtok(NULL, " \t");
				}
				else {
					/* Second recipient on a rule - do nothing */
				}

				if (p) {
					newrcp->recipient = strdup(p);
					newrcp->interval = defaultinterval;
					newrcp->stoprule = 0;
					newrcp->unmatchedonly = 0;
					newrcp->noalerts = 0;
					newrcp->next = NULL;
					currcp = newrcp;
					if (curlinerecips == NULL) curlinerecips = newrcp;
					pstate = P_RECIP;

					if (currule->recipients == NULL)
						currule->recipients = rcptail = newrcp;
					else {
						rcptail->next = newrcp;
						rcptail = newrcp;
					}
				}
				else {
					errprintf("Ignoring MAIL with no recipient at line %d\n", cfid);
					xfree(newrcp);
				}
				firsttoken = 0;
			}
			else if (currule && ((strncasecmp(p, "SCRIPT", 6) == 0) || scriptcmdactive)) {
				recip_t *newrcp;

				scriptcmdactive = 1;
				newrcp = (recip_t *)malloc(sizeof(recip_t));
				newrcp->cfid = cfid;
				newrcp->method = M_SCRIPT;
				newrcp->format = FRM_SCRIPT;
				newrcp->criteria = NULL;
				newrcp->scriptname = NULL;

				if (strncasecmp(p, "SCRIPT=", 7) == 0) {
					p += 7;
					newrcp->scriptname = strdup(p);
					p = strtok(NULL, " \t");
				}
				else if (strcasecmp(p, "SCRIPT") == 0) {
					p = strtok(NULL, " \t");
					if (p) {
						newrcp->scriptname = strdup(p);
						p = strtok(NULL, " \t");
					}
					else {
						errprintf("Invalid SCRIPT command at line %d\n", cfid);
					}
				}
				else {
					/* A second recipient for the same script as the previous one */
					newrcp->scriptname = strdup(currcp->scriptname);
				}

				if (p) {
					newrcp->recipient = strdup(p);
					newrcp->interval = defaultinterval;
					newrcp->stoprule = 0;
					newrcp->unmatchedonly = 0;
					newrcp->noalerts = 0;
					newrcp->next = NULL;
					currcp = newrcp;
					if (curlinerecips == NULL) curlinerecips = newrcp;
					pstate = P_RECIP;

					if (currule->recipients == NULL)
						currule->recipients = rcptail = newrcp;
					else {
						rcptail->next = newrcp;
						rcptail = newrcp;
					}
				}
				else {
					errprintf("Ignoring SCRIPT with no recipient at line %d\n", cfid);
					if (newrcp->scriptname) xfree(newrcp->scriptname);
					xfree(newrcp);
				}
				firsttoken = 0;
			}
			else {
				errprintf("Ignored unknown/unexpected token '%s' at line %d\n", p, cfid);
			}

			if (p) p = strtok(NULL, " ");
		}

		if (curlinerecips && currcp && (curlinerecips != currcp)) {
			/* We have multiple recipients on one line. Make sure criteria etc. get copied */
			recip_t *rwalk;

			/* All criteria etc. have been set on the last recipient (currcp) */
			for (rwalk = curlinerecips; (rwalk != currcp); rwalk = rwalk->next) {
				rwalk->format = currcp->format;
				rwalk->interval = currcp->interval;
				rwalk->criteria = currcp->criteria;
				rwalk->noalerts = currcp->noalerts;
			}
		}
	}

	flush_rule(currule);
	fclose(fd);

	MEMUNDEFINE(cfline);
	MEMUNDEFINE(fn);
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
		  case SR_UNKNOWN: break;
		  case SR_WANTED: printf("RECOVERED "); break;
		  case SR_NOTWANTED: printf("NORECOVERED "); break;
		}
		switch (crit->sendnotice) {
		  case SR_UNKNOWN: break;
		  case SR_WANTED: printf("NOTICE "); break;
		  case SR_NOTWANTED: printf("NONOTICE "); break;
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
			  case M_MAIL   : printf("MAIL %s ", recipwalk->recipient); break;
			  case M_SCRIPT : printf("SCRIPT %s %s ", recipwalk->scriptname, recipwalk->recipient); break;
			}
			switch (recipwalk->format) {
			  case FRM_TEXT  : printf("FORMAT=TEXT "); break;
			  case FRM_PLAIN : printf("FORMAT=PLAIN "); break;
			  case FRM_SMS   : printf("FORMAT=SMS "); break;
			  case FRM_PAGER : printf("FORMAT=PAGER "); break;
			  case FRM_SCRIPT: printf("FORMAT=SCRIPT "); break;
			}
			printf("REPEAT=%d ", (int)(recipwalk->interval / 60));
			if (recipwalk->criteria) dump_criteria(recipwalk->criteria, 1);
			if (recipwalk->unmatchedonly) printf("UNMATCHED ");
			if (recipwalk->stoprule) printf("STOP ");
			if (recipwalk->noalerts) printf("NOALERT ");
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

	if (strcmp(haystack, "*") == 0) {
		/* Match anything */
		return 1;
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

	xfree(xhay);
	xfree(xneedle);

	return result;
}

static int timematch(char *tspec)
{
	int result;

	result = within_sla(tspec, 0);

	return result;
}

static int criteriamatch(activealerts_t *alert, criteria_t *crit, criteria_t *rulecrit)
{
	/*
	 * See if the "crit" matches the "alert".
	 * Match on pagespec, hostspec, svcspec, colors, timespec, minduration, maxduration, sendrecovered
	 */

	time_t duration;
	int result, cfid = 0;
	char *pgname = alert->location->name;
	char *cfline = NULL;

	/* The top-level page needs a name - cannot match against an empty string */
	if (strlen(pgname) == 0) pgname = "/";

	if (crit) { cfid = crit->cfid; cfline = crit->cfline; }
	if (!cfid && rulecrit) cfid = rulecrit->cfid;
	if (!cfline && rulecrit) cfline = rulecrit->cfline;
	if (!cfline) cfline = "<undefined>";

	traceprintf("Matching host:service:page '%s:%s:%s' against rule line %d\n",
			alert->hostname->name, alert->testname->name, alert->location->name, cfid);

	if (crit && crit->pagespec && !namematch(pgname, crit->pagespec, crit->pagespecre)) { 
		traceprintf("Failed '%s' (pagename not in include list)\n", cfline);
		return 0; 
	}
	if (crit && crit->expagespec && namematch(pgname, crit->expagespec, crit->expagespecre)) { 
		traceprintf("Failed '%s' (pagename excluded)\n", cfline);
		return 0; 
	}

	if (crit && crit->hostspec && !namematch(alert->hostname->name, crit->hostspec, crit->hostspecre)) { 
		traceprintf("Failed '%s' (hostname not in include list)\n", cfline);
		return 0; 
	}
	if (crit && crit->exhostspec && namematch(alert->hostname->name, crit->exhostspec, crit->exhostspecre)) { 
		traceprintf("Failed '%s' (hostname excluded)\n", cfline);
		return 0; 
	}

	if (crit && crit->svcspec && !namematch(alert->testname->name, crit->svcspec, crit->svcspecre))  { 
		traceprintf("Failed '%s' (service not in include list)\n", cfline);
		return 0; 
	}
	if (crit && crit->exsvcspec && namematch(alert->testname->name, crit->exsvcspec, crit->exsvcspecre))  { 
		traceprintf("Failed '%s' (service excluded)\n", cfline);
		return 0; 
	}

	if (alert->state == A_NOTIFY) {
		/*
		 * Dont do the check until we are checking individual recipients (rulecrit is set).
		 * You dont need to have NOTICE on the top-level rule, it's enough if a recipient
		 * has it set. However, we do want to allow there to be a default defined in the
		 * rule; but it doesn't take effect until we start checking the recipients.
		 */
		if (rulecrit) {
			int n = (crit ? crit->sendnotice : -1);
			traceprintf("Checking NOTICE setting %d (rule:%d)\n", n, rulecrit->sendnotice);
			if (crit && (crit->sendnotice == SR_NOTWANTED)) result = 0;	/* Explicit NONOTICE */
			else if (crit && (crit->sendnotice == SR_WANTED)) result = 1;	/* Explicit NOTICE */
			else result = (rulecrit->sendnotice == SR_WANTED);		/* Not set, but rule has NOTICE */
		}
		else {
			result = 1;
		}

		if (!result) traceprintf("Failed '%s' (notice not wanted)\n", cfline);
		return result;
	}

	duration = (time(NULL) - alert->eventstart);
	if (crit && crit->minduration && (duration < crit->minduration)) { 
		traceprintf("Failed '%s' (min. duration %d<%d)\n", cfline, duration, crit->minduration);
		if (!printmode) return 0; 
	}

	if (crit && crit->maxduration && (duration > crit->maxduration)) { 
		traceprintf("Failed '%s' (max. duration %d>%d)\n", cfline, duration, crit->maxduration);
		if (!printmode) return 0; 
	}

	if (crit && crit->timespec && !timematch(crit->timespec)) { 
		traceprintf("Failed '%s' (time criteria)\n", cfline);
		if (!printmode) return 0; 
	}

	/* Check color. For RECOVERED messages, this holds the color of the alert, not the recovery state */
	if (crit && crit->colors) {
		result = (((1 << alert->color) & crit->colors) != 0);
		if (printmode) return 1;
	}
	else {
		result = (((1 << alert->color) & defaultcolors) != 0);
		if (printmode) return 1;
	}
	if (!result) {
		traceprintf("Failed '%s' (color)\n", cfline);
		return result;
	}

	if (alert->state == A_RECOVERED) {
		/*
		 * Dont do the check until we are checking individual recipients (rulecrit is set).
		 * You dont need to have RECOVERED on the top-level rule, it's enough if a recipient
		 * has it set. However, we do want to allow there to be a default defined in the
		 * rule; but it doesn't take effect until we start checking the recipients.
		 */
		if (rulecrit) {
			int n = (crit ? crit->sendrecovered : -1);
			traceprintf("Checking recovered setting %d (rule:%d)\n", n, rulecrit->sendrecovered);
			if (crit && (crit->sendrecovered == SR_NOTWANTED)) result = 0;		/* Explicit NORECOVERED */
			else if (crit && (crit->sendrecovered == SR_WANTED)) result = 1;	/* Explicit RECOVERED */
			else result = (rulecrit->sendrecovered == SR_WANTED);	/* Not set, but rule has RECOVERED */
		}
		else {
			result = 1;
		}

		if (printmode) return result;
	}

	if (result) {
		traceprintf("*** Match with '%s' ***\n", cfline);
	}

	return result;
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
			while (rulewalk && !criteriamatch(alert, rulewalk->criteria, NULL)) rulewalk = rulewalk->next;
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
				} while (rulewalk && !criteriamatch(alert, rulewalk->criteria, NULL));

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
	} while (rulewalk && recipwalk && !criteriamatch(alert, recipwalk->criteria, rulewalk->criteria));

	stoprulefound = (recipwalk && recipwalk->stoprule);

	printrule = rulewalk;
	return recipwalk;
}

static repeat_t *find_repeatinfo(activealerts_t *alert, recip_t *recip, int create)
{
	char *id, *method = "unknown";
	repeat_t *walk;

	switch (recip->method) {
	  case M_MAIL: method = "mail"; break;
	  case M_SCRIPT: method = "script"; break;
	}

	id = (char *) malloc(strlen(alert->hostname->name) + strlen(alert->testname->name) + strlen(method) + strlen(recip->recipient) + 4);
	sprintf(id, "%s|%s|%s|%s", alert->hostname->name, alert->testname->name, method, recip->recipient);
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

	/* Only subjects on FRM_TEXT and FRM_PLAIN messages */
	if ((recip->format != FRM_TEXT) && (recip->format != FRM_PLAIN)) return NULL;

	MEMDEFINE(subj);

	if ((alert->color >= 0) && (alert->color < COL_COUNT)) sev = sevtxt[alert->color];

	switch (alert->state) {
	  case A_PAGING:
	  case A_ACKED:
		subjfmt = (include_configid ? "Hobbit [%d] %s:%s %s [cfid:%d]" :  "Hobbit [%d] %s:%s %s");
		snprintf(subj, sizeof(subj)-1, subjfmt, 
			 alert->cookie, alert->hostname->name, alert->testname->name, sev, recip->cfid);
		break;

	  case A_NOTIFY:
		subjfmt = (include_configid ? "Hobbit %s:%s NOTICE [cfid:%d]" :  "Hobbit %s:%s NOTICE");
		snprintf(subj, sizeof(subj)-1, subjfmt, 
			 alert->hostname->name, alert->testname->name, recip->cfid);
		break;

	  case A_RECOVERED:
		subjfmt = (include_configid ? "Hobbit %s:%s recovered [cfid:%d]" :  "Hobbit %s:%s recovered");
		snprintf(subj, sizeof(subj)-1, subjfmt, 
			 alert->hostname->name, alert->testname->name, recip->cfid);
		break;

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
	static char *buf = NULL;
	static int buflen = 0;
	char *eoln, *bom, *p;
	char info[4096];

	MEMDEFINE(info);

	if (buf) *buf = '\0';

	if (alert->state == A_NOTIFY) {
		sprintf(info, "%s:%s INFO\n", alert->hostname->name, alert->testname->name);
		addtobuffer(&buf, &buflen, info);
		addtobuffer(&buf, &buflen, alert->pagemessage);
		MEMUNDEFINE(info);
		return buf;
	}

	switch (recip->format) {
	  case FRM_TEXT:
	  case FRM_PLAIN:
		bom = msg_data(alert->pagemessage);
		eoln = strchr(bom, '\n'); if (eoln) *eoln = '\0';

		/* If there's a "<-- flags:.... -->" then remove it from the message */
		if ((p = strstr(bom, "<!--")) != NULL) {
			/* Add the part of line 1 before the flags ... */
			*p = '\0'; addtobuffer(&buf, &buflen, bom); *p = '<'; 

			/* And the part of line 1 after the flags ... */
			p = strstr(p, "-->"); if (p) addtobuffer(&buf, &buflen, p+3);

			/* And if there is more than line 1, add it as well */
			if (eoln) {
				*eoln = '\n';
				addtobuffer(&buf, &buflen, eoln);
			}
		}
		else {
			if (eoln) *eoln = '\n';
			addtobuffer(&buf, &buflen, bom);
		}

		addtobuffer(&buf, &buflen, "\n");

		if (recip->format == FRM_TEXT) {
			sprintf(info, "See %s%s/bb-hostsvc.sh?HOSTSVC=%s.%s\n", 
				xgetenv("BBWEBHOST"), xgetenv("CGIBINURL"), 
				commafy(alert->hostname->name), alert->testname->name);
			addtobuffer(&buf, &buflen, info);
		}

		MEMUNDEFINE(info);
		return buf;

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
		MEMUNDEFINE(info);
		return buf;

	  case FRM_SCRIPT:
		sprintf(info, "%s:%s %s [%d]\n",
			alert->hostname->name, alert->testname->name, colorname(alert->color), alert->cookie);
		addtobuffer(&buf, &buflen, info);
		addtobuffer(&buf, &buflen, msg_data(alert->pagemessage));
		addtobuffer(&buf, &buflen, "\n");
		sprintf(info, "See %s%s/bb-hostsvc.sh?HOSTSVC=%s.%s\n", 
			xgetenv("BBWEBHOST"), xgetenv("CGIBINURL"), 
			commafy(alert->hostname->name), alert->testname->name);
		addtobuffer(&buf, &buflen, info);
		MEMUNDEFINE(info);
		return buf;

	  case FRM_PAGER:
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
	time_t now = time(NULL);
	char *alerttxt[A_DEAD+1] = { "Paging", "Acked", "Recovered", "Notify", "Dead" };

	dprintf("send_alert %s:%s state %d\n", alert->hostname->name, alert->testname->name, (int)alert->state);
	traceprintf("send_alert %s:%s state %s\n", 
		    alert->hostname->name, alert->testname->name, alerttxt[alert->state]);

	stoprulefound = 0;

	while (!stoprulefound && ((recip = next_recipient(alert, &first)) != NULL)) {
		/* If this is an "UNMATCHED" rule, ignore it if we have already sent out some alert */
		if (recip->unmatchedonly && (alertcount != 0)) {
			traceprintf("Recipient '%s' dropped, not unmatched (count=%d)\n", recip->recipient, alertcount);
			continue;
		}

		if (recip->noalerts && ((alert->state == A_PAGING) || (alert->state == A_RECOVERED))) {
			traceprintf("Recipient '%s' dropped (NOALERT)\n", recip->recipient);
			continue;
		}

		if (alert->state == A_PAGING) {
			repeat_t *rpt = NULL;

			/*
			 * This runs in a child-process context, so the record we
			 * might create here is NOT used later on.
			 */
			rpt = find_repeatinfo(alert, recip, 1);
			dprintf("  repeat %s at %d\n", rpt->recipid, rpt->nextalert);
			if (rpt->nextalert > now) {
				traceprintf("Recipient '%s' dropped, next alert due at %d > %d\n",
						rpt->recipid, (int)rpt->nextalert, (int)now);
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

		dprintf("  Alert for %s:%s to %s\n", alert->hostname->name, alert->testname->name, recip->recipient);
		switch (recip->method) {
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
				else {
					errprintf("ERROR: Cannot open command pipe for '%s' - alert lost!\n", cmd);
					traceprintf("Mail pipe failed - alert lost\n");
				}

				MEMUNDEFINE(cmd);
			}
			break;

		  case M_SCRIPT:
			{
				/* Setup all of the environment for a paging script */
				char *p;
				int ip1=0, ip2=0, ip3=0, ip4=0;
				char *bbalphamsg, *ackcode, *rcpt, *bbhostname, *bbhostsvc, *bbhostsvccommas, *bbnumeric, *machip, *bbsvcname, *bbsvcnum, *bbcolorlevel, *recovered, *downsecs, *downsecsmsg, *cfidtxt;
				pid_t scriptpid;

				cfidtxt = (char *)malloc(strlen("CFID=") + 10);
				sprintf(cfidtxt, "CFID=%d", recip->cfid);
				putenv(cfidtxt);

				p = message_text(alert, recip);
				bbalphamsg = (char *)malloc(strlen("BBALPHAMSG=") + strlen(p) + 1);
				sprintf(bbalphamsg, "BBALPHAMSG=%s", p);
				putenv(bbalphamsg);

				ackcode = (char *)malloc(strlen("ACKCODE=") + 10);
				sprintf(ackcode, "ACKCODE=%d", alert->cookie);
				putenv(ackcode);

				rcpt = (char *)malloc(strlen("RCPT=") + strlen(recip->recipient) + 1);
				sprintf(rcpt, "RCPT=%s", recip->recipient);
				putenv(rcpt);

				bbhostname = (char *)malloc(strlen("BBHOSTNAME=") + strlen(alert->hostname->name) + 1);
				sprintf(bbhostname, "BBHOSTNAME=%s", alert->hostname->name);
				putenv(bbhostname);

				bbhostsvc = (char *)malloc(strlen("BBHOSTSVC=") + strlen(alert->hostname->name) + 1 + strlen(alert->testname->name) + 1);
				sprintf(bbhostsvc, "BBHOSTSVC=%s.%s", alert->hostname->name, alert->testname->name);
				putenv(bbhostsvc);

				bbhostsvccommas = (char *)malloc(strlen("BBHOSTSVCCOMMAS=") + strlen(alert->hostname->name) + 1 + strlen(alert->testname->name) + 1);
				sprintf(bbhostsvccommas, "BBHOSTSVCCOMMAS=%s.%s", commafy(alert->hostname->name), alert->testname->name);
				putenv(bbhostsvccommas);

				bbnumeric = (char *)malloc(strlen("BBNUMERIC=") + 22 + 1);
				p = bbnumeric;
				p += sprintf(p, "BBNUMERIC=");
				p += sprintf(p, "%03d", servicecode(alert->testname->name));
				sscanf(alert->ip, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4);
				p += sprintf(p, "%03d%03d%03d%03d", ip1, ip2, ip3, ip4);
				p += sprintf(p, "%d", alert->cookie);
				putenv(bbnumeric);

				machip = (char *)malloc(strlen("MACHIP=") + 13);
				sprintf(machip, "MACHIP=%03d%03d%03d%03d", ip1, ip2, ip3, ip4);
				putenv(machip);

				bbsvcname = (char *)malloc(strlen("BBSVCNAME=") + strlen(alert->testname->name) + 1);
				sprintf(bbsvcname, "BBSVCNAME=%s", alert->testname->name);
				putenv(bbsvcname);

				bbsvcnum = (char *)malloc(strlen("BBSVCNUM=") + 10);
				sprintf(bbsvcnum, "BBSVCNUM=%d", servicecode(alert->testname->name));
				putenv(bbsvcnum);

				bbcolorlevel = (char *)malloc(strlen("BBCOLORLEVEL=") + strlen(colorname(alert->color)) + 1);
				sprintf(bbcolorlevel, "BBCOLORLEVEL=%s", colorname(alert->color));
				putenv(bbcolorlevel);

				recovered = (char *)malloc(strlen("RECOVERED=") + 2);
				sprintf(recovered, "RECOVERED=%d", ((alert->state == A_RECOVERED) ? 1 : 0));
				putenv(recovered);

				downsecs = (char *)malloc(strlen("DOWNSECS=") + 20);
				sprintf(downsecs, "DOWNSECS=%d", (int)(time(NULL) - alert->eventstart));
				putenv(downsecs);

				if (alert->state == A_RECOVERED) {
					downsecsmsg = (char *)malloc(strlen("DOWNSECSMSG=Event duration :") + 20);
					sprintf(downsecsmsg, "DOWNSECSMSG=Event duration : %d", (int)(time(NULL) - alert->eventstart));
				}
				else {
					downsecsmsg = strdup("DOWNSECSMSG=");
				}
				putenv(downsecsmsg);

				traceprintf("Script alert with command '%s' and recipient %s\n", recip->scriptname, recip->recipient);
				if (testonly) break;

				scriptpid = fork();
				if (scriptpid == 0) {
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
				else {
					errprintf("ERROR: Fork failed to launch script '%s' - alert lost\n", recip->scriptname);
					traceprintf("Script fork failed - alert lost\n");
				}

				/* Clean out the environment settings */
				putenv("CFID");            xfree(cfidtxt);
				putenv("BBALPHAMSG");      xfree(bbalphamsg);
				putenv("ACKCODE");         xfree(ackcode);
				putenv("RCPT");            xfree(rcpt);
				putenv("BBHOSTNAME");      xfree(bbhostname);
				putenv("BBHOSTSVC");       xfree(bbhostsvc);
				putenv("BBHOSTSVCCOMMAS"); xfree(bbhostsvccommas);
				putenv("BBNUMERIC");       xfree(bbnumeric);
				putenv("MACHIP");          xfree(machip);
				putenv("BBSVCNAME");       xfree(bbsvcname);
				putenv("BBSVCNUM");        xfree(bbsvcnum);
				putenv("BBCOLORLEVEL");    xfree(bbcolorlevel);
				putenv("RECOVERED");       xfree(recovered);
				putenv("DOWNSECS");        xfree(downsecs);
				putenv("DOWNSECSMSG") ;    xfree(downsecsmsg);
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
	time_t now = time(NULL);
	int first = 1;
	int found = 0;
	time_t nexttime = now+(30*86400);	/* 30 days from now */
	recip_t *recip;
	repeat_t *rpt;

	stoprulefound = 0;
	while (!stoprulefound && ((recip = next_recipient(alert, &first)) != NULL)) {
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

	/*
	 * If no current recipients, then try again real soon - we're probably
	 * just waiting for a minimum duration to trigger.
	 */
	if (!found) nexttime = now + 60;

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

	dprintf("cleanup_alert called for host %s, test %s\n", alert->hostname->name, alert->testname->name);

	id = (char *)malloc(strlen(alert->hostname->name)+strlen(alert->testname->name)+3);
	sprintf(id, "%s|%s|", alert->hostname->name, alert->testname->name);
	rptwalk = rpthead; rptprev = NULL;
	while (rptwalk) {
		if (strncmp(rptwalk->recipid, id, strlen(id)) == 0) {
			repeat_t *tmp = rptwalk;

			dprintf("cleanup_alert found recipient %s\n", rptwalk->recipid);

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
}

void clear_interval(activealerts_t *alert)
{
	int first = 1;
	recip_t *recip;
	repeat_t *rpt;

	alert->nextalerttime = 0;
	stoprulefound = 0;
	while (!stoprulefound && ((recip = next_recipient(alert, &first)) != NULL)) {
		rpt = find_repeatinfo(alert, recip, 0);
		if (rpt) {
			dprintf("Cleared repeat interval for %s\n", rpt->recipid);
			rpt->nextalert = 0;
		}
	}
}

int have_recipient(activealerts_t *alert)
{
	int first = 1;

	return (next_recipient(alert, &first) != NULL);
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

	MEMDEFINE(l);

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

	MEMUNDEFINE(l);
}

void alert_printmode(int on)
{
	printmode = on;
}

void print_alert_recipients(activealerts_t *alert, char **buf, int *buflen)
{
	char *normalfont = "COLOR=\"#FFFFCC\" FACE=\"Tahoma, Arial, Helvetica\"";
	char *stopfont = "COLOR=\"#33ebf4\" FACE=\"Tahoma, Arial, Helvetica\"";

	int first = 1;
	recip_t *recip;
	char l[4096];
	int count = 0;
	char *p, *fontspec;
	char codes[20];

	MEMDEFINE(l);
	MEMDEFINE(codes);

	fontspec = normalfont;
	stoprulefound = 0;
	while ((recip = next_recipient(alert, &first)) != NULL) {
		int mindur = 0, maxdur = 0;
		char *timespec = NULL;
		int colors = defaultcolors;
		int i, firstcolor = 1;
		int recovered = 0, notice = 0;

		count++;

		addtobuffer(buf, buflen, "<tr>");
		if (count == 1) {
			sprintf(l, "<td valign=top rowspan=###>%s</td>", alert->testname->name);
			addtobuffer(buf, buflen, l);
		}

		if (printrule->criteria) mindur = printrule->criteria->minduration;
		if (recip->criteria && recip->criteria->minduration && (recip->criteria->minduration > mindur)) mindur = recip->criteria->minduration;
		if (printrule->criteria) maxdur = printrule->criteria->maxduration;
		if (recip->criteria && recip->criteria->maxduration && (recip->criteria->maxduration < maxdur)) maxdur = recip->criteria->maxduration;
		if (printrule->criteria && printrule->criteria->timespec) timespec = printrule->criteria->timespec;
		if (recip->criteria && recip->criteria->timespec) {
			if (timespec == NULL) timespec = recip->criteria->timespec;
			else errprintf("Cannot handle nested timespecs yet\n");
		}

		if (printrule->criteria && printrule->criteria->colors) colors = (colors & printrule->criteria->colors);
		if (recip->criteria && recip->criteria->colors) colors = (colors & recip->criteria->colors);

		/*
		 * Recoveries are sent if
		 * - there are no recipient criteria, and the rule says yes;
		 * - the recipient criteria does not have a RECOVERED setting, and the rule says yes;
		 * - the recipient criteria says yes.
		 */
		if ( (!recip->criteria && printrule->criteria && (printrule->criteria->sendrecovered == SR_WANTED)) ||
		     (recip->criteria && (printrule->criteria->sendrecovered == SR_WANTED) && (recip->criteria->sendrecovered == SR_UNKNOWN)) ||
		     (recip->criteria && (recip->criteria->sendrecovered == SR_WANTED)) ) recovered = 1;

		if ( (!recip->criteria && printrule->criteria && (printrule->criteria->sendnotice == SR_WANTED)) ||
		     (recip->criteria && (printrule->criteria->sendnotice == SR_WANTED) && (recip->criteria->sendnotice == SR_UNKNOWN)) ||
		     (recip->criteria && (recip->criteria->sendnotice == SR_WANTED)) ) notice = 1;

		*codes = '\0';
		if (recip->noalerts) strcat(codes, "-A");
		if (recovered && !recip->noalerts) { if (strlen(codes)) strcat(codes, ",R"); else strcat(codes, "R"); }
		if (notice) { if (strlen(codes)) strcat(codes, ",N"); else strcat(codes, "N"); }
		if (recip->stoprule) { if (strlen(codes)) strcat(codes, ",S"); else strcat(codes, "S"); }
		if (recip->unmatchedonly) { if (strlen(codes)) strcat(codes, ",U"); else strcat(codes, "U"); }

		if (strlen(codes) == 0)
			sprintf(l, "<td><font %s>%s</font></td>", fontspec, recip->recipient);
		else
			sprintf(l, "<td><font %s>%s (%s)</font></td>", fontspec, recip->recipient, codes);
		addtobuffer(buf, buflen, l);

		sprintf(l, "<td align=center>%s</td>", durationstring(mindur));
		addtobuffer(buf, buflen, l);

		sprintf(l, "<td align=center>%s</td>", durationstring(maxdur));
		addtobuffer(buf, buflen, l);

		sprintf(l, "<td align=center>%s</td>", durationstring(recip->interval)); 
		addtobuffer(buf, buflen, l);

		if (timespec) sprintf(l, "<td align=center>%s</td>", timespec); else strcpy(l, "<td align=center>-</td>");
		addtobuffer(buf, buflen, l);

		addtobuffer(buf, buflen, "<td>");
		for (i = 0; (i < COL_COUNT); i++) {
			if ((1 << i) & colors) {
				sprintf(l, "%s%s", (firstcolor ? "" : ","), colorname(i));
				addtobuffer(buf, buflen, l);
				firstcolor = 0;
			}
		}
		addtobuffer(buf, buflen, "</td>");

		if (stoprulefound) fontspec = stopfont;

		addtobuffer(buf, buflen, "</tr>\n");
	}

	/* This is hackish - patch up the "rowspan" value, so it matches the number of recipient lines */
	sprintf(l, "%d   ", count);
	p = strstr(*buf, "rowspan=###");
	if (p) { p += strlen("rowspan="); memcpy(p, l, 3); }

	MEMUNDEFINE(l);
	MEMUNDEFINE(codes);
}

