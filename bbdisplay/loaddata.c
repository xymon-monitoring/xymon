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

static char rcsid[] = "$Id: loaddata.c,v 1.21 2003-01-05 08:19:10 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <wait.h>

#include "bbgen.h"
#include "util.h"
#include "loaddata.h"
#include "debug.h"

char    larrdcol[20] = "larrd";
int     enable_purpleupd = 1;
link_t  null_link = { "", "", "", NULL };	/* Null link for pages/hosts/whatever with no link */
col_t   null_column = { "", NULL };		/* Null column */

char	*rrdnames[] = { 
        "la",
        "disk",
        "memory",
        "tcp",
        "citrix",
        "users",
        "vmstat",
        "netstat",
        "iostat",
	"ntpstat",
        NULL
};


page_t *init_page(const char *name, const char *title)
{
	page_t *newpage = malloc(sizeof(page_t));

	strcpy(newpage->name, name);
	strcpy(newpage->title, title);
	newpage->color = -1;
	newpage->next = NULL;
	newpage->subpages = NULL;
	newpage->groups = NULL;
	newpage->hosts = NULL;
	newpage->parent = NULL;
	return newpage;
}

group_t *init_group(const char *title, const char *onlycols)
{
	group_t *newgroup = malloc(sizeof(group_t));

	strcpy(newgroup->title, title);
	if (onlycols && (strlen(onlycols))) {
		newgroup->onlycols = malloc(strlen(onlycols)+3); /* Add a '|' at start and end */
		sprintf(newgroup->onlycols, "|%s|", onlycols);
	}
	else newgroup->onlycols = NULL;
	newgroup->hosts = NULL;
	newgroup->next = NULL;
	return newgroup;
}

host_t *init_host(const char *hostname, const int ip1, const int ip2, const int ip3, const int ip4, 
		  const int dialup, const char *alerts)
{
	host_t 		*newhost = malloc(sizeof(host_t));
	hostlist_t	*newlist = malloc(sizeof(hostlist_t));

	strcpy(newhost->hostname, hostname);
	sprintf(newhost->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
	newhost->link = find_link(hostname);
	newhost->entries = NULL;
	newhost->color = -1;
	newhost->dialup = dialup;
	if (alerts) {
		newhost->alerts = malloc(strlen(alerts)+1);
		strcpy(newhost->alerts, alerts);
	}
	else {
		newhost->alerts = NULL;
	}
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

	newlink = malloc(sizeof(link_t));
	strcpy(newlink->filename, filename);
	strcpy(newlink->urlprefix, urlprefix);
	newlink->next = NULL;

	p = strrchr(filename, '.');
	if (p == NULL) p = (filename + strlen(filename));

	if ( (strcmp(p, ".php") == 0)    ||
             (strcmp(p, ".php3") == 0)   ||
	     (strcmp(p, ".shtml") == 0)  ||
	     (strcmp(p, ".html") == 0)   ||
	     (strcmp(p, ".htm") == 0))      
	{
		*p = '\0';
	}

	strcpy(newlink->name, filename);  /* Without extension, this time */

	return newlink;
}

col_t *find_or_create_column(const char *testname)
{
	col_t	*newcol;

	for (newcol = colhead; (newcol && (strcmp(testname, newcol->name) != 0)); newcol = newcol->next);
	if (newcol == NULL) {
		newcol = malloc(sizeof(col_t));
		strcpy(newcol->name, testname);
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

	return newcol;
}

state_t *init_state(const char *filename, int dopurple)
{
	FILE *fd;
	char	*p;
	char	hostname[60];
	char	testname[20];
	char	ackfilename[256];
	state_t *newstate;
	char	l[200];
	host_t	*host;
	struct stat st;
	time_t	now = time(NULL);

	char	bbcmd[250];
	char	bbdispaddr[20];

	/* Ignore summary files and dot-files */
	if ( (strncmp(filename, "summary.", 8) == 0) || (filename[0] == '.')) {
		return NULL;
	}

	p = getenv("BB"); if (p) strcpy(bbcmd, p);
	p = getenv("MACHINEADDR"); if (p) strcpy(bbdispaddr, p);
	if ( (strlen(bbcmd) == 0) || (strlen(bbdispaddr) == 0) ) {
		printf("BB and MACHINEADDR not found in environment\n");
		exit(1);
	}

	/* Pick out host- and test-name */
	strcpy(hostname, filename);
	p = strrchr(hostname, '.');

	/* Skip files that have no '.' in filename */
	if (p) {
		/* Pick out the testname ... */
		*p = '\0';
		strcpy(testname, p+1);

		/* ... and change hostname back into normal form */
		for (p=hostname; (*p); p++) {
			if (*p == ',') *p='.';
		}
	}
	else {
		return NULL;
	}

	newstate = malloc(sizeof(state_t));
	newstate->entry = malloc(sizeof(entry_t));
	newstate->next = NULL;

	newstate->entry->column = find_or_create_column(testname);
	newstate->entry->color = -1;
	strcpy(newstate->entry->age, "");
	newstate->entry->oldage = 0;

	/* Acked column ? */
	sprintf(ackfilename, "%s/ack.%s.%s", getenv("BBACKS"), hostname, testname);
	newstate->entry->acked = (stat(ackfilename, &st) == 0);

	host = find_host(hostname);
	newstate->entry->alert = checkalert(host, testname);
	newstate->entry->sumurl = NULL;

	stat(filename, &st);
	fd = fopen(filename, "r");

	if (fd && fgets(l, sizeof(l), fd)) {
		if (strncmp(l, "green ", 6) == 0) {
			newstate->entry->color = COL_GREEN;
		}
		else if (strncmp(l, "yellow ", 7) == 0) {
			newstate->entry->color = COL_YELLOW;
		}
		else if (strncmp(l, "red ", 4) == 0) {
			newstate->entry->color = COL_RED;
		}
		else if (strncmp(l, "blue ", 5) == 0) {
			newstate->entry->color = COL_BLUE;
		}
		else if (strncmp(l, "clear ", 6) == 0) {
			newstate->entry->color = COL_CLEAR;
		}
		else if (strncmp(l, "purple ", 7) == 0) {
			newstate->entry->color = COL_PURPLE;
		}
	}

	if (dopurple && (st.st_mtime <= now) && (strcmp(testname, larrdcol) != 0)) {
		/* PURPLE test! */

		char *p;
		char *purplemsg = malloc(st.st_size+1024);
		char msgline[200];

		if (host && host->dialup) {
			/* Dialup hosts go clear, not purple */
			newstate->entry->color = COL_CLEAR;
		}
		else {
			/* Not in bb-hosts, or logfile too old */
			newstate->entry->color = COL_PURPLE;
		}

		p = strchr(l, ' '); /* Skip old color */
		sprintf(purplemsg, "status+0 %s.%s %s %s", commafy(hostname), testname,
                        colorname(newstate->entry->color), p);

		if (host) {
			while (fgets(l, sizeof(l), fd)) {
				if ( (strncmp(l, "Status unchanged", 16) != 0) &&
				     (strncmp(l, "Encrypted status message", 24) != 0)  &&
				     (strncmp(l, "Status message received from", 28) != 0) ) {
					strcat(purplemsg, l);
				}
			}
		}
		else {
			/* No longer in bb-hosts */
			sprintf(msgline, "%s\n\n", hostname);
			strcat(purplemsg, msgline);

			sprintf(msgline, "This entry is no longer listed in %s/etc/bb-hosts.  To remove this\n",
				getenv("BBHOME"));
			strcat(purplemsg, msgline);

			sprintf(msgline, "purple message, please delete the log files for this host located in\n");
			strcat(purplemsg, msgline);

			sprintf(msgline, "%s, %s and %s if this host is no longer monitored.\n",
				getenv("BBLOGS"), getenv("BBHIST"), getenv("BBHISTLOGS"));
			strcat(purplemsg, msgline);
		}

		fclose(fd);

		combo_add(purplemsg);
		free(purplemsg);
	}
	else {
		if (strcmp(testname, larrdcol) != 0) {
			while (fgets(l, sizeof(l), fd) && (strncmp(l, "Status unchanged in ", 20) != 0)) ;

			if (strncmp(l, "Status unchanged in ", 20) == 0) {
				char *p;

				p = strchr(l, '\n'); if (p) *p = '\0';
				strcpy(newstate->entry->age, l+20);
				newstate->entry->oldage = (strstr(l+20, "days") != NULL);
			}
		}
		else {
			newstate->entry->oldage = 1;
		}

		fclose(fd);
	}


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
			if (strcmp(l->hostentry->hostname, host->hostname) == 0) 
				l->hostentry->entries = newstate->entry;
		}

	}
	else {
		/* No host for this test - must be missing from bb-hosts */
		newstate->entry->next = NULL;
	}

	return newstate;
}

summary_t *init_summary(char *name, char *receiver, char *url)
{
	summary_t *newsum;

	newsum = malloc(sizeof(summary_t));
	strcpy(newsum->name, name);
	strcpy(newsum->receiver, receiver);
	strcpy(newsum->url, url);
	newsum->next = NULL;

	return newsum;
}

dispsummary_t *init_displaysummary(char *fn)
{
	FILE *fd;
	char sumfn[256];
	char color[20];
	dispsummary_t *newsum = NULL;
	char *p, rowcol[200];
	struct stat st;

	sprintf(sumfn, "%s/%s", getenv("BBLOGS"), fn);
	stat(sumfn, &st);
	if (st.st_mtime < time(NULL)) {
		/* Drop old summaries */
		unlink(sumfn);
		return NULL;
	}

	fd = fopen(sumfn, "r");
	if (fd) {
		newsum = malloc(sizeof(dispsummary_t));

		fscanf(fd, "%s %s", color, newsum->url);
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

		strcpy(rowcol, fn+8);
		p = strrchr(rowcol, '.');
		if (p) *p = ' ';

		sscanf(rowcol, "%s %s", newsum->row, newsum->column);
		newsum->next = NULL;
		fclose(fd);
	}

	return newsum;
}

void getnamelink(char *l, char **name, char **link)
{
	unsigned char *p;

	*name = "";
	*link = "";

	/* Find first space and skip spaces */
	for (p=strchr(l, ' '); (p && (isspace (*p))); p++) ;

	*name = p; p = strchr(*name, ' ');
	if (p) {
		*p = '\0'; /* Null-terminate pagename */
		for (p++; (isspace(*p)); p++) ;
		*link = p;
	}
}


void getgrouptitle(char *l, char **title, char **onlycols)
{
	unsigned char *p;

	*title = "";
	*onlycols = NULL;

	if (strncmp(l, "group-only", 10) == 0) {
		/* Find first space and skip spaces */
		for (p=strchr(l, ' '); (p && (isspace (*p))); p++) ;
		*onlycols = p;

		/* Find next space and skip spaces */
		p = strchr(*onlycols, ' ');
		if (p) {
			*p = '\0';
			for (p++; isspace (*p); p++) ;
			*title = p;
		}
	}
	else if (strncmp(l, "group", 5) == 0) {
		/* Find first space and skip spaces */
		for (p=strchr(l, ' '); (p && (isspace (*p))); p++) ;

		*title = p;
	}
}

link_t *load_links(const char *directory, const char *urlprefix)
{
	DIR		*bblinks;
	struct dirent 	*d;
	char		fn[256];
	link_t		*curlink, *toplink, *newlink;

	toplink = curlink = NULL;
	bblinks = opendir(directory);
	if (!bblinks) {
		perror("Cannot read directory");
		exit(1);
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
	char dirname[200];
	char *p;

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


page_t *load_bbhosts(void)
{
	FILE 	*bbhosts;
	char 	l[200];
	char 	*name, *link, *onlycols;
	char 	hostname[65];
	page_t 	*toppage, *curpage, *cursubpage;
	group_t *curgroup;
	host_t	*curhost;
	int	ip1, ip2, ip3, ip4;
	char	*p;


	bbhosts = fopen(getenv("BBHOSTS"), "r");
	if (bbhosts == NULL)
		exit(1);

	curpage = toppage = init_page("", "");
	cursubpage = NULL;
	curgroup = NULL;
	curhost = NULL;

	while (fgets(l, sizeof(l), bbhosts)) {
		p = strchr(l, '\n'); if (p) { *p = '\0'; };

		if ((l[0] == '#') || (strlen(l) == 0)) {
			/* Do nothing - it's a comment */
		}
		else if (strncmp(l, "page", 4) == 0) {
			getnamelink(l, &name, &link);
			curpage->next = init_page(name, link);
			curpage = curpage->next;
			cursubpage = NULL;
			curgroup = NULL;
			curhost = NULL;
		}
		else if (strncmp(l, "subpage", 7) == 0) {
			getnamelink(l, &name, &link);
			if (cursubpage == NULL) {
				cursubpage = curpage->subpages = init_page(name, link);
			}
			else {
				cursubpage = cursubpage->next = init_page(name, link);
			}
			cursubpage->parent = curpage;
			curgroup = NULL;
			curhost = NULL;
		}
		else if (strncmp(l, "group", 5) == 0) {

			getgrouptitle(l, &link, &onlycols);
			if (curgroup == NULL) {
				curgroup = init_group(link, onlycols);
				if (cursubpage == NULL) {
					/* We're on a main page */
					curpage->groups = curgroup;
				}
				else {
					/* We're in a subpage */
					cursubpage->groups = curgroup;
				}
			}
			else {
				curgroup->next = init_group(link, onlycols);
				curgroup = curgroup->next;
			}
			curhost = NULL;
		}
		else if (sscanf(l, "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {
			int dialup = 0;
			char *p = strchr(l, '#');
			char *alertlist;

			if (p && strstr(p, " dialup")) dialup=1;

			if (p && (alertlist = strstr(p, "NK:"))) {
				alertlist += 2;
				*alertlist = ',';
				p = strchr(alertlist, ' ');
				if (p) {
					*p = ',';
				}
				else {
					strcat(alertlist, ",");
				}
			}

			if (curhost == NULL) {
				curhost = init_host(hostname, ip1, ip2, ip3, ip4, dialup, alertlist);
				if (curgroup != NULL) {
					curgroup->hosts = curhost;
				} else if (cursubpage != NULL) {
					cursubpage->hosts = curhost;
				}
				else if (curpage != NULL) {
					curpage->hosts = curhost;
				}
				else {
					/* Should not happen! */
					printf("Nowhere to put the host %s\n", hostname);
				}
			}
			else {
				curhost = curhost->next = init_host(hostname, ip1, ip2, ip3, ip4, dialup, alertlist);
			}
			curhost->parent = (cursubpage ? cursubpage : curpage);
		}
		else if (strncmp(l, "summary", 7) == 0) {
			/* summary row.column      IP-ADDRESS-OF-PARENT    http://bb4.com/ */
			char sumname[100];
			char receiver[60];
			char url[250];
			summary_t *newsum;

			if (sscanf(l, "summary %s %s %s", sumname, receiver, url) == 3) {
				newsum = init_summary(sumname, receiver, url);
				newsum->next = sumhead;
				sumhead = newsum;
			}
		}
		else {
		};
	}

	fclose(bbhosts);
	return toppage;
}


state_t *load_state(void)
{
	DIR		*bblogs;
	struct dirent 	*d;
	char		fn[256];
	state_t		*newstate, *topstate;
	int		dopurple;
	struct stat	st;

	chdir(getenv("BBLOGS"));
	if (stat(".bbstartup", &st) == -1) {
		/* Do purple if no ".bbstartup" file */
		dopurple = enable_purpleupd;
	}
	else {
		/* Don't do purple hosts ("avoid purple explosion on startup") */
		dopurple = 0;
		remove(".bbstartup");
	}

	if (dopurple) combo_start();

	topstate = NULL;
	bblogs = opendir(getenv("BBLOGS"));
	if (!bblogs) {
		perror("No logs!");
		exit(1);
	}

	while ((d = readdir(bblogs))) {
		strcpy(fn, d->d_name);
		if (fn[0] != '.') {
			newstate = init_state(fn, dopurple);
			if (newstate) {
				newstate->next = topstate;
				topstate = newstate;
			}
		}
	}

	closedir(bblogs);

	if (dopurple) combo_end();

	return topstate;
}

dispsummary_t *load_summaries(void)
{
	DIR		*bblogs;
	struct dirent 	*d;
	char		fn[256];
	dispsummary_t	*newsum, *head;

	head = NULL;

	bblogs = opendir(getenv("BBLOGS"));
	if (!bblogs) {
		perror("No logs!");
		exit(1);
	}

	while ((d = readdir(bblogs))) {
		strcpy(fn, d->d_name);
		if (strncmp(fn, "summary.", 8) == 0) {
			newsum = init_displaysummary(fn);
			if (newsum) {
				newsum->next = head;
				head = newsum;
			}
		}
	}

	closedir(bblogs);

	return head;
}

