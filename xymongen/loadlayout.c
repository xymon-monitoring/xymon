/*----------------------------------------------------------------------------*/
/* Xymon overview webpage generator tool.                                     */
/*                                                                            */
/* This file holds code to load the page-structure from the hosts.cfg file.   */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

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

#include "xymongen.h"
#include "util.h"
#include "loadlayout.h"

#define MAX_TARGETPAGES_PER_HOST 10

time_t	snapshot = 0;				/* Set if we are doing a snapshot */

char	*null_text = "";

/* List definition to search for page records */
typedef struct xymonpagelist_t {
	struct xymongen_page_t *pageentry;
	struct xymonpagelist_t *next;
} xymonpagelist_t;

static xymonpagelist_t *pagelisthead = NULL;
int	pagecount = 0;
int	hostcount = 0;

char    *wapcolumns = NULL;                     /* Default columns included in WAP cards */
char    *nopropyellowdefault = NULL;
char    *nopropreddefault = NULL;
char    *noproppurpledefault = NULL;
char    *nopropackdefault = NULL;

void addtopagelist(xymongen_page_t *page)
{
	xymonpagelist_t *newitem;

	newitem = (xymonpagelist_t *) calloc(1, sizeof(xymonpagelist_t));
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

xymongen_page_t *init_page(char *name, char *title, int vertical)
{
	xymongen_page_t *newpage = (xymongen_page_t *) calloc(1, sizeof(xymongen_page_t));

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
	newpage->vertical = vertical;
	newpage->pretitle = NULL;
	newpage->groups = NULL;
	newpage->hosts = NULL;
	newpage->parent = NULL;
	newpage->subpages = NULL;
	newpage->next = NULL;

	return newpage;
}

group_t *init_group(char *title, char *onlycols, char *exceptcols, int sorthosts)
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
	newgroup->sorthosts = sorthosts;
	newgroup->next = NULL;
	return newgroup;
}

host_t *init_host(char *hostname, int issummary,
		  char *displayname, char *clientalias,
		  char *comment, char *description, char *ip,
		  int dialup, double warnpct, int warnstops, char *reporttime,
		  char *alerts, int crittime, char *waps,
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
	newhost->ip = strdup(ip);
	newhost->pretitle = NULL;
	newhost->entries = NULL;
	newhost->color = -1;
	newhost->oldage = 1;
	newhost->dialup = dialup;
	newhost->reportwarnlevel = warnpct;
	newhost->reportwarnstops = warnstops;
	newhost->reporttime = (reporttime ? strdup(reporttime) : NULL);
	if (alerts && crittime) {
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
	newhost->nonongreen = 0;
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


void getparentnamelink(char *l, xymongen_page_t *toppage, xymongen_page_t **parent, char **name, char **link)
{
	/* "subparent NAME PARENTNAME title-or-link" splitup */
	char *p;
	char *parentname;
	xymonpagelist_t *walk;

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


xymongen_page_t *load_layout(char *pgset)
{
	char	pagetag[100], subpagetag[100], subparenttag[100], 
		vpagetag[100], vsubpagetag[100], vsubparenttag[100], 
		grouptag[100], summarytag[100], titletag[100], hosttag[100];
	char 	*name, *link, *onlycols, *exceptcols;
	char 	*hostname, *ip;
	xymongen_page_t 	*toppage, *curpage, *cursubpage, *cursubparent;
	group_t *curgroup;
	host_t	*curhost;
	char	*curtitle;
	char	*p;
	int	fqdn = get_fqdn();
	char	*cfgdata, *inbol, *ineol, insavchar = '\0', *lcopy = NULL;

	if (loadhostsfromxymond) {
		if (load_hostnames("@", NULL, fqdn) != 0) {
			errprintf("Cannot load host configuration from xymond\n");
			return NULL;
		}
	}
	else {
		if (load_hostnames(xgetenv("HOSTSCFG"), "dispinclude", fqdn) != 0) {
			errprintf("Cannot load host configuration from %s\n", xgetenv("HOSTSCFG"));
			return NULL;
		}
	}

	if (first_host() == NULL) {
		errprintf("Empty configuration from %s\n", (loadhostsfromxymond ? "xymond" : xgetenv("HOSTSCFG")));
		return NULL;
	}

	dbgprintf("load_layout(pgset=%s)\n", textornull(pgset));

	/*
	 * load_hostnames() picks up the hostname definitions, but not the page
	 * layout. So we will scan the file again, this time doing the layout.
	 */

	if (pgset == NULL) pgset = "";
	sprintf(pagetag, "%spage", pgset);
	sprintf(subpagetag, "%ssubpage", pgset);
	sprintf(subparenttag, "%ssubparent", pgset);
	sprintf(vpagetag, "v%spage", pgset);
	sprintf(vsubpagetag, "v%ssubpage", pgset);
	sprintf(vsubparenttag, "v%ssubparent", pgset);
	sprintf(grouptag, "%sgroup", pgset);
	sprintf(summarytag, "%ssummary", pgset);
	sprintf(titletag, "%stitle", pgset);
	sprintf(hosttag, "%s:", pgset); for (p=hosttag; (*p); p++) *p = toupper((int)*p);

	toppage = init_page("", "", 0);
	addtopagelist(toppage);
	curpage = NULL;
	cursubpage = NULL;
	curgroup = NULL;
	curhost = NULL;
	cursubparent = NULL;
	curtitle = NULL;

	inbol = cfgdata = hostscfg_content();
	while (inbol && *inbol) {
		char *key, *keyp;

		inbol += strspn(inbol, " \t");
		ineol = strchr(inbol, '\n');
		if (ineol) {
			while ((ineol > inbol) && (isspace(*ineol) || (*ineol == '\n'))) ineol--;
			if (*ineol != '\n') ineol++;

			insavchar = *ineol;
			*ineol = '\0';
		}

		if ((*inbol == '#') || (strlen(inbol) == 0)) goto nextline;

		dbgprintf("load_layout: -- got line '%s'\n", inbol);

		if (lcopy) xfree(lcopy);
		lcopy = strdup(inbol);
		key = strtok_r(lcopy, " \t\r\n", &keyp);

		if ((strncmp(inbol, pagetag, strlen(pagetag)) == 0) || (strncmp(inbol, vpagetag, strlen(vpagetag)) == 0)) {
			getnamelink(inbol, &name, &link);
			if (curpage == NULL) {
				/* First page - hook it on toppage as a subpage from there */
				curpage = toppage->subpages = init_page(name, link, (strncmp(inbol, vpagetag, strlen(vpagetag)) == 0));
			}
			else {
				curpage = curpage->next = init_page(name, link, (strncmp(inbol, vpagetag, strlen(vpagetag)) == 0));
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
		else if ( (strncmp(inbol, subpagetag, strlen(subpagetag)) == 0) || (strncmp(inbol, vsubpagetag, strlen(vsubpagetag)) == 0) ) {
			if (curpage == NULL) {
				errprintf("'subpage' ignored, no preceding 'page' tag : %s\n", inbol);
				goto nextline;
			}

			getnamelink(inbol, &name, &link);
			if (cursubpage == NULL) {
				cursubpage = curpage->subpages = init_page(name, link, (strncmp(inbol, vsubpagetag, strlen(vsubpagetag)) == 0));
			}
			else {
				cursubpage = cursubpage->next = init_page(name, link, (strncmp(inbol, vsubpagetag, strlen(vsubpagetag)) == 0));
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
		else if ( (strncmp(inbol, subparenttag, strlen(subparenttag)) == 0) || (strncmp(inbol, vsubparenttag, strlen(vsubparenttag)) == 0) ) {
			xymongen_page_t *parentpage, *walk;

			getparentnamelink(inbol, toppage, &parentpage, &name, &link);
			if (parentpage == NULL) {
				errprintf("'subparent' ignored, unknown parent page: %s\n", inbol);
				goto nextline;
			}

			cursubparent = init_page(name, link, (strncmp(inbol, vsubparenttag, strlen(vsubparenttag)) == 0));
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
		else if (strncmp(inbol, grouptag, strlen(grouptag)) == 0) {
			int sorthosts = (strstr(inbol, "group-sorted") != NULL);

			getgrouptitle(inbol, pgset, &link, &onlycols, &exceptcols);
			if (curgroup == NULL) {
				curgroup = init_group(link, onlycols, exceptcols, sorthosts);
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
				curgroup->next = init_group(link, onlycols, exceptcols, sorthosts);
				curgroup = curgroup->next;
			}
			if (curtitle) { curgroup->pretitle = curtitle; curtitle = NULL; }
			curhost = NULL;
		}
		else if (conn_is_ip(key) != 0) {
			void *xymonhost = NULL;
			int dialup, nonongreen, crittime = 1;
			double warnpct = reportwarnlevel;
			int warnstops = reportwarnstops;
			char *displayname, *clientalias, *comment, *description;
			char *alertlist, *onwaplist, *reporttime;
			char *nopropyellowlist, *nopropredlist, *noproppurplelist, *nopropacklist;
			char *targetpagelist[MAX_TARGETPAGES_PER_HOST];
			int targetpagecount;
			char *hval;

			ip = key;
			hostname = strtok_r(NULL, " \t\r\n", &keyp);

			/* Check for ".default." hosts - they are ignored. */
			if (*hostname == '.') goto nextline;

			if (!fqdn) {
				/* Strip any domain from the hostname */
				char *p = strchr(hostname, '.');
				if (p) *p = '\0';
			}

			/* Get the info */
			xymonhost = hostinfo(hostname);
			if (xymonhost == NULL) {
				errprintf("Confused - hostname '%s' cannot be found. Ignored\n", hostname);
				goto nextline;
			}

			/* Check for no-display hosts - they are ignored. */
			/* But only when we're building the default pageset */
			if ((strlen(pgset) == 0) && (xmh_item(xymonhost, XMH_FLAG_NODISP) != NULL)) goto nextline;

			for (targetpagecount=0; (targetpagecount < MAX_TARGETPAGES_PER_HOST); targetpagecount++) 
				targetpagelist[targetpagecount] = NULL;
			targetpagecount = 0;

			dialup = (xmh_item(xymonhost, XMH_FLAG_DIALUP) != NULL);
			nonongreen = (xmh_item(xymonhost, XMH_FLAG_NONONGREEN) != NULL);

			alertlist = xmh_item(xymonhost, XMH_NK);
			hval = xmh_item(xymonhost, XMH_NKTIME); if (hval) crittime = within_sla(xmh_item(xymonhost, XMH_HOLIDAYS), hval, 0);

			onwaplist = xmh_item(xymonhost, XMH_WML);
			nopropyellowlist = xmh_item(xymonhost, XMH_NOPROPYELLOW);
			if (nopropyellowlist == NULL) nopropyellowlist = xmh_item(xymonhost, XMH_NOPROP);
			nopropredlist = xmh_item(xymonhost, XMH_NOPROPRED);
			noproppurplelist = xmh_item(xymonhost, XMH_NOPROPPURPLE);
			nopropacklist = xmh_item(xymonhost, XMH_NOPROPACK);
			displayname = xmh_item(xymonhost, XMH_DISPLAYNAME);
			comment = xmh_item(xymonhost, XMH_COMMENT);
			description = xmh_item(xymonhost, XMH_DESCRIPTION);
			hval = xmh_item(xymonhost, XMH_WARNPCT); if (hval) warnpct = atof(hval);
			hval = xmh_item(xymonhost, XMH_WARNSTOPS); if (hval) warnstops = atof(hval);
			reporttime = xmh_item(xymonhost, XMH_REPORTTIME);

			clientalias = xmh_item(xymonhost, XMH_CLIENTALIAS);
			if (xymonhost && (strcmp(xmh_item(xymonhost, XMH_HOSTNAME), clientalias) == 0)) clientalias = NULL;

			if (xymonhost && (strlen(pgset) > 0)) {
				/* Walk the clone-list and pick up the target pages for this host */
				void *cwalk = xymonhost;
				do {
					hval = xmh_item_walk(cwalk);
					while (hval) {
						if (strncasecmp(hval, hosttag, strlen(hosttag)) == 0)
							targetpagelist[targetpagecount++] = strdup(hval+strlen(hosttag));
						hval = xmh_item_walk(NULL);
					}

					cwalk = next_host(cwalk, 1);
				} while (cwalk && 
					 (strcmp(xmh_item(cwalk, XMH_HOSTNAME), xmh_item(xymonhost, XMH_HOSTNAME)) == 0) &&
					 (targetpagecount < MAX_TARGETPAGES_PER_HOST) );

				/*
				 * HACK: Check if the pageset tag is present at all in the host
				 * entry. If it isn't, then drop this incarnation of the host.
				 *
				 * Without this, the following hosts.cfg file will have the
				 * www.hswn.dk host listed twice on the alternate pageset:
				 *
				 * adminpage nyc NYC
				 *
				 * 127.0.0.1   localhost      # bbd http://localhost/ CLIENT:osiris
				 * 172.16.10.2 www.xymon.com  # http://www.xymon.com/ ADMIN:nyc ssh noinfo
				 *
				 * page superdome Superdome
				 * 172.16.10.2 www.xymon.com # noconn
				 *
				 */
				if (strstr(inbol, hosttag) == NULL) targetpagecount = 0;
			}

			if (strlen(pgset) == 0) {
				/*
				 * Default pageset generated. Put the host into
				 * whatever group or page is current.
				 */
				if (curhost == NULL) {
					curhost = init_host(hostname, 0, displayname, clientalias,
							    comment, description,
							    ip, dialup, 
							    warnpct, warnstops, reporttime,
							    alertlist, crittime, onwaplist,
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
									    ip, dialup,
									    warnpct, warnstops, reporttime,
									    alertlist, crittime, onwaplist,
									    nopropyellowlist,nopropredlist, 
									    noproppurplelist, nopropacklist);
				}
				curhost->parent = (cursubparent ? cursubparent : (cursubpage ? cursubpage : curpage));
				if (curtitle) { curhost->pretitle = curtitle; curtitle = NULL; }
				curhost->nonongreen = nonongreen;
			}
			else if (targetpagecount) {

				int pgnum;

				for (pgnum=0; (pgnum < targetpagecount); pgnum++) {
					char *targetpagename = targetpagelist[pgnum];

					char savechar;
					int wantedgroup = 0;
					xymonpagelist_t *targetpage = NULL;

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
									    ip, dialup,
									    warnpct, warnstops, reporttime,
									    alertlist, crittime, onwaplist,
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
		else if (strncmp(inbol, summarytag, strlen(summarytag)) == 0) {
			/* summary row.column      IP-ADDRESS-OF-PARENT    http://xymon.com/ */
			char sumname[MAX_LINE_LEN];
			char receiver[MAX_LINE_LEN];
			char url[MAX_LINE_LEN];
			summary_t *newsum;

			if (sscanf(inbol, "summary %s %s %s", sumname, receiver, url) == 3) {
				newsum = init_summary(sumname, receiver, url);
				newsum->next = sumhead;
				sumhead = newsum;
			}
		}
		else if (strncmp(inbol, titletag, strlen(titletag)) == 0) {
			/* Save the title for the next entry */
			curtitle = strdup(skipwhitespace(skipword(inbol)));
		}

nextline:
		if (ineol) {
			*ineol = insavchar;
			if (*ineol != '\n') ineol = strchr(ineol, '\n');

			inbol = (ineol ? ineol+1 : NULL);
		}
		else
			inbol = NULL;
	}

	xfree(cfgdata);
	return toppage;
}

