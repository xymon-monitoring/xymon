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

page_t		*pagehead = NULL;

link_t  	*linkhead = NULL;
link_t		null_link = { "", "", "", NULL };

hostlist_t	*hosthead = NULL;
state_t		*statehead = NULL;
col_t   	*colhead = NULL;
col_t		null_column = { "", NULL };


col_list_t *gen_column_list(host_t *hostlist, int crit_only)
{
#undef DEBUG
	/*
	 * Build a list of the columns that are in use by
	 * hosts in the hostlist passed as parameter.
	 * The column list must be sorted by column name.
	 */

	col_list_t	*head;
	host_t		*h;
	entry_t		*e;
	col_list_t	*newlistitem, *collist_walk;

	/* Code de-obfuscation trick: Add a null record as the head item */
	/* Simplifies handling since head != NULL and we never have to insert at head of list */
	head = malloc(sizeof(col_list_t));
	head->column = &null_column;
	head->next = NULL;

	for (h = hostlist; (h); h = h->next) {
		for (e = h->entries; (e); e = e->next) {
			if ((!crit_only) || (e->color == COL_RED) || (e->color == COL_YELLOW) || (e->color == COL_PURPLE)) {
#ifdef DEBUG
				printf("Inserting %s\n", e->column->name);
#endif
				/* See where e->column should go in list */
				collist_walk = head; 
				while ( (collist_walk->next && 
                               		strcmp(e->column->name, ((col_list_t *)(collist_walk->next))->column->name) > 0) ) {
					collist_walk = collist_walk->next;
				}

#ifdef DEBUG
				printf("collist_walk is %s\n", collist_walk->column->name);
#endif
				if ((collist_walk->next == NULL) || ((col_list_t *)(collist_walk->next))->column != e->column) {
					/* collist_walk points to the entry before the new one */
					newlistitem = malloc(sizeof(col_list_t));
					newlistitem->column = e->column;
					newlistitem->next = collist_walk->next;
					collist_walk->next = newlistitem;
				}
#ifdef DEBUG
				{
					col_list_t *cl;
					for (cl = head; (cl); cl = cl->next) {
						printf("%s ", cl->column->name);
					}
					printf("\n");
				}
#endif
			}
		}
	}

#ifdef DEBUG
	printf("\n");
#endif

	/* Skip the dummy record */
	collist_walk = head; head = head->next; free(collist_walk);
	return (head);
}


char *colorname(int color)
{
	char *cs = "";

	switch (color) {
	  case COL_CLEAR:  cs = "clear"; break;
	  case COL_BLUE:   cs = "blue"; break;
	  case COL_PURPLE: cs = "purple"; break;
	  case COL_GREEN:  cs = "green"; break;
	  case COL_YELLOW: cs = "yellow"; break;
	  case COL_RED:    cs = "red"; break;
	}

	return cs;
}

char *dotgiffilename(entry_t *e)
{
	static char filename[20];

	strcpy(filename, colorname(e->color));
	if (e->acked) {
		strcat(filename, "-ack");
	}
	else {
		strcat(filename, (e->oldage ? "" : "-recent"));
	}
	strcat(filename, ".gif");

	return filename;
}

char *alttag(entry_t *e)
{
	static char tag[40];

	sprintf(tag, "%s:%s:", e->column->name, colorname(e->color));
	if (e->acked) {
		strcat(tag, "acked:");
	}
	strcat(tag, e->age);

	return tag;
}


char *commafy(char *hostname)
{
	static char s[256];
	char *p;

	strcpy(s, hostname);
	for (p = strchr(s, '.'); (p); p = strchr(s, '.')) *p = ',';
	return s;
}

void headfoot(FILE *output, char *pagetype, char *pagename, char *subpagename, char *head_or_foot, int bgcolor)
{
	int	fd;
	char 	filename[256];
	struct stat st;
	char	*template;
	char	*t_start, *t_next;
	char	savechar;
	time_t	now = time(NULL);

	sprintf(filename, "%s/web/%s_%s_%s", getenv("BBHOME"), pagename, subpagename, head_or_foot);
	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		sprintf(filename, "%s/web/%s_%s", getenv("BBHOME"), pagename, head_or_foot);
		fd = open(filename, O_RDONLY);
	}
	if (fd == -1) {
		sprintf(filename, "%s/web/%s_%s", getenv("BBHOME"), pagetype, head_or_foot);
		fd = open(filename, O_RDONLY);
	}

	if (fd != -1) {
		fstat(fd, &st);
		template = malloc(st.st_size + 1);
		read(fd, template, st.st_size);
		template[st.st_size] = '\0';
		close(fd);

		for (t_start = template, t_next = strchr(t_start, '&'); (t_next); ) {
			/* Copy from t_start to t_next unchanged */
			*t_next = '\0'; t_next++;
			fprintf(output, "%s", t_start);

			/* Find token */
			for (t_start = t_next; ((*t_next >= 'A') && (*t_next <= 'Z')); t_next++ ) ;
			savechar = *t_next; *t_next = '\0';

			if (strcmp(t_start, "BBREL") == 0)     fprintf(output, "%s", getenv("BBREL"));
			if (strcmp(t_start, "BBRELDATE") == 0) fprintf(output, "%s", getenv("BBRELDATE"));
			if (strcmp(t_start, "BBSKIN") == 0)    fprintf(output, "%s", getenv("BBSKIN"));
			if (strcmp(t_start, "BBWEB") == 0)     fprintf(output, "%s", getenv("BBWEB"));
			if (strcmp(t_start, "CGIBINURL") == 0) fprintf(output, "%s", getenv("CGIBINURL"));

			if (strcmp(t_start, "BBDATE") == 0)          fprintf(output, "%s", ctime(&now));
			if (strcmp(t_start, "BBBACKGROUND") == 0)    fprintf(output, "%s", colorname(bgcolor));
			
			*t_next = savechar; t_start = t_next; t_next = strchr(t_start, '&');
		}

		/* Remainder of file */
		fprintf(output, "%s", t_start);

		free(template);
	}
	else {
		fprintf(output, "<HTML><BODY> \n <HR size=4> \n <BR>%s is either missing or invalid, please create this file with your custom header<BR> \n<HR size=4>", filename);
	}
}


link_t *find_link(const char *name)
{
	link_t *l;

	for (l=linkhead; (l && (strcmp(l->name, name) != 0)); l = l->next);

	return (l ? l : &null_link);
}

char *columnlink(link_t *link, char *colname)
{
	static char linkurl[60];

	if (link != &null_link) {
		sprintf(linkurl, "%s/%s", link->urlprefix, link->filename);
	}
	else {
		sprintf(linkurl, "help/bb-help.html#%s", colname);
	}
	
	return linkurl;
}

char *hostlink(link_t *link)
{
	static char linkurl[60];

	if (link != &null_link) {
		sprintf(linkurl, "%s/%s", link->urlprefix, link->filename);
	}
	else {
		sprintf(linkurl, "bb.html");
	}

	return linkurl;
}


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
	return newpage;
}

group_t *init_group(const char *title)
{
	group_t *newgroup = malloc(sizeof(group_t));

	strcpy(newgroup->title, title);
	newgroup->hosts = NULL;
	newgroup->next = NULL;
	return newgroup;
}

host_t *init_host(const char *hostname, const int ip1, const int ip2, const int ip3, const int ip4, const int dialup)
{
	host_t 		*newhost = malloc(sizeof(host_t));
	hostlist_t	*newlist = malloc(sizeof(hostlist_t));

	strcpy(newhost->hostname, hostname);
	sprintf(newhost->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
	newhost->link = find_link(hostname);
	newhost->entries = NULL;
	newhost->color = -1;
	newhost->dialup = dialup;
	newhost->next = NULL;

	newlist->hostentry = newhost;
	newlist->next = hosthead;
	hosthead = newlist;

	return newhost;
}

host_t *find_host(const char *hostname)
{
	hostlist_t	*l;

	for (l=hosthead; (l && (strcmp(l->hostentry->hostname, hostname) != 0)); l = l->next) ;

	return (l ? l->hostentry : NULL);
}


link_t *init_link(const char *filename, const char *urlprefix)
{
	char *p;
	link_t *newlink = NULL;

	p = strrchr(filename, '.');
	if (p == NULL) return NULL;	/* Filename with no extension - not linkable */

	if ( (strcmp(p, ".php") == 0)   ||
	     (strcmp(p, ".html") == 0)  ||
	     (strcmp(p, ".htm") == 0)) {

		newlink = malloc(sizeof(link_t));
		strcpy(newlink->filename, filename);

		*p = '\0';
		strcpy(newlink->name, filename);  /* Without extension, this time */

		strcpy(newlink->urlprefix, urlprefix);
		newlink->next = NULL;
	}

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

	/* Ignore summary files and dot-files */
	if ( (strncmp(filename, "summary.", 8) == 0) || (filename[0] == '.')) {
		return NULL;
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

	if (dopurple && (st.st_mtime <= now)) {
		/* PURPLE test! */

		FILE *purplefile;
		char purplefilename[256];
		char fulllogfilename[256];
		char *p;

		if (host && host->dialup) {
			/* Dialup hosts go clear, not purple */
			newstate->entry->color = COL_CLEAR;
		}
		else {
			/* Not in bb-hosts, or logfile too old */
			newstate->entry->color = COL_PURPLE;
		}

		sprintf(fulllogfilename, "%s/%s", getenv("BBLOGS"), filename);
		sprintf(purplefilename, "%s/NEW.purple", getenv("BBTMP"));
		purplefile = fopen(purplefilename, "w");
		if (purplefile == NULL) {
			perror("Cannot open purplefile");
			exit(1);
		}
		p = strchr(l, ' '); /* Skip old color */
		fprintf(purplefile, "%s %s", colorname(newstate->entry->color), p);

		if (host) {
			while (fgets(l, sizeof(l), fd)) {
				if ( (strncmp(l, "Status unchanged", 16) != 0) &&
				     (strncmp(l, "Encrypted status message", 24) != 0)  &&
				     (strncmp(l, "Status message received from", 28) != 0) ) {
					fprintf(purplefile, "%s", l);
				}
			}
		}
		else {
			/* No longer in bb-hosts */
			fprintf(purplefile, "%s\n\n", hostname);
			fprintf(purplefile, "This entry is no longer listed in %s/etc/bb-hosts.  To remove this\n",
				getenv("BBHOME"));
			fprintf(purplefile, "purple message, please delete the log files for this host located in\n");
			fprintf(purplefile, "%s, %s and %s if this host is no longer monitored.\n",
				getenv("BBLOGS"), getenv("BBHIST"), getenv("BBHISTLOGS"));
		}

		fclose(fd);
		fclose(purplefile);
		rename(purplefilename, fulllogfilename);
	}
	else {
		while (fgets(l, sizeof(l), fd) && (strncmp(l, "Status unchanged in ", 20) != 0)) ;

		if (strncmp(l, "Status unchanged in ", 20) == 0) {
			char *p;

			p = strchr(l, '\n'); if (p) *p = '\0';
			strcpy(newstate->entry->age, l+20);
			newstate->entry->oldage = (strstr(l+20, "days") != NULL);
		}

		fclose(fd);
	}


	if (host) {
		newstate->hostname = host->hostname;
		newstate->entry->next = host->entries;
		host->entries = newstate->entry;
	}
	else {
		/* No host for this test - must be missing from bb-hosts */
		newstate->entry->next = NULL;

		/* Need to malloc() room for the hostname */
		newstate->hostname = malloc(strlen(hostname)+1);
		strcpy(newstate->hostname, hostname);
	}

	return newstate;
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


void getgrouptitle(char *l, char **title)
{
	unsigned char *p;

	*title = "";

	if (strncmp(l, "group-only", 10) == 0) {
		/* Find first space and skip spaces */
		for (p=strchr(l, ' '); (p && (isspace (*p))); p++) ;
		/* Find next space and skip spaces */
		for (p=strchr(p, ' '); (p && (isspace (*p))); p++) ;
		*title = p;
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
	char 	*name, *link;
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
	while (fgets(l, sizeof(l), bbhosts)) {
		p = strchr(l, '\n'); if (p) { *p = '\0'; };

		if (strncmp(l, "page", 4) == 0) {
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
			curgroup = NULL;
			curhost = NULL;
		}
		else if (strncmp(l, "group", 5) == 0) {
			getgrouptitle(l, &link);
			if (curgroup == NULL) {
				curgroup = init_group(link);
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
				curgroup->next = init_group(link);
				curgroup = curgroup->next;
			}
			curhost = NULL;
		}
		else if (sscanf(l, "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {
			int dialup = 0;
			char *p = strchr(l, '#');

			if (p && strstr(p, " dialup")) dialup=1;

			if (curhost == NULL) {
				curhost = init_host(hostname, ip1, ip2, ip3, ip4, dialup);
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
				curhost = curhost->next = init_host(hostname, ip1, ip2, ip3, ip4, dialup);
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
		dopurple = 1;
	}
	else {
		/* Don't do purple hosts ("avoid purple explosion on startup") */
		dopurple = 0;
		remove(".bbstartup");
	}

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

	return topstate;
}


void calc_hostcolors(hostlist_t *head)
{
	int		color;
	hostlist_t 	*h;
	entry_t		*e;

	for (h = head; (h); h = h->next) {
		color = 0;

		for (e = h->hostentry->entries; (e); e = e->next) {
			if (e->color > color) color = e->color;
		}

		/* Blue and clear is not propageted upwards */
		if ((color == COL_CLEAR) || (color == COL_BLUE)) color = COL_GREEN;

		h->hostentry->color = color;
	}
}

void calc_pagecolors(page_t *phead, char *indent1)
{
	page_t 	*p, *toppage;
	group_t *g;
	host_t  *h;
	int	color;
	char 	indent2[80];

	strcpy(indent2, indent1);
	strcat(indent2, "  >");

	for (toppage=phead; (toppage); toppage = toppage->next) {
		/* printf("%s Color: page=%s\n", indent1, toppage->name); */

		/* Start with the color of immediate hosts */
		color = (toppage->hosts ? toppage->hosts->color : -1);
		/* printf("%s Hostcolor %d\n",indent1 , color); */

		/* Then adjust with the color of hosts in immediate groups */
		for (g = toppage->groups; (g); g = g->next) {
			for (h = g->hosts; (h); h = h->next) {
				if (h->color > color) color = h->color;
			}
		}
		/* printf("%s Host+group color: %d\n", indent1, color); */

		/* Then adjust with the color of subpages, if any.  */
		/* These must be calculated first!                  */
		if (toppage->subpages) {
			/* printf("%s Calling calc of subpages\n", indent1); */
			calc_pagecolors(toppage->subpages, indent2);
		}

		/* printf("%s Subpages checked: ", indent1); */
		for (p = toppage->subpages; (p); p = p->next) {
			/* printf("%s ", p->name); */
			if (p->color > color) color = p->color;
		}
		/* printf("\n"); */

		toppage->color = color;
		/* printf("%s pagecolor: %d\n", indent1, color); */
	}
}


void delete_old_acks(void)
{
	DIR             *bbacks;
	struct dirent   *d;
	struct stat     st;
	time_t		now = time(NULL);
	char		fn[256];

	bbacks = opendir(getenv("BBACKS"));
	if (!bbacks) {
		perror("No BBACKS!");
		exit(1);
        }

	chdir(getenv("BBACKS"));
	while ((d = readdir(bbacks))) {
		strcpy(fn, d->d_name);
		if (strncmp(fn, "ack.", 4) == 0) {
			stat(fn, &st);
			if (S_ISREG(st.st_mode) && (st.st_mtime < now)) {
				unlink(fn);
			}
		}
	}
	closedir(bbacks);
}

void dumplinks(link_t *head)
{
	link_t *l;

	for (l = head; l; l = l->next) {
		printf("Link for host %s, URL/filename %s/%s\n", l->name, l->urlprefix, l->filename);
	}
}


void dumphosts(host_t *head, char *prefix)
{
	host_t *h;
	entry_t *e;
	char	format[80];

	strcpy(format, prefix);
	strcat(format, "Host: %s, ip: %s, color: %d, link: %s\n");

	for (h = head; (h); h = h->next) {
		printf(format, h->hostname, h->ip, h->color, h->link->filename);
		for (e = h->entries; (e); e = e->next) {
			printf("\t\t\t\t\tTest: %s, state %d, age: %s, oldage: %d\n", 
				e->column->name, e->color, e->age, e->oldage);
		}
	}
}

void dumpgroups(group_t *head, char *prefix, char *hostprefix)
{
	group_t *g;
	char    format[80];

	strcpy(format, prefix);
	strcat(format, "Group: %s\n");

	for (g = head; (g); g = g->next) {
		printf(format, g->title);
		dumphosts(g->hosts, hostprefix);
	}
}

void dumphostlist(hostlist_t *head)
{
	hostlist_t *h;

	for (h=head; (h); h=h->next) {
		printf("Hostlist entry: Hostname %s\n", h->hostentry->hostname);
	}
}


void dumpstatelist(state_t *head)
{
	state_t *s;

	for (s=statehead; (s); s=s->next) {
		printf("Host: %s, test:%s, state: %d, oldage: %d, age: %s\n",
			s->hostname,
			s->entry->column->name,
			s->entry->color,
			s->entry->oldage,
			s->entry->age);
	}
}

void dumpall(void)
{
	page_t *p, *q;

	for (p=pagehead; p; p = p->next) {
		printf("%sPage: %s, color: %d, title=%s\n", 
                       (strlen(p->name) == 0) ? "" : "    ", p->name, p->color, p->title);
		for (q = p->subpages; (q); q = q->next) {
			printf("\tSubpage: %s, color=%d, title=%s\n", q->name, q->color, q->title);
			dumpgroups(q->groups, "\t\t", "\t\t    ");
			dumphosts(q->hosts, "\t    ");
		}

		dumpgroups(p->groups, "\t","\t    ");
		dumphosts(p->hosts, "    ");
	}
	dumphosts(pagehead->hosts, "");
}


void do_hosts(host_t *head, FILE *output, char *grouptitle, int summarypage)
{
	host_t	*h;
	entry_t	*e;
	col_list_t *groupcols, *gc;
	int	genstatic;
	int	columncount;

	if (head == NULL)
		return;

	genstatic = ( (strcmp(getenv("BBLOGSTATUS"), "STATIC") == 0) ? 1 : 0);

	fprintf(output, "<A NAME=hosts-blk>&nbsp;</A>\n\n");

	groupcols = gen_column_list(head, summarypage);
	if (groupcols) {
		fprintf(output, "<TABLE SUMMARY=\"Group Block\" BORDER=0> \n <TR><TD VALIGN=MIDDLE ROWSPAN=2 CELLPADDING=2><CENTER><FONT %s>%s</FONT></CENTER></TD>\n", getenv("MKBBTITLE"), grouptitle);

		columncount = 1; /* Count the title also */
		for (gc=groupcols; (gc); gc = gc->next, columncount++) {
			fprintf(output, " <TD ALIGN=CENTER VALIGN=BOTTOM WIDTH=45>\n");
			fprintf(output, " <A HREF=\"%s/%s\"><FONT %s><B>%s</B></FONT></A> </TD>\n", 
				getenv("BBWEB"), columnlink(gc->column->link, gc->column->name), 
				getenv("MKBBCOLFONT"), gc->column->name);
		}
		fprintf(output, "</TR> \n<TR><TD COLSPAN=%d><HR WIDTH=100%%></TD></TR>\n\n", columncount);

		for (h = head; (h); h = h->next) {
			fprintf(output, "<TR>\n <TD NOWRAP><A NAME=\"%s\">\n", h->hostname);

			if (h->link != &null_link) {
				fprintf(output, "<A HREF=\"%s/%s\" TARGET=\"_blank\"><FONT %s>%s</FONT></A>\n </TD>",
					getenv("BBWEB"), hostlink(h->link), 
					getenv("MKBBROWFONT"), h->hostname);
			}
			else {
				fprintf(output, "<FONT %s>%s</FONT>\n </TD>",
					getenv("MKBBROWFONT"), h->hostname);
			}

			for (gc = groupcols; (gc); gc = gc->next) {
				fprintf(output, "<TD ALIGN=CENTER>");

				for (e = h->entries; (e && (e->column != gc->column)); e = e->next) ;
				if (e == NULL) {
					fprintf(output, "-");
				}
				else {
					if (genstatic) {
						fprintf(output, "<A HREF=\"%s/html/%s.%s.html\">",
							getenv("BBWEB"), h->hostname, e->column->name);
					}
					else {
						fprintf(output, "<A HREF=\"%s/bb-hostsvc.sh?HOSTSVC=%s.%s\">",
							getenv("CGIBINURL"), commafy(h->hostname), e->column->name);
					}
					fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
						getenv("BBSKIN"), dotgiffilename(e),
						alttag(e),
						getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
				}
				fprintf(output, "</TD>\n");
			}

			fprintf(output, "</TR>\n\n");
		}

		fprintf(output, "</TABLE><BR><BR>\n");
	}
}

void do_groups(group_t *head, FILE *output)
{
	group_t *g;

	if (head == NULL)
		return;

	fprintf(output, "<CENTER> \n\n<A NAME=begindata>&nbsp;</A>\n");

	for (g = head; (g); g = g->next) {
		do_hosts(g->hosts, output, g->title, 0);
	}
	fprintf(output, "\n</CENTER>\n");
}

void do_bb_page(page_t *page, char *filename)
{
	FILE	*output;
	page_t	*p;
	link_t  *link;

	output = fopen(filename, "w");
	if (output == NULL) {
		printf("Cannot open file %s\n", filename);
		return;
	}

	headfoot(output, "bb", "", "", "header", page->color);

	fprintf(output, "<BR>\n<CENTER>\n");

	if (page->next) {
		fprintf(output, "<A NAME=\"pages-blk\">\n");
		fprintf(output, "<TABLE SUMMARY=\"Page Block\" BORDER=0>\n");

		fprintf(output, "<TR><TD COLSPAN=2><CENTER> \n<FONT %s>\n", getenv("MKBBTITLE"));
		fprintf(output, "   %s\n", getenv("MKBBLOCAL"));
		fprintf(output, "</FONT></CENTER></TD></TR>\n");
		fprintf(output, "<TR><TD COLSPAN=2><HR WIDTH=100%%></TD></TR>\n");

		for (p = page->next; (p); p = p->next) {

			link = find_link(p->name);
			if (link != &null_link) {
				fprintf(output, "<TR><TD><FONT %s><A HREF=\"%s/%s\">%s</A></FONT></TD>\n", 
					getenv("MKBBROWFONT"),
					getenv("BBWEB"), hostlink(link), 
					p->title);
			}
			else {
				fprintf(output, "<TR><TD><FONT %s>%s</FONT></TD>\n", getenv("MKBBROWFONT"), p->title);
			}

			fprintf(output, "<TD><CENTER><A HREF=\"%s/%s/%s.html\">\n", getenv("BBWEB"), p->name, p->name);
			fprintf(output, "<IMG SRC=\"%s/%s.gif\" WIDTH=\"%s\" HEIGHT=\"%s\" BORDER=0 ALT=\"%s\"></A>\n", 
				getenv("BBSKIN"), colorname(p->color), 
				getenv("DOTWIDTH"), getenv("DOTHEIGHT"),
				colorname(p->color));
			fprintf(output, "</CENTER></TD></TR>\n");
		}

		fprintf(output, "</TABLE><BR><BR>\n");
		fprintf(output, "</CENTER>\n");
	}

	do_hosts(page->hosts, output, "", 0);
	do_groups(page->groups, output);

	headfoot(output, "bb", "", "", "footer", page->color);

	fclose(output);
}


void do_page(page_t *page, char *filename, char *upperpagename)
{
	FILE	*output;
	page_t	*p;
	link_t  *link;

	output = fopen(filename, "w");
	if (output == NULL) {
		printf("Cannot open file %s\n", filename);
		return;
	}

	headfoot(output, "bb", page->name, "", "header", page->color);

	fprintf(output, "<BR>\n<CENTER>\n");

	if (page->subpages) {
		fprintf(output, "<A NAME=\"pages-blk\">\n");
		fprintf(output, "<TABLE SUMMARY=\"Page Block\" BORDER=0>\n");

		fprintf(output, "<TR><TD COLSPAN=2><CENTER> \n<FONT %s>\n", getenv("MKBBTITLE"));
		fprintf(output, "   %s\n", getenv("MKBBSUBLOCAL"));
		fprintf(output, "</FONT></CENTER></TD></TR>\n");
		fprintf(output, "<TR><TD COLSPAN=2><HR WIDTH=100%%></TD></TR>");

		for (p = page->subpages; (p); p = p->next) {

			link = find_link(p->name);
			if (link != &null_link) {
				fprintf(output, "<TR><TD><FONT %s><A HREF=\"%s/%s\">%s</A></FONT></TD>\n", 
					getenv("MKBBROWFONT"),
					getenv("BBWEB"), hostlink(link), 
					p->title);
			}
			else {
				fprintf(output, "<TR><TD><FONT %s>%s</FONT></TD>\n", getenv("MKBBROWFONT"), p->title);
			}

			fprintf(output, "<TD><CENTER><A HREF=\"%s/%s/%s/%s.html\">\n", getenv("BBWEB"), upperpagename, p->name, p->name);
			fprintf(output, "<IMG SRC=\"%s/%s.gif\" WIDTH=\"%s\" HEIGHT=\"%s\" BORDER=0 ALT=\"%s\"></A>\n", 
				getenv("BBSKIN"), colorname(p->color), 
				getenv("DOTWIDTH"), getenv("DOTHEIGHT"),
				colorname(p->color));
			fprintf(output, "</CENTER></TD></TR>\n");
		}

		fprintf(output, "</TABLE><BR><BR>\n");
		fprintf(output, "</CENTER>\n");
	}

	do_hosts(page->hosts, output, "", 0);
	do_groups(page->groups, output);

	headfoot(output, "bb", page->name, "", "footer", page->color);

	fclose(output);
}

void do_subpage(page_t *page, char *filename, char *upperpagename)
{
	FILE	*output;

	output = fopen(filename, "w");
	if (output == NULL) {
		printf("Cannot open file %s\n", filename);
		return;
	}

	headfoot(output, "bb", upperpagename, page->name, "header", page->color);

	do_hosts(page->hosts, output, "", 0);
	do_groups(page->groups, output);

	headfoot(output, "bb", upperpagename, page->name, "footer", page->color);

	fclose(output);
}

void do_bb2_page(char *filename)
{
	FILE	*output;
	page_t	bb2page;
	hostlist_t *h;

	/* Build a "page" with the hosts that should be included in bb2 page */
	strcpy(bb2page.name, "");
	strcpy(bb2page.title, "");
	bb2page.color = COL_GREEN;
	bb2page.subpages = NULL;
	bb2page.groups = NULL;
	bb2page.hosts = NULL;
	bb2page.next = NULL;

	for (h=hosthead; (h); h=h->next) {
		if ((h->hostentry->color == COL_RED) || (h->hostentry->color == COL_YELLOW) || (h->hostentry->color == COL_PURPLE)) {
			host_t *newhost;

			if (h->hostentry->color > bb2page.color) bb2page.color = h->hostentry->color;

			/* We need to create a copy of the original record, */
			/* as we will diddle with the pointers */
			newhost = malloc(sizeof(host_t));
			memcpy(newhost, h->hostentry, sizeof(host_t));

			newhost->next = bb2page.hosts;
			bb2page.hosts = newhost;
		}
	}

	output = fopen(filename, "w");
	if (output == NULL) {
		perror("Cannot open file");
		exit(1);
	}

	headfoot(output, "bb2", "", "", "header", bb2page.color);

	fprintf(output, "<center>\n");
	fprintf(output, "\n<A NAME=begindata>&nbsp;</A> \n<A NAME=\"hosts-blk\">&nbsp;</A>\n");

	if (bb2page.hosts) {
		do_hosts(bb2page.hosts, output, "", 1);
	}
	else {
		/* "All Monitored Systems OK */
		fprintf(output, "<FONT SIZE=+2 FACE=\"Arial, Helvetica\"><BR><BR><I>All Monitored Systems OK</I></FONT><BR><BR>");
	}

	fprintf(output, "</center>\n");
	headfoot(output, "bb2", "", "", "footer", bb2page.color);

	fclose(output);
}


int main(int argc, char *argv[])
{
	char	pagedir[256];
	page_t *p, *q;

	if (argc > 1) {
		strcpy(pagedir, argv[1]);
	}
	else {
		sprintf(pagedir, "%s/www", getenv("BBHOME"));
	}

	linkhead = load_all_links();
	/* dumplinks(linkhead); */

	pagehead = load_bbhosts();
	/* dumphostlist(hosthead); */

	statehead = load_state();
	/* dumpstatelist(statehead); */

	calc_hostcolors(hosthead);

	calc_pagecolors(pagehead, "");
	/* For the ultimate top-page */
	for (p=pagehead; (p); p = p->next) {
		if (p->color > pagehead->color) pagehead->color = p->color;
	}

	delete_old_acks();

	/* Generate pages */
	if (chdir(pagedir) != 0) {
		printf("Cannot change to webpage directory %s\n", pagedir);
		exit(1);
	}

	do_bb_page(pagehead, "bb.html");
	for (p=pagehead->next; (p); p = p->next) {
		char dirfn[256], fn[256];

		/* Do SDM page - contains links to subpages, groups, hosts */
		sprintf(dirfn, "%s", p->name);
		mkdir(dirfn, 0755);
		sprintf(fn, "%s/%s.html", dirfn, p->name);
		do_page(p, fn, p->name);

		for (q = p->subpages; (q); q = q->next) {
			sprintf(dirfn, "%s/%s", p->name, q->name);
			mkdir(dirfn, 0755);
			sprintf(fn, "%s/%s.html", dirfn, q->name);
			do_subpage(q, fn, p->name);
		}
	}

	do_bb2_page("bb2.html");

	return 0;
}

