/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* This file contains code to load the page-structure from the bb-hosts file. */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: loadbbhosts.c,v 1.46 2006-07-20 16:06:41 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

#include "bbgen.h"
#include "util.h"
#include "loadbbhosts.h"

#define MAX_TARGETPAGES_PER_HOST 10

time_t	snapshot = 0;				/* Set if we are doing a snapshot */

char	*null_text = "";

/* List definition to search for page records */
typedef struct bbpagelist_t {
	struct bbgen_page_t *pageentry;
	struct bbpagelist_t *next;
} bbpagelist_t;

static bbpagelist_t *pagelisthead = NULL;
int	pagecount = 0;
int	hostcount = 0;

char    *wapcolumns = NULL;                     /* Default columns included in WAP cards */
char    *nopropyellowdefault = NULL;
char    *nopropreddefault = NULL;
char    *noproppurpledefault = NULL;
char    *nopropackdefault = NULL;

void addtopagelist(bbgen_page_t *page)
{
	bbpagelist_t *newitem;

	newitem = (bbpagelist_t *) calloc(1, sizeof(bbpagelist_t));
	newitem->pageentry = page;
	newitem->next = pagelisthead;
	pagelisthead = newitem;
}

char *build_noprop(char *defset, char *specset)
{
	static char result[MAX_LINE_LEN];
	char *set;
	char *item;
	char ibuf[MAX_LINE_LEN];
	char op;
	char *p;

	/* It's guaranteed that specset is non-NULL. defset may be NULL */
	if ((*specset != '+') && (*specset != '-')) {
		/* Old-style - specset is the full set of tests */
		sprintf(result, ",%s,", specset);
		return result;
	}

	set = strdup(specset);
	strcpy(result, ((defset != NULL) ? defset : ""));
	item = strtok(set, ",");

	while (item) {
		if ((*item == '-') || (*item == '+')) {
			op = *item;
			sprintf(ibuf, ",%s,", item+1);
		}
		else {
			op = '+';
			sprintf(ibuf, ",%s,", item);
		}

		p = strstr(result, ibuf);
		if (p && (op == '-')) {
			/* Remove this item */
			memmove(p, (p+strlen(item)), strlen(p));
		}
		else if ((p == NULL) && (op == '+')) {
			/* Add this item (it's not already included) */
			if (strlen(result) == 0) {
				sprintf(result, ",%s,", item+1);
			}
			else {
				strcat(result, item+1);
				strcat(result, ",");
			}
		}

		item = strtok(NULL, ",");
	}

	xfree(set);
	return result;	/* This may be an empty string */
}

bbgen_page_t *init_page(char *name, char *title)
{
	bbgen_page_t *newpage = (bbgen_page_t *) calloc(1, sizeof(bbgen_page_t));

	pagecount++;
	dbgprintf("init_page(%s, %s)\n", textornull(name), textornull(title));

	if (name) {
		newpage->name = strdup(name);
	}
	else name = null_text;

	if (title) {
		newpage->title = strdup(title);
	}else
		title = null_text;

	newpage->color = -1;
	newpage->oldage = 1;
	newpage->pretitle = NULL;
	newpage->groups = NULL;
	newpage->hosts = NULL;
	newpage->parent = NULL;
	newpage->subpages = NULL;
	newpage->next = NULL;

	return newpage;
}

group_t *init_group(char *title, char *onlycols, char *exceptcols)
{
	group_t *newgroup = (group_t *) calloc(1, sizeof(group_t));

	dbgprintf("init_group(%s, %s)\n", textornull(title), textornull(onlycols));

	if (title) {
		newgroup->title = strdup(title);
	}
	else title = null_text;

	if (onlycols) {
		newgroup->onlycols = (char *) malloc(strlen(onlycols)+3); /* Add a '|' at start and end */
		sprintf(newgroup->onlycols, "|%s|", onlycols);
	}
	else newgroup->onlycols = NULL;
	if (exceptcols) {
		newgroup->exceptcols = (char *) malloc(strlen(exceptcols)+3); /* Add a '|' at start and end */
		sprintf(newgroup->exceptcols, "|%s|", exceptcols);
	}
	else newgroup->exceptcols = NULL;
	newgroup->pretitle = NULL;
	newgroup->hosts = NULL;
	newgroup->next = NULL;
	return newgroup;
}

host_t *init_host(char *hostname, int issummary,
		  char *displayname, char *clientalias,
		  char *comment, char *description,
		  int ip1, int ip2, int ip3, int ip4, 
		  int dialup, double warnpct, char *reporttime,
		  char *alerts, int nktime, char *waps,
		  char *nopropyellowtests, char *nopropredtests, char *noproppurpletests, char *nopropacktests)
{
	host_t 		*newhost = (host_t *) calloc(1, sizeof(host_t));
	hostlist_t	*oldlist;

	hostcount++;
	dbgprintf("init_host(%s)\n", textornull(hostname));

	newhost->hostname = newhost->displayname = strdup(hostname);
	if (displayname) newhost->displayname = strdup(displayname);
	newhost->clientalias = (clientalias ? strdup(clientalias) : NULL);
	newhost->comment = (comment ? strdup(comment) : NULL);
	newhost->description = (description ? strdup(description) : NULL);
	sprintf(newhost->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
	newhost->pretitle = NULL;
	newhost->entries = NULL;
	newhost->color = -1;
	newhost->oldage = 1;
	newhost->dialup = dialup;
	newhost->reportwarnlevel = warnpct;
	newhost->reporttime = (reporttime ? strdup(reporttime) : NULL);
	if (alerts && nktime) {
		newhost->alerts = strdup(alerts);
	}
	else {
		newhost->alerts = NULL;
	}
	newhost->anywaps = 0;
	newhost->wapcolor = -1;

	/* Wap set is :
	 * - Specific WML: tag
	 * - NK: tag
	 * - --wap=COLUMN cmdline option
	 * - NULL
	 */
	if (waps || alerts) {
		newhost->waps = strdup(waps ? waps : alerts);
	}
	else {
		newhost->waps = wapcolumns;
	}

	if (nopropyellowtests) {
		char *p;
		p = skipword(nopropyellowtests); if (*p) *p = '\0'; else p = NULL;
		newhost->nopropyellowtests = strdup(build_noprop(nopropyellowdefault, nopropyellowtests));
		if (p) *p = ' ';
	}
	else {
		newhost->nopropyellowtests = nopropyellowdefault;
	}
	if (nopropredtests) {
		char *p;
		p = skipword(nopropredtests); if (*p) *p = '\0'; else p = NULL;
		newhost->nopropredtests = strdup(build_noprop(nopropreddefault, nopropredtests));
		if (p) *p = ' ';
	}
	else {
		newhost->nopropredtests = nopropreddefault;
	}
	if (noproppurpletests) {
		char *p;
		p = skipword(noproppurpletests); if (*p) *p = '\0'; else p = NULL;
		newhost->noproppurpletests = strdup(build_noprop(noproppurpledefault, noproppurpletests));
		if (p) *p = ' ';
	}
	else {
		newhost->noproppurpletests = noproppurpledefault;
	}
	if (nopropacktests) {
		char *p;
		p = skipword(nopropacktests); if (*p) *p = '\0'; else p = NULL;
		newhost->nopropacktests = strdup(build_noprop(nopropackdefault, nopropacktests));
		if (p) *p = ' ';
	}
	else {
		newhost->nopropacktests = nopropackdefault;
	}

	newhost->parent = NULL;
	newhost->nobb2 = 0;
	newhost->next = NULL;

	/* Summary hosts don't go into the host list */
	if (issummary) return newhost;

	/*
	 * Add this host to the hostlist_t list of known hosts.
	 * HOWEVER: It might be a duplicate! In that case, we need
	 * to figure out which host record we want to use.
	 */
	oldlist = find_hostlist(hostname);
	if (oldlist == NULL) {
		hostlist_t *newlist;

		newlist = (hostlist_t *) calloc(1, sizeof(hostlist_t));
		newlist->hostentry = newhost;
		newlist->clones = NULL;
		add_to_hostlist(newlist);
	}
	else {
		hostlist_t *clone = (hostlist_t *) calloc(1, sizeof(hostlist_t));

		dbgprintf("Duplicate host definition for host '%s'\n", hostname);

		clone->hostentry = newhost;
		clone->clones = oldlist->clones;
		oldlist->clones = clone;
	}

	return newhost;
}



void getnamelink(char *l, char **name, char **link)
{
	/* "page NAME title-or-link" splitup */
	char *p;

	dbgprintf("getnamelink(%s, ...)\n", textornull(l));

	*name = null_text;
	*link = null_text;

	/* Skip page/subpage keyword, and whitespace after that */
	p = skipwhitespace(skipword(l));

	*name = p; p = skipword(p);
	if (*p) {
		*p = '\0'; /* Null-terminate pagename */
		p++;
		*link = skipwhitespace(p);
	}
}


void getparentnamelink(char *l, bbgen_page_t *toppage, bbgen_page_t **parent, char **name, char **link)
{
	/* "subparent NAME PARENTNAME title-or-link" splitup */
	char *p;
	char *parentname;
	bbpagelist_t *walk;

	dbgprintf("getnamelink(%s, ...)\n", textornull(l));

	*name = null_text;
	*link = null_text;

	/* Skip page/subpage keyword, and whitespace after that */
	parentname = p = skipwhitespace(skipword(l));
	p = skipword(p);
	if (*p) {
		*p = '\0'; /* Null-terminate pagename */
		p++;
		*name = p = skipwhitespace(p);
	 	p = skipword(p);
		if (*p) {
			*p = '\0'; /* Null-terminate parentname */
			p++;
			*link = skipwhitespace(p);
		}
	}

	for (walk = pagelisthead; (walk && (strcmp(walk->pageentry->name, parentname) != 0)); walk = walk->next) ;
	if (walk) {
		*parent = walk->pageentry;
	}
	else {
		errprintf("Cannot find parent page '%s'\n", parentname);
		*parent = NULL;
	}
}


void getgrouptitle(char *l, char *pageset, char **title, char **onlycols, char **exceptcols)
{
	char grouponlytag[100], groupexcepttag[100], grouptag[100];

	*title = null_text;
	*onlycols = NULL;
	*exceptcols = NULL;

	sprintf(grouponlytag, "%sgroup-only", pageset);
	sprintf(groupexcepttag, "%sgroup-except", pageset);
	sprintf(grouptag, "%sgroup", pageset);

	dbgprintf("getgrouptitle(%s, ...)\n", textornull(l));

	if (strncmp(l, grouponlytag, strlen(grouponlytag)) == 0) {
		char *p;

		*onlycols = skipwhitespace(skipword(l));

		p = skipword(*onlycols);
		if (*p) {
			*p = '\0'; p++;
			*title = skipwhitespace(p);
		}
	}
	else if (strncmp(l, groupexcepttag, strlen(groupexcepttag)) == 0) {
		char *p;

		*exceptcols = skipwhitespace(skipword(l));

		p = skipword(*exceptcols);
		if (*p) {
			*p = '\0'; p++;
			*title = skipwhitespace(p);
		}
	}
	else if (strncmp(l, grouptag, strlen(grouptag)) == 0) {
		*title = skipwhitespace(skipword(l));
	}
}

summary_t *init_summary(char *name, char *receiver, char *url)
{
	summary_t *newsum;

	dbgprintf("init_summary(%s, %s, %s)\n", textornull(name), textornull(receiver), textornull(url));

	/* Sanity check */
	if ((name == NULL) || (receiver == NULL) || (url == NULL)) 
		return NULL;

	newsum = (summary_t *) calloc(1, sizeof(summary_t));
	newsum->name = strdup(name);
	newsum->receiver = strdup(receiver);
	newsum->url = strdup(url);
	newsum->next = NULL;

	return newsum;
}


bbgen_page_t *load_bbhosts(char *pgset)
{
	FILE 	*bbhosts;
	strbuffer_t *inbuf;
	char	pagetag[100], subpagetag[100], subparenttag[100], 
		grouptag[100], summarytag[100], titletag[100], hosttag[100];
	char 	*name, *link, *onlycols, *exceptcols;
	char 	hostname[MAX_LINE_LEN];
	bbgen_page_t 	*toppage, *curpage, *cursubpage, *cursubparent;
	group_t *curgroup;
	host_t	*curhost;
	char	*curtitle;
	int	ip1, ip2, ip3, ip4;
	char	*p;
	int	fqdn = get_fqdn();

	load_hostnames(xgetenv("BBHOSTS"), "dispinclude", fqdn);

	dbgprintf("load_bbhosts(pgset=%s)\n", textornull(pgset));

	/*
	 * load_hostnames() picks up the hostname definitions, but not the page
	 * layout. So we will scan the file again, this time doing the layout.
	 */
	bbhosts = stackfopen(xgetenv("BBHOSTS"), "r", NULL);
	if (bbhosts == NULL) {
		errprintf("Cannot open the BBHOSTS file '%s'\n", xgetenv("BBHOSTS"));
		return NULL;
	}

	if (pgset == NULL) pgset = "";
	sprintf(pagetag, "%spage", pgset);
	sprintf(subpagetag, "%ssubpage", pgset);
	sprintf(subparenttag, "%ssubparent", pgset);
	sprintf(grouptag, "%sgroup", pgset);
	sprintf(summarytag, "%ssummary", pgset);
	sprintf(titletag, "%stitle", pgset);
	sprintf(hosttag, "%s:", pgset); for (p=hosttag; (*p); p++) *p = toupper((int)*p);

	toppage = init_page("", "");
	addtopagelist(toppage);
	curpage = NULL;
	cursubpage = NULL;
	curgroup = NULL;
	curhost = NULL;
	cursubparent = NULL;
	curtitle = NULL;

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, "dispinclude")) {
		sanitize_input(inbuf, 0, 0); if (STRBUFLEN(inbuf) == 0) continue;

		dbgprintf("load_bbhosts: -- got line '%s'\n", STRBUF(inbuf));

		if (strncmp(STRBUF(inbuf), pagetag, strlen(pagetag)) == 0) {
			getnamelink(STRBUF(inbuf), &name, &link);
			if (curpage == NULL) {
				/* First page - hook it on toppage as a subpage from there */
				curpage = toppage->subpages = init_page(name, link);
			}
			else {
				curpage = curpage->next = init_page(name, link);
			}

			curpage->parent = toppage;
			if (curtitle) { 
				curpage->pretitle = curtitle; 
				curtitle = NULL; 
			}
			cursubpage = NULL;
			cursubparent = NULL;
			curgroup = NULL;
			curhost = NULL;
			addtopagelist(curpage);
		}
		else if (strncmp(STRBUF(inbuf), subpagetag, strlen(subpagetag)) == 0) {
			if (curpage == NULL) {
				errprintf("'subpage' ignored, no preceding 'page' tag : %s\n", STRBUF(inbuf));
				continue;
			}

			getnamelink(STRBUF(inbuf), &name, &link);
			if (cursubpage == NULL) {
				cursubpage = curpage->subpages = init_page(name, link);
			}
			else {
				cursubpage = cursubpage->next = init_page(name, link);
			}
			cursubpage->parent = curpage;
			if (curtitle) { 
				cursubpage->pretitle = curtitle; 
				curtitle = NULL;
			}
			cursubparent = NULL;
			curgroup = NULL;
			curhost = NULL;
			addtopagelist(cursubpage);
		}
		else if (strncmp(STRBUF(inbuf), subparenttag, strlen(subparenttag)) == 0) {
			bbgen_page_t *parentpage, *walk;

			getparentnamelink(STRBUF(inbuf), toppage, &parentpage, &name, &link);
			if (parentpage == NULL) {
				errprintf("'subparent' ignored, unknown parent page: %s\n", STRBUF(inbuf));
				continue;
			}

			cursubparent = init_page(name, link);
			if (parentpage->subpages == NULL) {
				parentpage->subpages = cursubparent;
			} 
			else {
				for (walk = parentpage->subpages; (walk->next); (walk = walk->next)) ;
				walk->next = cursubparent;
			}
			if (curtitle) { 
				cursubparent->pretitle = curtitle; 
				curtitle = NULL;
			}
			cursubparent->parent = parentpage;
			curgroup = NULL;
			curhost = NULL;
			addtopagelist(cursubparent);
		}
		else if (strncmp(STRBUF(inbuf), grouptag, strlen(grouptag)) == 0) {
			getgrouptitle(STRBUF(inbuf), pgset, &link, &onlycols, &exceptcols);
			if (curgroup == NULL) {
				curgroup = init_group(link, onlycols, exceptcols);
				if (cursubparent != NULL) {
					cursubparent->groups = curgroup;
				}
				else if (cursubpage != NULL) {
					/* We're in a subpage */
					cursubpage->groups = curgroup;
				}
				else if (curpage != NULL) {
					/* We're on a main page */
					curpage->groups = curgroup;
				}
				else {
					/* We're on the top page */
					toppage->groups = curgroup;
				}
			}
			else {
				curgroup->next = init_group(link, onlycols, exceptcols);
				curgroup = curgroup->next;
			}
			if (curtitle) { curgroup->pretitle = curtitle; curtitle = NULL; }
			curhost = NULL;
		}
		else if (sscanf(STRBUF(inbuf), "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {
			void *bbhost = NULL;
			int dialup, nobb2, nktime = 1;
			double warnpct = reportwarnlevel;
			char *displayname, *clientalias, *comment, *description;
			char *alertlist, *onwaplist, *reporttime;
			char *nopropyellowlist, *nopropredlist, *noproppurplelist, *nopropacklist;
			char *targetpagelist[MAX_TARGETPAGES_PER_HOST];
			int targetpagecount;
			char *bbval;

			/* Check for ".default." hosts - they are ignored. */
			if (*hostname == '.') continue;

			if (!fqdn) {
				/* Strip any domain from the hostname */
				char *p = strchr(hostname, '.');
				if (p) *p = '\0';
			}

			/* Get the info */
			bbhost = hostinfo(hostname);
			if (bbhost == NULL) {
				errprintf("Confused - hostname '%s' cannot be found. Ignored\n", hostname);
				continue;
			}

			/* Check for no-display hosts - they are ignored. */
			/* But only when we're building the default pageset */
			if ((strlen(pgset) == 0) && (bbh_item(bbhost, BBH_FLAG_NODISP) != NULL)) continue;

			for (targetpagecount=0; (targetpagecount < MAX_TARGETPAGES_PER_HOST); targetpagecount++) 
				targetpagelist[targetpagecount] = NULL;
			targetpagecount = 0;

			dialup = (bbh_item(bbhost, BBH_FLAG_DIALUP) != NULL);
			nobb2 = (bbh_item(bbhost, BBH_FLAG_NOBB2) != NULL);

			alertlist = bbh_item(bbhost, BBH_NK);
			bbval = bbh_item(bbhost, BBH_NKTIME); if (bbval) nktime = within_sla(bbval, 0);

			onwaplist = bbh_item(bbhost, BBH_WML);
			nopropyellowlist = bbh_item(bbhost, BBH_NOPROPYELLOW);
			if (nopropyellowlist == NULL) nopropyellowlist = bbh_item(bbhost, BBH_NOPROP);
			nopropredlist = bbh_item(bbhost, BBH_NOPROPRED);
			noproppurplelist = bbh_item(bbhost, BBH_NOPROPPURPLE);
			nopropacklist = bbh_item(bbhost, BBH_NOPROPACK);
			displayname = bbh_item(bbhost, BBH_DISPLAYNAME);
			comment = bbh_item(bbhost, BBH_COMMENT);
			description = bbh_item(bbhost, BBH_DESCRIPTION);
			bbval = bbh_item(bbhost, BBH_WARNPCT); if (bbval) warnpct = atof(bbval);
			reporttime = bbh_item(bbhost, BBH_REPORTTIME);

			clientalias = bbh_item(bbhost, BBH_CLIENTALIAS);
			if (bbhost && (strcmp(bbh_item(bbhost, BBH_HOSTNAME), clientalias) == 0)) clientalias = NULL;

			if (bbhost && (strlen(pgset) > 0)) {
				/* Walk the clone-list and pick up the target pages for this host */
				void *cwalk = bbhost;
				do {
					bbval = bbh_item_walk(cwalk);
					while (bbval) {
						if (strncasecmp(bbval, hosttag, strlen(hosttag)) == 0)
							targetpagelist[targetpagecount++] = strdup(bbval+strlen(hosttag));
						bbval = bbh_item_walk(NULL);
					}

					cwalk = next_host(cwalk, 1);
				} while (cwalk && 
					 (strcmp(bbh_item(cwalk, BBH_HOSTNAME), bbh_item(bbhost, BBH_HOSTNAME)) == 0) &&
					 (targetpagecount < MAX_TARGETPAGES_PER_HOST) );

				/*
				 * HACK: Check if the pageset tag is present at all in the host
				 * entry. If it isn't, then drop this incarnation of the host.
				 *
				 * Without this, the following bb-hosts file will have the
				 * www.hswn.dk host listed twice on the alternate pageset:
				 *
				 * adminpage nyc NYC
				 *
				 * 127.0.0.1   localhost      # bbd http://localhost/ CLIENT:osiris
				 * 172.16.10.2 www.hswn.dk    # http://www.hswn.dk/ ADMIN:nyc ssh noinfo
				 *
				 * page superdome Superdome
				 * 172.16.10.2 www.hswn.dk # noconn
				 *
				 */
				if (strstr(STRBUF(inbuf), hosttag) == NULL) targetpagecount = 0;
			}

			if (strlen(pgset) == 0) {
				/*
				 * Default pageset generated. Put the host into
				 * whatever group or page is current.
				 */
				if (curhost == NULL) {
					curhost = init_host(hostname, 0, displayname, clientalias,
							    comment, description,
							    ip1, ip2, ip3, ip4, dialup, 
							    warnpct, reporttime,
							    alertlist, nktime, onwaplist,
							    nopropyellowlist, nopropredlist, noproppurplelist, nopropacklist);
					if (curgroup != NULL) {
						curgroup->hosts = curhost;
					}
					else if (cursubparent != NULL) {
						cursubparent->hosts = curhost;
					}
					else if (cursubpage != NULL) {
						cursubpage->hosts = curhost;
					}
					else if (curpage != NULL) {
						curpage->hosts = curhost;
					}
					else {
						toppage->hosts = curhost;
					}
				}
				else {
					curhost = curhost->next = init_host(hostname, 0, displayname, clientalias,
									    comment, description,
									    ip1, ip2, ip3, ip4, dialup,
									    warnpct, reporttime,
									    alertlist, nktime, onwaplist,
									    nopropyellowlist,nopropredlist, 
									    noproppurplelist, nopropacklist);
				}
				curhost->parent = (cursubparent ? cursubparent : (cursubpage ? cursubpage : curpage));
				if (curtitle) { curhost->pretitle = curtitle; curtitle = NULL; }
				curhost->nobb2 = nobb2;
			}
			else if (targetpagecount) {

				int pgnum;

				for (pgnum=0; (pgnum < targetpagecount); pgnum++) {
					char *targetpagename = targetpagelist[pgnum];

					char savechar;
					int wantedgroup = 0;
					bbpagelist_t *targetpage = NULL;

					/* Put the host into the page specified by the PGSET: tag */
					p = strchr(targetpagename, ',');
					if (p) {
						savechar = *p;
						*p = '\0';
						wantedgroup = atoi(p+1);
					}
					else {
						savechar = '\0';
						p = targetpagename + strlen(targetpagename);
					}

					/* Find the page */
					if (strcmp(targetpagename, "*") == 0) {
						*targetpagename = '\0';
					}
					for (targetpage = pagelisthead; (targetpage && (strcmp(targetpagename, targetpage->pageentry->name) != 0)); targetpage = targetpage->next) ;

					*p = savechar;
					if (targetpage == NULL) {
						errprintf("Warning: Cannot find any target page named '%s' in set '%s' - dropping host '%s'\n", 
							targetpagename, pgset, hostname);
					}
					else {
						host_t *newhost = init_host(hostname, 0, displayname, clientalias,
									    comment, description,
									    ip1, ip2, ip3, ip4, dialup,
									    warnpct, reporttime,
									    alertlist, nktime, onwaplist,
									    nopropyellowlist,nopropredlist, 
									    noproppurplelist, nopropacklist);

						if (wantedgroup > 0) {
							group_t *gwalk;
							host_t  *hwalk;
							int i;

							for (gwalk = targetpage->pageentry->groups, i=1; (gwalk && (i < wantedgroup)); i++,gwalk=gwalk->next) ;
							if (gwalk) {
								if (gwalk->hosts == NULL)
									gwalk->hosts = newhost;
								else {
									for (hwalk = gwalk->hosts; (hwalk->next); hwalk = hwalk->next) ;
									hwalk->next = newhost;
								}
							}
							else {
								errprintf("Warning: Cannot find group %d for host %s - dropping host\n",
									wantedgroup, hostname);
							}
						}
						else {
							/* Just put in on the page's hostlist */
							host_t *walk;
	
							if (targetpage->pageentry->hosts == NULL)
								targetpage->pageentry->hosts = newhost;
							else {
								for (walk = targetpage->pageentry->hosts; (walk->next); walk = walk->next) ;
								walk->next = newhost;
							}
						}

						newhost->parent = targetpage->pageentry;
						if (curtitle) newhost->pretitle = curtitle;
					}

					curtitle = NULL;
				}
			}
		}
		else if (strncmp(STRBUF(inbuf), summarytag, strlen(summarytag)) == 0) {
			/* summary row.column      IP-ADDRESS-OF-PARENT    http://bb4.com/ */
			char sumname[MAX_LINE_LEN];
			char receiver[MAX_LINE_LEN];
			char url[MAX_LINE_LEN];
			summary_t *newsum;

			if (sscanf(STRBUF(inbuf), "summary %s %s %s", sumname, receiver, url) == 3) {
				newsum = init_summary(sumname, receiver, url);
				newsum->next = sumhead;
				sumhead = newsum;
			}
		}
		else if (strncmp(STRBUF(inbuf), titletag, strlen(titletag)) == 0) {
			/* Save the title for the next entry */
			curtitle = strdup(skipwhitespace(skipword(STRBUF(inbuf))));
		}
	}

	stackfclose(bbhosts);
	freestrbuffer(inbuf);

	return toppage;
}

