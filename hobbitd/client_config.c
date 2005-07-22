/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module                                                      */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: client_config.c,v 1.1 2005-07-22 16:14:06 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>

#include <pcre.h>

#include "libbbgen.h"
#include "client_config.h"

typedef struct c_load_t {
	float warnlevel, paniclevel;
} c_load_t;

typedef struct c_uptime_t {
	int recentlimit, ancientlimit;
} c_uptime_t;

typedef struct c_disk_t {
	char *fspattern;
	pcre *fspcre;
	int warnlevel, paniclevel;
} c_disk_t;

typedef struct c_mem_t {
	enum { C_MEM_PHYS, C_MEM_SWAP, C_MEM_ACT } memtype;
	int warnlevel, paniclevel;
} c_mem_t;

typedef struct c_proc_t {
	char *procpattern;
	pcre *procpcre;
	int pmin, pmax, pcount;
	int color;
} c_proc_t;

typedef enum { C_LOAD, C_UPTIME, C_DISK, C_MEM, C_PROC } ruletype_t;

typedef struct c_rule_t {
	char *hostpattern;
	pcre *hostpcre;
	char *exhostpattern;
	pcre *exhostpcre;
	char *pagepattern;
	pcre *pagepcre;
	char *expagepattern;
	pcre *expagepcre;
	char *timespec;
	ruletype_t ruletype;
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

void load_client_config(char *fn)
{
}

static c_rule_t *getrule(char *hostname, char *pagename, ruletype_t ruletype)
{
	static c_rule_t *rwalk = NULL;
	int found;

	if (hostname || pagename) rwalk = rulehead; else rwalk = rwalk->next;

	for (found = 0; (rwalk && !found); rwalk = rwalk->next) {
		if (rwalk->ruletype != ruletype) continue;
		if (rwalk->timespec && !timematch(rwalk->timespec)) continue;

		if (rwalk->exhostpattern && namematch(hostname, rwalk->exhostpattern, rwalk->exhostpcre)) continue;
		if (rwalk->hostpattern && !namematch(hostname, rwalk->hostpattern, rwalk->hostpcre)) continue;

		if (rwalk->expagepattern && namematch(pagename, rwalk->expagepattern, rwalk->expagepcre)) continue;
		if (rwalk->pagepattern && !namematch(pagename, rwalk->pagepattern, rwalk->pagepcre)) continue;

		/* If we get here, then we have something that matches */
		found = 1;
	}

	return rwalk;
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
	while (rule && !namematch(fsname, rule->rule.disk.fspattern, rule->rule.disk.fspcre)) {
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

	for (pwalk = phead; (pwalk); pwalk = pwalk->next) {
		if (namematch(pname, pwalk->rule->rule.proc.procpattern, pwalk->rule->rule.proc.procpcre)) pwalk->rule->rule.proc.pcount++;
	}
}

char *check_process_count(int *pcount, int *lowlim, int *uplim, int *pcolor)
{
	char *result;

	if (pmonwalk == NULL) return NULL;

	result = pmonwalk->rule->rule.proc.procpattern;
	*pcount = pmonwalk->rule->rule.proc.pcount;
	*lowlim = pmonwalk->rule->rule.proc.pmin;
	*uplim = pmonwalk->rule->rule.proc.pmax;
	*pcolor = COL_GREEN;

	if ((*lowlim !=  0) && (*pcount < *lowlim)) *pcolor = pmonwalk->rule->rule.proc.color;
	if ((*uplim  != -1) && (*pcount > *uplim)) *pcolor = pmonwalk->rule->rule.proc.color;

	pmonwalk = pmonwalk->next;

	return result;
}

