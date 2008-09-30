/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module                                                      */
/* This file has routines that load the hobbitd_client configuration and      */
/* finds the rules relevant for a particular test when applied.               */
/*                                                                            */
/* Copyright (C) 2005-2008 Henrik Storner <henrik@hswn.dk>                    */
/* "PORT" handling (C) Mirko Saam                                             */
/* "SVC" handling (C) Francois Lacroix                                        */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
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

typedef struct c_clock_t {
	int maxdiff;
} c_clock_t;

typedef struct c_disk_t {
	exprlist_t *fsexp;
	long warnlevel, paniclevel;
	int abswarn, abspanic;
	int dmin, dmax, dcount;
	int color;
	int ignored;
} c_disk_t;

typedef struct c_mem_t {
	enum { C_MEM_PHYS, C_MEM_SWAP, C_MEM_ACT } memtype;
	int warnlevel, paniclevel;
} c_mem_t;

typedef struct c_zos_mem_t {
       enum { C_MEM_CSA, C_MEM_ECSA, C_MEM_SQA, C_MEM_ESQA } zos_memtype;
       int warnlevel, paniclevel;
} c_zos_mem_t;

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

typedef struct c_paging_t {
	int warnlevel, paniclevel;
} c_paging_t;

#define FCHK_NOEXIST  (1 << 0)
#define FCHK_TYPE     (1 << 1)
#define FCHK_MODE     (1 << 2)
#define FCHK_MINLINKS (1 << 3)
#define FCHK_MAXLINKS (1 << 4)
#define FCHK_EQLLINKS (1 << 5)
#define FCHK_MINSIZE  (1 << 6)
#define FCHK_MAXSIZE  (1 << 7)
#define FCHK_EQLSIZE  (1 << 8)
#define FCHK_OWNERID  (1 << 10)
#define FCHK_OWNERSTR (1 << 11)
#define FCHK_GROUPID  (1 << 12)
#define FCHK_GROUPSTR (1 << 13)
#define FCHK_CTIMEMIN (1 << 16)
#define FCHK_CTIMEMAX (1 << 17)
#define FCHK_CTIMEEQL (1 << 18)
#define FCHK_MTIMEMIN (1 << 19)
#define FCHK_MTIMEMAX (1 << 20)
#define FCHK_MTIMEEQL (1 << 21)
#define FCHK_ATIMEMIN (1 << 22)
#define FCHK_ATIMEMAX (1 << 23)
#define FCHK_ATIMEEQL (1 << 24)
#define FCHK_MD5      (1 << 25)
#define FCHK_SHA1     (1 << 26)
#define FCHK_RMD160   (1 << 27)

#define CHK_OPTIONAL  (1 << 30)
#define CHK_TRACKIT   (1 << 31)
 
typedef struct c_file_t {
	exprlist_t *filename;
	int color;
	int ftype;
	off_t minsize, maxsize, eqlsize;
	unsigned int minlinks, maxlinks, eqllinks;
	unsigned int fmode;
	int ownerid, groupid;
	char *ownerstr, *groupstr;
	unsigned int minctimedif, maxctimedif, ctimeeql;
	unsigned int minmtimedif, maxmtimedif, mtimeeql;
	unsigned int minatimedif, maxatimedif, atimeeql;
	char *md5hash, *sha1hash, *rmd160hash;
} c_file_t;

typedef struct c_dir_t {
	exprlist_t *filename;
	int color;
	unsigned long maxsize, minsize;
} c_dir_t;

typedef struct c_port_t {
	exprlist_t *localexp;
	exprlist_t *exlocalexp;
	exprlist_t *remoteexp;
	exprlist_t *exremoteexp;
	exprlist_t *stateexp;
	exprlist_t *exstateexp;
	int pmin, pmax, pcount;
	int color;
} c_port_t;

typedef struct c_svc_t {
	exprlist_t *svcexp;
        exprlist_t *stateexp;
        exprlist_t *startupexp;
	char *startup, *state;
	int scount;
        int color;
} c_svc_t;

#define MIBCHK_MINVALUE  (1 << 0)
#define MIBCHK_MAXVALUE  (1 << 1)
#define MIBCHK_MATCH     (1 << 2)
typedef struct c_mibval_t {
	exprlist_t *mibvalexp;	/* Key composed of the mib name and the value name */
	exprlist_t *keyexp;	/* Match pattern for the mib table key */
	int color;
	long minval, maxval;
	exprlist_t *matchexp;

	/* 
	 * For optimization, we build a tree of c_rule_t pointers, indexed by a key
	 * which is combined from the mib-, key- and value-names. This tree is updated
	 * and/or used whenever an actual lookup happens for the thresholds.
	 * So when doing a lookup, we first check to see if the combination is in the
	 * tree; if not, then we scan the list by matching against the keyexp pattern
	 * and update the tree with the result.
	 */
	int havetree;
	RbtHandle valdeftree;
} c_mibval_t;

#define RRDDSCHK_GT     (1 << 0)
#define RRDDSCHK_GE     (1 << 1)
#define RRDDSCHK_LT     (1 << 2)
#define RRDDSCHK_LE     (1 << 3)
#define RRDDSCHK_EQ     (1 << 4)
#define RRDDSCHK_INTVL  (1 << 29)
typedef struct c_rrdds_t {
	exprlist_t *rrdkey;	/* Pattern match for filename of the RRD file */
	char *rrdds;		/* DS name */
	char *column;		/* Status column modified by this check */
	int color;
	/* For absolute min/max values of the data item */
	double limitval, limitval2;
} c_rrdds_t;

typedef enum { C_LOAD, C_UPTIME, C_CLOCK, C_DISK, C_MEM, C_PROC, C_LOG, C_FILE, C_DIR, C_PORT, C_SVC, C_PAGING, C_MIBVAL, C_RRDDS } ruletype_t;

typedef struct c_rule_t {
	exprlist_t *hostexp;
	exprlist_t *exhostexp;
	exprlist_t *pageexp;
	exprlist_t *expageexp;
	exprlist_t *classexp;
	exprlist_t *exclassexp;
	char *timespec, *statustext, *rrdidstr, *groups;
	ruletype_t ruletype;
	int cfid;
	unsigned int flags;
	struct c_rule_t *next;
	union {
		c_load_t load;
		c_uptime_t uptime;
		c_clock_t clock;
		c_disk_t disk;
		c_mem_t mem;
                c_zos_mem_t zos_mem;
		c_proc_t proc;
		c_log_t log;
		c_file_t fcheck;
		c_dir_t dcheck;
		c_port_t port;
		c_svc_t	svc;
		c_paging_t paging;
		c_mibval_t mibval;
		c_rrdds_t rrdds;
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


static off_t filesize_value(char *s)
{
	/* s is the size in BYTES */
	char *modifier;
	off_t result;

	modifier = (s + strspn(s, " 0123456789"));

#ifdef _LARGEFILE_SOURCE
	result = (off_t) str2ll(s, NULL);
#else
	result = (off_t) atol(s);
#endif

	switch (*modifier) {
	  case 'K': case 'k':
		result = (result << 10);
		break;

	  case 'M': case 'm':
		result = (result << 20);
		break;

	  case 'G': case 'g':
		result = (result << 30);
		break;

	  case 'T': case 't':
		result = (result << 40);
		break;

	  default:
		break;
	}

	return result;
}

static ruleset_t *ruleset(char *hostname, char *pagename, char *classname)
{
	/*
	 * This routine manages a list of rules that apply to a particular host.
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
	char *pagenamecopy, *pgtok;
	int pgmatchres, pgexclres;

	handle = rbtFind(ruletree, hostname);
	if (handle != rbtEnd(ruletree)) {
		/* We have the tree for this host */
		return (ruleset_t *)gettreeitem(ruletree, handle);
	}

	pagenamecopy = strdup(pagename);

	/* We must build the list of rules for this host */
	head = tail = NULL;
	for (rwalk = rulehead; (rwalk); rwalk = rwalk->next) {
		if (rwalk->exclassexp && namematch(classname, rwalk->exclassexp->pattern, rwalk->exclassexp->exp)) continue;
		if (rwalk->classexp && !namematch(classname, rwalk->classexp->pattern, rwalk->classexp->exp)) continue;
		if (rwalk->exhostexp && namematch(hostname, rwalk->exhostexp->pattern, rwalk->exhostexp->exp)) continue;
		if (rwalk->hostexp && !namematch(hostname, rwalk->hostexp->pattern, rwalk->hostexp->exp)) continue;

		pgmatchres = pgexclres = -1;
		pgtok = strtok(pagenamecopy, ",");
		while (pgtok) {
			if (rwalk->pageexp && (pgmatchres != 1))
				pgmatchres = (namematch(pgtok, rwalk->pageexp->pattern, rwalk->pageexp->exp) ? 1 : 0);
	
			if (rwalk->expageexp && (pgexclres != 1))
				pgexclres = (namematch(pgtok, rwalk->expageexp->pattern, rwalk->expageexp->exp) ? 1 : 0);
	
			pgtok = strtok(NULL, ",");
		}
		if (pgexclres == 1) continue;
		if (pgmatchres == 0) continue;

		/* All criteria match - add this rule to the list of rules for this host */
		itm = (ruleset_t *)calloc(1, sizeof(ruleset_t));
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

	xfree(pagenamecopy);

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
			    exprlist_t *curclass, exprlist_t *curexclass, 
			    char *curtime, char *curtext, char *curgroup,
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
	newitem->classexp = curclass;
	newitem->exclassexp = curexclass;
	if (curtime) newitem->timespec = strdup(curtime);
	if (curtext) newitem->statustext = strdup(curtext);
	if (curgroup) newitem->groups = strdup(curgroup);
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
	     (strncasecmp(token, "CLASS=", 6) == 0)	||
	     (strncasecmp(token, "EXCLASS=", 8) == 0)	||
	     (strncasecmp(token, "TEXT=", 5) == 0)	||
	     (strncasecmp(token, "GROUP=", 6) == 0)	||
	     (strncasecmp(token, "TIME=", 5) == 0)	) return 1;

	return 0;
}

static char *ftypestr(unsigned int ftype)
{
	if      (ftype == S_IFSOCK) return "socket";
	else if (ftype == S_IFREG)  return "file";
	else if (ftype == S_IFBLK)  return "block";
	else if (ftype == S_IFCHR)  return "char";
	else if (ftype == S_IFDIR)  return "dir";
	else if (ftype == S_IFIFO)  return "fifo";
	else if (ftype == S_IFLNK)  return "symlink";

	return "";
}

static char *grouplist = NULL;
void clearalertgroups(void)
{
	if (grouplist) xfree(grouplist);
}

char *getalertgroups(void)
{
	if (grouplist) {
		*(grouplist + strlen(grouplist) - 1) = '\0';
		return grouplist+1;
	}
	else return NULL;
}

void addalertgroup(char *group)
{
	char *key;
	int curlen;

	if (group == NULL) return;

	key = (char *)malloc(strlen(group)+3);
	sprintf(key, ",%s,", group);

	if (!grouplist) {
		grouplist = key;
		return;
	}

	if (strstr(grouplist, key)) {
		xfree(key);
		return;
	}

	curlen = strlen(grouplist);
	grouplist = (char *)realloc(grouplist, curlen + strlen(key) + 2);
	sprintf(grouplist + curlen, "%s,", key);
}

int load_client_config(char *configfn)
{
	/* (Re)load the configuration file without leaking memory */
	static void *configfiles = NULL;
	char fn[PATH_MAX];
	FILE *fd;
	strbuffer_t *inbuf;
	char *tok;
	exprlist_t *curhost, *curpage, *curclass, *curexhost, *curexpage, *curexclass;
	char *curtime, *curtext, *curgroup;
	c_rule_t *currule = NULL;
	int cfid = 0;

	MEMDEFINE(fn);

	if (configfn) strcpy(fn, configfn); else sprintf(fn, "%s/etc/hobbit-clients.cfg", xgetenv("BBHOME"));

	/* First check if there were no modifications at all */
	if (configfiles) {
		if (!stackfmodified(configfiles)){
			dbgprintf("No files modified, skipping reload of %s\n", fn);
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
		if (tmp->groups) xfree(tmp->groups);
		if (tmp->timespec) xfree(tmp->timespec);
		if (tmp->statustext) xfree(tmp->statustext);
		if (tmp->rrdidstr) xfree(tmp->rrdidstr);

		switch (tmp->ruletype) {
		  case C_MIBVAL:
			if (tmp->rule.mibval.havetree) rbtDelete(tmp->rule.mibval.valdeftree);
			break;

		  case C_RRDDS:
			if (tmp->rule.rrdds.rrdds) xfree(tmp->rule.rrdds.rrdds);
			if (tmp->rule.rrdds.column) xfree(tmp->rule.rrdds.column);
			break;

		  default:
			break;
		}
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

	curhost = curpage = curclass = curexhost = curexpage = curexclass = NULL;
	curtime = curtext = curgroup = NULL;
	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		exprlist_t *newhost, *newpage, *newexhost, *newexpage, *newclass, *newexclass;
		char *newtime, *newtext, *newgroup;
		int unknowntok = 0;

		cfid++;
		sanitize_input(inbuf, 1, 0); if (STRBUFLEN(inbuf) == 0) continue;

		newhost = newpage = newexhost = newexpage = newclass = newexclass = NULL;
		newtime = newtext = newgroup = NULL;
		currule = NULL;

		tok = wstok(STRBUF(inbuf));
		while (tok) {
			if (strncasecmp(tok, "HOST=", 5) == 0) {
				char *p = strchr(tok, '=');
				newhost = setup_expr(p+1, 0);
				if (currule) currule->hostexp = newhost;
				tok = wstok(NULL); continue;
			}
			else if (strncasecmp(tok, "EXHOST=", 7) == 0) {
				char *p = strchr(tok, '=');
				newexhost = setup_expr(p+1, 0);
				if (currule) currule->exhostexp = newexhost;
				tok = wstok(NULL); continue;
			}
			else if (strncasecmp(tok, "PAGE=", 5) == 0) {
				char *p = strchr(tok, '=');
				newpage = setup_expr(p+1, 0);
				if (currule) currule->pageexp = newpage;
				tok = wstok(NULL); continue;
			}
			else if (strncasecmp(tok, "EXPAGE=", 7) == 0) {
				char *p = strchr(tok, '=');
				newexpage = setup_expr(p+1, 0);
				if (currule) currule->expageexp = newexpage;
				tok = wstok(NULL); continue;
			}
			else if (strncasecmp(tok, "CLASS=", 6) == 0) {
				char *p = strchr(tok, '=');
				newclass = setup_expr(p+1, 0);
				if (currule) currule->classexp = newclass;
				tok = wstok(NULL); continue;
			}
			else if (strncasecmp(tok, "EXCLASS=", 8) == 0) {
				char *p = strchr(tok, '=');
				newexclass = setup_expr(p+1, 0);
				if (currule) currule->exclassexp = newexclass;
				tok = wstok(NULL); continue;
			}
			else if (strncasecmp(tok, "TIME=", 5) == 0) {
				char *p = strchr(tok, '=');
				if (currule) currule->timespec = strdup(p+1);
				else newtime = strdup(p+1);
				tok = wstok(NULL); continue;
			}
			else if (strncasecmp(tok, "TEXT=", 5) == 0) {
				char *p = strchr(tok, '=');
				if (currule) currule->statustext = strdup(p+1);
				else newtext = strdup(p+1);
				tok = wstok(NULL); continue;
			}
			else if (strncasecmp(tok, "GROUP=", 6) == 0) {
				char *p = strchr(tok, '=');
				if (currule) currule->groups = strdup(p+1);
				else newgroup = strdup(p+1);
				tok = wstok(NULL); continue;
			}
			else if (strncasecmp(tok, "DEFAULT", 6) == 0) {
				currule = NULL;
			}
			else if (strcasecmp(tok, "UP") == 0) {
				currule = setup_rule(C_UPTIME, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.uptime.recentlimit = 3600;
				currule->rule.uptime.ancientlimit = -1;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.uptime.recentlimit = 60*durationvalue(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.uptime.ancientlimit = 60*durationvalue(tok);
			}
			else if (strcasecmp(tok, "CLOCK") == 0) {
				currule = setup_rule(C_CLOCK, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.clock.maxdiff = 60;
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.clock.maxdiff = atoi(tok);
			}
			else if (strcasecmp(tok, "LOAD") == 0) {
				currule = setup_rule(C_LOAD, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.load.warnlevel = 5.0;
				currule->rule.load.paniclevel = 10.0;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.load.warnlevel = atof(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.load.paniclevel = atof(tok);
			}
			else if (strcasecmp(tok, "DISK") == 0) {
				currule = setup_rule(C_DISK, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.disk.abswarn = 0;
				currule->rule.disk.warnlevel = 90;
				currule->rule.disk.abspanic = 0;
				currule->rule.disk.paniclevel = 95;
				currule->rule.disk.dmin = 0;
				currule->rule.disk.dmax = -1;
				currule->rule.disk.color = COL_RED;
				currule->rule.disk.ignored = 0;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.fsexp = setup_expr(tok, 0);

				tok = wstok(NULL); if (isqual(tok)) continue;
				if (strcasecmp(tok, "ignore") == 0) {
					currule->rule.disk.ignored = 1;
					tok = wstok(NULL);
					continue;
				}
				currule->rule.disk.warnlevel = atol(tok);
				switch (*(tok + strspn(tok, "0123456789"))) {
				  case 'U':
				  case 'u': currule->rule.disk.abswarn = 1; break;
				  case '%': currule->rule.disk.abswarn = 0; break;
				  default : currule->rule.disk.abswarn = (currule->rule.disk.warnlevel > 200 ? 1 : 0); break;
				}

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.paniclevel = atol(tok);
				switch (*(tok + strspn(tok, "0123456789"))) {
				  case 'U':
				  case 'u': currule->rule.disk.abspanic = 1; break;
				  case '%': currule->rule.disk.abspanic = 0; break;
				  default : currule->rule.disk.abspanic = (currule->rule.disk.paniclevel > 200 ? 1 : 0); break;
				}

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.dmin = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.dmax = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.disk.color = parse_color(tok);
			}
			else if ((strcasecmp(tok, "MEMREAL") == 0) || (strcasecmp(tok, "MEMPHYS") == 0) || (strcasecmp(tok, "PHYS") == 0)) {
				currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.mem.memtype = C_MEM_PHYS;
				currule->rule.mem.warnlevel = 100;
				currule->rule.mem.paniclevel = 101;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.warnlevel = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.paniclevel = atoi(tok);
			}
			else if ((strcasecmp(tok, "MEMSWAP") == 0) || (strcasecmp(tok, "SWAP") == 0)) {
				currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.mem.memtype = C_MEM_SWAP;
				currule->rule.mem.warnlevel = 50;
				currule->rule.mem.paniclevel = 80;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.warnlevel = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.paniclevel = atoi(tok);
			}
			else if ((strcasecmp(tok, "MEMACT") == 0) || (strcasecmp(tok, "ACTUAL") == 0) || (strcasecmp(tok, "ACT") == 0)) {
				currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.mem.memtype = C_MEM_ACT;
				currule->rule.mem.warnlevel = 90;
				currule->rule.mem.paniclevel = 97;

				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.warnlevel = atoi(tok);
				tok = wstok(NULL); if (isqual(tok)) continue;
				currule->rule.mem.paniclevel = atoi(tok);
			}
                        else if (strcasecmp(tok, "MEMCSA") == 0) {
                                currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
                                currule->rule.zos_mem.zos_memtype = C_MEM_CSA;
                                currule->rule.zos_mem.warnlevel = 90;
                                currule->rule.zos_mem.paniclevel = 95;
 
                                tok = wstok(NULL); if (isqual(tok)) continue;
                                currule->rule.zos_mem.warnlevel = atoi(tok);
                                tok = wstok(NULL); if (isqual(tok)) continue;
                                currule->rule.zos_mem.paniclevel = atoi(tok);
                        }
                        else if (strcasecmp(tok, "MEMECSA") == 0) {
                                currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
                                currule->rule.zos_mem.zos_memtype = C_MEM_ECSA;
                                currule->rule.zos_mem.warnlevel = 90;
                                currule->rule.zos_mem.paniclevel = 95;
 
                                tok = wstok(NULL); if (isqual(tok)) continue;
                                currule->rule.zos_mem.warnlevel = atoi(tok);
                                tok = wstok(NULL); if (isqual(tok)) continue;
                                currule->rule.zos_mem.paniclevel = atoi(tok);
                        }
                        else if (strcasecmp(tok, "MEMSQA") == 0) {
                                currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
                                currule->rule.zos_mem.zos_memtype = C_MEM_SQA;
                                currule->rule.zos_mem.warnlevel = 90;
                                currule->rule.zos_mem.paniclevel = 95;
 
                                tok = wstok(NULL); if (isqual(tok)) continue;
                                currule->rule.zos_mem.warnlevel = atoi(tok);
                                tok = wstok(NULL); if (isqual(tok)) continue;
                                currule->rule.zos_mem.paniclevel = atoi(tok);
                        }
                        else if (strcasecmp(tok, "MEMESQA") == 0) {
                                currule = setup_rule(C_MEM, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
                                currule->rule.zos_mem.zos_memtype = C_MEM_ESQA;
                                currule->rule.zos_mem.warnlevel = 90;
                                currule->rule.zos_mem.paniclevel = 95;
 
                                tok = wstok(NULL); if (isqual(tok)) continue;
                                currule->rule.zos_mem.warnlevel = atoi(tok);
                                tok = wstok(NULL); if (isqual(tok)) continue;
                                currule->rule.zos_mem.paniclevel = atoi(tok);
                        }
			else if (strcasecmp(tok, "PROC") == 0) {
				int idx = 0;

				currule = setup_rule(C_PROC, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.proc.pmin = 1;
				currule->rule.proc.pmax = -1;
				currule->rule.proc.color = COL_RED;

				tok = wstok(NULL);
				currule->rule.proc.procexp = setup_expr(tok, 0);

				do {
					tok = wstok(NULL); if (!tok || isqual(tok)) { idx = -1; continue; }

					if (strncasecmp(tok, "min=", 4) == 0) {
						currule->rule.proc.pmin = atoi(tok+4);
					}
					else if (strncasecmp(tok, "max=", 4) == 0) {
						currule->rule.proc.pmax = atoi(tok+4);
						/* When we have an explicit max, minimum should not be higher */
						if (currule->rule.proc.pmax < currule->rule.proc.pmin) {
							currule->rule.proc.pmin = currule->rule.proc.pmax;
						}
					}
					else if (strncasecmp(tok, "color=", 6) == 0) {
						currule->rule.proc.color = parse_color(tok+6);
					}
					else if (strncasecmp(tok, "track", 5) == 0) {
						currule->flags |= CHK_TRACKIT;
						if (*(tok+5) == '=') currule->rrdidstr = strdup(tok+6);
					}
					else if (idx == 0) {
						currule->rule.proc.pmin = atoi(tok);
						idx++;
					}
					else if (idx == 1) {
						currule->rule.proc.pmax = atoi(tok);
						idx++;
					}
					else if (idx == 2) {
						currule->rule.proc.color = parse_color(tok);
						idx++;
					}
				} while (tok && (!isqual(tok)));

				/* It's easy to set max=0 when you only want to define a minimum */
				if (currule->rule.proc.pmin && (currule->rule.proc.pmax == 0)) {
					currule->rule.proc.pmax = -1;
				}
			}
			else if (strcasecmp(tok, "LOG") == 0) {
				int idx = 0;

				currule = setup_rule(C_LOG, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.log.logfile   = NULL;
				currule->rule.log.matchexp  = NULL;
				currule->rule.log.matchone  = NULL;
				currule->rule.log.ignoreexp = NULL;
				currule->rule.log.color     = COL_RED;

				do {
					tok = wstok(NULL); if (!tok || isqual(tok)) { idx = -1; continue; }

					if (strncasecmp(tok, "file=", 5) == 0) {
						currule->rule.log.logfile   = setup_expr(tok+5, 0);
					}
					else if (strncasecmp(tok, "match=", 6) == 0) {
						currule->rule.log.matchexp = setup_expr(tok+6, 1);
						currule->rule.log.matchone = setup_expr(tok+6, 0);
					}
					else if (strncasecmp(tok, "ignore=", 7) == 0) {
						currule->rule.log.ignoreexp = setup_expr(tok+7, 1);
					}
					else if (strncasecmp(tok, "color=", 6) == 0) {
						currule->rule.log.color = parse_color(tok+6);
					}
					else if (strcasecmp(tok, "optional") == 0) {
						currule->flags |= CHK_OPTIONAL;
					}
					else if (idx == 0) {
						currule->rule.log.logfile   = setup_expr(tok, 0);
						idx++;
					}
					else if (idx == 1) {
						currule->rule.log.matchexp = setup_expr(tok, 1);
						currule->rule.log.matchone = setup_expr(tok, 0);
						idx++;
					}
					else if (idx == 2) {
						currule->rule.log.color = parse_color(tok);
						idx++;
					}
					else if (idx == 3) {
						currule->rule.log.ignoreexp = setup_expr(tok, 1);
						idx++;
					}
				} while (tok && (!isqual(tok)));
			}
			else if (strcasecmp(tok, "FILE") == 0) {
				currule = setup_rule(C_FILE, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.fcheck.filename = NULL;
				currule->rule.fcheck.color = COL_RED;

				tok = wstok(NULL);
				currule->rule.fcheck.filename = setup_expr(tok, 0);
				do {
					tok = wstok(NULL); if (!tok || isqual(tok)) continue;

					if (strcasecmp(tok, "noexist") == 0) {
						currule->flags |= FCHK_NOEXIST;
					}
					else if (strncasecmp(tok, "type=", 5) == 0) {
						currule->flags |= FCHK_TYPE;
						if (strcasecmp(tok+5, "socket") == 0) currule->rule.fcheck.ftype = S_IFSOCK;
						else if (strcasecmp(tok+5, "file") == 0) currule->rule.fcheck.ftype = S_IFREG;
						else if (strcasecmp(tok+5, "block") == 0) currule->rule.fcheck.ftype = S_IFBLK;
						else if (strcasecmp(tok+5, "char") == 0) currule->rule.fcheck.ftype = S_IFCHR;
						else if (strcasecmp(tok+5, "dir") == 0) currule->rule.fcheck.ftype = S_IFDIR;
						else if (strcasecmp(tok+5, "fifo") == 0) currule->rule.fcheck.ftype = S_IFIFO;
						else if (strcasecmp(tok+5, "symlink") == 0) currule->rule.fcheck.ftype = S_IFLNK;
					}
					else if (strncasecmp(tok, "size>", 5) == 0) {
						currule->flags |= FCHK_MINSIZE;
						currule->rule.fcheck.minsize = filesize_value(tok+5);
					}
					else if (strncasecmp(tok, "size<", 5) == 0) {
						currule->flags |= FCHK_MAXSIZE;
						currule->rule.fcheck.maxsize = filesize_value(tok+5);
					}
					else if (strncasecmp(tok, "size=", 5) == 0) {
						currule->flags |= FCHK_EQLSIZE;
						currule->rule.fcheck.eqlsize = filesize_value(tok+5);
					}
					else if (strncasecmp(tok, "links>", 6) == 0) {
						currule->flags |= FCHK_MINLINKS;
						currule->rule.fcheck.minlinks = atol(tok+6);
					}
					else if (strncasecmp(tok, "links<", 6) == 0) {
						currule->flags |= FCHK_MAXLINKS;
						currule->rule.fcheck.maxlinks = atol(tok+6);
					}
					else if (strncasecmp(tok, "links=", 6) == 0) {
						currule->flags |= FCHK_EQLLINKS;
						currule->rule.fcheck.eqllinks = atol(tok+6);
					}
					else if (strncasecmp(tok, "mode=", 5) == 0) {
						currule->flags |= FCHK_MODE;
						currule->rule.fcheck.fmode = strtol(tok+5, NULL, 8);
					}
					else if ((strncasecmp(tok, "owner=", 6) == 0) ||
						 (strncasecmp(tok, "ownerid=", 8) == 0)) {
						char *eptr;
						int uid;
						
						uid = strtol(tok+6, &eptr, 10);
						if (*eptr == '\0') {
							/* All numeric */
							currule->flags |= FCHK_OWNERID;
							currule->rule.fcheck.ownerid = uid;
						}
						else {
							currule->flags |= FCHK_OWNERSTR;
							currule->rule.fcheck.ownerstr = strdup(tok+6);
						}
					}
					else if (strncasecmp(tok, "groupid=", 8) == 0) {
						/* Cannot use "group" because that is reserved */
						char *eptr;
						int uid;
						
						uid = strtol(tok+6, &eptr, 10);
						if (*eptr == '\0') {
							/* All numeric */
							currule->flags |= FCHK_GROUPID;
							currule->rule.fcheck.groupid = uid;
						}
						else {
							currule->flags |= FCHK_GROUPSTR;
							currule->rule.fcheck.groupstr = strdup(tok+6);
						}
					}
					else if (strncasecmp(tok, "mtime>", 6) == 0) {
						currule->flags |= FCHK_MTIMEMIN;
						currule->rule.fcheck.minmtimedif = atol(tok+6);
					}
					else if (strncasecmp(tok, "mtime<", 6) == 0) {
						currule->flags |= FCHK_MTIMEMAX;
						currule->rule.fcheck.maxmtimedif = atol(tok+6);
					}
					else if (strncasecmp(tok, "mtime=", 6) == 0) {
						currule->flags |= FCHK_MTIMEEQL;
						currule->rule.fcheck.mtimeeql = atol(tok+6);
					}
					else if (strncasecmp(tok, "ctime>", 6) == 0) {
						currule->flags |= FCHK_CTIMEMIN;
						currule->rule.fcheck.minctimedif = atol(tok+6);
					}
					else if (strncasecmp(tok, "ctime<", 6) == 0) {
						currule->flags |= FCHK_CTIMEMAX;
						currule->rule.fcheck.maxctimedif = atol(tok+6);
					}
					else if (strncasecmp(tok, "ctime=", 6) == 0) {
						currule->flags |= FCHK_CTIMEEQL;
						currule->rule.fcheck.ctimeeql = atol(tok+6);
					}
					else if (strncasecmp(tok, "atime>", 6) == 0) {
						currule->flags |= FCHK_ATIMEMIN;
						currule->rule.fcheck.minatimedif = atol(tok+6);
					}
					else if (strncasecmp(tok, "atime<", 6) == 0) {
						currule->flags |= FCHK_ATIMEMAX;
						currule->rule.fcheck.maxatimedif = atol(tok+6);
					}
					else if (strncasecmp(tok, "atime=", 6) == 0) {
						currule->flags |= FCHK_ATIMEEQL;
						currule->rule.fcheck.atimeeql = atol(tok+6);
					}
					else if (strncasecmp(tok, "md5=", 4) == 0) {
						currule->flags |= FCHK_MD5;
						currule->rule.fcheck.md5hash = strdup(tok+4);
					}
					else if (strncasecmp(tok, "sha1=", 5) == 0) {
						currule->flags |= FCHK_SHA1;
						currule->rule.fcheck.sha1hash = strdup(tok+5);
					}
					else if (strncasecmp(tok, "rmd160=", 7) == 0) {
						currule->flags |= FCHK_RMD160;
						currule->rule.fcheck.rmd160hash = strdup(tok+7);
					}
					else if (strncasecmp(tok, "track", 5) == 0) {
						currule->flags |= CHK_TRACKIT;
						if (*(tok+5) == '=') currule->rrdidstr = strdup(tok+6);
					}
					else if (strcasecmp(tok, "optional") == 0) {
						currule->flags |= CHK_OPTIONAL;
					}
					else {
						int col = parse_color(tok);
						if (col != -1) currule->rule.fcheck.color = col;
					}
				} while (tok && (!isqual(tok)));
			}
			else if (strcasecmp(tok, "DIR") == 0) {
				currule = setup_rule(C_DIR, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.dcheck.filename = NULL;
				currule->rule.dcheck.color = COL_RED;

				tok = wstok(NULL);
				currule->rule.dcheck.filename = setup_expr(tok, 0);
				do {
					tok = wstok(NULL); if (!tok || isqual(tok)) continue;

					if (strncasecmp(tok, "size<", 5) == 0) {
						currule->flags |= FCHK_MAXSIZE;
						currule->rule.dcheck.maxsize = atol(tok+5);
					}
					else if (strncasecmp(tok, "size>", 5) == 0) {
						currule->flags |= FCHK_MINSIZE;
						currule->rule.dcheck.minsize = atol(tok+5);
					}
					else if (strncasecmp(tok, "track", 5) == 0) {
						currule->flags |= CHK_TRACKIT;
						if (*(tok+5) == '=') currule->rrdidstr = strdup(tok+6);
					}
					else {
						int col = parse_color(tok);
						if (col != -1) currule->rule.dcheck.color = col;
					}
				} while (tok && (!isqual(tok)));
			}
			else if (strcasecmp(tok, "PORT") == 0) {
				currule = setup_rule(C_PORT, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);

				currule->rule.port.localexp = NULL;
				currule->rule.port.exlocalexp = NULL;
				currule->rule.port.remoteexp = NULL;
				currule->rule.port.exremoteexp = NULL;
				currule->rule.port.stateexp = NULL;
				currule->rule.port.exstateexp = NULL;
				currule->rule.port.pmin = 1;
				currule->rule.port.pmax = -1;
				currule->rule.port.color = COL_RED;

				/* parse syntax [local=ADDR] [remote=ADDR] [state=STATE] [min=mincount] [max=maxcount] [col=color] */
				do {
 					tok = wstok(NULL); if (!tok || isqual(tok)) continue;

					if (strncasecmp(tok, "local=", 6) == 0) {
						currule->rule.port.localexp = setup_expr(tok+6, 0);
					}
					else if (strncasecmp(tok, "exlocal=", 8) == 0) {
						currule->rule.port.exlocalexp = setup_expr(tok+8, 0);
					}
					else if (strncasecmp(tok, "remote=", 7) == 0) {
						currule->rule.port.remoteexp = setup_expr(tok+7, 0);
					}
					else if (strncasecmp(tok, "exremote=", 9) == 0) {
						currule->rule.port.exremoteexp = setup_expr(tok+9, 0);
					}
					else if (strncasecmp(tok, "state=", 6) == 0) {
						currule->rule.port.stateexp = setup_expr(tok+6, 0);
					}
					else if (strncasecmp(tok, "exstate=", 8) == 0) {
						currule->rule.port.exstateexp = setup_expr(tok+8, 0);
					}
					else if (strncasecmp(tok, "min=", 4) == 0) {
						currule->rule.port.pmin = atoi(tok+4);
					}
					else if (strncasecmp(tok, "max=", 4) == 0) {
						currule->rule.port.pmax = atoi(tok+4);

						/* When we have an explicit max, minimum should not be higher */
						if (currule->rule.port.pmax < currule->rule.port.pmin) {
							currule->rule.port.pmin = currule->rule.port.pmax;
						}
					}
					else if (strncasecmp(tok, "col=", 4) == 0) {
						currule->rule.port.color = parse_color(tok+4);
					}
					else if (strncasecmp(tok, "color=", 6) == 0) {
						currule->rule.port.color = parse_color(tok+6);
					}
					else if (strncasecmp(tok, "track", 5) == 0) {
						currule->flags |= CHK_TRACKIT;
						if (*(tok+5) == '=') currule->rrdidstr = strdup(tok+6);
					}
				} while (tok && (!isqual(tok)));
			}
			else if (strcasecmp(tok, "PAGING") == 0) {
				currule = setup_rule(C_PAGING, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);

				currule->rule.paging.warnlevel = 5;
				currule->rule.paging.paniclevel = 10;

				tok = wstok(NULL); if (!tok || isqual(tok)) continue;
				currule->rule.paging.warnlevel = atoi(tok);

				tok = wstok(NULL); if (!tok || isqual(tok)) continue;
				currule->rule.paging.paniclevel = atoi(tok);
			}
			else if (strcasecmp(tok, "SVC") == 0) {
				int idx = 0;

				currule = setup_rule(C_SVC, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);

				currule->rule.svc.svcexp = NULL;
				currule->rule.svc.startupexp = NULL;
				currule->rule.svc.stateexp = NULL;
				currule->rule.svc.state = NULL;
				currule->rule.svc.startup = NULL; 
				currule->rule.svc.color = COL_RED;

				tok = wstok(NULL);
				currule->rule.svc.svcexp = setup_expr(tok, 0);
				do {
					tok = wstok(NULL); if (!tok || isqual(tok)) { idx = -1; continue; }

					if (strncasecmp(tok, "startup=", 8) == 0) {
						currule->rule.svc.startupexp = setup_expr(tok+8, 0);
					}
					else if (strncasecmp(tok, "status=", 7) == 0) {
						currule->rule.svc.stateexp = setup_expr(tok+7, 0);
					}
					else if (strncasecmp(tok, "col=", 4) == 0) {
						currule->rule.svc.color = parse_color(tok+4);
					}
					else if (strncasecmp(tok, "color=", 6) == 0) {
						currule->rule.svc.color = parse_color(tok+6);
					}
				} while (tok && (!isqual(tok)));
			}
			else if (strcasecmp(tok, "MIB") == 0) {
				currule = setup_rule(C_MIBVAL, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.mibval.mibvalexp = NULL;
				currule->rule.mibval.keyexp = NULL;
				currule->rule.mibval.color = COL_RED;
				currule->rule.mibval.minval = -1;
				currule->rule.mibval.maxval = -1;
				currule->rule.mibval.matchexp = NULL;

				tok = wstok(NULL);
				currule->rule.mibval.mibvalexp = setup_expr(tok, 0);
				do {
					tok = wstok(NULL); if (!tok || isqual(tok)) continue;

					if (strncasecmp(tok, "key=", 4) == 0) {
						currule->rule.mibval.keyexp = setup_expr(tok+4, 0);
					}
					else if (strncasecmp(tok, "max=", 4) == 0) {
						currule->flags |= MIBCHK_MAXVALUE;
						currule->rule.mibval.maxval = atol(tok+4);
					}
					else if (strncasecmp(tok, "min=", 4) == 0) {
						currule->flags |= MIBCHK_MINVALUE;
						currule->rule.mibval.minval = atol(tok+4);
					}
					else if (strncasecmp(tok, "match=", 6) == 0) {
						currule->flags |= MIBCHK_MATCH;
						currule->rule.mibval.matchexp = setup_expr(tok+6, 0);
					}
					else if (strncasecmp(tok, "color=", 6) == 0) {
						int col = parse_color(tok+6);
						if (col != -1) currule->rule.mibval.color = col;
					}
				} while (tok && (!isqual(tok)));
			}
			else if (strcasecmp(tok, "DS") == 0) {
				char *key, *ds, *column;

				currule = setup_rule(C_RRDDS, curhost, curexhost, curpage, curexpage, curclass, curexclass, curtime, curtext, curgroup, cfid);
				currule->rule.rrdds.color = COL_RED;

				tok = wstok(NULL);
				column = tok;

				tok = wstok(NULL);
				key = tok;
				ds = (tok ? strrchr(tok, ':') : NULL);
				if (ds) { *ds = '\0'; ds++; }

				if (!column || !key || !ds) {
					errprintf("Invalid DS definition at line %d (missing column, key and/or dataset)\n", cfid);
					continue;
				}

				currule->rule.rrdds.rrdkey = setup_expr(key, 0);
				currule->rule.rrdds.rrdds = strdup(ds);
				currule->rule.rrdds.column = strdup(column);

				do {
					int getnumber = 0;

					tok = wstok(NULL); if (!tok || isqual(tok)) continue;

					if (strncasecmp(tok, ">=", 2) == 0) {
						if (currule->flags) currule->flags |= RRDDSCHK_INTVL;
						currule->flags |= RRDDSCHK_GE;
						getnumber = 2;
					}
					else if (strncasecmp(tok, "<=", 2) == 0) {
						if (currule->flags) currule->flags |= RRDDSCHK_INTVL;
						currule->flags |= RRDDSCHK_LE;
						getnumber = 2;
					}
					else if (strncasecmp(tok, ">", 1) == 0) {
						if (currule->flags) currule->flags |= RRDDSCHK_INTVL;
						currule->flags |= RRDDSCHK_GT;
						getnumber = 1;
					}
					else if (strncasecmp(tok, "<", 1) == 0) {
						if (currule->flags) currule->flags |= RRDDSCHK_INTVL;
						currule->flags |= RRDDSCHK_LT;
						getnumber = 1;
					}
					else if (strncasecmp(tok, "color=", 6) == 0) {
						int col = parse_color(tok+6);
						if (col != -1) currule->rule.rrdds.color = col;
					}

					if (getnumber) {
						if (currule->flags & RRDDSCHK_INTVL)
							currule->rule.rrdds.limitval2 = atof(tok+getnumber);
						else
							currule->rule.rrdds.limitval = atof(tok+getnumber);

						if ((currule->flags & RRDDSCHK_INTVL) && (currule->rule.rrdds.limitval > currule->rule.rrdds.limitval2)) {
							/* Swap the two values, so we always have limitval as the lower bound, and limitval2 as the upper */
							double tmp;

							tmp=currule->rule.rrdds.limitval;
							currule->rule.rrdds.limitval = currule->rule.rrdds.limitval2;
							currule->rule.rrdds.limitval2 = tmp;
						}
					}
				} while (tok && (!isqual(tok)));
			}
			else {
				errprintf("Unknown token '%s' ignored at line %d\n", tok, cfid);
				unknowntok = 1; tok = NULL; continue;
			}

			if (tok && !isqual(tok)) tok = wstok(NULL);
		}

		if (!currule && !unknowntok) {
			/* No rules on this line - its the new set of criteria */
			curhost = newhost;
			curpage = newpage;
			curclass = newclass;
			curexhost = newexhost;
			curexpage = newexpage;
			curexclass = newexclass;
			if (curtime) xfree(curtime); curtime = newtime;
			if (curtext) xfree(curtext); curtext = newtext;
			if (curgroup) xfree(curgroup); curgroup = newgroup;
		}
	}

	stackfclose(fd);
	freestrbuffer(inbuf);
	if (curtime) xfree(curtime);
	if (curtext) xfree(curtext);

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

		  case C_CLOCK:
			printf("CLOCK %d", rwalk->rule.clock.maxdiff);
			break;

		  case C_LOAD:
			printf("LOAD %.2f %.2f", rwalk->rule.load.warnlevel, rwalk->rule.load.paniclevel);
			break;

		  case C_DISK:
			printf("DISK %s", rwalk->rule.disk.fsexp->pattern);
			if (rwalk->rule.disk.ignored)
				printf("IGNORE");
			else {
				printf(" %lu%c", rwalk->rule.disk.warnlevel, (rwalk->rule.disk.abswarn ? 'U' : '%'));
				printf(" %lu%c", rwalk->rule.disk.paniclevel, (rwalk->rule.disk.abspanic  ? 'U' : '%'));
				printf(" %d %d %s", rwalk->rule.disk.dmin, rwalk->rule.disk.dmax, colorname(rwalk->rule.disk.color));
			}
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
			printf("LOG %s MATCH=%s COLOR=%s",
				rwalk->rule.log.logfile->pattern, 
				rwalk->rule.log.matchexp->pattern,
				colorname(rwalk->rule.log.color));
			if (rwalk->rule.log.ignoreexp) printf(" IGNORE=%s", rwalk->rule.log.ignoreexp->pattern);
			break;

		  case C_FILE:
			printf("FILE %s %s", rwalk->rule.fcheck.filename->pattern, 
				colorname(rwalk->rule.fcheck.color));

			if (rwalk->flags & FCHK_NOEXIST) 
				printf(" noexist");
			if (rwalk->flags & FCHK_TYPE)
				printf(" type=%s", ftypestr(rwalk->rule.fcheck.ftype));
			if (rwalk->flags & FCHK_MODE) 
				printf(" mode=%o", rwalk->rule.fcheck.fmode);
#ifdef _LARGEFILE_SOURCE
			if (rwalk->flags & FCHK_MINSIZE) 
				printf(" size>%lld", rwalk->rule.fcheck.minsize);
			if (rwalk->flags & FCHK_MAXSIZE) 
				printf(" size<%lld", rwalk->rule.fcheck.maxsize);
			if (rwalk->flags & FCHK_EQLSIZE) 
				printf(" size=%lld", rwalk->rule.fcheck.eqlsize);
#else
			if (rwalk->flags & FCHK_MINSIZE) 
				printf(" size>%ld", rwalk->rule.fcheck.minsize);
			if (rwalk->flags & FCHK_MAXSIZE) 
				printf(" size<%ld", rwalk->rule.fcheck.maxsize);
			if (rwalk->flags & FCHK_EQLSIZE) 
				printf(" size=%ld", rwalk->rule.fcheck.eqlsize);
#endif
			if (rwalk->flags & FCHK_MINLINKS) 
				printf(" links>%u", rwalk->rule.fcheck.minlinks);
			if (rwalk->flags & FCHK_MAXLINKS) 
				printf(" links<%u", rwalk->rule.fcheck.maxlinks);
			if (rwalk->flags & FCHK_EQLLINKS) 
				printf(" links=%u", rwalk->rule.fcheck.eqllinks);
			if (rwalk->flags & FCHK_OWNERID) 
				printf(" owner=%u", rwalk->rule.fcheck.ownerid);
			if (rwalk->flags & FCHK_OWNERSTR) 
				printf(" owner=%s", rwalk->rule.fcheck.ownerstr);
			if (rwalk->flags & FCHK_GROUPID) 
				printf(" group=%u", rwalk->rule.fcheck.groupid);
			if (rwalk->flags & FCHK_GROUPSTR) 
				printf(" group=%s", rwalk->rule.fcheck.groupstr);
			if (rwalk->flags & FCHK_CTIMEMIN) 
				printf(" ctime>%u", rwalk->rule.fcheck.minctimedif);
			if (rwalk->flags & FCHK_CTIMEMAX) 
				printf(" ctime<%u", rwalk->rule.fcheck.maxctimedif);
			if (rwalk->flags & FCHK_CTIMEEQL) 
				printf(" ctime=%u", rwalk->rule.fcheck.ctimeeql);
			if (rwalk->flags & FCHK_MTIMEMIN) 
				printf(" mtime>%u", rwalk->rule.fcheck.minmtimedif);
			if (rwalk->flags & FCHK_MTIMEMAX) 
				printf(" mtime<%u", rwalk->rule.fcheck.maxmtimedif);
			if (rwalk->flags & FCHK_MTIMEEQL) 
				printf(" mtime=%u", rwalk->rule.fcheck.mtimeeql);
			if (rwalk->flags & FCHK_ATIMEMIN) 
				printf(" atime>%u", rwalk->rule.fcheck.minatimedif);
			if (rwalk->flags & FCHK_ATIMEMAX) 
				printf(" atime<%u", rwalk->rule.fcheck.maxatimedif);
			if (rwalk->flags & FCHK_ATIMEEQL) 
				printf(" atime=%u", rwalk->rule.fcheck.atimeeql);
			if (rwalk->flags & FCHK_MD5) 
				printf(" md5=%s", rwalk->rule.fcheck.md5hash);
			if (rwalk->flags & FCHK_SHA1) 
				printf(" sha1=%s", rwalk->rule.fcheck.sha1hash);
			if (rwalk->flags & FCHK_RMD160) 
				printf(" rmd160=%s", rwalk->rule.fcheck.rmd160hash);
			break;

		  case C_DIR:
			printf("DIR %s %s", rwalk->rule.dcheck.filename->pattern, 
				colorname(rwalk->rule.dcheck.color));

			if (rwalk->flags & FCHK_MINSIZE) 
				printf(" size>%lu", rwalk->rule.dcheck.minsize);
			if (rwalk->flags & FCHK_MAXSIZE) 
				printf(" size<%lu", rwalk->rule.dcheck.maxsize);
			break;

		  case C_PORT:
			printf("PORT");
			if (rwalk->rule.port.localexp)
				printf(" local=%s", rwalk->rule.port.localexp->pattern);
			if (rwalk->rule.port.exlocalexp)
				printf(" exlocal=%s", rwalk->rule.port.exlocalexp->pattern);
			if (rwalk->rule.port.remoteexp)
				printf(" remote=%s", rwalk->rule.port.remoteexp->pattern);
			if (rwalk->rule.port.exremoteexp)
				printf(" exremote=%s", rwalk->rule.port.exremoteexp->pattern);
			if (rwalk->rule.port.stateexp)
				printf(" state=%s", rwalk->rule.port.stateexp->pattern);
			if (rwalk->rule.port.exstateexp)
				printf(" exstate=%s", rwalk->rule.port.exstateexp->pattern);
			if (rwalk->rule.port.pmin != -1)
				printf(" min=%d", rwalk->rule.port.pmin);
			if (rwalk->rule.port.pmax != -1)
				printf(" max=%d", rwalk->rule.port.pmax);
			printf(" color=%s", colorname(rwalk->rule.port.color));
			break;

		  case C_PAGING:
			printf("PAGING %d %d", rwalk->rule.paging.warnlevel, rwalk->rule.paging.paniclevel);
			break;

                  case C_SVC:
                        printf("SVC");
                        if (rwalk->rule.svc.svcexp)
                                printf(" %s", rwalk->rule.svc.svcexp->pattern);
                        if (rwalk->rule.svc.stateexp)
                                printf(" status=%s", rwalk->rule.svc.stateexp->pattern);
                        if (rwalk->rule.svc.startupexp)
                                printf(" startup=%s", rwalk->rule.svc.startupexp->pattern);
                        printf(" color=%s", colorname(rwalk->rule.svc.color));
                        break;

		  case C_MIBVAL:
			printf("MIB");
			if (rwalk->rule.mibval.mibvalexp)
				printf(" %s", rwalk->rule.mibval.mibvalexp->pattern);
			if (rwalk->rule.mibval.keyexp)
				printf(" key=%s", rwalk->rule.mibval.keyexp->pattern);
			if (rwalk->flags & MIBCHK_MINVALUE) 
				printf(" min=%ld", rwalk->rule.mibval.minval);
			if (rwalk->flags & MIBCHK_MAXVALUE) 
				printf(" max=%ld", rwalk->rule.mibval.maxval);
			if (rwalk->flags & MIBCHK_MATCH) 
				printf(" match=%s", rwalk->rule.mibval.matchexp->pattern);
			printf(" color=%s", colorname(rwalk->rule.mibval.color));
			break;

		  case C_RRDDS:
			printf("DS %s %s:%s", rwalk->rule.rrdds.column,
				rwalk->rule.rrdds.rrdkey->pattern, rwalk->rule.rrdds.rrdds);
			if (rwalk->flags & RRDDSCHK_GT) 
				printf(" >%.2f", rwalk->rule.rrdds.limitval);
			if (rwalk->flags & RRDDSCHK_GE) 
				printf(" >=%.2f", rwalk->rule.rrdds.limitval);

			if (rwalk->flags & RRDDSCHK_INTVL) {
				if (rwalk->flags & RRDDSCHK_LT) 
					printf(" <%.2f", rwalk->rule.rrdds.limitval2);
				if (rwalk->flags & RRDDSCHK_LE) 
					printf(" <=%.2f", rwalk->rule.rrdds.limitval2);
			}
			else {
				if (rwalk->flags & RRDDSCHK_LT) 
					printf(" <%.2f", rwalk->rule.rrdds.limitval);
				if (rwalk->flags & RRDDSCHK_LE) 
					printf(" <=%.2f", rwalk->rule.rrdds.limitval);
			}
			printf(" color=%s", colorname(rwalk->rule.rrdds.color));
			break;
		}

		if (rwalk->flags & CHK_TRACKIT) {
			printf(" TRACK");
			if (rwalk->rrdidstr) printf("=%s", rwalk->rrdidstr);
		}

		if (rwalk->flags & CHK_OPTIONAL) printf(" OPTIONAL");

		if (rwalk->timespec) printf(" TIME=%s", rwalk->timespec);
		if (rwalk->hostexp) printf(" HOST=%s", rwalk->hostexp->pattern);
		if (rwalk->exhostexp) printf(" EXHOST=%s", rwalk->exhostexp->pattern);
		if (rwalk->pageexp) printf(" HOST=%s", rwalk->pageexp->pattern);
		if (rwalk->expageexp) printf(" EXHOST=%s", rwalk->expageexp->pattern);
		if (rwalk->classexp) printf(" CLASS=%s", rwalk->classexp->pattern);
		if (rwalk->exclassexp) printf(" EXCLASS=%s", rwalk->exclassexp->pattern);
		if (rwalk->statustext) printf(" TEXT=%s", rwalk->statustext);
		printf(" (line: %d)\n", rwalk->cfid);
	}
}

static c_rule_t *getrule(char *hostname, char *pagename, char *classname, void *hinfo, ruletype_t ruletype)
{
	static ruleset_t *rwalk = NULL;
	char *holidayset;

	if (hostname || pagename) {
		rwalk = ruleset(hostname, pagename, classname); 
	}
	else {
		rwalk = rwalk->next;
	}

	holidayset = (hinfo ? bbh_item(hinfo, BBH_HOLIDAYS) : NULL);

	for (; (rwalk); rwalk = rwalk->next) {
		if (rwalk->rule->ruletype != ruletype) continue;
		if (rwalk->rule->timespec && !timematch(holidayset, rwalk->rule->timespec)) continue;

		/* If we get here, then we have something that matches */
		return rwalk->rule;
	}

	return NULL;
}

int get_cpu_thresholds(void *hinfo, char *classname, 
		       float *loadyellow, float *loadred, int *recentlimit, int *ancientlimit, int *maxclockdiff)
{
	int result = 0;
	char *hostname, *pagename;
	c_rule_t *rule;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_ALLPAGEPATHS);

	*loadyellow = 5.0;
	*loadred = 10.0;

	rule = getrule(hostname, pagename, classname, hinfo, C_LOAD);
	if (rule) {
		*loadyellow = rule->rule.load.warnlevel;
		*loadred    = rule->rule.load.paniclevel;
		result = rule->cfid;
	}

	*recentlimit = 3600;
	*ancientlimit = -1;

	rule = getrule(hostname, pagename, classname, hinfo, C_UPTIME);
	if (rule) {
		*recentlimit  = rule->rule.uptime.recentlimit;
		*ancientlimit = rule->rule.uptime.ancientlimit;
		result = rule->cfid;
	}

	*maxclockdiff = -1;
	rule = getrule(hostname, pagename, classname, hinfo, C_CLOCK);
	if (rule) {
		*maxclockdiff = rule->rule.clock.maxdiff;
	}

	return result;
}

int get_disk_thresholds(void *hinfo, char *classname, 
			char *fsname, 
			long *warnlevel, long *paniclevel, 
			int *abswarn, int *abspanic,
			int *ignored, char **group)
{
	char *hostname, *pagename;
	c_rule_t *rule;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_ALLPAGEPATHS);

	*warnlevel = 90;
	*paniclevel = 95;
	*abswarn = 0;
	*abspanic = 0;
	*ignored = 0;
	*group = NULL;

	rule = getrule(hostname, pagename, classname, hinfo, C_DISK);
	while (rule && !namematch(fsname, rule->rule.disk.fsexp->pattern, rule->rule.disk.fsexp->exp)) {
		rule = getrule(NULL, NULL, NULL, hinfo, C_DISK);
	}

	if (rule) {
		*warnlevel = rule->rule.disk.warnlevel;
		*abswarn = rule->rule.disk.abswarn;
		*paniclevel = rule->rule.disk.paniclevel;
		*abspanic = rule->rule.disk.abspanic;
		*ignored = rule->rule.disk.ignored;
		*group = rule->groups;
		return rule->cfid;
	}

	return 0;
}

char *check_rrdds_thresholds(char *hostname, char *classname, char *pagepaths, char *rrdkey, RbtHandle valnames, char *vals)
{
	static strbuffer_t *resbuf = NULL;
	char msgline[1024];
	c_rule_t *rule;
	char *valscopy = NULL;
	char **vallist = NULL;
	RbtIterator handle;
	rrdtplnames_t *tpl;
	double val;
	void *hinfo;

	if (!resbuf) resbuf = newstrbuffer(0);
	clearstrbuffer(resbuf);

	hinfo = hostinfo(hostname);
	rule = getrule(hostname, pagepaths, classname, hinfo, C_RRDDS);
	while (rule) {
		int rulematch = 0;

		if (!namematch(rrdkey, rule->rule.rrdds.rrdkey->pattern, rule->rule.rrdds.rrdkey->exp)) goto nextrule;

		handle = rbtFind(valnames, rule->rule.rrdds.rrdds);
		if (handle == rbtEnd(valnames)) goto nextrule;
		tpl = (rrdtplnames_t *)gettreeitem(valnames, handle);

		/* Split the value-string into individual numbers that we can index */
		if (!vallist) {
			char *p;
			int idx = 0;

			valscopy = strdup(vals);
			vallist = calloc(128, sizeof(char *));
			vallist[0] = valscopy;

			for (p = strchr(valscopy, ':'); (p); p = strchr(p+1, ':')) {
				vallist[++idx] = p+1;
				*p = '\0';
			}
		}

		if (vallist[tpl->idx] == NULL) goto nextrule;
		val = atof(vallist[tpl->idx]);

		/* Do the checks */
		if (rule->flags & RRDDSCHK_INTVL) {
			rulematch = ( ( ((rule->flags & RRDDSCHK_GT) && (val > rule->rule.rrdds.limitval))  ||
				        ((rule->flags & RRDDSCHK_GE) && (val >= rule->rule.rrdds.limitval)) ) &&
				      ( ((rule->flags & RRDDSCHK_LT) && (val < rule->rule.rrdds.limitval2))  ||
				        ((rule->flags & RRDDSCHK_LE) && (val <= rule->rule.rrdds.limitval2)) ) );

			if (!rule->statustext) {
				char fmt[100];

				strcpy(fmt, "&N=&V (");
				if (rule->flags & RRDDSCHK_GT) strcat(fmt, " > &L");
				else if (rule->flags & RRDDSCHK_GE) strcat(fmt, " >= &L");
				strcat(fmt, " and");
				if (rule->flags & RRDDSCHK_LT) strcat(fmt, " < &U)");
				else if (rule->flags & RRDDSCHK_LE) strcat(fmt, " <= &U)");

				rule->statustext = strdup(fmt);
			}
		}
		else {
			rulematch = ( ((rule->flags & RRDDSCHK_GT) && (val > rule->rule.rrdds.limitval))  ||
				      ((rule->flags & RRDDSCHK_GE) && (val >= rule->rule.rrdds.limitval)) ||
				      ((rule->flags & RRDDSCHK_LT) && (val < rule->rule.rrdds.limitval))  ||
				      ((rule->flags & RRDDSCHK_LE) && (val <= rule->rule.rrdds.limitval))   );

			if (!rule->statustext) {
				char *fmt = "";

				if      (rule->flags & RRDDSCHK_GT) fmt = "&N=&V (> &L)";
				else if (rule->flags & RRDDSCHK_GE) fmt = "&N=&V (>= &L)";
				else if (rule->flags & RRDDSCHK_LT) fmt = "&N=&V (< &L)";
				else if (rule->flags & RRDDSCHK_LE) fmt = "&N=&V (<= &L)";

				rule->statustext = strdup(fmt);
			}
		}

		if (rulematch) {
			char *bot, *marker;

			sprintf(msgline, "modify %s.%s %s rrdds ", 
				hostname, rule->rule.rrdds.column,
				colorname(rule->rule.rrdds.color));
			addtobuffer(resbuf, msgline);

			/* Format and add the status text */
			bot = rule->statustext;
			do {
				marker = strchr(bot, '&');
				if (marker) {
					*marker = '\0';
					addtobuffer(resbuf, bot);
					*marker = '&';
					switch (*(marker+1)) {
					  case 'N': addtobuffer(resbuf, rule->rule.rrdds.rrdds); 
						    bot = marker+2; 
						    break;

					  case 'V': addtobuffer(resbuf, vallist[tpl->idx]); 
						    bot = marker+2; 
						    break;

					  case 'L': sprintf(msgline, "%.2f", rule->rule.rrdds.limitval); 
						    addtobuffer(resbuf, msgline);
						    bot = marker+2;
						    break;

					  case 'U': sprintf(msgline, "%.2f", (rule->flags & RRDDSCHK_INTVL) ? rule->rule.rrdds.limitval2 : rule->rule.rrdds.limitval); 
						    addtobuffer(resbuf, msgline);
						    bot = marker+2;
						    break;

					  default:  addtobuffer(resbuf, "&"); bot = marker+1; break;
					}
				}
				else {
					addtobuffer(resbuf, bot);
					bot = NULL;
				}
			} while (bot);

			addtobuffer(resbuf, "\n\n");
		}

nextrule:
		rule = getrule(NULL, NULL, NULL, hinfo, C_RRDDS);
	}

	if (valscopy) xfree(valscopy);
	if (vallist) xfree(vallist);

	return (STRBUFLEN(resbuf) > 0) ? STRBUF(resbuf) : NULL;
}

void get_memory_thresholds(void *hinfo, char *classname,
			   int *physyellow, int *physred, int *swapyellow, int *swapred, int *actyellow, int *actred)
{
	char *hostname, *pagename;
	c_rule_t *rule;
	int gotphys = 0, gotswap = 0, gotact = 0;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_ALLPAGEPATHS);

	*physyellow = 100;
	*physred = 101;
	*swapyellow = 50;
	*swapred = 80;
	*actyellow = 90;
	*actred = 97;

	rule = getrule(hostname, pagename, classname, hinfo, C_MEM);
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
		rule = getrule(NULL, NULL, NULL, hinfo, C_MEM);
	}
}

void get_zos_memory_thresholds(void *hinfo, char *classname,
                               int *csayellow, int *csared, int *ecsayellow, int *ecsared,
                              int *sqayellow, int *sqared, int *esqayellow, int *esqared)
{
        char *hostname, *pagename;
        c_rule_t *rule;
        int gotcsa = 0, gotecsa = 0, gotsqa = 0, gotesqa = 0;

        hostname = bbh_item(hinfo, BBH_HOSTNAME);
        pagename = bbh_item(hinfo, BBH_ALLPAGEPATHS);

        *csayellow = 90;
        *csared = 95;
        *ecsayellow = 90;
        *ecsared = 95;
        *sqayellow = 90;
        *sqared = 95;
        *esqayellow = 90;
        *esqared = 95;

        rule = getrule(hostname, pagename, classname, hinfo, C_MEM);
        while (rule) {
                switch (rule->rule.zos_mem.zos_memtype) {
                  case C_MEM_CSA:
                        if (!gotcsa) {
                                *csayellow = rule->rule.zos_mem.warnlevel;
                                *csared    = rule->rule.zos_mem.paniclevel;
                                gotcsa     = 1;
                        }
                        break;
                  case C_MEM_ECSA:
                        if (!gotecsa) {
                                *ecsayellow = rule->rule.zos_mem.warnlevel;
                                *ecsared    = rule->rule.zos_mem.paniclevel;
                                gotecsa     = 1;
                        }
                        break;
                  case C_MEM_SQA:
                        if (!gotsqa) {
                                *sqayellow = rule->rule.zos_mem.warnlevel;
                                *sqared    = rule->rule.zos_mem.paniclevel;
                                gotsqa     = 1;
                        }
                        break;
                  case C_MEM_ESQA:
                        if (!gotesqa) {
                                *esqayellow = rule->rule.zos_mem.warnlevel;
                                *esqared    = rule->rule.zos_mem.paniclevel;
                                gotesqa     = 1;
                        }
                        break;
                }
                rule = getrule(NULL, NULL, NULL, hinfo, C_MEM);
        }
}

int get_paging_thresholds(void *hinfo, char *classname, int *pagingyellow, int *pagingred)
{
	int result = 0;
	char *hostname, *pagename;
	c_rule_t *rule;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_PAGEPATH);

	*pagingyellow = 5;
	*pagingred = 10;

	rule = getrule(hostname, pagename, classname, hinfo, C_PAGING);
	if (rule) {
		*pagingyellow = rule->rule.paging.warnlevel;
		*pagingred    = rule->rule.paging.paniclevel;
		result = rule->cfid;
	}

	return result;
}


int get_mibval_thresholds(void *hinfo, char *classname, 
			  char *mibname, char *keyname, char *valname,
			  long *minval, long *maxval, void **matchexp, int *color, char **group)
{
	static RbtHandle mibnametree;
	static int have_mibnametree = 0;
	char *hostname, *pagename, *mibkeyval_id;
	c_rule_t *rule;
	RbtIterator namhandle, valdefhandle;
	RbtHandle valdeftree;

	if (!have_mibnametree) {
		mibnametree = rbtNew(name_compare);
		have_mibnametree = 1;
	}

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_PAGEPATH);

	/* Any potential rules at all ? */
	rule = getrule(hostname, pagename, classname, hinfo, C_MIBVAL);
	if (!rule) return -1;

	*minval = LONG_MIN;
	*maxval = LONG_MAX;
	*matchexp = NULL;
	*color = COL_GREEN;
	*group = NULL;

	/* 
	 * Configuration rules are indexed by three items:
	 * - the MIB Name
	 * - the MIB Key
	 * - the Value Name
	 *
	 * The MIB- and Value-names are static, so we combine these into
	 * a single key which is referenced directly in the configuration.
	 * This is the pattern listed as the first MIB criteria in the config,
	 * stored in the "mibvalexp" field.
	 * For the MIB Keys we want to use a regex (so the config can refer to
	 * all "eth.*" interfaces), so when searching for a rule we must walk
	 * the list of potential rules for this MIB+Value name, and match the
	 * actual key value against the pattern.
	 *
	 * So all in all rules are keyed with the MIB+Key+Name strings as
	 * a unique key. Hence, to speed things up we gradually build a tree
	 * with this key, which points directly to the rule for this item.
	 * This is the "valdeftree" tree.
	 *
	 * TODO: A further optimization would be to somehow keep
	 * track of how many single MIB variables have a configuration rule,
	 * to avoid scanning for a configuration for variables when all config
	 * items have been used in a message. I cannot tell right away if this
	 * is possible - perhaps by counting the number of configuration cache
	 * hits while processing a message, and for the next message from the 
	 * same source then only process data until this count has been done?
	 *
	 * Finally, for optimising memory usage, the MIB+Key+Name strings
	 * are not duplicated for each key; instead we have a separate token-
	 * tree (mibnametree) which holds these.
	 */

	/* Setup the key and find/insert it into the mibnametree */
	mibkeyval_id = (char *)malloc(strlen(mibname) + (keyname ? strlen(keyname) : 0) + strlen(valname) + 3);
	sprintf(mibkeyval_id, "%s!%s!%s", mibname, (keyname ? keyname : ""), valname);
	namhandle = rbtFind(mibnametree, mibkeyval_id);
	if (namhandle == rbtEnd(mibnametree)) {
		rbtInsert(mibnametree, mibkeyval_id, mibkeyval_id);
	}
	else {
		xfree(mibkeyval_id); /* Discard our copy - we now use the tree value */
		mibkeyval_id = (char *)gettreeitem(mibnametree, namhandle);
	}

	/* Create the rule tree (if it does not exist); look up the ruleset */
	if (!rule->rule.mibval.havetree) {
		rule->rule.mibval.havetree = 1;
		valdeftree = rule->rule.mibval.valdeftree = rbtNew(name_compare);
		valdefhandle = rbtEnd(rule->rule.mibval.valdeftree);
	}
	else {
		valdeftree = rule->rule.mibval.valdeftree;
		valdefhandle = rbtFind(valdeftree, mibkeyval_id);
	}

	if (valdefhandle == rbtEnd(valdeftree)) {
		/* 
		 * Ruleset not in the tree. 
		 * Scan the configuration set for a rule matching this 
		 * MIB+Value name, and where the keyname matches.
		 * Then insert the result into the tree (even if there is no
		 * rule matching at all - we also cache the negative lookups!
		 */
		int found = 0;
		char *mibval_id = (char *)malloc(strlen(mibname) + strlen(valname) + 2);
		sprintf(mibval_id, "%s:%s", mibname, valname);

		while (rule && !found) {
			found = namematch(mibval_id, rule->rule.mibval.mibvalexp->pattern, rule->rule.mibval.mibvalexp->exp);
			if (found && keyname && rule->rule.mibval.keyexp)
				found = namematch(keyname, rule->rule.mibval.keyexp->pattern, rule->rule.mibval.keyexp->exp);
			if (!found) rule = getrule(NULL, NULL, NULL, hinfo, C_MIBVAL);
		}

		rbtInsert(valdeftree, mibkeyval_id, rule);

		xfree(mibval_id);
	}
	else {
		/* Found the rule */
		rule = (c_rule_t *)gettreeitem(valdeftree, valdefhandle);
	}

	if (rule) {
		*color = rule->rule.mibval.color;
		*group = rule->groups;
		if (rule->flags & MIBCHK_MINVALUE) *minval = rule->rule.mibval.minval;
		if (rule->flags & MIBCHK_MAXVALUE) *maxval = rule->rule.mibval.maxval;
		if (rule->flags & MIBCHK_MATCH) *matchexp = rule->rule.mibval.matchexp;
	}

	return (rule ? rule->cfid : 0);
}


int check_mibvals(void *hinfo, char *classname, 
		  char *mibname, char *keyname, char *mibdata,
		  strbuffer_t *summarybuf, int *anyrules)
{
	char *bol, *eoln, *dnam, *dval, *delimp, delim;
	long minval, maxval, actval;
	void *matchexp;
	int rulecolor, color = COL_GREEN;
	char msgline[MAX_LINE_LEN];
	char *group;

	/* 
	 * Scan a single section of MIB data - without the [key] line - 
	 * and check all values against the configured limits.
	 */
	*anyrules = 1;
	bol = mibdata;
	while (bol && *anyrules) {
		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
		dnam = bol + strspn(bol, " \t");
		delimp = dnam + strcspn(dnam, " ="); delim = *delimp; *delimp = '\0';
		dval = delimp + 1; dval += strspn(dval, " ="); actval = atol(dval);

		switch (get_mibval_thresholds(hinfo, classname, mibname, keyname, dnam, &minval, &maxval, &matchexp, &rulecolor, &group)) {
		  case -1:
			/* This means: No rules at all for this host. So just drop all further processing */
			*anyrules = 0;
			break;

		  case 0:
			/* No rules for this key/value, but there might be for others */
			break;

		  default:
			if (actval < minval) {
				if (keyname)
					sprintf(msgline, "&%s %s:%s %ld (minimum: %ld)\n",
						colorname(rulecolor), keyname, dnam, actval, minval);
				else 
					sprintf(msgline, "&%s %s %ld (minimum: %ld)\n",
						colorname(rulecolor), dnam, actval, minval);
				addtobuffer(summarybuf, msgline);
				if (rulecolor > color) color = rulecolor;
				if (group) addalertgroup(group);
			}

			if (actval > maxval) {
				if (keyname)
					sprintf(msgline, "&%s %s:%s %ld (maximum: %ld)\n",
						colorname(rulecolor), keyname, dnam, actval, maxval);
				else 
					sprintf(msgline, "&%s %s %ld (maximum: %ld)\n",
						colorname(rulecolor), dnam, actval, maxval);
				addtobuffer(summarybuf, msgline);
				if (rulecolor > color) color = rulecolor;
				if (group) addalertgroup(group);
			}

			if (matchexp && !namematch(dval, ((exprlist_t *)matchexp)->pattern, ((exprlist_t *)matchexp)->exp)) {
				if (keyname)
					sprintf(msgline, "&%s %s:%s %s (expected: %s)\n",
						colorname(rulecolor), keyname, dnam, dval, ((exprlist_t *)matchexp)->pattern);
				else 
					sprintf(msgline, "&%s %s %s (expected: %s)\n",
						colorname(rulecolor), dnam, dval, ((exprlist_t *)matchexp)->pattern);
				addtobuffer(summarybuf, msgline);
				if (rulecolor > color) color = rulecolor;
				if (group) addalertgroup(group);
			}
			break;
		}

		*delimp = delim;
		if (eoln) {
			*eoln = '\n';
			bol = eoln + 1;
		}
		else {
			bol = NULL;
		}
	}

	return color;
}


int scan_log(void *hinfo, char *classname, 
	     char *logname, char *logdata, char *section, strbuffer_t *summarybuf)
{
	int result = COL_GREEN;
	char *hostname, *pagename;
	c_rule_t *rule;
	int nofile = 0;
	char *boln, *eoln;
	char msgline[PATH_MAX];

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_ALLPAGEPATHS);
	
	nofile = (strncmp(logdata, "Cannot open logfile ", 20) == 0);

	for (rule = getrule(hostname, pagename, classname, hinfo, C_LOG); (rule); rule = getrule(NULL, NULL, NULL, hinfo, C_LOG)) {
		int anylines = 0;

		/* First, check if the filename matches */
		if (!namematch(logname, rule->rule.log.logfile->pattern, rule->rule.log.logfile->exp)) continue;

		if (nofile) {
			if (!(rule->flags & CHK_OPTIONAL)) {
				if (COL_YELLOW > result) result = COL_YELLOW;
				addalertgroup(rule->groups);
				addtobuffer(summarybuf, "&yellow Logfile not accessible \n");
			}

			continue;
		}

		/* Next, check for a match anywhere in the data*/
		if (!patternmatch(logdata, rule->rule.log.matchexp->pattern, rule->rule.log.matchexp->exp)) continue;

		/* Some data in there matches what we want. Look at each line. */
		boln = logdata;
		while (boln) {
			eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';
			if (patternmatch(boln, rule->rule.log.matchone->pattern, rule->rule.log.matchone->exp)) {
				/* It matches. But maybe we'll ignore it ? */
				if (!(rule->rule.log.ignoreexp && patternmatch(boln, rule->rule.log.ignoreexp->pattern, rule->rule.log.ignoreexp->exp))) {
					/* We wants it ... */
					anylines++;
					sprintf(msgline, "&%s ", colorname(rule->rule.log.color));
					addtobuffer(summarybuf, msgline);
					addtobuffer(summarybuf, boln);
					addtobuffer(summarybuf, "\n");
				}
			}

			if (eoln) {
				*eoln = '\n';
				boln = eoln+1;
			}
			else boln = NULL;
		}

		/* We have a match */
		if (anylines) {
			dbgprintf("Log rule at line %d matched\n", rule->cfid);
			if (rule->rule.log.color != COL_GREEN) addalertgroup(rule->groups);
			if (rule->rule.log.color > result) result = rule->rule.log.color;
		}
	}

	return result;
}

int check_file(void *hinfo, char *classname, 
	       char *filename, char *filedata, char *section, 
	       strbuffer_t *summarybuf, off_t *filesize, 
	       char **id, int *trackit, int *anyrules)
{
	int result = COL_GREEN;
	char *hostname, *pagename;
	c_rule_t *rwalk;
	char *boln, *eoln;
	char msgline[PATH_MAX];

	int exists = 1, ftype = 0, islink = 0;
	off_t fsize = 0;
	unsigned int fmode = 0, linkcount = 0;
	int ownerid = -1, groupid = -1;
	char *ownerstr = NULL, *groupstr = NULL;
	unsigned int ctime = 0, mtime = 0, atime = 0, clock = 0;
	unsigned int ctimedif, mtimedif, atimedif;
	char *md5hash = NULL, *sha1hash = NULL, *rmd160hash = NULL;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_ALLPAGEPATHS);
	*trackit = *anyrules = 0;

	boln = filedata;
	while (boln && *boln) {
		eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';

		if (strncmp(boln, "ERROR:", 6) == 0) {
			exists = 0;
		}
		else if (strncmp(boln, "type:", 5) == 0) {
			char *tstr;

			tstr = strchr(boln, '(');
			if (tstr) {
				if (strncmp(tstr, "(file", 5) == 0) ftype = S_IFREG;
				else if (strncmp(tstr, "(directory", 10) == 0) ftype = S_IFDIR;
				else if (strncmp(tstr, "(char-device", 12) == 0) ftype = S_IFCHR;
				else if (strncmp(tstr, "(block-device", 13) == 0) ftype = S_IFBLK;
				else if (strncmp(tstr, "(FIFO", 5) == 0) ftype = S_IFIFO;
				else if (strncmp(tstr, "(socket", 7) == 0) ftype = S_IFSOCK;
				else if (strstr(tstr, ", symlink -> ") == 0) islink = 1;
			}
		}
		else if (strncmp(boln, "mode:", 5) == 0) {
			fmode = strtol(boln+5, NULL, 8);
		}
		else if (strncmp(boln, "linkcount:", 10) == 0) {
			linkcount = atoi(boln+6);
		}
		else if (strncmp(boln, "owner:", 6) == 0) {
			ownerid = atoi(boln+6);
			ownerstr = strchr(boln, '('); 
			if (ownerstr) {
				char *estr;
				ownerstr++;
				estr = strchr(ownerstr, ')'); if (estr) *estr = '\0';
			}
		}
		else if (strncmp(boln, "group:", 6) == 0) {
			groupid = atoi(boln+6);
			groupstr = strchr(boln, '('); 
			if (groupstr) {
				char *estr;
				groupstr++;
				estr = strchr(groupstr, ')'); if (estr) *estr = '\0';
			}
		}
		else if (strncmp(boln, "size:", 5) == 0) {
			fsize = filesize_value(boln+5);
		}
		else if (strncmp(boln, "clock:", 6) == 0) {
			clock = atoi(boln+6);
		}
		else if (strncmp(boln, "atime:", 6) == 0) {
			atime = atoi(boln+6);
		}
		else if (strncmp(boln, "ctime:", 6) == 0) {
			ctime = atoi(boln+6);
		}
		else if (strncmp(boln, "mtime:", 6) == 0) {
			mtime = atoi(boln+6);
		}
		else if (strncmp(boln, "md5:", 4) == 0) {
			md5hash = boln+4;
		}
		else if (strncmp(boln, "sha1:", 5) == 0) {
			sha1hash = boln+5;
		}
		else if (strncmp(boln, "rmd160:", 7) == 0) {
			rmd160hash = boln+7;
		}

		if (eoln) { boln = eoln+1; } else boln = NULL;
	}

	*filesize = fsize;

	if (clock == 0) clock = getcurrenttime(NULL);
	ctimedif = clock - ctime;
	atimedif = clock - atime;
	mtimedif = clock - mtime;

	for (rwalk = getrule(hostname, pagename, classname, hinfo, C_FILE); (rwalk); rwalk = getrule(NULL, NULL, NULL, hinfo, C_FILE)) {
		int rulecolor = COL_GREEN;

		/* First, check if the filename matches */
		if (!namematch(filename, rwalk->rule.fcheck.filename->pattern, rwalk->rule.fcheck.filename->exp)) continue;

		*anyrules = 1;
		if (!exists) {
			if (rwalk->flags & CHK_OPTIONAL) goto nextcheck;

			if (!(rwalk->flags & FCHK_NOEXIST)) {
				/* Required file does not exist */
				rulecolor = rwalk->rule.fcheck.color;
				addtobuffer(summarybuf, "File is missing\n");
			}
			goto nextcheck;
		}

		if (rwalk->flags & FCHK_NOEXIST) {
			/* File exists, but it shouldn't */
			rulecolor = rwalk->rule.fcheck.color;
			addtobuffer(summarybuf, "File exists\n");
			goto nextcheck;
		}

		if (rwalk->flags & FCHK_TYPE) {
			if ( ((rwalk->rule.fcheck.ftype == S_IFLNK) && !islink) || (rwalk->rule.fcheck.ftype != ftype) ) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File is a %s - should be %s\n", 
					ftypestr(ftype), ftypestr(rwalk->rule.fcheck.ftype));
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_MODE) {
			if (rwalk->rule.fcheck.fmode != fmode) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File is mode %03o - should be %03o\n", 
					fmode, rwalk->rule.fcheck.fmode);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_MINSIZE) {
			if (fsize < rwalk->rule.fcheck.minsize) {
				rulecolor = rwalk->rule.fcheck.color;
#ifdef _LARGEFILE_SOURCE
				sprintf(msgline, "File has size %lld  - should be >%lld\n", 
					fsize, rwalk->rule.fcheck.minsize);
#else
				sprintf(msgline, "File has size %ld  - should be >%ld\n", 
					fsize, rwalk->rule.fcheck.minsize);
#endif
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_MAXSIZE) {
			if (fsize > rwalk->rule.fcheck.maxsize) {
				rulecolor = rwalk->rule.fcheck.color;
#ifdef _LARGEFILE_SOURCE
				sprintf(msgline, "File has size %lld  - should be <%lld\n", 
					fsize, rwalk->rule.fcheck.maxsize);
#else
				sprintf(msgline, "File has size %ld  - should be <%ld\n", 
					fsize, rwalk->rule.fcheck.maxsize);
#endif
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_EQLSIZE) {
			if (fsize != rwalk->rule.fcheck.eqlsize) {
				rulecolor = rwalk->rule.fcheck.color;
#ifdef _LARGEFILE_SOURCE
				sprintf(msgline, "File has size %lld  - should be %lld\n", 
					fsize, rwalk->rule.fcheck.eqlsize);
#else
				sprintf(msgline, "File has size %ld  - should be %ld\n", 
					fsize, rwalk->rule.fcheck.eqlsize);
#endif
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_MINLINKS) {
			if (linkcount < rwalk->rule.fcheck.minlinks) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File has linkcount %u  - should be >%u\n", 
					linkcount, rwalk->rule.fcheck.minlinks);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_MAXLINKS) {
			if (linkcount > rwalk->rule.fcheck.maxlinks) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File has linkcount %u  - should be <%u\n", 
					linkcount, rwalk->rule.fcheck.maxlinks);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_OWNERID) {
			if (ownerid != rwalk->rule.fcheck.ownerid) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File is owned by user %u  - should be %u\n", 
					ownerid, rwalk->rule.fcheck.ownerid);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_OWNERSTR) {
			if (!ownerstr) ownerstr = "(No owner data)";
			if (strcmp(ownerstr, rwalk->rule.fcheck.ownerstr) != 0) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File is owned by user %s  - should be %s\n", 
					ownerstr, rwalk->rule.fcheck.ownerstr);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_GROUPID) {
			if (groupid != rwalk->rule.fcheck.groupid) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File is owned by group %u  - should be %u\n", 
					groupid, rwalk->rule.fcheck.groupid);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_GROUPSTR) {
			if (!groupstr) groupstr = "(No group data)";
			if (strcmp(groupstr, rwalk->rule.fcheck.groupstr) != 0) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File is owned by group %s  - should be %s\n", 
					groupstr, rwalk->rule.fcheck.groupstr);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_CTIMEMIN) {
			if (ctimedif < rwalk->rule.fcheck.minctimedif) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File status changed %u seconds ago - should be >%u\n", 
					ctimedif, rwalk->rule.fcheck.minctimedif);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_CTIMEMAX) {
			if (ctimedif > rwalk->rule.fcheck.maxctimedif) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File status changed %u seconds ago - should be <%u\n", 
					ctimedif, rwalk->rule.fcheck.maxctimedif);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_MTIMEMIN) {
			if (mtimedif < rwalk->rule.fcheck.minmtimedif) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File was modified %u seconds ago - should be >%u\n", 
					mtimedif, rwalk->rule.fcheck.minmtimedif);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_MTIMEMAX) {
			if (mtimedif > rwalk->rule.fcheck.maxmtimedif) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File was modified %u seconds ago - should be <%u\n", 
					mtimedif, rwalk->rule.fcheck.maxmtimedif);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_ATIMEMIN) {
			if (atimedif < rwalk->rule.fcheck.minatimedif) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File was accessed %u seconds ago - should be >%u\n", 
					atimedif, rwalk->rule.fcheck.minatimedif);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_ATIMEMAX) {
			if (atimedif > rwalk->rule.fcheck.maxatimedif) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File was accessed %u seconds ago - should be <%u\n", 
					atimedif, rwalk->rule.fcheck.maxatimedif);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_MD5) {
			if (!md5hash) md5hash = "(No MD5 data)";
			if (strcmp(md5hash, rwalk->rule.fcheck.md5hash) != 0) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File has MD5 hash %s  - should be %s\n", 
					md5hash, rwalk->rule.fcheck.md5hash);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_SHA1) {
			if (!sha1hash) sha1hash = "(No SHA1 data)";
			if (strcmp(sha1hash, rwalk->rule.fcheck.sha1hash) != 0) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File has SHA1 hash %s  - should be %s\n", 
					sha1hash, rwalk->rule.fcheck.sha1hash);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & FCHK_RMD160) {
			if (!rmd160hash) rmd160hash = "(No RMD160 data)";
			if (strcmp(rmd160hash, rwalk->rule.fcheck.rmd160hash) != 0) {
				rulecolor = rwalk->rule.fcheck.color;
				sprintf(msgline, "File has RMD160 hash %s  - should be %s\n", 
					rmd160hash, rwalk->rule.fcheck.rmd160hash);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & CHK_TRACKIT) {
			*trackit = (trackit || (ftype == S_IFREG));
			*id = rwalk->rrdidstr;
		}

nextcheck:
		if (rulecolor != COL_GREEN) addalertgroup(rwalk->groups);
		if (rulecolor > result) result = rulecolor;
	}

	return result;
}

int check_dir(void *hinfo, char *classname, 
	      char *filename, char *filedata, char *section, 
	      strbuffer_t *summarybuf, unsigned long *dirsize, 
	      char **id, int *trackit)
{
	int result = COL_GREEN;
	char *hostname, *pagename;
	c_rule_t *rwalk;
	char *boln, *eoln;
	char msgline[PATH_MAX];

	unsigned long dsize = 0;

	hostname = bbh_item(hinfo, BBH_HOSTNAME);
	pagename = bbh_item(hinfo, BBH_ALLPAGEPATHS);
	*trackit = 0;

	boln = filedata;
	while (boln && *boln) {
		unsigned long sz;
		char *p;

		eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';

		/*
		 * We need to check the directory name on each line, to
		 * find the line that gives us the exact directory we want.
		 * NB: Assumes the output is in the form
		 *    12345   /foo/bar/baz
		 */
		sz = atol(boln);
		p = boln + strcspn(boln, " \t");
		if (isspace((int)*p)) p += strspn(p, " \t");
		if (strcmp(p, filename) == 0) dsize = sz;

		if (eoln) { *eoln = '\0'; boln = eoln+1; } else boln = NULL;
	}

	*dirsize = dsize;

	/* Got the data? */
	if (dsize == 0) {
		sprintf(msgline, "Could not determine size of directory %s\n", filename);
		addtobuffer(summarybuf, msgline);
		return COL_YELLOW;
	}

	for (rwalk = getrule(hostname, pagename, classname, hinfo, C_DIR); (rwalk); rwalk = getrule(NULL, NULL, NULL, hinfo, C_DIR)) {
		int rulecolor = COL_GREEN;

		/* First, check if the filename matches */
		if (!namematch(filename, rwalk->rule.fcheck.filename->pattern, rwalk->rule.fcheck.filename->exp)) continue;

		if (rwalk->flags & FCHK_MAXSIZE) {
			if (dsize > rwalk->rule.dcheck.maxsize) {
				rulecolor = rwalk->rule.dcheck.color;
				sprintf(msgline, "Directory has size %lu  - should be <%lu\n", 
					dsize, rwalk->rule.dcheck.maxsize);
				addtobuffer(summarybuf, msgline);
			}
		}
		else if (rwalk->flags & FCHK_MINSIZE) {
			if (dsize < rwalk->rule.dcheck.minsize) {
				rulecolor = rwalk->rule.dcheck.color;
				sprintf(msgline, "Directory has size %lu  - should be >%lu\n", 
					dsize, rwalk->rule.dcheck.minsize);
				addtobuffer(summarybuf, msgline);
			}
		}
		if (rwalk->flags & CHK_TRACKIT) {
			*trackit = 1;
			*id = rwalk->rrdidstr;
		}

		if (rulecolor != COL_GREEN) addalertgroup(rwalk->groups);
		if (rulecolor > result) result = rulecolor;
	}

	return result;
}


typedef struct mon_proc_t {
	c_rule_t *rule;
	struct mon_proc_t *next;
} mon_proc_t;

static int clear_counts(void *hinfo, char *classname, ruletype_t ruletype, 
			mon_proc_t **head, mon_proc_t **tail, mon_proc_t **walk)
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
	pagename = bbh_item(hinfo, BBH_ALLPAGEPATHS);

	rule = getrule(hostname, pagename, classname, hinfo, ruletype);
	while (rule) {
		mon_proc_t *newitem = (mon_proc_t *)calloc(1, sizeof(mon_proc_t));

		newitem->rule = rule;
		newitem->next = NULL;
		if (*tail) { (*tail)->next = newitem; *tail = newitem; }
		else { *head = *tail = newitem; }

		count++;
		switch (rule->ruletype) {
		  case C_DISK : rule->rule.disk.dcount = 0; break;
		  case C_PROC : rule->rule.proc.pcount = 0; break;
		  case C_PORT : rule->rule.port.pcount = 0; break;
                  case C_SVC : rule->rule.svc.scount = 0; break;
		  default: break;
		}

		rule = getrule(NULL, NULL, NULL, hinfo, ruletype);
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

static int check_expr_match(char *s, exprlist_t *inclexp, exprlist_t *exclexp)
{
	int inclmatch = 0;
	int exclmatch = 0;

	if (inclexp) {
		if (namematch(s, inclexp->pattern, inclexp->exp)) inclmatch = 1;
	}
	else inclmatch = 1;

	/* If rejected by include spec, no need to check excludes */
	if (inclmatch == 0) return 0;

	if (exclexp) {
		if (namematch(s, exclexp->pattern, exclexp->exp)) exclmatch = 1;
	}

	/* If the exclude matched, then the whole thing does not match */
	if (exclmatch) return 0;

	/* Include- and exclude-patterns match OK, we have a match */
	return 1;
}

static void add_count3(char *pname0, char *pname1, char *pname2 , mon_proc_t *head)
{
	mon_proc_t *pwalk;
	int mymatch;
	
	if (!pname0) return;
	if (!pname1) return;
	if (!pname2) return;

	for (pwalk = head; (pwalk); pwalk = pwalk->next) {
		switch (pwalk->rule->ruletype) {
		  case C_PORT:
		        mymatch = 0;

			if (check_expr_match(pname0, pwalk->rule->rule.port.localexp, pwalk->rule->rule.port.exlocalexp)) mymatch++;
			if (check_expr_match(pname1, pwalk->rule->rule.port.remoteexp, pwalk->rule->rule.port.exremoteexp)) mymatch++;
			if (check_expr_match(pname2, pwalk->rule->rule.port.stateexp, pwalk->rule->rule.port.exstateexp)) mymatch++;

			if (mymatch == 3) {pwalk->rule->rule.port.pcount++;}
			break;

                  case C_SVC: 
                        mymatch = 0;

                        if (check_expr_match(pname0, pwalk->rule->rule.svc.svcexp, NULL)) {
				mymatch++;
				/* Match service name backup is status */
				pwalk->rule->rule.svc.startup = strdup(pname1);
				pwalk->rule->rule.svc.state = strdup(pname2);
				/* Since we match service name we can check startup ans status */
				if (check_expr_match(pname1, pwalk->rule->rule.svc.startupexp, NULL)) mymatch++;
				if (check_expr_match(pname2, pwalk->rule->rule.svc.stateexp, NULL)) mymatch++;
			}
                        if (mymatch == 3) {pwalk->rule->rule.svc.scount++;}
                        break;

		  default:
			break;
		}
	}
}

static char *check_count(int *count, ruletype_t ruletype, int *lowlim, int *uplim, int *color, mon_proc_t **walk, char **id, int *trackit,
		char **group)
{
	char *result = NULL;

	if (*walk == NULL) return NULL;

	switch (ruletype) {
	  case C_PROC:
		result = (*walk)->rule->statustext;
		if (!result) result = (*walk)->rule->rule.proc.procexp->pattern;
		*count = (*walk)->rule->rule.proc.pcount;
		*lowlim = (*walk)->rule->rule.proc.pmin;
		*uplim = (*walk)->rule->rule.proc.pmax;
		*color = COL_GREEN;
		if ((*lowlim !=  0) && (*count < *lowlim)) *color = (*walk)->rule->rule.proc.color;
		if ((*uplim  != -1) && (*count > *uplim)) *color = (*walk)->rule->rule.proc.color;
		*trackit = ((*walk)->rule->flags & CHK_TRACKIT);
		*id = (*walk)->rule->rrdidstr;
		if (group) *group = (*walk)->rule->groups;
		break;

	  case C_DISK:
		result = (*walk)->rule->rule.disk.fsexp->pattern;
		*count = (*walk)->rule->rule.disk.dcount;
		*lowlim = (*walk)->rule->rule.disk.dmin;
		*uplim = (*walk)->rule->rule.disk.dmax;
		*color = COL_GREEN;
		if ((*lowlim !=  0) && (*count < *lowlim)) *color = (*walk)->rule->rule.disk.color;
		if ((*uplim  != -1) && (*count > *uplim)) *color = (*walk)->rule->rule.disk.color;
		if (group) *group = (*walk)->rule->groups;
		break;

	  case C_PORT:
		result = (*walk)->rule->statustext;
		if (!result) {
			int sz = 0;
			char *p;

			if ((*walk)->rule->rule.port.localexp)
				sz += strlen((*walk)->rule->rule.port.localexp->pattern) + 10;
			if ((*walk)->rule->rule.port.exlocalexp)
				sz += strlen((*walk)->rule->rule.port.exlocalexp->pattern) + 10;
			if ((*walk)->rule->rule.port.remoteexp)
				sz += strlen((*walk)->rule->rule.port.remoteexp->pattern) + 10;
			if ((*walk)->rule->rule.port.exremoteexp)
				sz += strlen((*walk)->rule->rule.port.exremoteexp->pattern) + 10;
			if ((*walk)->rule->rule.port.stateexp)
				sz += strlen((*walk)->rule->rule.port.stateexp->pattern) + 10;
			if ((*walk)->rule->rule.port.exstateexp)
				sz += strlen((*walk)->rule->rule.port.exstateexp->pattern) + 10;

			(*walk)->rule->statustext = (char *)malloc(sz + 10);
			p = (*walk)->rule->statustext;
			if ((*walk)->rule->rule.port.localexp)
				p += sprintf(p, "local=%s ", (*walk)->rule->rule.port.localexp->pattern);
			if ((*walk)->rule->rule.port.exlocalexp)
				p += sprintf(p, "exlocal=%s ", (*walk)->rule->rule.port.exlocalexp->pattern);
			if ((*walk)->rule->rule.port.remoteexp)
				p += sprintf(p, "remote=%s ", (*walk)->rule->rule.port.remoteexp->pattern);
			if ((*walk)->rule->rule.port.exremoteexp)
				p += sprintf(p, "exremote=%s ", (*walk)->rule->rule.port.exremoteexp->pattern);
			if ((*walk)->rule->rule.port.stateexp)
				p += sprintf(p, "state=%s ", (*walk)->rule->rule.port.stateexp->pattern);
			if ((*walk)->rule->rule.port.exstateexp)
				p += sprintf(p, "exstate=%s ", (*walk)->rule->rule.port.exstateexp->pattern);
			*p = '\0';
			strcat((*walk)->rule->statustext, ":");

			result = (*walk)->rule->statustext;
		}
		*count = (*walk)->rule->rule.port.pcount;
		*lowlim = (*walk)->rule->rule.port.pmin;
		*uplim = (*walk)->rule->rule.port.pmax;
		*color = COL_GREEN;
		if ((*lowlim !=  0) && (*count < *lowlim)) *color = (*walk)->rule->rule.port.color;
		if ((*uplim  != -1) && (*count > *uplim)) *color = (*walk)->rule->rule.port.color;
		*trackit = ((*walk)->rule->flags & CHK_TRACKIT);
		*id = (*walk)->rule->rrdidstr;
		if (group) *group = (*walk)->rule->groups;
		break;

          case C_SVC:
 		result = (*walk)->rule->statustext;
                if (!result) { 
			int sz = 0;
			char *p;

			if ((*walk)->rule->rule.svc.svcexp->pattern)
				sz = strlen((*walk)->rule->rule.svc.svcexp->pattern);
			/* Current state */
			if ((*walk)->rule->rule.svc.startup)
				sz += strlen((*walk)->rule->rule.svc.startup);
			else
				sz += strlen("Not Found");
			if ((*walk)->rule->rule.svc.state)
				sz += strlen((*walk)->rule->rule.svc.state);
			/* Rule state */
			if ((*walk)->rule->rule.svc.startupexp->pattern)
				sz += strlen((*walk)->rule->rule.svc.startupexp->pattern);
                        if ((*walk)->rule->rule.svc.stateexp->pattern)
                                sz += strlen((*walk)->rule->rule.svc.stateexp->pattern);

			(*walk)->rule->statustext = (char *)malloc(sz + 12);
			p = (*walk)->rule->statustext;
			if ((*walk)->rule->rule.svc.svcexp->pattern)
				p += sprintf(p, "%s is", (*walk)->rule->rule.svc.svcexp->pattern);
			if ((*walk)->rule->rule.svc.startup)
				p += sprintf(p, " %s", (*walk)->rule->rule.svc.startup);
			else
				p += sprintf(p, " %s", "Not Found");
			if ((*walk)->rule->rule.svc.state)
				p += sprintf(p, " %s",	(*walk)->rule->rule.svc.state);
                        if ((*walk)->rule->rule.svc.startupexp->pattern)
                                 p += sprintf(p, " req %s", (*walk)->rule->rule.svc.startupexp->pattern);
			if ((*walk)->rule->rule.svc.stateexp->pattern)
				p += sprintf(p, " %s", (*walk)->rule->rule.svc.stateexp->pattern);
			*p = '\0';

			result = (*walk)->rule->statustext;
			/* We free the extra buffer */
			if ((*walk)->rule->rule.svc.state)
				xfree((*walk)->rule->rule.svc.state);
			if ((*walk)->rule->rule.svc.startup)
				xfree((*walk)->rule->rule.svc.startup);
		}
                *count = (*walk)->rule->rule.svc.scount;
		*color = COL_GREEN;
		if (*count == 0) *color = (*walk)->rule->rule.svc.color;
                if (group) *group = (*walk)->rule->groups;
                break;

	  default: break;
	}

	*walk = (*walk)->next;

	return result;
}

static mon_proc_t *phead = NULL, *ptail = NULL, *pmonwalk = NULL;
static mon_proc_t *dhead = NULL, *dtail = NULL, *dmonwalk = NULL;
static mon_proc_t *porthead = NULL, *porttail = NULL, *portmonwalk = NULL;
static mon_proc_t *svchead = NULL, *svctail = NULL, *svcmonwalk = NULL;

int clear_process_counts(void *hinfo, char *classname)
{
	return clear_counts(hinfo, classname, C_PROC, &phead, &ptail, &pmonwalk);
}

int clear_disk_counts(void *hinfo, char *classname)
{
	return clear_counts(hinfo, classname, C_DISK, &dhead, &dtail, &dmonwalk);
}

int clear_port_counts(void *hinfo, char *classname)
{
	return clear_counts(hinfo, classname, C_PORT, &porthead, &porttail, &portmonwalk);
}

int clear_svc_counts(void *hinfo, char *classname)
{
        return clear_counts(hinfo, classname, C_SVC, &svchead, &svctail, &svcmonwalk);
}

void add_process_count(char *pname)
{
	add_count(pname, phead);
}

void add_disk_count(char *dname)
{
	add_count(dname, dhead);
}

void add_port_count(char *localstr, char *foreignstr, char *stname)
{
	add_count3(localstr, foreignstr, stname, porthead);
}

void add_svc_count(char *localstr, char *foreignstr, char *stname)
{
        add_count3(localstr, foreignstr, stname, svchead);
}

char *check_process_count(int *count, int *lowlim, int *uplim, int *color, char **id, int *trackit, char **group)
{
	return check_count(count, C_PROC, lowlim, uplim, color, &pmonwalk, id, trackit, group);
}

char *check_disk_count(int *count, int *lowlim, int *uplim, int *color, char **group)
{
	return check_count(count, C_DISK, lowlim, uplim, color, &dmonwalk, NULL, NULL, group);
}

char *check_port_count(int *count, int *lowlim, int *uplim, int *color, char **id, int *trackit, char **group)
{
	return check_count(count, C_PORT, lowlim, uplim, color, &portmonwalk, id, trackit, group);
}

char *check_svc_count(int *count, int *color, char **group)
{
        return check_count(count, C_SVC, NULL, NULL, color, &svcmonwalk, NULL, NULL, group);
}

