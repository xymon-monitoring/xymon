#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "bbgen.h"

page_t	*pagehead = NULL;
link_t  *linkhead = NULL;
link_t	null_link = { "", "", NULL };
state_t	*statehead = NULL;
col_t   *colhead = NULL;

link_t *find_link(const char *name)
{
	link_t *l;

	for (l=linkhead; (l && (strcmp(l->name, name) != 0)); l = l->next);

	return (l ? l : &null_link);
}

page_t *init_page(const char *name, const char *title)
{
	page_t *newpage = malloc(sizeof(page_t));

	strcpy(newpage->name, name);
	strcpy(newpage->title, title);
	newpage->color = COL_GREEN;
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

host_t *init_host(const char *hostname, const int ip1, const int ip2, const int ip3, const int ip4)
{
	host_t *newhost = malloc(sizeof(host_t));

	strcpy(newhost->hostname, hostname);
	sprintf(newhost->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
	newhost->link = find_link(hostname);
	newhost->entries = NULL;
	newhost->next = NULL;
	return newhost;
}

link_t *init_link(const char *filename)
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

state_t *init_state(const char *filename)
{
	FILE *fd;
	char	*p;
	char	hostname[60];
	char	testname[20];
	state_t *newstate;
	char	l[200];

	strcpy(hostname, filename);
	p = strrchr(hostname, '.');
	if (p) {
		*p = '\0';
		strcpy(testname, p+1);
		for (p=hostname; (*p); p++) {
			if (*p == ',') {
				*p='.';
			}
		}
	}
	else {
		return NULL;
	}

	newstate = malloc(sizeof(state_t));
	strcpy(newstate->hostname, hostname);
	newstate->entry = malloc(sizeof(entry_t));
	newstate->next = NULL;

	newstate->entry->column = find_or_create_column(testname);
	newstate->entry->color = -1;

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
		else {
			newstate->entry->color = COL_PURPLE;
		}
	}

	newstate->entry->age = 0;	/* FIXME */

	fclose(fd);

	newstate->entry->next = NULL;

	return newstate;
}

void getnamelink(char *l, char **name, char **link)
{
	char *p;

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
	char *p;

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

link_t *load_links(void)
{
	DIR		*bblinks;
	struct dirent 	*d;
	link_t		*curlink, *toplink, *newlink;

	toplink = NULL;
	bblinks = opendir(getenv("BBNOTES"));
	if (!bblinks) {
		perror("No notes");
		exit(1);
	}

	while ((d = readdir(bblinks))) {
		newlink = init_link(d->d_name);
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


	bbhosts = fopen(getenv("BBHOSTS"), "r");
	if (bbhosts == NULL)
		exit(1);

	curpage = toppage = init_page("*TOP*", "");
	while (fgets(l, sizeof(l), bbhosts)) {
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
			if (curhost == NULL) {
				curhost = init_host(hostname, ip1, ip2, ip3, ip4);
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
				curhost = curhost->next = init_host(hostname, ip1, ip2, ip3, ip4);
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
	state_t		*newstate, *topstate;

	chdir(getenv("BBLOGS"));

	topstate = NULL;
	bblogs = opendir(getenv("BBLOGS"));
	if (!bblogs) {
		perror("No logs!");
		exit(1);
	}

	while ((d = readdir(bblogs))) {
		if (d->d_name[0] != '.') {
			newstate = init_state(d->d_name);
			if (newstate) {
				newstate->next = topstate;
				topstate = newstate;
			}
		}
	}

	closedir(bblogs);

	return topstate;
}



void dumphosts(host_t *head, char *format)
{
	host_t *h;

	for (h = head; (h); h = h->next) {
		printf(format, h->hostname, h->ip, h->link->filename);
	}
}

void dumpgroups(group_t *head, char *format, char *hostformat)
{
	group_t *g;

	for (g = head; (g); g = g->next) {
		printf(format, g->title);
		dumphosts(g->hosts, hostformat);
	}
}

void dumpstate(state_t *head)
{
	state_t *s;

	for (s=statehead; (s); s=s->next) {
		printf("Host: %s, test:%s, state: %d\n",
			s->hostname,
			s->entry->column->name,
			s->entry->color);
	}
}

int main(int argc, char *argv[])
{
	page_t *p, *q;
	link_t *l;

	linkhead = load_links();
	for (l = linkhead; l; l = l->next) {
		printf("Link for host %s, filename %s\n", l->name, l->filename);
	}

	pagehead = load_bbhosts();
	for (p=pagehead; p; p = p->next) {
		printf("Page: %s, title=%s\n", p->name, p->title);
		for (q = p->subpages; (q); q = q->next) {
			printf("\tSubpage: %s, title=%s\n", q->name, q->title);
			dumpgroups(q->groups, "\t\tGroup: %s\n", "\t\t    Host: %s, ip: %s, link:%s\n");
			dumphosts(q->hosts, "\t    Host: %s, ip: %s, link: %s\n");
		}

		dumpgroups(p->groups, "\tGroup: %s\n","\t    Host: %s, ip: %s, link: %s\n");
		dumphosts(p->hosts, "    Host: %s, ip: %s, link: %s\n");
	}
	dumphosts(pagehead->hosts, "Host: %s, ip: %s, link: %s\n");

	statehead = load_state();
	dumpstate(statehead);
	return 0;
}

