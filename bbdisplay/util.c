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

static char rcsid[] = "$Id: util.c,v 1.2 2002-11-26 12:03:04 hstoerne Exp $";

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

int eventcolor(char *colortext)
{
	if 	(strcmp(colortext, "cl") == 0)	return COL_CLEAR;
	else if (strcmp(colortext, "bl") == 0)	return COL_BLUE;
	else if (strcmp(colortext, "pu") == 0)	return COL_PURPLE;
	else if (strcmp(colortext, "gr") == 0)	return COL_GREEN;
	else if (strcmp(colortext, "ye") == 0)	return COL_YELLOW;
	else if (strcmp(colortext, "re") == 0)	return COL_RED;
	else return -1;
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

			if (strcmp(t_start, "BBREL") == 0)     		fprintf(output, "%s", getenv("BBREL"));
			else if (strcmp(t_start, "BBRELDATE") == 0) 	fprintf(output, "%s", getenv("BBRELDATE"));
			else if (strcmp(t_start, "BBSKIN") == 0)    	fprintf(output, "%s", getenv("BBSKIN"));
			else if (strcmp(t_start, "BBWEB") == 0)     	fprintf(output, "%s", getenv("BBWEB"));
			else if (strcmp(t_start, "CGIBINURL") == 0) 	fprintf(output, "%s", getenv("CGIBINURL"));

			else if (strcmp(t_start, "BBDATE") == 0)        fprintf(output, "%s", ctime(&now));
			else if (strcmp(t_start, "BBBACKGROUND") == 0)  fprintf(output, "%s", colorname(bgcolor));
			else fprintf(output, "&");			/* No substitution - copy the ampersand */
			
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


int checkalert(host_t *host, char *test)
{
	char testname[30];

	if ((!host) || (!host->alerts)) return 0;

	sprintf(testname, ",%s,", test);
	return (strstr(host->alerts, testname) ? 1 : 0);
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


host_t *find_host(const char *hostname)
{
	hostlist_t	*l;

	for (l=hosthead; (l && (strcmp(l->hostentry->hostname, hostname) != 0)); l = l->next) ;

	return (l ? l->hostentry : NULL);
}


char *histlogurl(char *hostname, char *service, time_t histtime)
{
	static char url[512];
	char timestr[40];

	/* cgi-bin/bb-histlog.sh?HOST=SLS-P-CE1.slsdomain.sls.dk&SERVICE=msgs&TIMEBUF=Fri_Nov_22_16:01:08_2002 */
	
	strftime(timestr, sizeof(timestr), "%a_%b_%d_%H:%M:%S_%Y", localtime(&histtime));
	sprintf(url, "%s/bb-histlog.sh?HOST=%s&SERVICE=%s&TIMEBUF=%s", 
		getenv("CGIBINURL"), hostname, service, timestr);

	return url;
}


