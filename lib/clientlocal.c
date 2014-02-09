/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the            */
/* client-local.cfg file into memory and finding the proper host entry.       */
/*                                                                            */
/* Copyright (C) 2006-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

typedef struct clientconfig_t {
	pcre *hostptn, *classptn, *osptn;
	strbuffer_t *config;
	struct clientconfig_t *next;
} clientconfig_t;
static clientconfig_t *cchead = NULL;

typedef struct cctree_t {
	char *hostname;
	char *config;
} cctree_t;
static void *cctree = NULL;

/* Feature flag: Set to 1 to merge all matching clientconfig entries into one */
static int ccmergemode = 0;


void load_clientconfig(void)
{
	static char *configfn = NULL;
	static void *clientconflist = NULL;
	FILE *fd;
	strbuffer_t *buf;
	clientconfig_t *cctail = NULL;

	if (!configfn) {
		configfn = (char *)malloc(strlen(xgetenv("XYMONHOME"))+ strlen("/etc/client-local.cfg") + 1);
		sprintf(configfn, "%s/etc/client-local.cfg", xgetenv("XYMONHOME"));
	}

	/* First check if there were no modifications at all */
	if (clientconflist) {
		if (!stackfmodified(clientconflist)){
			dbgprintf("No files modified, skipping reload of %s\n", configfn);
			return;
		}
		else {
			stackfclist(&clientconflist);
			clientconflist = NULL;
		}
	}

	/* Must reload the config, clear out old cache data */
	if (cchead) {
		clientconfig_t *walk, *zombie;

		walk = cchead;
		while (walk) {
			zombie = walk; walk = walk->next;
			if (zombie->hostptn) freeregex(zombie->hostptn);
			if (zombie->classptn) freeregex(zombie->classptn);
			if (zombie->osptn) freeregex(zombie->osptn);
			if (zombie->config) freestrbuffer(zombie->config);
			xfree(zombie);
		}
		cchead = NULL;
	}

	if (cctree) {
		xtreePos_t handle;
		cctree_t *rec;

		handle = xtreeFirst(cctree);
		while (handle != xtreeEnd(cctree)) {
			rec = xtreeData(cctree, handle);
			xfree(rec->hostname);
			xfree(rec->config);
			handle = xtreeNext(cctree, handle);
		}

		xtreeDestroy(cctree);
		cctree = NULL;
	}

	buf = newstrbuffer(0);
	fd = stackfopen(configfn, "r", &clientconflist); if (!fd) return;
	while (stackfgets(buf, NULL)) {
		static int insection = 0;
		char *p, *ptn;

		/* Ignore comments and blank lines */
		p = STRBUF(buf); p += strspn(p, " \t\r\n"); if ((*p == '#') || (*p == '\0')) continue;

		if (insection) {
			if (*p != '[') {
				if (cctail) {
					if (!cctail->config) cctail->config = newstrbuffer(0);
					addtostrbuffer(cctail->config, buf);
				}
			}
			else {
				insection = 0;
			}
		}

		if (!insection) {
			if (*STRBUF(buf) == '[') {
				pcre *hostptn = NULL, *classptn = NULL, *osptn = NULL;
				clientconfig_t *newrec;

				p = STRBUF(buf) + strcspn(STRBUF(buf), "]\r\n");
				if (*p == ']') {
					*p = '\0'; strbufferrecalc(buf);
					ptn = STRBUF(buf) + 1;
					if (strncasecmp(ptn, "host=", 5) == 0) {
						ptn += 5; if (*ptn == '%') ptn++;
						hostptn = compileregex((strcmp(ptn, "*") == 0) ? "." : ptn);
						if (!hostptn) errprintf("Invalid host pattern in client-local.cfg: %s\n", ptn);
					}
					else if (strncasecmp(ptn, "class=", 6) == 0) {
						ptn += 6; if (*ptn == '%') ptn++;
						classptn = compileregex((strcmp(ptn, "*") == 0) ? "." : ptn);
						if (!classptn) errprintf("Invalid class pattern in client-local.cfg: %s\n", ptn);
					}
					else if (strncasecmp(ptn, "os=", 3) == 0) {
						ptn += 3; if (*ptn == '%') ptn++;
						osptn = compileregex((strcmp(ptn, "*") == 0) ? "." : ptn);
						if (!osptn) errprintf("Invalid os pattern in client-local.cfg: %s\n", ptn);
					}
					else if (*(ptn + strlen(ptn) - 1) == '*') {
						/* It's a "blabla*" */
						*(ptn-1) = '^';  /* Ok, we know there is a '[' first */
						strbufferchop(buf, 1);
						hostptn = compileregex(ptn-1);
					}
					else {
						/* Old-style matching, must anchor it and match on all possible patterns */
						*(ptn-1) = '^';  /* Ok, we know there is a '[' first */
						addtobuffer(buf, "$");
						/* Compile it three times, because we free each expression when reloading the config */
						hostptn = compileregex(ptn-1);
						classptn = compileregex(ptn-1);
						osptn = compileregex(ptn-1);
					}


					if (hostptn || classptn || osptn) {
						newrec = (clientconfig_t *)calloc(1, sizeof(clientconfig_t));
						newrec->hostptn = hostptn;
						newrec->classptn = classptn;
						newrec->osptn = osptn;
						newrec->next = NULL;
						if (!cchead) {
							cchead = cctail = newrec;
						}
						else {
							cctail->next = newrec;
							cctail = newrec;
						}

						insection = 1;
					}
				}
			}
		}
	}
	stackfclose(fd);

	freestrbuffer(buf);
}

char *get_clientconfig(char *hostname, char *hostclass, char *hostos)
{
	xtreePos_t handle;
	cctree_t *rec = NULL;

	if (!cchead) return NULL;

	if (!cctree) cctree = xtreeNew(strcasecmp);

	handle = xtreeFind(cctree, hostname);
	if (handle == xtreeEnd(cctree)) {
		strbuffer_t *config = newstrbuffer(0);
		clientconfig_t *walk = cchead;

		if (!ccmergemode) {
			/* Old-style: Find the first match of hostname, classname or osname - in that priority */
			clientconfig_t *hostmatch = NULL, *classmatch = NULL, *osmatch = NULL;

			while (walk && !hostmatch) {	/* Can stop if we find a hostmatch, since those are priority 1 */
				if      (walk->hostptn && !hostmatch && matchregex(hostname, walk->hostptn))	hostmatch = walk;
				else if (walk->classptn && !classmatch && matchregex(hostclass, walk->classptn)) classmatch = walk;
				else if (walk->osptn && !osmatch && matchregex(hostos, walk->osptn)) osmatch = walk;

				walk = walk->next;
			}

			if (hostmatch && hostmatch->config) addtostrbuffer(config, hostmatch->config);
			else if (classmatch && classmatch->config) addtostrbuffer(config, classmatch->config);
			else if (osmatch && osmatch->config) addtostrbuffer(config, osmatch->config);
		}
		else {
			/* Merge mode: Merge all matching entries into one */
			while (walk) {
				if ( (walk->hostptn && matchregex(hostname, walk->hostptn))    ||
				     (walk->classptn && matchregex(hostclass, walk->classptn)) ||
				     (walk->osptn && matchregex(hostos, walk->osptn)) ) {
					if (walk->config) addtostrbuffer(config, walk->config);
				}

				walk = walk->next;
			}
		}

		rec = (cctree_t *)calloc(1, sizeof(cctree_t));
		rec->hostname = strdup(hostname);
		rec->config = grabstrbuffer(config);
		xtreeAdd(cctree, rec->hostname, rec);
	}
	else {
		rec = (cctree_t *)xtreeData(cctree, handle);
	}

	return (rec ? rec->config : NULL);
}

void set_clientlocal_mergemode(int onoff)
{
	ccmergemode = (onoff != 0);
}

