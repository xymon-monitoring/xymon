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

static char rcsid[] = "$Id: loaddata.c,v 1.67 2003-05-22 13:51:22 henrik Exp $";

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
#include "loaddata.h"
#include "larrdgen.h"
#include "infogen.h"
#include "debug.h"

char    *nopropyellowdefault = NULL;
char    *nopropreddefault = NULL;
char	*larrdgraphs_default = NULL;
int     enable_purpleupd = 1;
int	purpledelay = 0;			/* Lifetime of purple status-messages. Default 0 for
						   compatibility with standard bb-display.sh behaviour */
char	*ignorecolumns = NULL;			/* Columns that will be ignored totally */

link_t  null_link = { "", "", "", NULL };	/* Null link for pages/hosts/whatever with no link */
bbgen_col_t   null_column = { "", NULL };		/* Null column */
char	*null_text = "";

int	hostcount = 0;
int	statuscount = 0;
int	pagecount = 0;
int	purplecount = 0;

static pagelist_t *pagelisthead = NULL;


char *skipword(const char *l)
{
	char *p;

	for (p=l; (*p && (!isspace((int)*p))); p++) ;
	return p;
}


char *skipwhitespace(const char *l)
{
	char *p;

	for (p=l; (*p && (isspace((int)*p))); p++) ;
	return p;
}


void addtopagelist(bbgen_page_t *page)
{
	pagelist_t *newitem;

	newitem = malloc(sizeof(pagelist_t));
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

	set = malcop(specset);
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
	bbgen_page_t *newpage = malloc(sizeof(bbgen_page_t));

	pagecount++;
	dprintf("init_page(%s, %s)\n", textornull(name), textornull(title));

	if (name) {
		newpage->name = malcop(name);
	}
	else name = null_text;

	if (title) {
		newpage->title = malcop(title);
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
	group_t *newgroup = malloc(sizeof(group_t));

	dprintf("init_group(%s, %s)\n", textornull(title), textornull(onlycols));

	if (title) {
		newgroup->title = malcop(title);
	}
	else title = null_text;

	if (onlycols) {
		newgroup->onlycols = malloc(strlen(onlycols)+3); /* Add a '|' at start and end */
		sprintf(newgroup->onlycols, "|%s|", onlycols);
	}
	else newgroup->onlycols = NULL;
	newgroup->pretitle = NULL;
	newgroup->hosts = NULL;
	newgroup->next = NULL;
	return newgroup;
}

host_t *init_host(const char *hostname, const int ip1, const int ip2, const int ip3, const int ip4, 
		  const int dialup, const char *alerts, const char *waps,
		  char *tags,
		  const char *nopropyellowtests, const char *nopropredtests,
		  const char *larrdgraphs)
{
	host_t 		*newhost = malloc(sizeof(host_t));
	hostlist_t	*newlist = malloc(sizeof(hostlist_t));

	hostcount++;
	dprintf("init_host(%s, %d,%d,%d.%d, %d, %s, %s, %s, %s)\n", 
		textornull(hostname), ip1, ip2, ip3, ip4,
		dialup, textornull(alerts), textornull(tags),
		textornull(nopropyellowtests), textornull(nopropredtests));

	newhost->hostname = malcop(hostname);
	sprintf(newhost->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
	newhost->link = find_link(hostname);
	newhost->pretitle = NULL;
	newhost->entries = NULL;
	newhost->color = -1;
	newhost->oldage = 1;
	newhost->dialup = dialup;
	if (alerts) {
		char *p;
		p = skipword(alerts); if (*p) *p = '\0'; else p = NULL;

		newhost->alerts = malloc(strlen(alerts)+3);
		sprintf(newhost->alerts, ",%s,", alerts);
		if (p) *p = ' ';
	}
	else {
		newhost->alerts = NULL;
	}
	if (waps) {
		char *p;
		p = skipword(waps); if (*p) *p = '\0'; else p = NULL;

		newhost->waps = malloc(strlen(waps)+3);
		sprintf(newhost->waps, ",%s,", waps);
		if (p) *p = ' ';
	}
	else {
		newhost->waps = NULL;
	}
	if (nopropyellowtests) {
		char *p;
		p = skipword(nopropyellowtests); if (*p) *p = '\0'; else p = NULL;
		newhost->nopropyellowtests = malcop(build_noprop(nopropyellowdefault, nopropyellowtests));
		if (p) *p = ' ';
	}
	else {
		newhost->nopropyellowtests = nopropyellowdefault;
	}
	if (nopropredtests) {
		char *p;
		p = skipword(nopropredtests); if (*p) *p = '\0'; else p = NULL;
		newhost->nopropredtests = malcop(build_noprop(nopropreddefault, nopropredtests));
		if (p) *p = ' ';
	}
	else {
		newhost->nopropredtests = nopropreddefault;
	}
	if (larrdgraphs) {
		char *p;
		p = skipword(larrdgraphs); if (*p) *p = '\0'; else p = NULL;
		newhost->larrdgraphs = malcop(larrdgraphs);
		if (p) *p = ' ';
	}
	else newhost->larrdgraphs = larrdgraphs_default;
	if (tags) {
		newhost->rawentry = malcop(tags);
	}
	else newhost->rawentry = null_text;
	newhost->parent = newhost->next = NULL;
	newhost->rrds = NULL;

	newlist->hostentry = newhost;
	newlist->next = hosthead;
	hosthead = newlist;

	return newhost;
}

link_t *init_link(char *filename, const char *urlprefix)
{
	char *p;
	link_t *newlink = NULL;

	dprintf("init_link(%s, %s)\n", textornull(filename), textornull(urlprefix));

	newlink = malloc(sizeof(link_t));
	newlink->filename = malcop(filename);
	newlink->urlprefix = malcop(urlprefix);
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
	newlink->name = malcop(filename);

	return newlink;
}

bbgen_col_t *find_or_create_column(const char *testname)
{
	static bbgen_col_t *lastcol = NULL;	/* Cache the last lookup */
	bbgen_col_t *newcol;

	dprintf("find_or_create_column(%s)\n", textornull(testname));
	if (lastcol && (strcmp(testname, lastcol->name) == 0))
		return lastcol;

	for (newcol = colhead; (newcol && (strcmp(testname, newcol->name) != 0)); newcol = newcol->next);
	if (newcol == NULL) {
		newcol = malloc(sizeof(bbgen_col_t));
		newcol->name = malcop(testname);
		newcol->link = find_link(testname);

		/* No need to maintain this list in order */
		if (colhead == NULL) {
			colhead = newcol;
			newcol->next = NULL;
		}
		else {
			newcol->next = colhead;
			colhead = newcol;
		}
	}
	lastcol = newcol;

	return newcol;
}

state_t *init_state(const char *filename, int dopurple, int *is_purple)
{
	FILE 		*fd;
	char		*p;
	char		*hostname;
	char		*testname;
	state_t 	*newstate;
	char		l[MAXMSG];
	host_t		*host;
	struct stat 	log_st;
	time_t		now = time(NULL);

	statuscount++;
	dprintf("init_state(%s, %d, ...)\n", textornull(filename), dopurple);

	*is_purple = 0;

	/* Ignore summary files and dot-files (this catches "." and ".." also) */
	if ( (strncmp(filename, "summary.", 8) == 0) || (filename[0] == '.')) {
		return NULL;
	}

	/* Check that we can access this file */
	if ( (stat(filename, &log_st) == -1)       || 
	     (!S_ISREG(log_st.st_mode))            ||
	     ((fd = fopen(filename, "r")) == NULL)   ) {
		errprintf("Weird file BBLOGS/%s skipped\n", filename);
		return NULL;
	}

	/* Pick out host- and test-name */
	hostname = malcop(filename);
	p = strrchr(hostname, '.');

	/* Skip files that have no '.' in filename */
	if (p) {
		/* Pick out the testname ... */
		*p = '\0'; p++;
		testname = malcop(p);

		/* ... and change hostname back into normal form */
		for (p=hostname; (*p); p++) {
			if (*p == ',') *p='.';
		}
	}
	else {
		free(hostname);
		fclose(fd);
		return NULL;
	}

	sprintf(l, ",%s,", testname);
	if (ignorecolumns && strstr(ignorecolumns, l)) {
		free(hostname);
		free(testname);
		fclose(fd);
		return NULL;	/* Ignore this type of test */
	}

	newstate = malloc(sizeof(state_t));
	newstate->entry = malloc(sizeof(entry_t));
	newstate->next = NULL;

	newstate->entry->column = find_or_create_column(testname);
	newstate->entry->color = -1;
	strcpy(newstate->entry->age, "");
	newstate->entry->oldage = 0;
	newstate->entry->propagate = 1;

	host = find_host(hostname);
	if (host) {
		newstate->entry->alert = checkalert(host->alerts, testname);
		newstate->entry->onwap = checkalert(host->waps, testname);
	}
	else {
		dprintf("   hostname %s not found\n", hostname);
		newstate->entry->alert = newstate->entry->onwap = 0;
	}

	newstate->entry->sumurl = NULL;

	if (fgets(l, sizeof(l), fd)) {
		newstate->entry->color = parse_color(l);
	}

	if ( (log_st.st_mtime <= now) && (strcmp(testname, larrdcol) != 0) && (strcmp(testname, infocol) != 0) ) {
		/* Log file too old = go purple */

		*is_purple = 1;
		purplecount++;

		if (host && host->dialup) {
			/* Dialup hosts go clear, not purple */
			newstate->entry->color = COL_CLEAR;
		}
		else {
			/* Not in bb-hosts, or logfile too old */
			newstate->entry->color = COL_PURPLE;
		}
	}

	/* Acked column ? */
	if (newstate->entry->color != COL_GREEN) {
		struct stat ack_st;
		char ackfilename[MAX_PATH];

		sprintf(ackfilename, "%s/ack.%s.%s", getenv("BBACKS"), hostname, testname);
		newstate->entry->acked = (stat(ackfilename, &ack_st) == 0);
	}
	else {
		newstate->entry->acked = 0;
	}

	newstate->entry->propagate = checkpropagation(host, testname, newstate->entry->color);

	if (dopurple && *is_purple) {
		/* Send a message to update status to purple */

		char *p;
		char *purplemsg;
		int bufleft = log_st.st_size + 1024;

		init_status(newstate->entry->color);

		for (p = strchr(l, ' '); (p && (*p == ' ')); p++); /* Skip old color */

		purplemsg = malloc(bufleft);
		sprintf(purplemsg, "status+%d %s.%s %s %s", purpledelay,
			commafy(hostname), testname,
                        colorname(newstate->entry->color), (p ? p : ""));
		bufleft -= strlen(purplemsg);

		if (host) {
			while (fgets(l, sizeof(l), fd)) {
				if (strncmp(l, "Status unchanged", 16) == 0) {
					char *p;

					p = strchr(l, '\n'); if (p) *p = '\0';
					strncat(newstate->entry->age, l+20, sizeof(newstate->entry->age)-1);
					newstate->entry->oldage = (strstr(l+20, "days") != NULL);
				}
				else if ( (strncmp(l, "Encrypted status message", 24) != 0)  &&
				          (strncmp(l, "Status message received from", 28) != 0) ) {
					strncat(purplemsg, l, bufleft);
				}
			}
			/* Avoid newlines piling up at end of logfile */
			for (p = purplemsg + strlen(purplemsg) - 1; 
				((p > purplemsg) && ((*p == '\n') || (*p == '\r')) ); p--) ;
			if (p>purplemsg) *(p+1) = '\0';
			strcat(purplemsg, "\n\n");
		}
		else {
			/* No longer in bb-hosts */
			sprintf(l, "%s\n\n", hostname);
			strcat(purplemsg, l);

			sprintf(l, "This entry is no longer listed in %s/etc/bb-hosts.  To remove this\n",
				getenv("BBHOME"));
			strcat(purplemsg, l);

			sprintf(l, "purple message, please delete the log files for this host located in\n");
			strcat(purplemsg, l);

			sprintf(l, "%s, %s and %s if this host is no longer monitored.\n",
				getenv("BBLOGS"), getenv("BBHIST"), getenv("BBHISTLOGS"));
			strcat(purplemsg, l);
		}

		addtostatus(purplemsg);
		free(purplemsg);
		finish_status();
	}
	else {
		if (*is_purple) {
			/* 
			 * dopurple is false, so we are not updating purple messages.
			 * That means we can use the age of the log file as an indicator
			 * for how old this status message really is.
			 */

			time_t fileage = (now - log_st.st_mtime);

			newstate->entry->oldage = (fileage >= 86400);
			if (fileage >= 86400)
				sprintf(newstate->entry->age, "%.2f days", (fileage / 86400.0));
			else if (fileage > 3600)
				sprintf(newstate->entry->age, "%.2f hours", (fileage / 3600.0));
			else
				sprintf(newstate->entry->age, "%.2f minutes", (fileage / 60.0));
		}
		else {
			if ((strcmp(testname, larrdcol) != 0) && (strcmp(testname, infocol) != 0)) {
				while (fgets(l, sizeof(l), fd) && (strncmp(l, "Status unchanged in ", 20) != 0)) ;

				if (strncmp(l, "Status unchanged in ", 20) == 0) {
					char *p;

					p = strchr(l, '\n'); if (p) *p = '\0';
					strncat(newstate->entry->age, l+20, sizeof(newstate->entry->age)-1);
					newstate->entry->oldage = (strstr(l+20, "days") != NULL);
				}
			}
			else {
				newstate->entry->oldage = 1;
			}
		}
	}

	dprintf("init_state: hostname=%s, testname=%s, color=%d, acked=%d, age=%s, oldage=%d, propagate=%d, alert=%d, *is_purple=%d\n",
		textornull(hostname), textornull(testname), 
		newstate->entry->color, newstate->entry->acked,
		textornull(newstate->entry->age), newstate->entry->oldage,
		newstate->entry->propagate, newstate->entry->alert, *is_purple);

	if (host) {
        	hostlist_t      *l;

		/* There may be multiple host entries, if a host is
		 * listed in several locations in bb-hosts (for display purposes).
		 * This is handled by updating ALL of the hostrecords that match
		 * this hostname, instead of just the one found by find_host().
		 * Bug reported by Bluejay Adametz of Fuji.
		 */
		newstate->entry->next = host->entries;
		for (l=hosthead; (l); l = l->next) {
			if (strcmp(l->hostentry->hostname, host->hostname) == 0) {
				l->hostentry->entries = newstate->entry;
			}
		}

	}
	else {
		/* No host for this test - must be missing from bb-hosts */
		newstate->entry->next = NULL;
	}

	free(hostname);
	free(testname);
	fclose(fd);

	return newstate;
}

summary_t *init_summary(char *name, char *receiver, char *url)
{
	summary_t *newsum;

	dprintf("init_summary(%s, %s, %s)\n", textornull(name), textornull(receiver), textornull(url));

	/* Sanity check */
	if ((name == NULL) || (receiver == NULL) || (url == NULL)) 
		return NULL;

	newsum = malloc(sizeof(summary_t));
	newsum->name = malcop(name);
	newsum->receiver = malcop(receiver);
	newsum->url = malcop(url);
	newsum->next = NULL;

	return newsum;
}

dispsummary_t *init_displaysummary(char *fn)
{
	FILE *fd;
	char sumfn[MAX_PATH];
	struct stat st;
	char l[MAX_LINE_LEN];
	dispsummary_t *newsum = NULL;

	dprintf("init_displaysummary(%s)\n", textornull(fn));

	sprintf(sumfn, "%s/%s", getenv("BBLOGS"), fn);

	/* Check that we can access this file */
	if ( (stat(sumfn, &st) == -1)          || 
	     (!S_ISREG(st.st_mode))            ||     /* Not a regular file */
	     ((fd = fopen(sumfn, "r")) == NULL)   ) {
		errprintf("Weird summary file BBLOGS/%s skipped\n", fn);
		return NULL;
	}

	if (st.st_mtime < time(NULL)) {
		/* Stale summary file - ignore and delete */
		errprintf("Stale summary file BBLOGS/%s - deleted\n", fn);
		unlink(sumfn);
		return NULL;
	}

	if (fgets(l, sizeof(l), fd)) {
		char *p, *rowcol;
		char *color = malloc(strlen(l));

		newsum = malloc(sizeof(dispsummary_t));
		newsum->url = malloc(strlen(l));

		sscanf(l, "%s %s", color, newsum->url);

		if (strncmp(color, "green", 5) == 0) {
			newsum->color = COL_GREEN;
		}
		else if (strncmp(color, "yellow", 6) == 0) {
			newsum->color = COL_YELLOW;
		}
		else if (strncmp(color, "red", 3) == 0) {
			newsum->color = COL_RED;
		}
		else if (strncmp(color, "blue", 4) == 0) {
			newsum->color = COL_BLUE;
		}
		else if (strncmp(color, "clear", 5) == 0) {
			newsum->color = COL_CLEAR;
		}
		else if (strncmp(color, "purple", 6) == 0) {
			newsum->color = COL_PURPLE;
		}

		rowcol = malloc(strlen(fn) + 1);
		strcpy(rowcol, fn+8);
		p = strrchr(rowcol, '.');
		if (p) *p = ' ';

		newsum->column = malloc(strlen(rowcol)+1);
		newsum->row = malloc(strlen(rowcol)+1);
		sscanf(rowcol, "%s %s", newsum->row, newsum->column);
		newsum->next = NULL;

		free(color);
		free(rowcol);
	}
	else {
		errprintf("Read error reading from file %s\n", sumfn);
		newsum = NULL;
	}


	fclose(fd);
	return newsum;
}

void getnamelink(char *l, char **name, char **link)
{
	/* "page NAME title-or-link" splitup */
	unsigned char *p;

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
	unsigned char *p;
	char *parentname;
	pagelist_t *walk;

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
		unsigned char *p;

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
	char		fn[MAX_PATH];
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
	char dirname[MAX_PATH];
	char *p;

	dprintf("load_all_links()\n");

	strcpy(dirname, getenv("BBNOTES"));
	head1 = load_links(dirname, "notes");

	/* Change xxx/xxx/xxx/notes into xxx/xxx/xxx/help */
	p = strrchr(dirname, '/'); *p = '\0'; strcat(dirname, "/help");
	head2 = load_links(dirname, "help");

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


bbgen_page_t *load_bbhosts(char *pgset)
{
	FILE 	*bbhosts;
	char 	l[MAX_LINE_LEN];
	char	pagetag[100], subpagetag[100], subparenttag[100], 
		grouptag[100], summarytag[100], titletag[100], hosttag[100];
	char 	*name, *link, *onlycols;
	char 	hostname[MAX_LINE_LEN];
	bbgen_page_t 	*toppage, *curpage, *cursubpage, *cursubparent;
	group_t *curgroup;
	host_t	*curhost;
	char	*curtitle;
	int	ip1, ip2, ip3, ip4;
	char	*p;

	dprintf("load_bbhosts(pgset=%s)\n", textornull(pgset));

	bbhosts = stackfopen(getenv("BBHOSTS"), "r");
	if (bbhosts == NULL) {
		errprintf("Cannot open the BBHOSTS file '%s'\n", getenv("BBHOSTS"));
		exit(1);
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

	while (stackfgets(l, sizeof(l), "include")) {
		p = strchr(l, '\n'); 
		if (p) {
			*p = '\0'; 
		}
		else {
			errprintf("Warning: Lines in bb-hosts too long or has no newline: '%s'\n", l);
			fflush(stdout);
		}

		dprintf("load_bbhosts: -- got line '%s'\n", l);

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
		else if (sscanf(l, "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {
			int dialup = 0;
			char *startoftags = strchr(l, '#');
			char *alertlist, *onwaplist, *nopropyellowlist, *nopropredlist;
			char *larrdgraphs;
			char *targetpagename;

			alertlist = onwaplist = nopropyellowlist = nopropredlist = larrdgraphs = NULL;

			if (startoftags && strstr(startoftags, " dialup")) dialup=1;

			if (startoftags && (alertlist = strstr(startoftags, "NK:"))) {
				alertlist += 3;
			}
			if (startoftags && (onwaplist = strstr(startoftags, "WAP:"))) {
				onwaplist += 3;
			}
			else onwaplist = alertlist;

			if (startoftags && (nopropyellowlist = strstr(startoftags, "NOPROP:"))) {
				nopropyellowlist += 7;
			}

			if (startoftags && (nopropyellowlist = strstr(startoftags, "NOPROPYELLOW:"))) {
				nopropyellowlist += 7;
			}

			if (startoftags && (nopropredlist = strstr(startoftags, "NOPROPRED:"))) {
				nopropredlist += 10;
			}
			if (startoftags && (larrdgraphs = strstr(startoftags, "LARRD:"))) {
				larrdgraphs += 6;
			}

			if (strlen(pgset) == 0) {
				/*
				 * Default pageset generated. Put the host into
				 * whatever group or page is current.
				 */
				if (curhost == NULL) {
					curhost = init_host(hostname, ip1, ip2, ip3, ip4, dialup, alertlist, onwaplist,
							    startoftags, nopropyellowlist, nopropredlist,
							    larrdgraphs);
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
					curhost = curhost->next = init_host(hostname, ip1, ip2, ip3, ip4, dialup, 
									    alertlist, onwaplist,
									    startoftags, nopropyellowlist,nopropredlist,
									    larrdgraphs);
				}
				curhost->parent = (cursubparent ? cursubparent : (cursubpage ? cursubpage : curpage));
				if (curtitle) { curhost->pretitle = curtitle; curtitle = NULL; }
			}
			else if (startoftags && ((targetpagename = strstr(startoftags, hosttag)) != NULL)) {

				char savechar;
				pagelist_t *targetpage = NULL;
				int wantedgroup = 0;

				/* Put the host into the page specified by the PGSET: tag */
				targetpagename += strlen(hosttag);
				for (p=targetpagename; (*p && (*p != ' ') && (*p != '\t') && (*p != ',')); p++) ;
				savechar = *p; *p = '\0';

				if (savechar == ',') {
					wantedgroup = atoi(p+1);
				}

				/* Find the page */
				for (targetpage = pagelisthead; (targetpage && (strcmp(targetpagename, targetpage->pageentry->name) != 0)); targetpage = targetpage->next) ;

				*p = savechar;
				if (targetpage == NULL) {
					errprintf("Warning: Cannot find any target page named %s - dropping host %s'\n", 
						targetpagename, hostname);
				}
				else {
					host_t *newhost = init_host(hostname, ip1, ip2, ip3, ip4, dialup, 
								    alertlist, onwaplist,
								    startoftags, nopropyellowlist,nopropredlist,
								    larrdgraphs);

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
					if (curtitle) { newhost->pretitle = curtitle; curtitle = NULL; }
				}
			}
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
			curtitle = malcop(skipwhitespace(skipword(l)));
		}
		else {
		};
	}

	stackfclose(bbhosts);
	return toppage;
}


state_t *load_state(dispsummary_t **sumhead)
{
	DIR		*bblogs;
	struct dirent 	*d;
	char		fn[MAX_PATH];
	state_t		*newstate, *topstate;
	dispsummary_t	*newsum, *topsum;
	int		dopurple;
	struct stat	st;
	int		purplecount = 0;
	int		is_purple;

	dprintf("load_state()\n");

	chdir(getenv("BBLOGS"));
	if (stat(".bbstartup", &st) == -1) {
		/* Do purple if no ".bbstartup" file */
		dopurple = enable_purpleupd;
	}
	else {
		time_t now;

		/* Starting up - don't do purple hosts ("avoid purple explosion on startup") */
		dopurple = 0;

		/* Check if enough time has passed to remove the startup file */
		time(&now);
		if ((now - st.st_mtime) > 300) {
			remove(".bbstartup");
		}
	}

	if (dopurple) combo_start();

	topstate = NULL;
	topsum = NULL;

	bblogs = opendir(getenv("BBLOGS"));
	if (!bblogs) {
		errprintf("No logs! Cannot cd to the BBLOGS directory %s\n", getenv("BBLOGS"));
		exit(1);
	}

	while ((d = readdir(bblogs))) {
		strcpy(fn, d->d_name);

		if (strncmp(fn, "summary.", 8) == 0) {
			newsum = init_displaysummary(fn);
			if (newsum) {
				newsum->next = topsum;
				topsum = newsum;
			}
		}
		else {
			is_purple = 0;

			newstate = init_state(fn, dopurple, &is_purple);
			if (newstate) {
				newstate->next = topstate;
				topstate = newstate;
			}

			if (dopurple) {
				if (is_purple) purplecount++;
				if (purplecount > MAX_PURPLE_PER_RUN) {
					dopurple = 0;
					errprintf("%s : Too many purple updates (>%d) - disabling updates for purple logs\n", 
						timestamp, MAX_PURPLE_PER_RUN);
				}
			}
		}
	}

	closedir(bblogs);

	if (dopurple) combo_end();

	*sumhead = topsum;
	return topstate;
}

