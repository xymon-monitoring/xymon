/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module                                                      */
/* This file has routines that load the hobbitd_client configuration and      */
/* finds the rules relevant for a particular test when applied.               */
/*                                                                            */
/* Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: client_config.c,v 1.18 2006-04-02 20:27:14 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

#include <pcre.h>

#include "libbbgen.h"
#include "client_config.h"

typedef struct exprlist_t {
	char *pattern;
	pcre *exp;
	struct exprlist_t *next;
} exprlist_t;

typedef struct c_load_t {
	float warnlevel, paniclevel;
} c_load_t;

typedef struct c_uptime_t {
	int recentlimit, ancientlimit;
} c_uptime_t;

typedef struct c_disk_t {
	exprlist_t *fsexp;
	unsigned long warnlevel, paniclevel;
	int absolutes;
	int dmin, dmax, dcount;
	int color;
} c_disk_t;

typedef struct c_mem_t {
	enum { C_MEM_PHYS, C_MEM_SWAP, C_MEM_ACT } memtype;
	int warnlevel, paniclevel;
} c_mem_t;

typedef struct c_proc_t {
	exprlist_t *procexp;
	int pmin, pmax, pcount;
	int color;
} c_proc_t;

typedef struct c_log_t {
	exprlist_t *logfile;
	exprlist_t *matchexp, *matchone, *ignoreexp;
	int color;
} c_log_t;

typedef enum { C_LOAD, C_UPTIME, C_DISK, C_MEM, C_PROC, C_LOG } ruletype_t;

typedef struct c_rule_t {
	exprlist_t *hostexp;
	exprlist_t *exhostexp;
	exprlist_t *pageexp;
	exprlist_t *expageexp;
	char *timespec;
	ruletype_t ruletype;
	int cfid;
	struct c_rule_t *next;
	union {
		c_load_t load;
		c_uptime_t uptime;
		c_disk_t disk;
		c_mem_t mem;
		c_proc_t proc;
		c_log_t log;
	} rule;
} c_rule_t;

static c_rule_t *rulehead = NULL;
static c_rule_t *ruletail = NULL;
static exprlist_t *exprhead = NULL;

/* ruletree is a tree indexed by hostname of the rules. */
typedef struct ruleset_t {
	c_rule_t *rule;
	struct ruleset_t *next;
} ruleset_t;
static int havetree = 0;
static RbtHandle ruletree;


static ruleset_t *ruleset(char *hostname, char *pagename)
{
	/*
	 * This routine manages a list of rules that apply to this particular host.
	 *
	 * We maintain a tree indexed by hostname. Each node in the tree contains
	 * a list of c_rule_t records, which point to individual rules in the full
	 * list of rules. So instead of walking the entire list of rules for all hosts,
	 * we can just go through those rules that are relevant for a given host.
	 * This should speed up client-rule matching tremendously, since all of
	 * the expensive pagename/hostname matches are only performed initially 
	 * when the list of rules for the host is decided.
	 */
	RbtIterator handle;
	c_rule_t *rwalk;
	ruleset_t *head, *tail, *itm;

	handle = rbtFind(ruletree, hostname);
	if (handle != rbtEnd(ruletree)) {
		/* We have the tree for this host */
		void *k1, *k2;
		rbtKeyValue(ruletree, handle, &k1, &k2);
		dprintf("Found list starting at cfid %d\n", ((ruleset_t *)k2)->rule->cfid);
		return (ruleset_t *)k2;
	}

	/* We must build the list of rules for this host */
	head = tail = NULL;
	for (rwalk = rulehead; (rwalk); rwalk = rwalk->next) {
		if (rwalk->exhostexp && namematch(hostname, rwalk->exhostexp->pattern, rwalk->exhostexp->exp)) continue;
		if (rwalk->hostexp && !namematch(hostname, rwalk->hostexp->pattern, rwalk->hostexp->exp)) continue;
		if (rwalk->expageexp && namematch(pagename, rwalk->expageexp->pattern, rwalk->expageexp->exp)) continue;
		if (rwalk->pageexp && !namematch(pagename, rwalk->pageexp->pattern, rwalk->pageexp->exp)) continue;
		/* All criteria match - add this rule to the list of rules for this host */
		itm = (ruleset_t *)malloc(sizeof(ruleset_t));
		itm->rule = rwalk;
		itm->next = NULL;
		if (head == NULL) {
			head = tail = itm;
		}
		else { 
			tail->next = itm;
			tail = itm;
		}
	}

	/* Add the list to the tree */
	rbtInsert(ruletree, strdup(hostname), head);

	return head;
}

static exprlist_t *setup_expr(char *ptn, int multiline)
{
	exprlist_t *newitem = (exprlist_t *)calloc(1, sizeof(exprlist_t));

	newitem->pattern = strdup(ptn);
	if (*ptn == '%') {
		if (multiline)
			newitem->exp = multilineregex(ptn+1);
		else
			newitem->exp = compileregex(ptn+1);
	}
	newitem->next = exprhead;
	exprhead = newitem;

	return newitem;
}

static c_rule_t *setup_rule(ruletype_t ruletype, 
			    exprlist_t *curhost, exprlist_t *curexhost, 
			    exprlist_t *curpage, exprlist_t *curexpage, 
			    char *curtime,
			    int cfid)
{
	c_rule_t *newitem = (c_rule_t *)calloc(1, sizeof(c_rule_t));
	if (ruletail) { ruletail->next = newitem; ruletail = newitem; }
	else rulehead = ruletail = newitem;

	newitem->ruletype = ruletype;
	newitem->hostexp = curhost;
	newitem->exhostexp = curexhost;
	newitem->pageexp = curpage;
	newitem->expageexp = curexpage;
	if (curtime) newitem->timespec = strdup(curtime);
	newitem->cfid = cfid;

	return newitem;
}


static int isqual(char *token)
{
	if (!token) return 1;

	if ( (strncasecmp(token, "HOST=", 5) == 0)	||
	     (strncasecmp(token, "EXHOST=", 7) == 0)	||
	     (strncasecmp(token, "PAGE=", 5) == 0)	||
	     (strncasecmp(token, "EXPAGE=", 7) == 0)	||
	     (strncasecmp(token, "TIME=", 5) == 0)	) return 1;

	return 0;
}

int load_client_config(char *configfn)
{
	/* (Re)load the configuration file without leaking memory */
	static void *configfiles = NULL;
	char fn[PATH_MAX];
	FILE *fd;
	strbuffer_t *inbuf;
	char *tok;
	exprlist_t *curhost, *curpage, *curexhost, *curexpage;
	char *curtime;
	c_rule_t *currule = NULL;
	int cfid = 0;

	MEMDEFINE(fn);

	if (configfn) strcpy(fn, configfn); else sprintf(fn, "%s/etc/hobbit-clients.cfg", xgetenv("BBHOME"));

	/* First check if there were no modifications at all */
	if (configfiles) {
		if (!stackfmodified(configfiles)){
			dprintf("No files modified, skipping reload of %s\n", fn);
			return 0;
		}
		else {
			stackfclist(&configfiles);
			configfiles = NULL;
		}
	}

	fd = stackfopen(fn, "r", &configfiles);
	if (!fd) { 
		errprintf("Cannot load config file %s: %s\n", fn, strerror(errno)); 
		MEMUNDEFINE(fn); 
		return 0;
	}

	/* First free the old list, if any */
	while (rulehead) {
		c_rule_t *tmp = rulehead;
		rulehead = rulehead->next;
		if (tmp->timespec) xfree(tmp->timespec);
		xfree(tmp);
	}
	rulehead = ruletail = NULL;
	while (exprhead) {
		exprlist_t *tmp = exprhead;
		exprhead = exprhead->next;
		if (tmp->pattern) xfree(tmp->pattern);
		if (tmp->exp) pcre_free(tmp->exp);
		xfree(tmp);
	}
	exprhead = NULL;

	if (havetree) {
		RbtIterator handle;
		void *k1, *k2;
		char *key;
		ruleset_t *head, *itm;

		handle = rbtBegin(ruletree);
		while (handle != rbtEnd(ruletree)) {
			rbtKeyValue(ruletree, handle, &k1, &k2);
			key = (char *)k1;
			head = (ruleset_t *)k2;
			xfree(key);
			while (head) {
				itm = head; head = head->next; xfree(itm);
			}
			handle = rbtNext(ruletree, handle);
		}
		rbtDelete(ruletree);
		havetree = 0;
	}

	curhost = curpage = curexhost = curexpage = NULL;
	curtime = NULL;
	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		exprlist_t *newhost, *newpage, *newexhost, *newexpage;
		char *newtime;
		int unknowntok = 0;

		cfid++;
		sanitize_input(inbuf, 1, 0); if (STRBUFLEN(inbuf) == 0) continue;

		newhost = newpage = newexhost = newexpage = NULL;
		newtime = NULL;
		currule = NULL;

		tok = wstok(STRBUF(inbuf));
		while (tok) {
			if (strncasecmp(tok, "HOST=", 5) == 0) {
				char *p = strchr(tok, '=');
				newhost = setup_expr(p+1, 0);
				if (currule) currule->hostexp = newhost;
			}
			else if (strncasecmp(tok, "EXHOST=", 7) == 0) {
				char *p = strchr(tok, '=');
				newexhost = setup_expr(p+1, 0);
				if (currule) currule->exhostexp = newexhost;
			}
			else if (strncasecmp(tok, "PAGE=", 5) == 0) {
				char *p = strchr(tok, '=');
				newpage = setup_expr(p+1, 0);
				if (currule) currule->pageexp = newpage;
			}
			else if (strncasecmp(tok, "EXPAGE=", 7) == 0) {
				char *p = strchr(tok, '=');
				newexpage = setup_expr(p+1, 0);
				if (currule) currule->expageexp = newexpage;
			}
			else if (strncasecmp(tok, "TIME=", 5) == 0) {
				char *p = strchr(tok, '=');
				if (currule) currule->timespec = strdup(p+1);
				else newtime = strdup(p+1);
			}
			else if (strncasecmp(tok, "DEFAULT", 6) == 0) {
				currule = NULL;
			}
			else if (strcasecmp(tok, "UP") == 0) {
				currule = setup_rule(C_UPTIME, curhost, curexhost, curpage, curexpage, curtime, cfid);
				currule->rule.uptime.recentlimit = 3600;
				currule->rule.uptime.ancientlimit = -1;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.uptime.recentlimit = 60*durationvalue(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.uptime.ancientlimit = 60*durationvalue(tok);
			}
			else if (strcasecmp(tok, "LOAD") == 0) {
				currule = setup_rule(C_LOAD, curhost, curexhost, curpage, curexpage, curtime, cfid);
				currule->rule.load.warnlevel = 5.0;
				currule->rule.load.paniclevel = atof(tok);

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.load.warnlevel = atof(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.load.paniclevel = atof(tok);
			}
			else if (strcasecmp(tok, "DISK") == 0) {
				char modchar = '\0';
				currule = setup_rule(C_DISK, curhost, curexhost, curpage, curexpage, curtime, cfid);
				currule->rule.disk.absolutes = 0;
				currule->rule.disk.warnlevel = 90;
				currule->rule.disk.paniclevel = 95;
				currule->rule.disk.dmin = 0;
				currule->rule.disk.dmax = -1;
				currule->rule.disk.color = COL_RED;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.fsexp = setup_expr(tok, 0);

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.warnlevel = atol(tok);
				modchar = *(tok + strspn(tok, "0123456789"));
				if (modchar && (modchar != '%')) {
					currule->rule.disk.absolutes += 1;
					switch (modchar) {
					  case 'k': case 'K' : break;
					  case 'm': case 'M' : currule->rule.disk.warnlevel *= 1024; break;
					  case 'g': case 'G' : currule->rule.disk.warnlevel *= 1024*1024; break;
					}
				}

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.paniclevel = atol(tok);
				modchar = *(tok + strspn(tok, "0123456789"));
				if (modchar && (modchar != '%')) {
					currule->rule.disk.absolutes += 2;
					switch (modchar) {
					  case 'k': case 'K' : break;
					  case 'm': case 'M' : currule->rule.disk.warnlevel *= 1024; break;
					  case 'g': case 'G' : currule->rule.disk.warnlevel *= 1024*1024; break;
					}
				}

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.dmin = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.dmax = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.color = parse_color(tok);
			}
			else if ((strcasecmp(tok, "MEMREAL") == 0) || (strcasecmp(tok, "MEMPHYS") == 0) || (strcasecmp(tok, "PHYS") == 0)) {
				currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curtime, cfid);
				currule->rule.mem.memtype = C_MEM_PHYS;
				currule->rule.mem.warnlevel = 100;
				currule->rule.mem.paniclevel = 101;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.warnlevel = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.paniclevel = atoi(tok);
			}
			else if ((strcasecmp(tok, "MEMSWAP") == 0) || (strcasecmp(tok, "SWAP") == 0)) {
				currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curtime, cfid);
				currule->rule.mem.memtype = C_MEM_SWAP;
				currule->rule.mem.warnlevel = 50;
				currule->rule.mem.paniclevel = 80;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.warnlevel = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.paniclevel = atoi(tok);
			}
			else if ((strcasecmp(tok, "MEMACT") == 0) || (strcasecmp(tok, "ACTUAL") == 0) || (strcasecmp(tok, "ACT") == 0)) {
				currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curtime, cfid);
				currule->rule.mem.memtype = C_MEM_ACT;
				currule->rule.mem.warnlevel = 90;
				currule->rule.mem.paniclevel = 97;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.warnlevel = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.paniclevel = atoi(tok);
			}
			else if (strcasecmp(tok, "PROC") == 0) {
				currule = setup_rule(C_PROC, curhost, curexhost, curpage, curexpage, curtime, cfid);
				tok = wstok(NULL);
				currule->rule.proc.procexp = setup_expr(tok, 0);
				currule->rule.proc.pmin = 1;
				currule->rule.proc.pmax = -1;
				currule->rule.proc.color = COL_RED;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.proc.pmin = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.proc.pmax = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.proc.color = parse_color(tok);

				/* It's easy to set max=0 when you only want to define a minimum */
				if (currule->rule.proc.pmin && (currule->rule.proc.pmax == 0))
					currule->rule.proc.pmax = -1;
			}
			else if (strcasecmp(tok, "LOG") == 0) {
				currule = setup_rule(C_LOG, curhost, curexhost, curpage, curexpage, curtime, cfid);
				currule->rule.log.matchexp  = NULL;
				currule->rule.log.matchone  = NULL;
				currule->rule.log.ignoreexp = NULL;
				currule->rule.log.color     = COL_RED;

				tok = wstok(NULL);
				currule->rule.log.logfile   = setup_expr(tok, 0);
				tok = wstok(NULL);
				currule->rule.log.matchexp  = setup_expr(tok, 1);
				currule->rule.log.matchone  = setup_expr(tok, 0);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.log.color     = parse_color(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.log.ignoreexp = setup_expr(tok, 1);
			}
			else {
				unknowntok = 1;
				errprintf("Unknown token '%s' ignored at line %d\n", tok, cfid);
			}

			tok = (unknowntok ? NULL : wstok(NULL));
		}

		if (!currule && !unknowntok) {
			/* No rules on this line - its the new set of criteria */
			curhost = newhost;
			curpage = newpage;
			curexhost = newexhost;
			curexpage = newexpage;
			if (curtime) xfree(curtime); curtime = newtime;
		}
	}

	stackfclose(fd);
	freestrbuffer(inbuf);
	if (curtime) xfree(curtime);

	/* Create the ruletree, but leave it empty - it will be filled as clients report */
	ruletree = rbtNew(name_compare);
	havetree = 1;

	MEMUNDEFINE(fn);
	return 1;
}

void dump_client_config(void)
{
	c_rule_t *rwalk;

	for (rwalk = rulehead; (rwalk); rwalk = rwalk->next) {
		switch (rwalk->ruletype) {
		  case C_UPTIME:
			printf("UP %d %d", rwalk->rule.uptime.recentlimit, rwalk->rule.uptime.ancientlimit);
			break;
		  case C_LOAD:
			printf("LOAD %.2f %.2f", rwalk->rule.load.warnlevel, rwalk->rule.load.paniclevel);
			break;
		  case C_DISK:
			printf("DISK %s", rwalk->rule.disk.fsexp->pattern);
			printf(" %lu%c", rwalk->rule.disk.warnlevel, (rwalk->rule.disk.absolutes & 1) ? 'K' : '%');
			printf(" %lu%c", rwalk->rule.disk.paniclevel, (rwalk->rule.disk.absolutes & 2) ? 'K' : '%');
			printf(" %d %d %s", rwalk->rule.disk.dmin, rwalk->rule.disk.dmax, colorname(rwalk->rule.disk.color));
			break;
		  case C_MEM:
			switch (rwalk->rule.mem.memtype) {
			  case C_MEM_PHYS: printf("MEMREAL"); break;
			  case C_MEM_SWAP: printf("MEMSWAP"); break;
			  case C_MEM_ACT: printf("MEMACT"); break;
			}
			printf(" %d %d", rwalk->rule.mem.warnlevel, rwalk->rule.mem.paniclevel);
			break;
		  case C_PROC:
			if (strchr(rwalk->rule.proc.procexp->pattern, ' ') ||
			    strchr(rwalk->rule.proc.procexp->pattern, '\t')) {
				printf("PROC \"%s\" %d %d %s", rwalk->rule.proc.procexp->pattern,
				       rwalk->rule.proc.pmin, rwalk->rule.proc.pmax, colorname(rwalk->rule.proc.color));
			}
			else {
				printf("PROC %s %d %d %s", rwalk->rule.proc.procexp->pattern,
				       rwalk->rule.proc.pmin, rwalk->rule.proc.pmax, colorname(rwalk->rule.proc.color));
			}
			break;
		  case C_LOG:
			printf("LOG %s %s %s %s\n",
				rwalk->rule.log.logfile->pattern, rwalk->rule.log.matchexp->pattern,
				(rwalk->rule.log.ignoreexp ? rwalk->rule.log.ignoreexp->pattern : ""),
				colorname(rwalk->rule.log.color));
			break;
		}

		if (rwalk->timespec) printf(" TIME=%s", rwalk->timespec);
		if (rwalk->hostexp) printf(" HOST=%s", rwalk->hostexp->pattern);
		if (rwalk->exhostexp) printf(" EXHOST=%s", rwalk->exhostexp->pattern);
		if (rwalk->pageexp) printf(" HOST=%s", rwalk->pageexp->pattern);
		if (rwalk->expageexp) printf(" EXHOST=%s", rwalk->expageexp->pattern);
		printf(" (line: %d)\n", rwalk->cfid);
	}
}

static c_rule_t *getrule(char *hostname, char *pagename, ruletype_t ruletype)
{
	static ruleset_t *rwalk = NULL;

	if (hostname || pagename) {
		rwalk = ruleset(hostname, pagename); 
	}
	else {
		rwalk = rwalk->next;
	}

	for (; (rwalk); rwalk = rwalk->next) {
		if (rwalk->rule->ruletype != ruletype) continue;
		if (rwalk->rule->timespec && !timematch(rwalk->rule->timespec)) continue;

		/* If we get here, then we have something that matches */
		return rwalk->rule;
	}

	return NULL;
}

int get_cpu_thresholds(namelist_t *hinfo, float *loadyellow, float *loadred, int *recentlimit, int *ancientlimit)
{
	char *hostname, *pagename;
	c_rule_t *rule;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_PAGEPATH);


	*loadyellow = 5.0;
	*loadred = 10.0;

	rule = getrule(hostname, pagename, C_LOAD);
	if (rule) {
		*loadyellow = rule->rule.load.warnlevel;
		*loadred    = rule->rule.load.paniclevel;
	}


	*recentlimit = 3600;
	*ancientlimit = -1;

	rule = getrule(hostname, pagename, C_UPTIME);
	if (rule) {
		*recentlimit  = rule->rule.uptime.recentlimit;
		*ancientlimit = rule->rule.uptime.ancientlimit;
		return rule->cfid;
	}

	return 0;
}

int get_disk_thresholds(namelist_t *hinfo, char *fsname, unsigned long *warnlevel, unsigned long *paniclevel, int *absolutes)
{
	char *hostname, *pagename;
	c_rule_t *rule;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_PAGEPATH);

	*warnlevel = 90;
	*paniclevel = 95;
	*absolutes = 0;

	rule = getrule(hostname, pagename, C_DISK);
	while (rule && !namematch(fsname, rule->rule.disk.fsexp->pattern, rule->rule.disk.fsexp->exp)) {
		rule = getrule(NULL, NULL, C_DISK);
	}

	if (rule) {
		*warnlevel = rule->rule.disk.warnlevel;
		*paniclevel = rule->rule.disk.paniclevel;
		*absolutes = rule->rule.disk.absolutes;
		return rule->cfid;
	}

	return 0;
}

void get_memory_thresholds(namelist_t *hinfo, 
		int *physyellow, int *physred, int *swapyellow, int *swapred, int *actyellow, int *actred)
{
	char *hostname, *pagename;
	c_rule_t *rule;
	int gotphys = 0, gotswap = 0, gotact = 0;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_PAGEPATH);

	*physyellow = 100;
	*physred = 101;
	*swapyellow = 50;
	*swapred = 80;
	*actyellow = 90;
	*actred = 97;

	rule = getrule(hostname, pagename, C_MEM);
	while (rule) {
		switch (rule->rule.mem.memtype) {
		  case C_MEM_PHYS:
			if (!gotphys) {
				*physyellow = rule->rule.mem.warnlevel;
				*physred    = rule->rule.mem.paniclevel;
				gotphys     = 1;
			}
			break;
		  case C_MEM_ACT:
			if (!gotact) {
				*actyellow  = rule->rule.mem.warnlevel;
				*actred     = rule->rule.mem.paniclevel;
				gotact      = 1;
			}
			break;
		  case C_MEM_SWAP:
			if (!gotswap) {
				*swapyellow = rule->rule.mem.warnlevel;
				*swapred    = rule->rule.mem.paniclevel;
				gotswap     = 1;
			}
			break;
		}
		rule = getrule(NULL, NULL, C_MEM);
	}
}

int scan_log(namelist_t *hinfo, char *logname, char *logdata, strbuffer_t *summarybuf)
{
	int result = COL_GREEN;
	char *hostname, *pagename;
	c_rule_t *rule;
	int firstmatch = 1;
	int anylines = 0;
	char *boln, *eoln;
	char msgline[1024];
	strbuffer_t *linesfromlogfile;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_PAGEPATH);
	linesfromlogfile = newstrbuffer(0);
	
	for (rule = getrule(hostname, pagename, C_LOG); (rule); rule = getrule(NULL, NULL, C_LOG)) {
		/* First, check if the filename matches */
		if (!namematch(logname, rule->rule.log.logfile->pattern, rule->rule.log.logfile->exp)) continue;

		/* Next, check for a match anywhere in the data*/
		if (!namematch(logdata, rule->rule.log.matchexp->pattern, rule->rule.log.matchexp->exp))
			continue;

		/* Some data in there matches what we want. Look at each line. */
		boln = logdata;
		while (boln) {
			eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';
			if (namematch(boln, rule->rule.log.matchone->pattern, rule->rule.log.matchone->exp)) {
				/* It matches. But maybe we'll ignore it ? */
				if (!(rule->rule.log.ignoreexp && namematch(boln, rule->rule.log.ignoreexp->pattern, rule->rule.log.ignoreexp->exp))) {
					/* We wants it ... */
					anylines++;
					sprintf(msgline, "&%s ", colorname(rule->rule.log.color));
					addtobuffer(linesfromlogfile, msgline);
					addtobuffer(linesfromlogfile, boln);
					addtobuffer(linesfromlogfile, "\n");
				}
			}

			if (eoln) {
				*eoln = '\n';
				boln = eoln+1;
			}
			else boln = NULL;
		}

		/* We have a match */
		if (anylines && (rule->rule.log.color > result)) result = rule->rule.log.color;

		if (firstmatch) {
			sprintf(msgline, "\nFound in %s:\n", logname);
			addtobuffer(summarybuf, msgline);
			firstmatch = 0;
		}

		addtostrbuffer(summarybuf, linesfromlogfile);
		clearstrbuffer(linesfromlogfile);
	}

	freestrbuffer(linesfromlogfile);
	return result;
}


typedef struct mon_proc_t {
	c_rule_t *rule;
	struct mon_proc_t *next;
} mon_proc_t;

static int clear_counts(namelist_t *hinfo, ruletype_t ruletype, mon_proc_t **head, mon_proc_t **tail, mon_proc_t **walk)
{
	char *hostname, *pagename;
	c_rule_t *rule;
	int count = 0;

	while (*head) {
		mon_proc_t *tmp = *head;
		*head = (*head)->next;
		xfree(tmp);
	}
	*head = *tail = *walk = NULL;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_PAGEPATH);

	rule = getrule(hostname, pagename, ruletype);
	while (rule) {
		mon_proc_t *newitem = (mon_proc_t *)malloc(sizeof(mon_proc_t));

		newitem->rule = rule;
		newitem->next = NULL;
		if (*tail) { (*tail)->next = newitem; *tail = newitem; }
		else { *head = *tail = newitem; }

		count++;
		switch (rule->ruletype) {
		  case C_DISK : rule->rule.disk.dcount = 0; break;
		  case C_PROC : rule->rule.proc.pcount = 0; break;
		  default: break;
		}

		rule = getrule(NULL, NULL, ruletype);
	}

	*walk = *head;
	return count;
}

static void add_count(char *pname, mon_proc_t *head)
{
	mon_proc_t *pwalk;

	if (!pname) return;

	for (pwalk = head; (pwalk); pwalk = pwalk->next) {
		switch (pwalk->rule->ruletype) {
		  case C_PROC:
			if (!pwalk->rule->rule.proc.procexp->exp) {
				/* 
				 * No pattern, just see if the token in the config file is
				 * present in the string we got from "ps". So you can setup
				 * the config to look for "cron" and it will actually find "/usr/sbin/cron".
				 */
				if (strstr(pname, pwalk->rule->rule.proc.procexp->pattern))
					pwalk->rule->rule.proc.pcount++;
			}
			else {
				if (namematch(pname, pwalk->rule->rule.proc.procexp->pattern, pwalk->rule->rule.proc.procexp->exp))
					pwalk->rule->rule.proc.pcount++;
			}
			break;

		  case C_DISK:
			if (!pwalk->rule->rule.disk.fsexp->exp) {
				if (strstr(pname, pwalk->rule->rule.disk.fsexp->pattern))
					pwalk->rule->rule.disk.dcount++;
			}
			else {
				if (namematch(pname, pwalk->rule->rule.disk.fsexp->pattern, pwalk->rule->rule.disk.fsexp->exp))
					pwalk->rule->rule.disk.dcount++;
			}
			break;

		  default: break;
		}
	}
}

static char *check_count(int *count, ruletype_t ruletype, int *lowlim, int *uplim, int *color, mon_proc_t **walk)
{
	char *result = NULL;

	if (*walk == NULL) return NULL;

	switch (ruletype) {
	  case C_PROC:
		result = (*walk)->rule->rule.proc.procexp->pattern;
		*count = (*walk)->rule->rule.proc.pcount;
		*lowlim = (*walk)->rule->rule.proc.pmin;
		*uplim = (*walk)->rule->rule.proc.pmax;
		*color = COL_GREEN;
		if ((*lowlim !=  0) && (*count < *lowlim)) *color = (*walk)->rule->rule.proc.color;
		if ((*uplim  != -1) && (*count > *uplim)) *color = (*walk)->rule->rule.proc.color;
		break;

	  case C_DISK:
		result = (*walk)->rule->rule.disk.fsexp->pattern;
		*count = (*walk)->rule->rule.disk.dcount;
		*lowlim = (*walk)->rule->rule.disk.dmin;
		*uplim = (*walk)->rule->rule.disk.dmax;
		*color = COL_GREEN;
		if ((*lowlim !=  0) && (*count < *lowlim)) *color = (*walk)->rule->rule.disk.color;
		if ((*uplim  != -1) && (*count > *uplim)) *color = (*walk)->rule->rule.disk.color;
		break;

	  default: break;
	}

	*walk = (*walk)->next;

	return result;
}

static mon_proc_t *phead = NULL, *ptail = NULL, *pmonwalk = NULL;
static mon_proc_t *dhead = NULL, *dtail = NULL, *dmonwalk = NULL;

int clear_process_counts(namelist_t *hinfo)
{
	return clear_counts(hinfo, C_PROC, &phead, &ptail, &pmonwalk);
}

int clear_disk_counts(namelist_t *hinfo)
{
	return clear_counts(hinfo, C_DISK, &dhead, &dtail, &dmonwalk);
}

void add_process_count(char *pname)
{
	add_count(pname, phead);
}

void add_disk_count(char *dname)
{
	add_count(dname, dhead);
}

char *check_process_count(int *count, int *lowlim, int *uplim, int *color)
{
	return check_count(count, C_PROC, lowlim, uplim, color, &pmonwalk);
}

char *check_disk_count(int *count, int *lowlim, int *uplim, int *color)
{
	return check_count(count, C_DISK, lowlim, uplim, color, &dmonwalk);
}

