/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module                                                      */
/* This file has routines that load the hobbitd_client configuration and      */
/* finds the rules relevant for a particular test when applied.               */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: client_config.c,v 1.7 2005-08-23 21:15:29 henrik Exp $";

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
	int warnlevel, paniclevel;
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

typedef enum { C_LOAD, C_UPTIME, C_DISK, C_MEM, C_PROC } ruletype_t;

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
	} rule;
} c_rule_t;

static c_rule_t *rulehead = NULL;
static c_rule_t *ruletail = NULL;
static exprlist_t *exprhead = NULL;

static exprlist_t *setup_expr(char *ptn)
{
	exprlist_t *newitem = (exprlist_t *)calloc(1, sizeof(exprlist_t));

	newitem->pattern = strdup(ptn);
	if (*ptn == '%') newitem->exp = compileregex(ptn+1);
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


int load_client_config(char *configfn)
{
	/* (Re)load the configuration file without leaking memory */
	static time_t lastload = 0;     /* Last time the config file was loaded */
	char fn[PATH_MAX];
	struct stat st;
	FILE *fd;
	char *inbuf = NULL;
	int inbufsz;
	char *p, *tok;
	exprlist_t *curhost, *curpage, *curexhost, *curexpage;
	char *curtime;
	c_rule_t *currule = NULL;
	int cfid = 0;

	MEMDEFINE(fn);

	if (configfn) strcpy(fn, configfn); else sprintf(fn, "%s/etc/hobbit-clients.cfg", xgetenv("BBHOME"));
	if (stat(fn, &st) == -1) {
		errprintf("Cannot stat config file %s: %s\n", fn, strerror(errno)); 
		MEMUNDEFINE(fn); 
		return 0;
	}

	if (st.st_mtime == lastload) { MEMUNDEFINE(fn); return 0; }
	lastload = st.st_mtime;

	fd = fopen(fn, "r");
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

	initfgets(fd);
	curhost = curpage = curexhost = curexpage = NULL;
	curtime = NULL;
	while (unlimfgets(&inbuf, &inbufsz, fd)) {
		exprlist_t *newhost, *newpage, *newexhost, *newexpage;
		char *newtime;
		int unknowntok = 0;

		cfid++;
		sanitize_input(inbuf);
		if (strlen(inbuf) == 0) continue;

		newhost = newpage = newexhost = newexpage = NULL;
		newtime = NULL;
		currule = NULL;

		tok = wstok(inbuf);
		while (tok) {
			if (strncasecmp(tok, "HOST=", 5) == 0) {
				p = strchr(tok, '=');
				newhost = setup_expr(p+1);
				if (currule) currule->hostexp = newhost;
			}
			else if (strncasecmp(tok, "EXHOST=", 7) == 0) {
				p = strchr(tok, '=');
				newexhost = setup_expr(p+1);
				if (currule) currule->exhostexp = newexhost;
			}
			else if (strncasecmp(tok, "PAGE=", 5) == 0) {
				p = strchr(tok, '=');
				newpage = setup_expr(p+1);
				if (currule) currule->pageexp = newpage;
			}
			else if (strncasecmp(tok, "EXPAGE=", 7) == 0) {
				p = strchr(tok, '=');
				newexpage = setup_expr(p+1);
				if (currule) currule->expageexp = newexpage;
			}
			else if (strncasecmp(tok, "TIME=", 5) == 0) {
				p = strchr(tok, '=');
				if (currule) currule->timespec = strdup(p+1);
				else newtime = strdup(p+1);
			}
			else if (strcasecmp(tok, "UP") == 0) {
				currule = setup_rule(C_UPTIME, curhost, curexhost, curpage, curexpage, curtime, cfid);
				p = wstok(NULL); 
				currule->rule.uptime.recentlimit = (p ? 60*durationvalue(p): 3600);
				p = wstok(NULL);
				currule->rule.uptime.ancientlimit = (p ? 60*durationvalue(p): -1);
			}
			else if (strcasecmp(tok, "LOAD") == 0) {
				currule = setup_rule(C_LOAD, curhost, curexhost, curpage, curexpage, curtime, cfid);
				p = wstok(NULL); 
				currule->rule.load.warnlevel = (p ? atof(p) : 5.0);
				p = wstok(NULL); 
				currule->rule.load.paniclevel = (p ? atof(p): 8.0);
			}
			else if (strcasecmp(tok, "DISK") == 0) {
				currule = setup_rule(C_DISK, curhost, curexhost, curpage, curexpage, curtime, cfid);
				p = wstok(NULL); 
				if (p) currule->rule.disk.fsexp = setup_expr(p);
				p = wstok(NULL);
				currule->rule.disk.warnlevel = (p ? atoi(p) : 90);
				p = wstok(NULL);
				currule->rule.disk.paniclevel = (p ? atoi(p): 95);
				p = wstok(NULL);
			}
			else if ((strcasecmp(tok, "MEMREAL") == 0) || (strcasecmp(tok, "MEMPHYS") == 0) || (strcasecmp(tok, "PHYS") == 0)) {
				currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curtime, cfid);
				currule->rule.mem.memtype = C_MEM_PHYS;
				p = wstok(NULL);
				currule->rule.mem.warnlevel = (p ? atoi(p) : 100);
				p = wstok(NULL);
				currule->rule.mem.paniclevel = (p ? atoi(p) : 101);
			}
			else if ((strcasecmp(tok, "MEMSWAP") == 0) || (strcasecmp(tok, "SWAP") == 0)) {
				currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curtime, cfid);
				currule->rule.mem.memtype = C_MEM_SWAP;
				p = wstok(NULL);
				currule->rule.mem.warnlevel = (p ? atoi(p) : 50);
				p = wstok(NULL);
				currule->rule.mem.paniclevel = (p ? atoi(p) : 80);
			}
			else if ((strcasecmp(tok, "MEMACT") == 0) || (strcasecmp(tok, "ACTUAL") == 0) || (strcasecmp(tok, "ACT") == 0)) {
				currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curtime, cfid);
				currule->rule.mem.memtype = C_MEM_ACT;
				p = wstok(NULL);
				currule->rule.mem.warnlevel = (p ? atoi(p) : 90);
				p = wstok(NULL);
				currule->rule.mem.paniclevel = (p ? atoi(p) : 97);
			}
			else if (strcasecmp(tok, "PROC") == 0) {
				currule = setup_rule(C_PROC, curhost, curexhost, curpage, curexpage, curtime, cfid);
				p = wstok(NULL); 
				if (p) currule->rule.proc.procexp = setup_expr(p);
				p = wstok(NULL);
				currule->rule.proc.pmin = (p ? atoi(p) : 1);
				p = wstok(NULL);
				currule->rule.proc.pmax = (p ? atoi(p) : -1);
				p = wstok(NULL);
				currule->rule.proc.color = (p ? parse_color(p) : COL_RED);

				/* It's easy to set max=0 when you only want to define a minimum */
				if (currule->rule.proc.pmin && (currule->rule.proc.pmax == 0))
					currule->rule.proc.pmax = -1;
			}
			else {
				unknowntok = 1;
				errprintf("Unknown token '%s' ignored at line %d\n", tok, cfid);
			}

			tok = (unknowntok ? NULL : wstok(NULL));
		}

		if (!currule && !unknowntok) {
			/* No rules on this line - its the new set of defaults */
			curhost = newhost;
			curpage = newpage;
			curexhost = newexhost;
			curexpage = newexpage;
			if (curtime) xfree(curtime); curtime = newtime;
		}
	}

	fclose(fd);
	if (inbuf) xfree(inbuf);
	if (curtime) xfree(curtime);

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
			printf("DISK %s %d %d", rwalk->rule.disk.fsexp->pattern,
			       rwalk->rule.disk.warnlevel, rwalk->rule.disk.paniclevel);
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
				printf("PROC \"%s\" %d %d", rwalk->rule.proc.procexp->pattern,
				       rwalk->rule.proc.pmin, rwalk->rule.proc.pmax);
			}
			else {
				printf("PROC %s %d %d", rwalk->rule.proc.procexp->pattern,
				       rwalk->rule.proc.pmin, rwalk->rule.proc.pmax);
			}
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
	static c_rule_t *rwalk = NULL;
	static char *ahost = NULL, *apage = NULL;

	if (hostname || pagename) {
		rwalk = rulehead; 
		ahost = hostname;
		apage = pagename;
	}
	else {
		rwalk = rwalk->next;
	}

	for (; (rwalk); rwalk = rwalk->next) {
		if (rwalk->ruletype != ruletype) continue;
		if (rwalk->timespec && !timematch(rwalk->timespec)) continue;

		if (rwalk->exhostexp && namematch(ahost, rwalk->exhostexp->pattern, rwalk->exhostexp->exp)) continue;
		if (rwalk->hostexp && !namematch(ahost, rwalk->hostexp->pattern, rwalk->hostexp->exp)) continue;

		if (rwalk->expageexp && namematch(apage, rwalk->expageexp->pattern, rwalk->expageexp->exp)) continue;
		if (rwalk->pageexp && !namematch(apage, rwalk->pageexp->pattern, rwalk->pageexp->exp)) continue;

		/* If we get here, then we have something that matches */
		return rwalk;
	}

	return NULL;
}

void get_cpu_thresholds(namelist_t *hinfo, float *loadyellow, float *loadred, int *recentlimit, int *ancientlimit)
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
	}
}

void get_disk_thresholds(namelist_t *hinfo, char *fsname, int *warnlevel, int *paniclevel)
{
	char *hostname, *pagename;
	c_rule_t *rule;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_PAGEPATH);

	*warnlevel = 90;
	*paniclevel = 95;

	rule = getrule(hostname, pagename, C_DISK);
	while (rule && !namematch(fsname, rule->rule.disk.fsexp->pattern, rule->rule.disk.fsexp->exp)) {
		rule = getrule(NULL, NULL, C_DISK);
	}

	if (rule) {
		*warnlevel = rule->rule.disk.warnlevel;
		*paniclevel = rule->rule.disk.paniclevel;
	}
}

void get_memory_thresholds(namelist_t *hinfo, 
		int *physyellow, int *physred, int *swapyellow, int *swapred, int *actyellow, int *actred)
{
	char *hostname, *pagename;
	c_rule_t *rule;

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
			*physyellow = rule->rule.mem.warnlevel;
			*physred    = rule->rule.mem.paniclevel;
			break;
		  case C_MEM_ACT:
			*actyellow  = rule->rule.mem.warnlevel;
			*actred     = rule->rule.mem.paniclevel;
			break;
		  case C_MEM_SWAP:
			*swapyellow = rule->rule.mem.warnlevel;
			*swapred    = rule->rule.mem.paniclevel;
			break;
		}
		rule = getrule(NULL, NULL, C_MEM);
	}
}

typedef struct mon_proc_t {
	c_rule_t *rule;
	struct mon_proc_t *next;
} mon_proc_t;
static mon_proc_t *phead = NULL, *ptail = NULL, *pmonwalk = NULL;

int clear_process_counts(namelist_t *hinfo)
{
	char *hostname, *pagename;
	c_rule_t *rule;
	int count = 0;

	while (phead) {
		mon_proc_t *tmp = phead;
		phead = phead->next;
		xfree(tmp);
	}
	phead = ptail = pmonwalk = NULL;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_PAGEPATH);

	rule = getrule(hostname, pagename, C_PROC);
	while (rule) {
		mon_proc_t *newitem = (mon_proc_t *)malloc(sizeof(mon_proc_t));

		newitem->rule = rule;
		newitem->next = NULL;
		if (ptail) { ptail->next = newitem; ptail = newitem; }
		else { phead = ptail = newitem; }

		count++;
		rule->rule.proc.pcount = 0;
		rule = getrule(NULL, NULL, C_PROC);
	}

	pmonwalk = phead;
	return count;
}

void add_process_count(char *pname)
{
	mon_proc_t *pwalk;
	int ovector[10];
	int result;

	for (pwalk = phead; (pwalk); pwalk = pwalk->next) {
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
	}
}

char *check_process_count(int *pcount, int *lowlim, int *uplim, int *pcolor)
{
	char *result;

	if (pmonwalk == NULL) return NULL;

	result = pmonwalk->rule->rule.proc.procexp->pattern;
	*pcount = pmonwalk->rule->rule.proc.pcount;
	*lowlim = pmonwalk->rule->rule.proc.pmin;
	*uplim = pmonwalk->rule->rule.proc.pmax;
	*pcolor = COL_GREEN;

	if ((*lowlim !=  0) && (*pcount < *lowlim)) *pcolor = pmonwalk->rule->rule.proc.color;
	if ((*uplim  != -1) && (*pcount > *uplim)) *pcolor = pmonwalk->rule->rule.proc.color;

	pmonwalk = pmonwalk->next;

	return result;
}

