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

static char rcsid[] = "$Id: util.c,v 1.17 2003-02-10 22:17:52 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <utime.h>

#include "bbgen.h"
#include "util.h"

int	use_recentgifs = 0;
char	timestamp[30];

extern  int debug;

/* Stuff for combo message handling */
int		bbmsgcount = 0;		/* Number of messages transmitted */
int		bbstatuscount = 0;	/* Number of status items reported */
static int	bbmsginuse;		/* Anything in the buffer ? */
static char	bbmsg[MAXMSG];		/* Complete combo message buffer */
static char	msgbuf[MAXMSG-50];	/* message buffer for one status message */
static int	msgcolor;		/* color of status message in msgbuf */

static char hostenv_svc[20];
static char hostenv_host[200];
static char hostenv_ip[20];
static char hostenv_color[20];


void init_timestamp(void)
{
	time_t	now;

        now = time(NULL);
        strcpy(timestamp, ctime(&now));
        timestamp[strlen(timestamp)-1] = '\0';

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
	  default:
			   cs = "unknown";
			   break;
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
	else if (use_recentgifs) {
		strcat(filename, (e->oldage ? "" : "-recent"));
	}
	strcat(filename, ".gif");

	return filename;
}

char *alttag(entry_t *e)
{
	static char tag[50];

	sprintf(tag, "%s:%s:", e->column->name, colorname(e->color));
	if (e->acked) {
		strcat(tag, "acked:");
	}
	if (!e->propagate) {
		strcat(tag, "nopropagate:");
	}
	strcat(tag, e->age);

	return tag;
}


char *weekday_text(char *dayspec)
{
	static char result[80];
	static char *dayname[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	char *p;

	if (strcmp(dayspec, "*") == 0) {
		strcpy(result, "All days");
		return result;
	}

	result[0] = '\0';
	for (p=dayspec; (*p); p++) {
		switch (*p) {
			case '0': case '1': case '2':
			case '3': case '4': case '5':
			case '6':
				strcat(result, dayname[(*p)-'0']);
				break;
			case '-':
				strcat(result, "-");
				break;
			case ' ':
			case ',':
				strcat(result, ",");
				break;
		}
	}
	return result;
}


char *time_text(char *timespec)
{
	static char result[80];

	if (strcmp(timespec, "*") == 0) {
		strcpy(result, "0000-2359");
	}
	else {
		strcpy(result, timespec);
	}

	return result;
}


char *hostpage_link(host_t *host)
{

	/* Provide a link to the page where this host lives */
	static char pagelink[150];

	if (((page_t *)host->parent)->parent) {
		page_t *parentpage, *parentsubpage;

		parentsubpage = host->parent;
		parentpage = parentsubpage->parent;

		/* "/bb/sdm/customer/customer.html" */
		sprintf(pagelink, "%s/%s/%s.html",
			parentpage->name, parentsubpage->name, parentsubpage->name);
	}
	else {
		/* "/bb/sdm/sdm.html" */
		sprintf(pagelink, "%s/%s.html",
			((page_t *)host->parent)->name, ((page_t *)host->parent)->name);
	}

	return pagelink;
}


char *hostpage_name(host_t *host)
{
	/* Provide a link to the page where this host lives */
	static char pagename[150];

	if (((page_t *)host->parent)->parent) {
		page_t *parentpage, *parentsubpage;

		parentsubpage = host->parent;
		parentpage = parentsubpage->parent;

		/* "sdm/customer" */
		sprintf(pagename, "%s/%s", parentpage->title, parentsubpage->title);
	}
	else {
		/* "sdm" */
		sprintf(pagename, "%s", ((page_t *)host->parent)->title);
	}

	return pagename;
}

char *commafy(char *hostname)
{
	static char s[256];
	char *p;

	strcpy(s, hostname);
	for (p = strchr(s, '.'); (p); p = strchr(s, '.')) *p = ',';
	return s;
}

void sethostenv(char *host, char *ip, char *svc, char *color)
{
	strcpy(hostenv_host,  host);
	strcpy(hostenv_ip,    ip);
	strcpy(hostenv_svc,   svc);
	strcpy(hostenv_color, color);
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
			else if (strcmp(t_start, "BBCOLOR") == 0)       fprintf(output, "%s", hostenv_color);
			else if (strcmp(t_start, "BBSVC") == 0)         fprintf(output, "%s", hostenv_svc);
			else if (strcmp(t_start, "BBHOST") == 0)        fprintf(output, "%s", hostenv_host);
			else if (strcmp(t_start, "BBIP") == 0)          fprintf(output, "%s", hostenv_ip);
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


int checkpropagation(host_t *host, char *test, int color)
{
	/* NB: Default is to propagate test, i.e. return 1 */
	char testname[30];

	if (!host) return 1;

	sprintf(testname, ",%s,", test);
	if (color == COL_RED) {
		if (host->nopropredtests && strstr(host->nopropredtests, testname)) return 0;
	}

	if (color == COL_YELLOW) {
		if (host->nopropyellowtests && strstr(host->nopropyellowtests, testname)) return 0;
		if (host->nopropredtests && strstr(host->nopropredtests, testname)) return 0;
	}

	return 1;
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
	char d1[40],d2[3],d3[40];

	/* cgi-bin/bb-histlog.sh?HOST=SLS-P-CE1.slsdomain.sls.dk&SERVICE=msgs&TIMEBUF=Fri_Nov_7_16:01:08_2002 */
	
	/* Hmm - apparently no format to generate a day-of-month with no leading 0. */
        strftime(d1, sizeof(d1), "%a_%b_", localtime(&histtime));
        strftime(d2, sizeof(d2), "%d", localtime(&histtime));
	if (d2[0] == '0') strcpy(d2, d2+1);
        strftime(d3, sizeof(d3), "_%H:%M:%S_%Y", localtime(&histtime));

	sprintf(url, "%s/bb-histlog.sh?HOST=%s&SERVICE=%s&TIMEBUF=%s%s%s", 
		getenv("CGIBINURL"), hostname, service, d1,d2,d3);

	return url;
}


static int minutes(char *p)
{
	/* Converts string HHMM to number indicating minutes since midnight (0-1440) */
	return (10*(*(p+0)-'0')+(*(p+1)-'0'))*60 + (10*(*(p+2)-'0')+(*(p+3)-'0'));
}

int within_sla(char *l)
{
	/*
	 * Usage: slatime hostline
	 *    SLASPEC is of the form SLA=W:HHMM:HHMM[,WXHHMM:HHMM]*
	 *    "W" = weekday : '*' = all, 'W' = Monday-Friday, '0'..'6' = Sunday ..Saturday
	 */

	char *p;
	char *slaspec = NULL;

	time_t tnow;
	struct tm *now;

	int result = 0;
	int found = 0;
	int starttime,endtime,curtime;

	p = strstr(l, "SLA=");
	if (p) {
		slaspec = p + 4;
		tnow = time(NULL);
		now = localtime(&tnow);

		// printf("SLA er %s\n", slaspec);
		// printf("Now is weekday %d, time is %d:%d\n", now->tm_wday, now->tm_hour, now->tm_min);

		/*
		 * Now find the appropriate SLA definition.
		 * We take advantage of the fixed (11 bytes + delimiter) length of each entry.
		 */
		while ( (!found) && (*slaspec != '\0') && (!isspace((unsigned int)*slaspec)) )
		{
			if ( (*slaspec == '*') || 						/* Any day */
                             (*slaspec == now->tm_wday+'0') ||					/* This day */
                             ((*slaspec == 'W') && (now->tm_wday >= 1) && (now->tm_wday <=5)) )	/* Monday thru Friday */
			{
				/* Weekday matches */
				// printf("Now checking slaspec=%s\n", slaspec);
				starttime = minutes(slaspec+2);
				endtime = minutes(slaspec+7);
				curtime = now->tm_hour*60+now->tm_min;
				// printf("start,end,current time = %d, %d, %d\n\n", starttime,endtime,curtime);
				found = ((curtime >= starttime) && (curtime <= endtime));
			};

			if (!found) {
				slaspec +=12;
			};
		}
		result = found;
	}
	else {
		/* No SLA -> default to always included */
		result = 1;
	}

	return result;
}


static void sendmessage(char *msg)
{
	char 	bbcmd[256];
	char 	bbdisp[256];
	pid_t	childpid;
	int	childstat;

	strcpy(bbcmd, getenv("BB"));
	strcpy(bbdisp, getenv("BBDISP"));
	
	childpid = fork();
	if (childpid == -1) {
		printf("%s: Fork error\n", timestamp);
	}
	else if (childpid == 0) {
		execl(bbcmd, "bb", bbdisp, msg, NULL);
	}
	else {
		wait(&childstat);
		if (WIFEXITED(childstat) && (WEXITSTATUS(childstat) != 0) ) {
			printf("%s: Whoops ! bb failed to send message - returns status %d\n", 
				timestamp, WEXITSTATUS(childstat));
		}
	}

	bbmsgcount++;
}


/* Routines for handling combo message transmission */
void combo_start(void)
{
	strcpy(bbmsg, "combo\n");
	bbmsginuse = 0;
}

void combo_flush(void)
{

	if (!bbmsginuse) {
		if (debug) printf("Flush, but bbmsg is empty\n");
		return;
	}

	if (debug) {
		char *p1, *p2;

		printf("Flushing combo message\n");
		p1 = p2 = bbmsg;

		do {
			p2++;
			p1 = strstr(p2, "\nstatus ");
			if (p1) {
				p1++; /* Skip the newline */
				p2 = strchr(p1, '\n');
				*p2='\0';
				printf("      %s\n", p1);
				*p2='\n';
			}
		} while (p1);
	}

	sendmessage(bbmsg);
	combo_start();	/* Get ready for the next */
}

void combo_add(char *buf)
{
	/* Check if there is room for the message + 2 newlines */
	if ((strlen(bbmsg) + strlen(buf) + 200) >= MAXMSG) {
		/* Nope ... flush buffer */
		combo_flush();
	}
	else {
		/* Yep ... add delimiter before new status (but not before the first!) */
		if (bbmsginuse) strcat(bbmsg, "\n\n");
	}

	strcat(bbmsg, buf);
	bbmsginuse = 1;
}

void combo_end(void)
{
	combo_flush();
	if (debug) {
		printf("%s: %d status messages merged into %d transmissions\n", 
			timestamp, bbstatuscount, bbmsgcount);
	}
}


void init_status(int color)
{
	msgbuf[0] = '\0';
	msgcolor = color;
	bbstatuscount++;
}

void addtostatus(char *p)
{
	if ((strlen(msgbuf) + strlen(p)) < sizeof(msgbuf))
		strcat(msgbuf, p);
	else {
		strncat(msgbuf, p, sizeof(msgbuf)-strlen(msgbuf)-1);
	}
}

void finish_status(void)
{

	if (debug) {
		char *p = strchr(msgbuf, '\n');

		*p = '\0';
		printf("Adding to combo msg: %s\n", msgbuf);
		*p = '\n';
	}

	/* Non-green messages go out NOW. Or we get no alarms ... */
	if (msgcolor != COL_GREEN) {
		sendmessage(msgbuf);
	}
	else {
		combo_add(msgbuf);
	}
}

void envcheck(char *envvars[])
{
	int i;
	int ok = 1;

	for (i = 0; (envvars[i]); i++) {
		if (getenv(envvars[i]) == NULL) {
			fprintf(stderr, "Environment variable %s not defined\n", envvars[i]);
			ok = 0;
		}
	}

	if (!ok) {
		fprintf(stderr, "Aborting\n");
		exit (1);
	}
}


int run_columngen(char *column, int update_interval, int enabled)
{
	/* If updating is enabled, check timestamp of $BBTMP/.COLUMN-gen */
	/* If older than update_interval, do the update. */

	char	fn[256];
	struct stat st;
	FILE    *fd;
	time_t  now;
	struct utimbuf filetime;

	if (!enabled)
		return 0;

	sprintf(fn, "%s/.%s-gen", getenv("BBTMP"), column);
	if (stat(fn, &st) == -1) {
		/* No such file - create it, and do the update */
		fd = fopen(fn, "w");
		fclose(fd);
		return 1;
	}
	else {
		/* Check timestamp, and update it if too old */
		time(&now);
		if ((now - st.st_ctime) > update_interval) {
			filetime.actime = filetime.modtime = now;
			utime(fn, &filetime);
			return 1;
		}
	}

	return 0;
}


char *realurl(char *url)
{
	static char result[200];
	char *p;
	
	p = url;

	if (strncmp(p, "content=", 8) == 0) {
		p += 8;
	}

	if (strncmp(p, "https2:", 7) == 0) {
		p += 7;
		sprintf(result, "https:%s", p);
	} else if (strncmp(p, "httpsm:", 7) == 0) {
		p += 7;
		sprintf(result, "https:%s", p);
	} else if (strncmp(p, "https:", 6) == 0) {
		p += 6;
		sprintf(result, "https:%s", p);
	} else if (strncmp(p, "http:", 5) == 0) {
		strcpy(result, p);
	}

	return result;
}

