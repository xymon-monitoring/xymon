/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: loadbbhosts.c,v 1.8 2004-12-12 22:11:24 henrik Exp $";

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

/* WEB prefixes for host notes and help-files */
char *notesskin = NULL;	/* BBNOTESSKIN */
char *helpskin = NULL;	/* BBHELPSKIN */

char    *wapcolumns = NULL;                     /* Default columns included in WAP cards */
char    *nopropyellowdefault = NULL;
char    *nopropreddefault = NULL;
char    *noproppurpledefault = NULL;
char    *nopropackdefault = NULL;

void addtopagelist(bbgen_page_t *page)
{
	bbpagelist_t *newitem;

	newitem = (bbpagelist_t *) malloc(sizeof(bbpagelist_t));
	newitem->pageentry = page;
	newitem->next = pagelisthead;
	pagelisthead = newitem;
}

char *build_noprop(const char *defset, const char *specset)
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

	free(set);
	return ((strlen(result) > 0) ? result : NULL);
}

bbgen_page_t *init_page(const char *name, const char *title)
{
	bbgen_page_t *newpage = (bbgen_page_t *) malloc(sizeof(bbgen_page_t));

	pagecount++;
	dprintf("init_page(%s, %s)\n", textornull(name), textornull(title));

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

group_t *init_group(const char *title, const char *onlycols)
{
	group_t *newgroup = (group_t *) malloc(sizeof(group_t));

	dprintf("init_group(%s, %s)\n", textornull(title), textornull(onlycols));

	if (title) {
		newgroup->title = strdup(title);
	}
	else title = null_text;

	if (onlycols) {
		newgroup->onlycols = (char *) malloc(strlen(onlycols)+3); /* Add a '|' at start and end */
		sprintf(newgroup->onlycols, "|%s|", onlycols);
	}
	else newgroup->onlycols = NULL;
	newgroup->pretitle = NULL;
	newgroup->hosts = NULL;
	newgroup->next = NULL;
	return newgroup;
}

host_t *init_host(const char *hostname, const char *displayname, const char *clientalias,
		  const char *comment, const char *description,
		  const int ip1, const int ip2, const int ip3, const int ip4, 
		  const int dialup, const int prefer, const double warnpct, const char *reporttime,
		  char *alerts, int nktime, char *waps, char *tags,
		  char *nopropyellowtests, char *nopropredtests, char *noproppurpletests, char *nopropacktests,
		  int modembanksize)
{
	host_t 		*newhost = (host_t *) malloc(sizeof(host_t));
	hostlist_t	*oldlist;

	hostcount++;
	dprintf("init_host(%s, %d,%d,%d.%d, %d, %d, %s, %s, %s, %s, %s %s)\n", 
		textornull(hostname), ip1, ip2, ip3, ip4,
		dialup, prefer, textornull(alerts), textornull(tags),
		textornull(nopropyellowtests), textornull(nopropredtests), 
		textornull(noproppurpletests), textornull(nopropacktests));

	newhost->hostname = newhost->displayname = strdup(hostname);
	if (displayname) newhost->displayname = strdup(displayname);
	newhost->clientalias = (clientalias ? strdup(clientalias) : NULL);
	newhost->comment = (comment ? strdup(comment) : NULL);
	newhost->description = (description ? strdup(description) : NULL);
	sprintf(newhost->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
	newhost->link = find_link(hostname);
	newhost->pretitle = NULL;
	newhost->entries = NULL;
	newhost->color = -1;
	newhost->oldage = 1;
	newhost->prefer = prefer;
	newhost->dialup = dialup;
	newhost->reportwarnlevel = warnpct;
	newhost->reporttime = (reporttime ? strdup(reporttime) : NULL);
	if (alerts && nktime) {
		char *p;
		p = skipword(alerts); if (*p) *p = '\0'; else p = NULL;

		newhost->alerts = (char *) malloc(strlen(alerts)+3);
		sprintf(newhost->alerts, ",%s,", alerts);
		if (p) *p = ' ';
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
		char *p;
		p = skipword((waps ? waps : alerts)); if (*p) *p = '\0'; else p = NULL;
		newhost->waps = strdup(build_noprop(wapcolumns, (waps ? waps : alerts)));
		if (p) *p = ' ';
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
	if (tags) {
		newhost->rawentry = strdup(tags);
	}
	else newhost->rawentry = null_text;
	newhost->parent = NULL;
	newhost->banks = NULL;
	newhost->banksize = modembanksize;
	if (modembanksize) {
		int i;
		newhost->banks = (int *) malloc(modembanksize * sizeof(int));
		for (i=0; i<modembanksize; i++) newhost->banks[i] = -1;

		if (comment) {
			newhost->comment = (char *) realloc(newhost->comment, strlen(newhost->comment) + 22);
			sprintf(newhost->comment+strlen(newhost->comment), " - [%s]", newhost->ip);
		}
		else {
			newhost->comment = newhost->ip;
		}
	}
	newhost->nobb2 = 0;
	newhost->next = NULL;

	/*
	 * Add this host to the hostlist_t list of known hosts.
	 * HOWEVER: It might be a duplicate! In that case, we need
	 * to figure out which host record we want to use.
	 */
	for (oldlist = hosthead; (oldlist && (strcmp(oldlist->hostentry->hostname, hostname) != 0)); oldlist = oldlist->next) ;
	if (oldlist == NULL) {
		hostlist_t *newlist;

		newlist = (hostlist_t *) malloc(sizeof(hostlist_t));
		newlist->hostentry = newhost;
		newlist->clones = NULL;
		newlist->next = hosthead;
		hosthead = newlist;
	}
	else {
		int usenew = 0;
		hostlist_t *clone = (hostlist_t *) malloc(sizeof(hostlist_t));

		dprintf("Duplicate host definition for host '%s'\n", hostname);

		if (newhost->prefer && !oldlist->hostentry->prefer) {
			usenew = 1;
			dprintf("Using new entry as it has 'prefer' tag and old entry does not\n");
		}
		else if (newhost->prefer && oldlist->hostentry->prefer) {
			usenew = 0;
			errprintf("Warning: Multiple prefer entries for host %s - using first one\n", hostname);
		}
		else if (!newhost->prefer && !oldlist->hostentry->prefer) {
			if ( (strcmp(oldlist->hostentry->ip, "0.0.0.0") == 0) && (strcmp(newhost->ip, "0.0.0.0") != 0) ) {
				usenew = 1;
				dprintf("Using new entry as old one has IP 0.0.0.0\n");
			}

			if ( strstr(oldlist->hostentry->rawentry, "noconn") && (strstr(newhost->rawentry, "noconn") == NULL) ) {
				usenew = 1;
				dprintf("Using new entry as old one has noconn\n");
			}
		}

		if (usenew) {
			clone->hostentry = oldlist->hostentry;
			clone->clones = NULL;
			clone->next = oldlist->clones;
			oldlist->clones = clone;

			oldlist->hostentry = newhost;
		}
		else {
			clone->hostentry = newhost;
			clone->clones = NULL;
			clone->next = oldlist->clones;
			oldlist->clones = clone;
		}
	}

	return newhost;
}

link_t *init_link(char *filename, const char *urlprefix)
{
	char *p;
	link_t *newlink = NULL;

	dprintf("init_link(%s, %s)\n", textornull(filename), textornull(urlprefix));

	newlink = (link_t *) malloc(sizeof(link_t));
	newlink->filename = strdup(filename);
	newlink->urlprefix = strdup(urlprefix);
	newlink->next = NULL;

	p = strrchr(filename, '.');
	if (p == NULL) p = (filename + strlen(filename));

	if ( (strcmp(p, ".php") == 0)    ||
             (strcmp(p, ".php3") == 0)   ||
             (strcmp(p, ".asp") == 0)    ||
             (strcmp(p, ".doc") == 0)    ||
	     (strcmp(p, ".shtml") == 0)  ||
	     (strcmp(p, ".phtml") == 0)  ||
	     (strcmp(p, ".html") == 0)   ||
	     (strcmp(p, ".htm") == 0))      
	{
		*p = '\0';
	}

	/* Without extension, this time */
	newlink->name = strdup(filename);

	return newlink;
}


void getnamelink(char *l, char **name, char **link)
{
	/* "page NAME title-or-link" splitup */
	char *p;

	dprintf("getnamelink(%s, ...)\n", textornull(l));

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

	dprintf("getnamelink(%s, ...)\n", textornull(l));

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


void getgrouptitle(char *l, char *pageset, char **title, char **onlycols)
{
	char grouponlytag[100], grouptag[100];

	*title = null_text;
	*onlycols = NULL;

	sprintf(grouponlytag, "%sgroup-only", pageset);
	sprintf(grouptag, "%sgroup", pageset);

	dprintf("getgrouptitle(%s, ...)\n", textornull(l));

	if (strncmp(l, grouponlytag, strlen(grouponlytag)) == 0) {
		char *p;

		*onlycols = skipwhitespace(skipword(l));

		p = skipword(*onlycols);
		if (*p) {
			*p = '\0'; p++;
			*title = skipwhitespace(p);
		}
	}
	else if (strncmp(l, grouptag, strlen(grouptag)) == 0) {
		*title = skipwhitespace(skipword(l));
	}
}

link_t *load_links(const char *directory, const char *urlprefix)
{
	DIR		*bblinks;
	struct dirent 	*d;
	char		fn[PATH_MAX];
	link_t		*curlink, *toplink, *newlink;

	dprintf("load_links(%s, %s)\n", textornull(directory), textornull(urlprefix));

	toplink = curlink = NULL;
	bblinks = opendir(directory);
	if (!bblinks) {
		errprintf("Cannot read links in directory %s\n", directory);
		return NULL;
	}

	while ((d = readdir(bblinks))) {
		strcpy(fn, d->d_name);
		newlink = init_link(fn, urlprefix);
		if (newlink) {
			if (toplink == NULL) {
				toplink = newlink;
			}
			else {
				curlink->next = newlink;
			}
			curlink = newlink;
		}
	}
	closedir(bblinks);
	return toplink;
}

link_t *load_all_links(void)
{
	link_t *l, *head1, *head2;
	char dirname[PATH_MAX];
	char *p;

	dprintf("load_all_links()\n");

	if (getenv("BBNOTESSKIN")) notesskin = strdup(getenv("BBNOTESSKIN"));
	else { 
		notesskin = (char *) malloc(strlen(getenv("BBWEB")) + strlen("/notes") + 1);
		sprintf(notesskin, "%s/notes", getenv("BBWEB"));
	}

	if (getenv("BBHELPSKIN")) helpskin = strdup(getenv("BBHELPSKIN"));
	else { 
		helpskin = (char *) malloc(strlen(getenv("BBWEB")) + strlen("/help") + 1);
		sprintf(helpskin, "%s/help", getenv("BBWEB"));
	}

	strcpy(dirname, getenv("BBNOTES"));
	head1 = load_links(dirname, notesskin);

	/* Change xxx/xxx/xxx/notes into xxx/xxx/xxx/help */
	p = strrchr(dirname, '/'); *p = '\0'; strcat(dirname, "/help");
	head2 = load_links(dirname, helpskin);

	if (head1) {
		/* Append help-links to list of notes-links */
		for (l = head1; (l->next); l = l->next) ;
		l->next = head2;
	}
	else {
		/* /notes was empty, so just return the /help list */
		head1 = head2;
	}

	return head1;
}

summary_t *init_summary(char *name, char *receiver, char *url)
{
	summary_t *newsum;

	dprintf("init_summary(%s, %s, %s)\n", textornull(name), textornull(receiver), textornull(url));

	/* Sanity check */
	if ((name == NULL) || (receiver == NULL) || (url == NULL)) 
		return NULL;

	newsum = (summary_t *) malloc(sizeof(summary_t));
	newsum->name = strdup(name);
	newsum->receiver = strdup(receiver);
	newsum->url = strdup(url);
	newsum->next = NULL;

	return newsum;
}


bbgen_page_t *load_bbhosts(char *pgset)
{
	FILE 	*bbhosts;
	char 	l[MAX_LINE_LEN], lcop[MAX_LINE_LEN];
	char	pagetag[100], subpagetag[100], subparenttag[100], 
		grouptag[100], summarytag[100], titletag[100], hosttag[100];
	char 	*name, *link, *onlycols;
	char 	hostname[MAX_LINE_LEN];
	bbgen_page_t 	*toppage, *curpage, *cursubpage, *cursubparent;
	group_t *curgroup;
	host_t	*curhost;
	char	*curtitle;
	int	ip1, ip2, ip3, ip4;
	int	modembanksize;
	char	*p;

	dprintf("load_bbhosts(pgset=%s)\n", textornull(pgset));

	bbhosts = stackfopen(getenv("BBHOSTS"), "r");
	if (bbhosts == NULL) {
		errprintf("Cannot open the BBHOSTS file '%s'\n", getenv("BBHOSTS"));
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

	while (stackfgets(l, sizeof(l), "include", "dispinclude")) {
		p = strchr(l, '\n'); 
		if (p) {
			*p = '\0'; 
		}
		else {
			errprintf("Warning: Lines in bb-hosts too long or has no newline: '%s'\n", l);
			fflush(stdout);
		}

		dprintf("load_bbhosts: -- got line '%s'\n", l);

		modembanksize = 0;

		if ((l[0] == '#') || (strlen(l) == 0)) {
			/* Do nothing - it's a comment */
		}
		else if (strncmp(l, pagetag, strlen(pagetag)) == 0) {
			getnamelink(l, &name, &link);
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
		else if (strncmp(l, subpagetag, strlen(subpagetag)) == 0) {
			if (curpage == NULL) {
				errprintf("'subpage' ignored, no preceding 'page' tag : %s\n", l);
				continue;
			}

			getnamelink(l, &name, &link);
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
		else if (strncmp(l, subparenttag, strlen(subparenttag)) == 0) {
			bbgen_page_t *parentpage, *walk;

			getparentnamelink(l, toppage, &parentpage, &name, &link);
			if (parentpage == NULL) {
				errprintf("'subparent' ignored, unknown parent page: %s\n", l);
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
		else if (strncmp(l, grouptag, strlen(grouptag)) == 0) {
			getgrouptitle(l, pgset, &link, &onlycols);
			if (curgroup == NULL) {
				curgroup = init_group(link, onlycols);
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
				curgroup->next = init_group(link, onlycols);
				curgroup = curgroup->next;
			}
			if (curtitle) { curgroup->pretitle = curtitle; curtitle = NULL; }
			curhost = NULL;
		}
		else if ( (sscanf(l, "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) ||
		          (!reportstart && !snapshot && (sscanf(l, "dialup %s %d.%d.%d.%d %d", hostname, &ip1, &ip2, &ip3, &ip4, &modembanksize) == 6) && (modembanksize > 0)) ) {
			int dialup = 0;
			int prefer = 0;
			int nodisp = 0;
			int nobb2 = 0;
			int nktime = 1;
			double warnpct = reportwarnlevel;
			char *alertlist, *onwaplist, *nopropyellowlist, *nopropredlist, *noproppurplelist, *nopropacklist;
			char *reporttime;
			char *displayname, *clientalias, *comment, *description;
			char *targetpagelist[MAX_TARGETPAGES_PER_HOST];
			int targetpagecount;
			char *tag;
			char *startoftags = strchr(l, '#');

			displayname = clientalias = NULL;

			/* If FQDN is not set, strip any domain off the hostname */
			if (!fqdn) {
				char *p = strchr(hostname, '.');
				if (p) {
					/* Save full name as "displayname", and modify hostname to be with no domain */
					displayname = strdup(hostname);
					*p = '\0';
				}
			}

			if (startoftags) {
				strcpy(lcop, startoftags+1);
				tag = strtok(lcop, " \t\r\n");
			}
			else tag = NULL;

			alertlist = onwaplist = nopropyellowlist = nopropredlist = noproppurplelist = nopropacklist = reporttime = NULL;
			comment = description = NULL;
			for (targetpagecount=0; (targetpagecount < MAX_TARGETPAGES_PER_HOST); targetpagecount++) 
				targetpagelist[targetpagecount] = NULL;
			targetpagecount = 0;

			while (tag) {
				if (strcmp(tag, "dialup") == 0) 
					dialup = 1;
				else if (strcmp(tag, "prefer") == 0) 
					prefer = 1;
				else if ((strcmp(tag, "nodisp") == 0) || (strcmp(tag, "NODISP") == 0)) {
					if (strlen(pgset) == 0) nodisp = 1;
				}
				else if ((strcmp(tag, "nobb2") == 0) || (strcmp(tag, "NOBB2") == 0))
					nobb2 = 1;
				else if (argnmatch(tag, "NK:")) 
					alertlist = strdup(tag+strlen("NK:"));
				else if (argnmatch(tag, "NKTIME=")) 
					nktime = within_sla(tag, "NKTIME", 1);
				else if (argnmatch(tag, "WML:")) 
					onwaplist = strdup(tag+strlen("WML:"));
				else if (argnmatch(tag, "NOPROP:")) 
					nopropyellowlist = strdup(tag+strlen("NOPROP:"));
				else if (argnmatch(tag, "NOPROPYELLOW:")) 
					nopropyellowlist = strdup(tag+strlen("NOPROPYELLOW:"));
				else if (argnmatch(tag, "NOPROPRED:")) 
					nopropredlist = strdup(tag+strlen("NOPROPRED:"));
				else if (argnmatch(tag, "NOPROPPURPLE:")) 
					noproppurplelist = strdup(tag+strlen("NOPROPPURPLE:"));
				else if (argnmatch(tag, "NOPROPACK:")) 
					nopropacklist = strdup(tag+strlen("NOPROPACK:"));
				else if (argnmatch(tag, "NAME:")) {
					p = tag+strlen("NAME:");
					displayname = (char *) malloc(strlen(l));
					if (*p == '\"') {
						p++;
						strcpy(displayname, p);
						p = strchr(displayname, '\"');
						if (p) *p = '\0'; 
						else {
							/* Scan forward to next " in input stream */
							tag = strtok(NULL, "\"\r\n");
							if (tag) {
								strcat(displayname, " ");
								strcat(displayname, tag);
							}
						}
					}
					else {
						strcpy(displayname, p);
					}
				}
				else if (argnmatch(tag, "CLIENT:")) {
					p = tag+strlen("CLIENT:");
					clientalias = strdup(p);
				}
				else if (argnmatch(tag, "COMMENT:")) {
					p = tag+strlen("COMMENT:");
					comment = (char *) malloc(strlen(l));
					if (*p == '\"') {
						p++;
						strcpy(comment, p);
						p = strchr(comment, '\"');
						if (p) *p = '\0'; 
						else {
							/* Scan forward to next " in input stream */
							tag = strtok(NULL, "\"\r\n");
							if (tag) {
								strcat(comment, " ");
								strcat(comment, tag);
							}
						}
					}
					else {
						strcpy(comment, p);
					}
				}
				else if (argnmatch(tag, "DESCR:")) {
					p = tag+strlen("DESCR:");
					description = (char *) malloc(strlen(l));
					if (*p == '\"') {
						p++;
						strcpy(description, p);
						p = strchr(description, '\"');
						if (p) *p = '\0'; 
						else {
							/* Scan forward to next " in input stream */
							tag = strtok(NULL, "\"\r\n");
							if (tag) {
								strcat(description, " ");
								strcat(description, tag);
							}
						}
					}
					else {
						strcpy(description, p);
					}
				}
				else if (argnmatch(tag, "WARNPCT:")) 
					warnpct = atof(tag+8);
				else if (argnmatch(tag, "REPORTTIME=")) 
					reporttime = strdup(tag);
				else if (argnmatch(tag, hosttag)) {
					targetpagelist[targetpagecount++] = strdup(tag+strlen(hosttag));
				}

				if (tag) tag = strtok(NULL, " \t\r\n");
			}

			if (nodisp) {
				/*
				 * Ignore this host.
				 */
			}
			else if (strlen(pgset) == 0) {
				/*
				 * Default pageset generated. Put the host into
				 * whatever group or page is current.
				 */
				if (curhost == NULL) {
					curhost = init_host(hostname, displayname, clientalias,
							    comment, description,
							    ip1, ip2, ip3, ip4, dialup, prefer, 
							    warnpct, reporttime,
							    alertlist, nktime, onwaplist,
							    startoftags, 
							    nopropyellowlist, nopropredlist, noproppurplelist, nopropacklist,
							    modembanksize);
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
					curhost = curhost->next = init_host(hostname, displayname, clientalias,
									    comment, description,
									    ip1, ip2, ip3, ip4, dialup, prefer, 
									    warnpct, reporttime,
									    alertlist, nktime, onwaplist,
									    startoftags, 
									    nopropyellowlist,nopropredlist, 
									    noproppurplelist, nopropacklist,
									    modembanksize);
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
						errprintf("Warning: Cannot find any target page named %s - dropping host %s'\n", 
							targetpagename, hostname);
					}
					else {
						host_t *newhost = init_host(hostname, displayname, clientalias,
									    comment, description,
									    ip1, ip2, ip3, ip4, dialup, prefer, 
									    warnpct, reporttime,
									    alertlist, nktime, onwaplist,
									    startoftags, 
									    nopropyellowlist,nopropredlist, 
									    noproppurplelist, nopropacklist,
									    modembanksize);

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

			if (displayname) free(displayname);
			if (description) free(description);
			if (comment) free(comment);
			if (alertlist) free(alertlist);
			if (onwaplist) free(onwaplist);
			if (nopropyellowlist) free(nopropyellowlist);
			if (nopropredlist) free(nopropredlist);
			if (noproppurplelist) free(noproppurplelist);
			if (nopropacklist) free(nopropacklist);
			if (reporttime) free(reporttime);
			for (targetpagecount=0; (targetpagecount < MAX_TARGETPAGES_PER_HOST); targetpagecount++) 
				if (targetpagelist[targetpagecount]) free(targetpagelist[targetpagecount]);
		}
		else if (strncmp(l, summarytag, strlen(summarytag)) == 0) {
			/* summary row.column      IP-ADDRESS-OF-PARENT    http://bb4.com/ */
			char sumname[MAX_LINE_LEN];
			char receiver[MAX_LINE_LEN];
			char url[MAX_LINE_LEN];
			summary_t *newsum;

			if (sscanf(l, "summary %s %s %s", sumname, receiver, url) == 3) {
				newsum = init_summary(sumname, receiver, url);
				newsum->next = sumhead;
				sumhead = newsum;
			}
		}
		else if (strncmp(l, titletag, strlen(titletag)) == 0) {
			/* Save the title for the next entry */
			curtitle = strdup(skipwhitespace(skipword(l)));
		}
		else {
		};
	}

	stackfclose(bbhosts);
	return toppage;
}

