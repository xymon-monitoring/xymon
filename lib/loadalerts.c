/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the            */
/* alerts.cfg file which holds information about the Xymon alert       */
/* configuration.                                                             */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

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

#include <pcre.h>

#include "libxymon.h"


/* token's are the pre-processor macros we expand while parsing the config file */
typedef struct token_t {
	char *name;
	char *value;
	struct token_t *next;
} token_t;
static token_t *tokhead = NULL;

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
static int printmode = 0;
static rule_t *printrule = NULL;

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
	static strbuffer_t *result = NULL;
	char *inp;

	if (result == NULL) result = newstrbuffer(8192);
	clearstrbuffer(result);

	inp = buf;
	while (inp) {
		char *p;

		p = strchr(inp, '$');
		if (p == NULL) {
			addtobuffer(result, inp);
			inp = NULL;
		}
		else {
			token_t *twalk;
			char savech;
			int n;

			*p = '\0';
			addtobuffer(result, inp);
			p = (p+1);

			n = strcspn(p, "\t $.,|%!()[]{}+?/&@:;*");
			savech = *(p+n);
			*(p+n) = '\0';
			for (twalk = tokhead; (twalk && strcmp(p, twalk->name)); twalk = twalk->next) ;
			*(p+n) = savech;

			if (twalk) addtobuffer(result, twalk->value);
			inp = p+n;
		}
	}

	return STRBUF(result);
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
	if (crit->cfline)        xfree(crit->cfline);
	if (crit->pagespec)      xfree(crit->pagespec);
	if (crit->pagespecre)    pcre_free(crit->pagespecre);
	if (crit->expagespec)    xfree(crit->expagespec);
	if (crit->expagespecre)  pcre_free(crit->expagespecre);
	if (crit->dgspec)        xfree(crit->dgspec);
	if (crit->dgspecre)      pcre_free(crit->dgspecre);
	if (crit->exdgspec)      xfree(crit->exdgspec);
	if (crit->exdgspecre)    pcre_free(crit->exdgspecre);
	if (crit->hostspec)      xfree(crit->hostspec);
	if (crit->hostspecre)    pcre_free(crit->hostspecre);
	if (crit->exhostspec)    xfree(crit->exhostspec);
	if (crit->exhostspecre)  pcre_free(crit->exhostspecre);
	if (crit->svcspec)       xfree(crit->svcspec);
	if (crit->svcspecre)     pcre_free(crit->svcspecre);
	if (crit->exsvcspec)     xfree(crit->exsvcspec);
	if (crit->exsvcspecre)   pcre_free(crit->exsvcspecre);
	if (crit->classspec)     xfree(crit->classspec);
	if (crit->classspecre)   pcre_free(crit->classspecre);
	if (crit->exclassspec)   xfree(crit->exclassspec);
	if (crit->exclassspecre) pcre_free(crit->exclassspecre);
	if (crit->groupspec)     xfree(crit->groupspec);
	if (crit->groupspecre)   pcre_free(crit->groupspecre);
	if (crit->exgroupspec)   xfree(crit->exgroupspec);
	if (crit->exgroupspecre) pcre_free(crit->exgroupspecre);
	if (crit->timespec)      xfree(crit->timespec);
	if (crit->extimespec)    xfree(crit->extimespec);
}

int load_alertconfig(char *configfn, int defcolors, int defaultinterval)
{
	/* (Re)load the configuration file without leaking memory */
	static void *configfiles = NULL;
	char fn[PATH_MAX];
	FILE *fd;
	strbuffer_t *inbuf;
	char *p;
	rule_t *currule = NULL;
	recip_t *currcp = NULL, *rcptail = NULL;

	MEMDEFINE(fn);

	if (configfn) strcpy(fn, configfn); else sprintf(fn, "%s/etc/alerts.cfg", xgetenv("XYMONHOME"));

	/* First check if there were no modifications at all */
	if (configfiles) {
		if (!stackfmodified(configfiles)){
			dbgprintf("No files modified, skipping reload of %s\n", fn);
			MEMUNDEFINE(fn); 
			return 0;
		}
		else {
			stackfclist(&configfiles);
			configfiles = NULL;
		}
	}

	fd = stackfopen(fn, "r", &configfiles);
	if (!fd) { 
		errprintf("Cannot open configuration file %s: %s\n", fn, strerror(errno));
		MEMUNDEFINE(fn); 
		return 0; 
	}

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
	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		int firsttoken = 1;
		int mailcmdactive = 0, scriptcmdactive = 0;
		recip_t *curlinerecips = NULL;

		cfid++;
		sanitize_input(inbuf, 1, 0);

		/* Skip empty lines */
		if (STRBUFLEN(inbuf) == 0) continue;

		if ((*STRBUF(inbuf) == '$') && strchr(STRBUF(inbuf), '=')) {
			/* Define a macro */
			token_t *newtok = (token_t *) malloc(sizeof(token_t));
			char *delim;

			delim = strchr(STRBUF(inbuf), '=');
			*delim = '\0';
			newtok->name = strdup(STRBUF(inbuf)+1);	/* Skip the '$' */
			newtok->value = strdup(preprocess(delim+1));
			newtok->next = tokhead;
			tokhead = newtok;
			continue;
		}

		strncpy(cfline, STRBUF(inbuf), (sizeof(cfline)-1));
		cfline[sizeof(cfline)-1] = '\0';

		/* Expand macros inside the line before parsing */
		p = strtok(preprocess(STRBUF(inbuf)), " \t");
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
			else if ((strncasecmp(p, "DISPLAYGROUP=", 13) == 0) || (strncasecmp(p, "DISPLAYGROUPS=", 14) == 0)) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->dgspec = strdup(val);
				if (*(crit->dgspec) == '%') crit->dgspecre = compileregex(crit->dgspec+1);
				firsttoken = 0;
			}
			else if ((strncasecmp(p, "EXDISPLAYGROUP=", 15) == 0) || (strncasecmp(p, "EXDISPLAYGROUPS=", 16) == 0)) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->exdgspec = strdup(val);
				if (*(crit->exdgspec) == '%') crit->exdgspecre = compileregex(crit->exdgspec+1);
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
			else if (strncasecmp(p, "CLASS=", 6) == 0) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->classspec = strdup(val);
				if (*(crit->classspec) == '%') crit->classspecre = compileregex(crit->classspec+1);
				firsttoken = 0;
			}
			else if (strncasecmp(p, "EXCLASS=", 8) == 0) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->exclassspec = strdup(val);
				if (*(crit->exclassspec) == '%') crit->exclassspecre = compileregex(crit->exclassspec+1);
				firsttoken = 0;
			}
			else if (strncasecmp(p, "GROUP=", 6) == 0) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->groupspec = strdup(val);
				if (*(crit->groupspec) == '%') crit->groupspecre = compileregex(crit->groupspec+1);
				firsttoken = 0;
			}
			else if (strncasecmp(p, "EXGROUP=", 8) == 0) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->exgroupspec = strdup(val);
				if (*(crit->exgroupspec) == '%') crit->exgroupspecre = compileregex(crit->exgroupspec+1);
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
			else if ((strncasecmp(p, "EXTIME=", 7) == 0) || (strncasecmp(p, "EXTIMES=", 8) == 0)) {
				char *val;
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				val = strchr(p, '=')+1;
				crit = setup_criteria(&currule, &currcp);
				crit->extimespec = strdup(val);
				firsttoken = 0;
			}
			else if (strncasecmp(p, "DURATION", 8) == 0) {
				criteria_t *crit;

				if (firsttoken) { flush_rule(currule); currule = NULL; currcp = NULL; pstate = P_NONE; }
				crit = setup_criteria(&currule, &currcp);
				if (*(p+8) == '>') {
					if (*(p+9) == '=')
						crit->minduration = 60*durationvalue(p+10);
					else
						crit->minduration = 60*durationvalue(p+9) + 1;
				}
				else if (*(p+8) == '<') {
					if (*(p+9) == '=')
						crit->maxduration = 60*durationvalue(p+10);
					else
						crit->maxduration = 60*durationvalue(p+9) - 1;
				}
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
				else if (strcasecmp(p+7, "TEXT") == 0) currcp->format = ALERTFORM_TEXT;
				else if (strcasecmp(p+7, "PLAIN") == 0) currcp->format = ALERTFORM_PLAIN;
				else if (strcasecmp(p+7, "SMS") == 0) currcp->format = ALERTFORM_SMS;
				else if (strcasecmp(p+7, "PAGER") == 0) currcp->format = ALERTFORM_PAGER;
				else if (strcasecmp(p+7, "SCRIPT") == 0) currcp->format = ALERTFORM_SCRIPT;
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
				newrcp = (recip_t *)calloc(1, sizeof(recip_t));
				newrcp->cfid = cfid;
				newrcp->method = M_MAIL;
				newrcp->format = ALERTFORM_TEXT;

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
				newrcp = (recip_t *)calloc(1, sizeof(recip_t));
				newrcp->cfid = cfid;
				newrcp->method = M_SCRIPT;
				newrcp->format = ALERTFORM_SCRIPT;

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
			else if (currule && (strncasecmp(p, "IGNORE", 6) == 0)) {
				recip_t *newrcp;

				newrcp = (recip_t *)calloc(1, sizeof(recip_t));
				newrcp->cfid = cfid;
				newrcp->method = M_IGNORE;
				newrcp->format = ALERTFORM_NONE;
				newrcp->interval = defaultinterval;
				newrcp->stoprule = 1;
				currcp = newrcp;
				if (curlinerecips == NULL) curlinerecips = newrcp;
				pstate = P_RECIP;

				if (currule->recipients == NULL)
					currule->recipients = rcptail = newrcp;
				else {
					rcptail->next = newrcp;
					rcptail = newrcp;
				}

				firsttoken = 0;
			}
			else {
				errprintf("Ignored unknown/unexpected token '%s' at line %d\n", p, cfid);
			}

			if (p) p = strtok(NULL, " \t");
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
	stackfclose(fd);
	freestrbuffer(inbuf);

	MEMUNDEFINE(cfline);
	MEMUNDEFINE(fn);

	return 1;
}

static void dump_criteria(criteria_t *crit, int isrecip)
{
	if (crit->pagespec) printf("PAGE=%s ", crit->pagespec);
	if (crit->expagespec) printf("EXPAGE=%s ", crit->expagespec);
	if (crit->dgspec) printf("DISPLAYGROUP=%s ", crit->dgspec);
	if (crit->exdgspec) printf("EXDISPLAYGROUP=%s ", crit->exdgspec);
	if (crit->hostspec) printf("HOST=%s ", crit->hostspec);
	if (crit->exhostspec) printf("EXHOST=%s ", crit->exhostspec);
	if (crit->svcspec) printf("SERVICE=%s ", crit->svcspec);
	if (crit->exsvcspec) printf("EXSERVICE=%s ", crit->exsvcspec);
	if (crit->classspec) printf("CLASS=%s ", crit->classspec);
	if (crit->exclassspec) printf("EXCLASS=%s ", crit->exclassspec);
	if (crit->groupspec) printf("GROUP=%s ", crit->groupspec);
	if (crit->exgroupspec) printf("EXGROUP=%s ", crit->exgroupspec);
	if (crit->colors) {
		int i, first = 1;

		printf("COLOR=");
		for (i = 0; (i < COL_COUNT); i++) {
			if ((1 << i) & crit->colors) {
				dbgprintf("first=%d, i=%d\n", first, i);
				printf("%s%s", (first ? "" : ","), colorname(i));
				first = 0;
			}
		}
		printf(" ");
	}

	if (crit->timespec) printf("TIME=%s ", crit->timespec);
	if (crit->extimespec) printf("EXTIME=%s ", crit->extimespec);
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

void dump_alertconfig(int showlines)
{
	rule_t *rulewalk;
	recip_t *recipwalk;

	for (rulewalk = rulehead; (rulewalk); rulewalk = rulewalk->next) {
		if (showlines) printf("%5d\t", rulewalk->cfid);

		dump_criteria(rulewalk->criteria, 0);
		printf("\n");

		for (recipwalk = rulewalk->recipients; (recipwalk); recipwalk = recipwalk->next) {
			printf("\t");
			switch (recipwalk->method) {
			  case M_MAIL   : printf("MAIL %s ", recipwalk->recipient); break;
			  case M_SCRIPT : printf("SCRIPT %s %s ", recipwalk->scriptname, recipwalk->recipient); break;
			  case M_IGNORE : printf("IGNORE "); break;
			}
			switch (recipwalk->format) {
			  case ALERTFORM_TEXT  : printf("FORMAT=TEXT "); break;
			  case ALERTFORM_PLAIN : printf("FORMAT=PLAIN "); break;
			  case ALERTFORM_SMS   : printf("FORMAT=SMS "); break;
			  case ALERTFORM_PAGER : printf("FORMAT=PAGER "); break;
			  case ALERTFORM_SCRIPT: printf("FORMAT=SCRIPT "); break;
			  case ALERTFORM_NONE  : break;
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

int stoprulefound = 0;

static int criteriamatch(activealerts_t *alert, criteria_t *crit, criteria_t *rulecrit, int *anymatch, time_t *nexttime)
{
	/*
	 * See if the "crit" matches the "alert".
	 * Match on pagespec, dgspec, hostspec, svcspec, classspec, groupspec, colors, timespec, extimespec, minduration, maxduration, sendrecovered
	 */

	static char *pgnames = NULL;
	int pgmatchres, pgexclres;
	time_t duration = (getcurrenttime(NULL) - alert->eventstart);
	int result, cfid = 0;
	char *pgtok, *cfline = NULL;
	void *hinfo = hostinfo(alert->hostname);

	/* The top-level page needs a name - cannot match against an empty string */
	if (pgnames) xfree(pgnames);
	pgnames = strdup((*alert->location == '\0') ? "/" : alert->location);

	if (crit) { cfid = crit->cfid; cfline = crit->cfline; }
	if (!cfid && rulecrit) cfid = rulecrit->cfid;
	if (!cfline && rulecrit) cfline = rulecrit->cfline;
	if (!cfline) cfline = "<undefined>";

	traceprintf("Matching host:service:dgroup:page '%s:%s:%s:%s' against rule line %d\n",
			alert->hostname, alert->testname, xmh_item(hinfo, XMH_DGNAME), alert->location, cfid);

	if (alert->state == A_PAGING) {
		/* Check max-duration now - it's fast and easy. */
		if (crit && crit->maxduration && (duration > crit->maxduration)) { 
			traceprintf("Failed '%s' (max. duration %d>%d)\n", cfline, duration, crit->maxduration);
			if (!printmode) return 0; 
		}
	}

	if (crit && crit->classspec && !namematch(alert->classname, crit->classspec, crit->classspecre)) { 
		traceprintf("Failed '%s' (class not in include list)\n", cfline);
		return 0; 
	}
	if (crit && crit->exclassspec && namematch(alert->classname, crit->exclassspec, crit->exclassspecre)) { 
		traceprintf("Failed '%s' (class excluded)\n", cfline);
		return 0; 
	}

	/* alert->groups is a comma-separated list of groups, so it needs some special handling */
	/* 
	 * NB: Dont check groups when RECOVERED - the group list for recovery messages is always empty.
	 * It doesn't matter if we match a recipient who was not in the group that originally
	 * got the alert - we will later check who has received the alert, and only those that
	 * have will get the recovery message.
	 */
	if (crit && (crit->groupspec || crit->exgroupspec) && (alert->state != A_RECOVERED)) {
		char *grouplist;
		char *tokptr;

		grouplist = (alert->groups && (*(alert->groups))) ? strdup(alert->groups) : NULL;
		if (crit->groupspec) {
			char *onegroup;
			int iswanted = 0;

			if (grouplist) {
				/* There is a group label on the alert, so it must match */
				onegroup = strtok_r(grouplist, ",", &tokptr);
				while (onegroup && !iswanted) {
					iswanted = (namematch(onegroup, crit->groupspec, crit->groupspecre));
					onegroup = strtok_r(NULL, ",", &tokptr);
				}
			}

			if (!iswanted) {
				/*
				 * Either the alert had a group list that didn't match, or
				 * there was no group list and the rule listed one.
				 * In both cases, it's a failed match.
				 */
				traceprintf("Failed '%s' (group not in include list)\n", cfline);
				if (grouplist) xfree(grouplist);
				return 0; 
			}
		}

		if (crit->exgroupspec && grouplist) {
			char *onegroup;

			/* Excluded groups are only handled when the alert does have a group list */

			strcpy(grouplist, alert->groups); /* Might have been used in the include list */
			onegroup = strtok_r(grouplist, ",", &tokptr);
			while (onegroup) {
				if (namematch(onegroup, crit->exgroupspec, crit->exgroupspecre)) { 
					traceprintf("Failed '%s' (group excluded)\n", cfline);
					xfree(grouplist);
					return 0; 
				}
				onegroup = strtok_r(NULL, ",", &tokptr);
			}
		}

		if (grouplist) xfree(grouplist);
	}

	pgmatchres = pgexclres = -1;
	pgtok = strtok(pgnames, ",");
	while (pgtok) {
		if (crit && crit->pagespec && (pgmatchres != 1))
			pgmatchres = (namematch(pgtok, crit->pagespec, crit->pagespecre) ? 1 : 0);

		if (crit && crit->expagespec && (pgexclres != 1))
			pgexclres = (namematch(pgtok, crit->expagespec, crit->expagespecre) ? 1 : 0);

		pgtok = strtok(NULL, ",");
	}
	if (pgexclres == 1) {
		traceprintf("Failed '%s' (pagename excluded)\n", cfline);
		return 0; 
	}
	if (pgmatchres == 0) {
		traceprintf("Failed '%s' (pagename not in include list)\n", cfline);
		return 0;
	}

	if (crit && crit->dgspec && !namematch(xmh_item(hinfo, XMH_DGNAME), crit->dgspec, crit->dgspecre)) { 
		traceprintf("Failed '%s' (displaygroup not in include list)\n", cfline);
		return 0; 
	}
	if (crit && crit->exdgspec && namematch(xmh_item(hinfo, XMH_DGNAME), crit->exdgspec, crit->exdgspecre)) { 
		traceprintf("Failed '%s' (displaygroup excluded)\n", cfline);
		return 0; 
	}

	if (crit && crit->hostspec && !namematch(alert->hostname, crit->hostspec, crit->hostspecre)) { 
		traceprintf("Failed '%s' (hostname not in include list)\n", cfline);
		return 0; 
	}
	if (crit && crit->exhostspec && namematch(alert->hostname, crit->exhostspec, crit->exhostspecre)) { 
		traceprintf("Failed '%s' (hostname excluded)\n", cfline);
		return 0; 
	}

	if (crit && crit->svcspec && !namematch(alert->testname, crit->svcspec, crit->svcspecre))  { 
		traceprintf("Failed '%s' (service not in include list)\n", cfline);
		return 0; 
	}
	if (crit && crit->exsvcspec && namematch(alert->testname, crit->exsvcspec, crit->exsvcspecre))  { 
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

	/* At this point, we know the configuration may result in an alert. */
	if (anymatch) (*anymatch)++;

	/* 
	 * Duration checks should be done on real paging messages only. 
	 * Not on recovery- or notify-messages.
	 */
	if (alert->state == A_PAGING) {
		if (crit && crit->minduration && (duration < crit->minduration)) { 
			if (nexttime) {
				time_t mynext = alert->eventstart + crit->minduration;

				if ((*nexttime == -1) || (*nexttime > mynext)) *nexttime = mynext;
			}
			traceprintf("Failed '%s' (min. duration %d<%d)\n", cfline, duration, crit->minduration);
			if (!printmode) return 0; 
		}
	}

	/*
	 * Time restrictions apply to ALL messages.
	 * Before 4.2, these were only applied to ALERT messages,
	 * not RECOVERED and NOTIFY messages. This caused some
	 * unfortunate alerts in the middle of the night because
	 * some random system recovered ... not good. So apply
	 * this check to all messages.
	 */
	if (crit && ( (crit->timespec && !timematch(xmh_item(hinfo, XMH_HOLIDAYS), crit->timespec)) || 
		      (crit->extimespec && timematch(xmh_item(hinfo, XMH_HOLIDAYS), crit->extimespec)) ) ) {
		/* Try again in a minute */
		if (nexttime) *nexttime = getcurrenttime(NULL) + 60;
		traceprintf("Failed '%s' (time/extime criteria)\n", cfline);
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

	if ((alert->state == A_RECOVERED) || (alert->state == A_DISABLED)) {
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

recip_t *next_recipient(activealerts_t *alert, int *first, int *anymatch, time_t *nexttime)
{
	static rule_t *rulewalk = NULL;
	static recip_t *recipwalk = NULL;

	if (anymatch) *anymatch = 0;

	do {
		if (*first) {
			/* Start at beginning of rules-list and find the first matching rule. */
			*first = 0;
			rulewalk = rulehead;
			while (rulewalk && !criteriamatch(alert, rulewalk->criteria, NULL, NULL, NULL)) rulewalk = rulewalk->next;
			if (rulewalk) {
				/* Point recipwalk at the list of possible candidates */
				dbgprintf("Found a first matching rule\n");
				recipwalk = rulewalk->recipients; 
			}
			else {
				/* No matching rules */
				dbgprintf("Found no first matching rule\n");
				recipwalk = NULL;
			}
		}
		else {
			if (!recipwalk) {
				/* Should not happen! */
			}
			else if (recipwalk->next) {
				/* Check the next recipient in the current rule */
				recipwalk = recipwalk->next;
			}
			else {
				/* End of recipients in current rule. Go to the next matching rule */
				do {
					rulewalk = rulewalk->next;
				} while (rulewalk && !criteriamatch(alert, rulewalk->criteria, NULL, NULL, NULL));

				if (rulewalk) {
					/* Point recipwalk at the list of possible candidates */
					dbgprintf("Found a secondary matching rule\n");
					recipwalk = rulewalk->recipients; 
				}
				else {
					/* No matching rules */
					dbgprintf("No more secondary matching rule\n");
					recipwalk = NULL;
				}
			}
		}
	} while (rulewalk && recipwalk && !criteriamatch(alert, recipwalk->criteria, rulewalk->criteria, anymatch, nexttime));

	stoprulefound = (recipwalk && recipwalk->stoprule);

	printrule = rulewalk;
	return recipwalk;
}


int have_recipient(activealerts_t *alert, int *anymatch)
{
	int first = 1;

	return (next_recipient(alert, &first, anymatch, NULL) != NULL);
}


void alert_printmode(int on)
{
	printmode = on;
}

void print_alert_recipients(activealerts_t *alert, strbuffer_t *buf)
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

	if (printmode == 2) {
		/* For print-out usage - e.g. confreport.cgi */
		normalfont = "COLOR=\"#000000\" FACE=\"Tahoma, Arial, Helvetica\"";
		stopfont = "COLOR=\"#FF0000\" FACE=\"Tahoma, Arial, Helvetica\"";
	}

	fontspec = normalfont;
	stoprulefound = 0;
	while ((recip = next_recipient(alert, &first, NULL, NULL)) != NULL) {
		int mindur = 0, maxdur = INT_MAX;
		char *timespec = NULL; char *extimespec = NULL;
		int colors = defaultcolors;
		int i, firstcolor = 1;
		int recovered = 0, notice = 0;

		count++;

		addtobuffer(buf, "<tr>");
		if (count == 1) {
			sprintf(l, "<td valign=top rowspan=###>%s</td>", alert->testname);
			addtobuffer(buf, l);
		}

		/*
		 * The min/max duration of an alert can be controlled by both the actual rule,
		 * and by the recipient specification.
		 * The rule must be fulfilled before the recipient even gets into play, so
		 * if there is a min/max duration on the rule then this becomes the default
		 * and recipient-specific settings can only increase the minduration/decrease
		 * the maxduration.
		 * On the other hand, if there is no rule-setting then the recipient-specific
		 * settings determine everything.
		 */
		if (printrule->criteria && printrule->criteria->minduration) mindur = printrule->criteria->minduration;
		if (recip->criteria && recip->criteria->minduration && (recip->criteria->minduration > mindur)) mindur = recip->criteria->minduration;
		if (printrule->criteria && printrule->criteria->maxduration) maxdur = printrule->criteria->maxduration;
		if (recip->criteria && recip->criteria->maxduration && (recip->criteria->maxduration < maxdur)) maxdur = recip->criteria->maxduration;
		if (printrule->criteria && printrule->criteria->timespec) timespec = printrule->criteria->timespec;
		if (printrule->criteria && printrule->criteria->extimespec) extimespec = printrule->criteria->extimespec;
		if (recip->criteria && recip->criteria->timespec) {
			if (timespec == NULL) timespec = recip->criteria->timespec;
			else errprintf("Cannot handle nested timespecs yet\n");
		}
		if (recip->criteria && recip->criteria->extimespec) {
			if (extimespec == NULL) extimespec = recip->criteria->extimespec;
			else errprintf("Cannot handle nested extimespecs yet\n");
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
		if (recip->method == M_IGNORE) {
			recip->recipient = "-- ignored --";
		}
		if (recip->noalerts) { if (strlen(codes)) strcat(codes, ",A"); else strcat(codes, "-A"); }
		if (recovered && !recip->noalerts) { if (strlen(codes)) strcat(codes, ",R"); else strcat(codes, "R"); }
		if (notice) { if (strlen(codes)) strcat(codes, ",N"); else strcat(codes, "N"); }
		if (recip->stoprule) { if (strlen(codes)) strcat(codes, ",S"); else strcat(codes, "S"); }
		if (recip->unmatchedonly) { if (strlen(codes)) strcat(codes, ",U"); else strcat(codes, "U"); }

		if (strlen(codes) == 0)
			sprintf(l, "<td><font %s>%s</font></td>", fontspec, recip->recipient);
		else
			sprintf(l, "<td><font %s>%s (%s)</font></td>", fontspec, recip->recipient, codes);
		addtobuffer(buf, l);

		sprintf(l, "<td align=center>%s</td>", durationstring(mindur));
		addtobuffer(buf, l);

		/* maxdur=INT_MAX means "no max duration". So set it to 0 for durationstring() to do the right thing */
		if (maxdur == INT_MAX) maxdur = 0;
		sprintf(l, "<td align=center>%s</td>", durationstring(maxdur));
		addtobuffer(buf, l);

		sprintf(l, "<td align=center>%s</td>", durationstring(recip->interval)); 
		addtobuffer(buf, l);

		if (timespec) sprintf(l, "<td align=center>%s</td>", timespec); else strcpy(l, "<td align=center>-</td>");
		if (extimespec) sprintf(l, "<td align=center>%s</td>", extimespec); else strcpy(l, "<td align=center>-</td>");
		addtobuffer(buf, l);

		addtobuffer(buf, "<td>");
		for (i = 0; (i < COL_COUNT); i++) {
			if ((1 << i) & colors) {
				sprintf(l, "%s%s", (firstcolor ? "" : ","), colorname(i));
				addtobuffer(buf, l);
				firstcolor = 0;
			}
		}
		addtobuffer(buf, "</td>");

		if (stoprulefound) fontspec = stopfont;

		addtobuffer(buf, "</tr>\n");
	}

	/* This is hackish - patch up the "rowspan" value, so it matches the number of recipient lines */
	sprintf(l, "%d   ", count);
	p = strstr(STRBUF(buf), "rowspan=###");
	if (p) { p += strlen("rowspan="); memcpy(p, l, 3); }

	MEMUNDEFINE(l);
	MEMUNDEFINE(codes);
}

