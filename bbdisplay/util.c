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

static char rcsid[] = "$Id: util.c,v 1.118 2004-08-14 20:50:38 henrik Exp $";

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
#include <stdarg.h>
#include <signal.h>
#include <sys/resource.h>
#include <limits.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"

int	use_recentgifs = 0;
int	unpatched_bbd = 0;
char	timestamp[30];

extern  int debug;

/* Stuff for headfoot - variables we can set dynamically */
static char hostenv_svc[20];
static char hostenv_host[200];
static char hostenv_ip[20];
static char hostenv_color[20];
static time_t hostenv_reportstart = 0;
static time_t hostenv_reportend = 0;
static char hostenv_repwarn[20];
static char hostenv_reppanic[20];
static time_t hostenv_snapshot = 0;

/* Stuff for reading files that include other files */
typedef struct stackfd_t {
	FILE *fd;
	struct stackfd_t *next;
} stackfd_t;
static stackfd_t *fdhead = NULL;
static char stackfd_base[MAX_PATH];
static char stackfd_mode[10];

char *errbuf = NULL;
static unsigned int errbufsize = 0;

/* Data used while crashing - cannot depend on the stack being usable */
static char signal_bbcmd[MAX_PATH];
static char signal_bbdisp[1024];
static char signal_msg[1024];
static char signal_bbtmp[MAX_PATH];

void errprintf(const char *fmt, ...)
{
	char msg[4096];
	va_list args;

	va_start(args, fmt);
#ifdef NO_VSNPRINTF
	vsprintf(msg, fmt, args);
#else
	vsnprintf(msg, sizeof(msg), fmt, args);
#endif
	va_end(args);

	fprintf(stderr, "%s", msg);
	fflush(stderr);

	if (errbuf == NULL) {
		errbufsize = 8192;
		errbuf = (char *) malloc(errbufsize);
		*errbuf = '\0';
	}
	else if ((strlen(errbuf) + strlen(msg)) > errbufsize) {
		errbufsize += 8192;
		errbuf = (char *) realloc(errbuf, errbufsize);
	}

	strcat(errbuf, msg);
}


FILE *stackfopen(char *filename, char *mode)
{
	FILE *newfd;
	stackfd_t *newitem;
	char stackfd_filename[MAX_PATH];

	if (fdhead == NULL) {
		char *p;

		strcpy(stackfd_base, filename);
		p = strrchr(stackfd_base, '/'); if (p) *(p+1) = '\0';

		strcpy(stackfd_mode, mode);

		strcpy(stackfd_filename, filename);
	}
	else {
		if (*filename == '/')
			strcpy(stackfd_filename, filename);
		else
			sprintf(stackfd_filename, "%s/%s", stackfd_base, filename);
	}

	newfd = fopen(stackfd_filename, stackfd_mode);
	if (newfd != NULL) {
		newitem = (stackfd_t *) malloc(sizeof(stackfd_t));
		newitem->fd = newfd;
		newitem->next = fdhead;
		fdhead = newitem;
	}

	return newfd;
}


int stackfclose(FILE *fd)
{
	int result;
	stackfd_t *olditem;

	if (fd != NULL) {
		/* Close all */
		while (fdhead != NULL) {
			olditem = fdhead;
			fdhead = fdhead->next;
			fclose(olditem->fd);
			free(olditem);
		}
		stackfd_base[0] = '\0';
		stackfd_mode[0] = '\0';
		result = 0;
	}
	else {
		olditem = fdhead;
		fdhead = fdhead->next;
		result = fclose(olditem->fd);
		free(olditem);
	}

	return result;
}

static char *read_line_1(struct linebuf_t *buffer, FILE *stream, int *docontinue)
{
	char l[MAX_LINE_LEN];
	char *p, *start;

	*docontinue = 0;
	if (fgets(l, sizeof(l), stream) == NULL) return NULL;

	p = strchr(l, '\n'); if (p) *p = '\0';

	/* Strip leading spaces */
	for (start=l; (*start && isspace((int) *start)); start++) ;

	/* Strip trailing spaces while looking for continuation character */
	for (p = start + strlen(start) - 1; ((p > start) && (isspace((int) *p) || (*p == '\\')) ); p--) {
		if (*p == '\\') *docontinue = 1;
	}
	*(p+1) = '\0';

	if ((strlen(start) + strlen(buffer->buf) + 2) > buffer->buflen) {
		buffer->buflen += MAX_LINE_LEN;
		buffer->buf = (char *)realloc(buffer->buf, buffer->buflen);
	}

	strcat(buffer->buf, start);
	if (*docontinue) strcat(buffer->buf, " ");
	return buffer->buf;
}

char *read_line(struct linebuf_t *buffer, FILE *stream)
{
	char *result = NULL;
	int docontinue = 0;

	if (buffer->buf == NULL) {
		buffer->buflen = MAX_LINE_LEN;
		buffer->buf = (char *)malloc(buffer->buflen);
	}
	*(buffer->buf) = '\0';

	do {
		result = read_line_1(buffer, stream, &docontinue);
	} while (result && docontinue);

	return result;
}


char *stackfgets(char *buffer, unsigned int bufferlen, char *includetag1, char *includetag2)
{
	char *result;

	result = fgets(buffer, bufferlen, fdhead->fd);

	if (result && 
		( (strncmp(buffer, includetag1, strlen(includetag1)) == 0) ||
		  (includetag2 && (strncmp(buffer, includetag2, strlen(includetag2)) == 0)) )) {
		char *newfn = strchr(buffer, ' ');
		char *eol = strchr(buffer, '\n');

		while (newfn && *newfn && isspace((int)*newfn)) newfn++;
		if (eol) *eol = '\0';
		
		if (newfn && (stackfopen(newfn, "r") != NULL))
			return stackfgets(buffer, bufferlen, includetag1, includetag2);
		else {
			errprintf("WARNING: Cannot open include file '%s', line was:%s\n", textornull(newfn), buffer);
			if (eol) *eol = '\n';
			return result;
		}
	}
	else if (result == NULL) {
		/* end-of-file on read */
		stackfclose(NULL);
		if (fdhead != NULL)
			return stackfgets(buffer, bufferlen, includetag1, includetag2);
		else
			return NULL;
	}

	return result;
}


char *malcop(const char *s)
{
	char *buf;

	if (s == NULL) return NULL;

	buf = (char *) malloc(strlen(s)+1);
	strcpy(buf, s);
	return buf;
}


void init_timestamp(void)
{
	time_t	now;

        now = time(NULL);
        strcpy(timestamp, ctime(&now));
        timestamp[strlen(timestamp)-1] = '\0';

}

int get_fqdn(void)
{
	int result = 1;

	/* Get FQDN setting */
	if (getenv("FQDN")) {
		if (strcmp(getenv("FQDN"), "TRUE") == 0) {
			result = 1;
		}
		else {
			result = 0;
		}
	}

	return result;
}

char *skipword(char *l)
{
	char *p;

	for (p=l; (*p && (!isspace((int)*p))); p++) ;
	return p;
}


char *skipwhitespace(char *l)
{
	char *p;

	for (p=l; (*p && (isspace((int)*p))); p++) ;
	return p;
}


int argnmatch(char *arg, char *match)
{
	return (strncmp(arg, match, strlen(match)) == 0);
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

int parse_color(char *colortext)
{
	char inpcolor[10];

	strncpy(inpcolor, colortext, 7);
	inpcolor[7] = '\0';
	strcat(inpcolor, " ");

	if (strncmp(inpcolor, "green ", 6) == 0) {
		return COL_GREEN;
	}
	else if (strncmp(inpcolor, "yellow ", 7) == 0) {
		return COL_YELLOW;
	}
	else if (strncmp(inpcolor, "red ", 4) == 0) {
		return COL_RED;
	}
	else if (strncmp(inpcolor, "blue ", 5) == 0) {
		return COL_BLUE;
	}
	else if (strncmp(inpcolor, "clear ", 6) == 0) {
		return COL_CLEAR;
	}
	else if (strncmp(inpcolor, "purple ", 7) == 0) {
		return COL_PURPLE;
	}

	return -1;
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

char *dotgiffilename(int color, int acked, int oldage)
{
	static char filename[20]; /* yellow-recent.gif */

	strcpy(filename, colorname(color));
	if (acked) {
		strcat(filename, "-ack");
	}
	else if (use_recentgifs) {
		strcat(filename, (oldage ? "" : "-recent"));
	}
	strcat(filename, ".gif");

	return filename;
}

char *alttag(entry_t *e)
{
	static char tag[1024];

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
	/* Provide a link to the page where this host lives, relative to BBWEB */

	static char pagelink[MAX_PATH];
	char tmppath[MAX_PATH];
	bbgen_page_t *pgwalk;

	if (host->parent && (strlen(((bbgen_page_t *)host->parent)->name) > 0)) {
		sprintf(pagelink, "%s.html", ((bbgen_page_t *)host->parent)->name);
		for (pgwalk = host->parent; (pgwalk); pgwalk = pgwalk->parent) {
			if (strlen(pgwalk->name)) {
				sprintf(tmppath, "%s/%s", pgwalk->name, pagelink);
				strcpy(pagelink, tmppath);
			}
		}
	}
	else {
		sprintf(pagelink, "bb.html");
	}

	return pagelink;
}


char *hostpage_name(host_t *host)
{
	/* Provide a link to the page where this host lives */

	static char pagename[MAX_PATH];
	char tmpname[MAX_PATH];
	bbgen_page_t *pgwalk;

	if (host->parent && (strlen(((bbgen_page_t *)host->parent)->name) > 0)) {
		pagename[0] = '\0';
		for (pgwalk = host->parent; (pgwalk); pgwalk = pgwalk->parent) {
			if (strlen(pgwalk->name)) {
				strcpy(tmpname, pgwalk->title);
				if (strlen(pagename)) {
					strcat(tmpname, "/");
					strcat(tmpname, pagename);
				}
				strcpy(pagename, tmpname);
			}
		}
	}
	else {
		sprintf(pagename, "Top page");
	}

	return pagename;
}

char *commafy(char *hostname)
{
	static char s[MAX_LINE_LEN];
	char *p;

	strcpy(s, hostname);
	for (p = strchr(s, '.'); (p); p = strchr(s, '.')) *p = ',';
	return s;
}

void sethostenv(char *host, char *ip, char *svc, char *color)
{
	hostenv_host[0] = hostenv_ip[0] = hostenv_svc[0] = hostenv_color[0] = '\0';
	strncat(hostenv_host,  host,  sizeof(hostenv_host)-1);
	strncat(hostenv_ip,    ip,    sizeof(hostenv_ip)-1);
	strncat(hostenv_svc,   svc,   sizeof(hostenv_svc)-1);
	strncat(hostenv_color, color, sizeof(hostenv_color)-1);
}

void sethostenv_report(time_t reportstart, time_t reportend, double repwarn, double reppanic)
{
	hostenv_reportstart = reportstart;
	hostenv_reportend = reportend;
	sprintf(hostenv_repwarn, "%.2f", repwarn);
	sprintf(hostenv_reppanic, "%.2f", reppanic);
}

void sethostenv_snapshot(time_t snapshot)
{
	hostenv_snapshot = snapshot;
}

void headfoot(FILE *output, char *pagetype, char *pagepath, char *head_or_foot, int bgcolor)
{
	int	fd;
	char 	filename[MAX_PATH];
	struct stat st;
	char	*templatedata;
	char	*t_start, *t_next;
	char	savechar;
	time_t	now = time(NULL);
	char	*hfpath;

	/*
	 * "pagepath" is the relative path for this page, e.g. 
	 * - for "bb.html" it is ""
	 * - for a page, it is "pagename/"
	 * - for a subpage, it is "pagename/subpagename/"
	 *
	 * BB allows header/footer files named bb_PAGE_header or bb_PAGE_SUBPAGE_header
	 * so we need to scan for an existing file - starting with the
	 * most detailed one, and working up towards the standard "web/bb_TYPE" file.
	 */

	hfpath = malcop(pagepath); 
	/* Trim off excess trailing slashes */
	while (*(hfpath + strlen(hfpath) - 1) == '/') {
		*(hfpath + strlen(hfpath) - 1) = '\0';
	}
	fd = -1;

	while ((fd == -1) && strlen(hfpath)) {
		char *p;
		char *elemstart;

		sprintf(filename, "%s/web/", getenv("BBHOME"));
		p = strchr(hfpath, '/'); elemstart = hfpath;
		while (p) {
			*p = '\0';
			strcat(filename, elemstart);
			strcat(filename, "_");
			*p = '/';
			p++;
			elemstart = p; p = strchr(elemstart, '/');
		}
		strcat(filename, elemstart);
		strcat(filename, "_");
		strcat(filename, head_or_foot);

		dprintf("Trying header/footer file '%s'\n", filename);
		fd = open(filename, O_RDONLY);

		if (fd == -1) {
			p = strrchr(hfpath, '/');
			if (p == NULL) p = hfpath;
			*p = '\0';
		}
	}
	free(hfpath);

	if (fd == -1) {
		/* Fall back to default head/foot file. */
		sprintf(filename, "%s/web/%s_%s", getenv("BBHOME"), pagetype, head_or_foot);
		dprintf("Trying header/footer file '%s'\n", filename);
		fd = open(filename, O_RDONLY);
	}

	if (fd != -1) {
		fstat(fd, &st);
		templatedata = (char *) malloc(st.st_size + 1);
		read(fd, templatedata, st.st_size);
		templatedata[st.st_size] = '\0';
		close(fd);

		for (t_start = templatedata, t_next = strchr(t_start, '&'); (t_next); ) {
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

			else if (strcmp(t_start, "BBDATE") == 0) {
				if (hostenv_reportstart != 0) {
					char starttime[20], endtime[20];

					strftime(starttime, sizeof(starttime), "%b %d %Y", localtime(&hostenv_reportstart));
					strftime(endtime, sizeof(endtime), "%b %d %Y", localtime(&hostenv_reportend));
					if (strcmp(starttime, endtime) == 0)
						fprintf(output, "%s", starttime);
					else
						fprintf(output, "%s - %s", starttime, endtime);
				}
				else if (hostenv_snapshot != 0) {
					fprintf(output, "%s", ctime(&hostenv_snapshot));
				}
				else {
					fprintf(output, "%s", ctime(&now));
				}
			}

			else if (strcmp(t_start, "BBBACKGROUND") == 0)  {
				if (unpatched_bbd && (strcmp(pagetype, "hostsvc") == 0)) {
					fprintf(output, "%s/bkg-%s.gif", 
						getenv("BBSKIN"), colorname(bgcolor));
				}
				else {
					fprintf(output, "%s", colorname(bgcolor));
				}
			}
			else if (strcmp(t_start, "BBCOLOR") == 0)       fprintf(output, "%s", hostenv_color);
			else if (strcmp(t_start, "BBSVC") == 0)         fprintf(output, "%s", hostenv_svc);
			else if (strcmp(t_start, "BBHOST") == 0)        fprintf(output, "%s", hostenv_host);
			else if (strcmp(t_start, "BBIP") == 0)          fprintf(output, "%s", hostenv_ip);
			else if (strcmp(t_start, "BBIPNAME") == 0) {
				if (strcmp(hostenv_ip, "0.0.0.0") == 0) fprintf(output, "%s", hostenv_host);
				else fprintf(output, "%s", hostenv_ip);
			}
			else if (strcmp(t_start, "BBREPWARN") == 0)     fprintf(output, "%s", hostenv_repwarn);
			else if (strcmp(t_start, "BBREPPANIC") == 0)    fprintf(output, "%s", hostenv_reppanic);
			else fprintf(output, "&");			/* No substitution - copy the ampersand */
			
			*t_next = savechar; t_start = t_next; t_next = strchr(t_start, '&');
		}

		/* Remainder of file */
		fprintf(output, "%s", t_start);

		free(templatedata);
	}
	else {
		fprintf(output, "<HTML><BODY> \n <HR size=4> \n <BR>%s is either missing or invalid, please create this file with your custom header<BR> \n<HR size=4>", filename);
	}
}


void do_bbext(FILE *output, char *extenv, char *family)
{
	/* Extension scripts. These are ad-hoc, and implemented as a
	 * simple pipe. So we do a fork here ...
	 */

	char *bbexts, *p;
	FILE *inpipe;
	char extfn[MAX_PATH];
	char buf[4096];
	
	p = getenv(extenv);
	if (p == NULL)
		/* No extension */
		return;

	bbexts = malcop(p);
	p = strtok(bbexts, "\t ");

	while (p) {
		/* Dont redo the eventlog or acklog things */
		if ((strcmp(p, "eventlog.sh") != 0) &&
		    (strcmp(p, "acklog.sh") != 0)) {
			sprintf(extfn, "%s/ext/%s/%s", getenv("BBHOME"), family, p);
			inpipe = popen(extfn, "r");
			if (inpipe) {
				while (fgets(buf, sizeof(buf), inpipe)) 
					fputs(buf, output);
				pclose(inpipe);
			}
		}
		p = strtok(NULL, "\t ");
	}

	free(bbexts);
}


int checkalert(char *alertlist, char *test)
{
	char *testname;
	int result;

	if (!alertlist) return 0;

	testname = (char *) malloc(strlen(test)+3);
	sprintf(testname, ",%s,", test);
	result = (strstr(alertlist, testname) ? 1 : 0);

	free(testname);
	return result;
}


static int checknopropagation(char *testname, char *noproptests)
{
	if (noproptests == NULL) return 0;

	if (strcmp(noproptests, ",*,") == 0) return 1;
	if (strstr(noproptests, testname) != NULL) return 1;

	return 0;
}

int checkpropagation(host_t *host, char *test, int color, int acked)
{
	/* NB: Default is to propagate test, i.e. return 1 */
	char *testname;
	int result = 1;

	if (!host) return 1;

	testname = (char *) malloc(strlen(test)+3);
	sprintf(testname, ",%s,", test);
	if (acked) {
		if (checknopropagation(testname, host->nopropacktests)) result = 0;
	}

	if (result) {
		if (color == COL_RED) {
			if (checknopropagation(testname, host->nopropredtests)) result = 0;
		}
		else if (color == COL_YELLOW) {
			if (checknopropagation(testname, host->nopropyellowtests)) result = 0;
			if (checknopropagation(testname, host->nopropredtests)) result = 0;
		}
		else if (color == COL_PURPLE) {
			if (checknopropagation(testname, host->noproppurpletests)) result = 0;
		}
	}

	free(testname);
	return result;
}


link_t *find_link(const char *name)
{
	/* We cache the last link searched for */
	static link_t *lastlink = NULL;
	link_t *l;

	if (lastlink && (strcmp(lastlink->name, name) == 0))
		return lastlink;

	for (l=linkhead; (l && (strcmp(l->name, name) != 0)); l = l->next);
	lastlink = l;

	return (l ? l : &null_link);
}

char *columnlink(link_t *link, char *colname)
{
	static char linkurl[MAX_PATH];

	if (link != &null_link) {
		sprintf(linkurl, "%s/%s", link->urlprefix, link->filename);
	}
	else {
		sprintf(linkurl, "%s/help/bb-help.html#%s", getenv("BBWEB"), colname);
	}
	
	return linkurl;
}

char *hostlink(link_t *link)
{
	static char linkurl[MAX_PATH];

	if (link != &null_link) {
		sprintf(linkurl, "%s/%s", link->urlprefix, link->filename);
	}
	else {
		sprintf(linkurl, "%s/bb.html", getenv("BBWEB"));
	}

	return linkurl;
}


char *urldoclink(const char *docurl, const char *hostname)
{
	/*
	 * docurl is a user defined text string to build
	 * a documentation url. It is expanded with the
	 * hostname.
	 */

	static char linkurl[MAX_PATH];

	if (docurl) {
		sprintf(linkurl, docurl, hostname);
	}
	else {
		linkurl[0] = '\0';
	}

	return linkurl;
}


char *cleanurl(char *url)
{
	static char cleaned[MAX_PATH];
	char *pin, *pout;
	int  lastwasslash = 0;

	for (pin=url, pout=cleaned, lastwasslash=0; (*pin); pin++) {
		if (*pin == '/') {
			if (!lastwasslash) { 
				*pout = *pin; 
				pout++; 
			}
			lastwasslash = 1;
		}
		else {
			*pout = *pin; 
			pout++;
			lastwasslash = 0;
		}
	}
	*pout = '\0';

	return cleaned;
}


host_t *find_host(const char *hostname)
{
	static hostlist_t *lastsearch = NULL;
	hostlist_t	*l;

	/* We cache the last result */
	if (lastsearch && (strcmp(lastsearch->hostentry->hostname, hostname) == 0)) 
		return lastsearch->hostentry;

	/* Search for the host */
	for (l=hosthead; (l && (strcmp(l->hostentry->hostname, hostname) != 0)); l = l->next) ;
	lastsearch = l;

	return (l ? l->hostentry : NULL);
}


bbgen_col_t *find_or_create_column(const char *testname, int create)
{
	static bbgen_col_t *colhead = NULL;	/* Head of column-name list */
	static bbgen_col_t *lastcol = NULL;	/* Cache the last lookup */
	bbgen_col_t *newcol;

	dprintf("find_or_create_column(%s)\n", textornull(testname));
	if (lastcol && (strcmp(testname, lastcol->name) == 0))
		return lastcol;

	for (newcol = colhead; (newcol && (strcmp(testname, newcol->name) != 0)); newcol = newcol->next);
	if (newcol == NULL) {
		if (!create) return NULL;

		newcol = (bbgen_col_t *) malloc(sizeof(bbgen_col_t));
		newcol->name = malcop(testname);
		newcol->listname = (char *)malloc(strlen(testname)+1+2); sprintf(newcol->listname, ",%s,", testname);
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


char *histlogurl(char *hostname, char *service, time_t histtime)
{
	static char url[MAX_PATH];
	char d1[40],d2[3],d3[40];

	/* cgi-bin/bb-histlog.sh?HOST=SLS-P-CE1.slsdomain.sls.dk&SERVICE=msgs&TIMEBUF=Fri_Nov_7_16:01:08_2002 */
	
	/* Hmm - apparently no format to generate a day-of-month with no leading 0. */
        strftime(d1, sizeof(d1), "%a_%b_", localtime(&histtime));
        strftime(d2, sizeof(d2), "%d", localtime(&histtime));
	if (d2[0] == '0') strcpy(d2, d2+1);
        strftime(d3, sizeof(d3), "_%H:%M:%S_%Y", localtime(&histtime));

	sprintf(url, "%s/bb-histlog.sh?HOST=%s&amp;SERVICE=%s&amp;TIMEBUF=%s%s%s", 
		getenv("CGIBINURL"), hostname, service, d1,d2,d3);

	return url;
}


static int minutes(char *p)
{
	/* Converts string HHMM to number indicating minutes since midnight (0-1440) */
	return (10*(*(p+0)-'0')+(*(p+1)-'0'))*60 + (10*(*(p+2)-'0')+(*(p+3)-'0'));
}

int within_sla(char *l, char *tag, int defresult)
{
	/*
	 * Usage: slatime hostline
	 *    SLASPEC is of the form SLA=W:HHMM:HHMM[,WXHHMM:HHMM]*
	 *    "W" = weekday : '*' = all, 'W' = Monday-Friday, '0'..'6' = Sunday ..Saturday
	 */

	char *p;
	char *slaspec = NULL;
	char *tagspec;

	time_t tnow;
	struct tm *now;

	int result = 0;
	int found = 0;
	int starttime,endtime,curtime;

	tagspec = (char *) malloc(strlen(tag)+2);
	sprintf(tagspec, "%s=", tag);
	p = strstr(l, tagspec);
	if (p) {
		slaspec = p + strlen(tagspec);
		tnow = time(NULL);
		now = localtime(&tnow);

		/*
		 * Now find the appropriate SLA definition.
		 * We take advantage of the fixed (11 bytes + delimiter) length of each entry.
		 */
		while ( (!found) && slaspec && (*slaspec != '\0') && (!isspace((unsigned int)*slaspec)) )
		{
			dprintf("Now checking slaspec='%s'", slaspec);

			if ( (*slaspec == '*') || 						/* Any day */
                             (*slaspec == now->tm_wday+'0') ||					/* This day */
                             ((toupper((int)*slaspec) == 'W') && (now->tm_wday >= 1) && (now->tm_wday <=5)) )	/* Monday thru Friday */
			{
				/* Weekday matches */
				starttime = minutes(slaspec+2);
				endtime = minutes(slaspec+7);
				curtime = now->tm_hour*60+now->tm_min;
				found = ((curtime >= starttime) && (curtime <= endtime));
				dprintf("\tstart,end,current time = %d, %d, %d - found=%d\n", 
					starttime,endtime,curtime,found);
			}
			else {
				dprintf("\tWeekday does not match\n");
			}

			if (!found) {
				slaspec = strchr(slaspec, ',');
				if (slaspec) slaspec++;
			};
		}
		result = found;
	}
	else {
		/* No SLA -> default to always included */
		result = defresult;
	}
	free(tagspec);

	return result;
}


int periodcoversnow(char *tag)
{
	/*
	 * Tag format: "-DAY-HHMM-HHMM:"
	 * DAY = 0-6 (Sun .. Mon), or W (1..5)
	 */

	time_t tnow;
	struct tm *now;

        int result = 1;
        char *dayspec, *starttime, *endtime;
	unsigned int istart, iend, inow;
	char *p;

        if ((tag == NULL) || (*tag != '-')) return 1;

	dayspec = (char *) malloc(strlen(tag)+1+12); /* Leave room for expanding 'W' and '*' */
	starttime = (char *) malloc(strlen(tag)+1); 
	endtime = (char *) malloc(strlen(tag)+1); 

	strcpy(dayspec, (tag+1));
	for (p=dayspec; ((*p == 'W') || (*p == '*') || ((*p >= '0') && (*p <= '6'))); p++) ;
	if (*p != '-') {
		free(endtime); free(starttime); free(dayspec); return 1;
	}
	*p = '\0';

	p++;
	strcpy(starttime, p); p = starttime;
	if ( (strlen(starttime) < 4) || 
	     !isdigit((int) *p)            || 
	     !isdigit((int) *(p+1))        ||
	     !isdigit((int) *(p+2))        ||
	     !isdigit((int) *(p+3))        ||
	     !(*(p+4) == '-') )          goto out;
	else *(starttime+4) = '\0';

	p+=5;
	strcpy(endtime, p); p = endtime;
	if ( (strlen(endtime) < 4) || 
	     !isdigit((int) *p)          || 
	     !isdigit((int) *(p+1))      ||
	     !isdigit((int) *(p+2))      ||
	     !isdigit((int) *(p+3))      ||
	     !(*(p+4) == ':') )          goto out;
	else *(endtime+4) = '\0';

	tnow = time(NULL);
	now = localtime(&tnow);


	/* We have a timespec. So default to "not included" */
	result = 0;

	/* Check day-spec */
	if (strchr(dayspec, 'W')) strcat(dayspec, "12345");
	if (strchr(dayspec, '*')) strcat(dayspec, "0123456");
	if (strchr(dayspec, ('0' + now->tm_wday)) == NULL) goto out;

	/* Calculate minutes since midnight for start, end and now */
	istart = (600 * (starttime[0]-'0'))   +
		 (60  * (starttime[1]-'0'))   +
		 (10  * (starttime[2]-'0'))   +
		 (1   * (starttime[3]-'0'));
	iend   = (600 * (endtime[0]-'0'))     +
		 (60  * (endtime[1]-'0'))     +
		 (10  * (endtime[2]-'0'))     +
		 (1   * (endtime[3]-'0'));
	inow   = 60*now->tm_hour + now->tm_min;

	if ((inow < istart) || (inow > iend)) goto out;

	result = 1;
out:
	free(endtime); free(starttime); free(dayspec); 
	return result;
}


void envcheck(char *envvars[])
{
	int i;
	int ok = 1;

	for (i = 0; (envvars[i]); i++) {
		if (getenv(envvars[i]) == NULL) {
			errprintf("Environment variable %s not defined\n", envvars[i]);
			ok = 0;
		}
	}

	if (!ok) {
		errprintf("Aborting\n");
		exit (1);
	}
}


int run_columngen(char *column, int update_interval, int enabled)
{
	/* If updating is enabled, check timestamp of $BBTMP/.COLUMN-gen */
	/* If older than update_interval, do the update. */

	char	stampfn[MAX_PATH];
	struct stat st;
	FILE    *fd;
	time_t  now;
	struct utimbuf filetime;

	if (!enabled)
		return 0;

	sprintf(stampfn, "%s/.%s-gen", getenv("BBTMP"), column);

	if (stat(stampfn, &st) == -1) {
		/* No such file - create it, and do the update */
		fd = fopen(stampfn, "w");
		fclose(fd);
		return 1;
	}
	else {
		/* Check timestamp, and update it if too old */
		time(&now);
		if ((now - st.st_ctime) > update_interval) {
			filetime.actime = filetime.modtime = now;
			utime(stampfn, &filetime);
			return 1;
		}
	}

	return 0;
}


void drop_genstatfiles(void)
{
	char fn[MAX_PATH];
	struct stat st, stampst;

	sprintf(fn, "%s/.bbstartup", getenv("BBLOGS"));
	if (stat(fn, &st) == 0) {
		sprintf(fn, "%s/.larrd-gen", getenv("BBTMP"));
		if ( (stat(fn, &stampst) == 0) && (stampst.st_ctime < st.st_ctime) ) unlink(fn);
		sprintf(fn, "%s/.info-gen", getenv("BBTMP"));
		if ( (stat(fn, &stampst) == 0) && (stampst.st_ctime < st.st_ctime) ) unlink(fn);
	}
}

char *urlip(const char *url, char *hostip, char *hostname)
{
	/* This routine changes the URL to use an IP-address instead of the hostname */
	char *urlcopy = malcop(url);
	static char result[MAX_LINE_LEN];
	char *p, *hoststart, *restofurl, *portnumber;

	if (hostname) *hostname = '\0';
	strcpy(result, urlcopy);

	/* First find where the hostname starts */
	p = strstr(urlcopy, "://");
	if (p == NULL) return result;

	hoststart = p+3;
	result[hoststart-urlcopy] = '\0';

	/* Now cut off the part of the URL that is not the hostname */
	p = strchr(hoststart, '/');
	if (p) { *p = '\0'; restofurl = p+1; } else restofurl = "";

	/* hoststart points to start of hostname, restofurl to the part after the hostname */
	p = strchr(hoststart, '@');
	if (p) {
		/* URL contains "login:password@" sequence */
		*p = '\0';
		strcat(result, hoststart); strcat(result, "@");
		hoststart = p+1;
	}

	/* Any port number lurking here? */
	portnumber = strchr(hoststart, ':');
	if (portnumber) { *portnumber = '\0'; portnumber++; }

	if (hostname) strcpy(hostname, hoststart);
	strcat(result, hostip);
	if (portnumber) {
		strcat(result, ":");
		strcat(result, portnumber);
	}
	strcat(result, "/");
	strcat(result, restofurl);

	free(urlcopy);
	return result;
}


char *realurl(char *url, char **proxy, char **proxyuserpwd, char **ip, char **hosthdr)
{
	static char result[MAX_LINE_LEN];
	static char proxyresult[MAX_LINE_LEN];
	static char proxyuserpwdresult[MAX_LINE_LEN];
	static char ipresult[MAX_LINE_LEN];
	static char hosthdrresult[MAX_LINE_LEN];
	char *p;
	char *urlstart;
	char *restorechar = NULL;

	result[0] = proxyresult[0] = proxyuserpwdresult[0] = ipresult[0] = hosthdrresult[0] = '\0';
	if (proxy) *proxy = NULL;
	if (proxyuserpwd) *proxyuserpwd = NULL;
	if (ip) *ip = NULL;
	if (hosthdr) *hosthdr = NULL;
	p = url;

	/* First handle any leading "cont;" "post;" and "content=" */
	if (strncmp(p, "content=", 8) == 0) {
		p += 8;
	}
	else if (strncmp(p, "cont;", 5) == 0) {
		p += 5;
		restorechar = strrchr(p, ';');
		if (restorechar) *restorechar = '\0';
	}
	else if (strncmp(p, "nocont;", 7) == 0) {
		p += 7;
		restorechar = strrchr(p, ';');
		if (restorechar) *restorechar = '\0';
	}
	else if (strncmp(p, "post;", 5) == 0) {
		p += 5;
		restorechar = strrchr(p, ';');
		if (restorechar) {
			/* Go back 2 ';' for a post-test */
			char *rest2char = restorechar;

			*restorechar = '\0'; /* Cut off the expected data */
			restorechar = strrchr(p, ';');
			*rest2char = ';';
			if (restorechar) *restorechar = '\0';
		}
	}
	else if (strncmp(p, "nopost;", 7) == 0) {
		p += 7;
		restorechar = strrchr(p, ';');
		if (restorechar) {
			/* Go back 2 ';' for a post-test */
			char *rest2char = restorechar;

			*restorechar = '\0'; /* Cut off the expected data */
			restorechar = strrchr(p, ';');
			*rest2char = ';';
			if (restorechar) *restorechar = '\0';
		}
	}
	else if (strncmp(p, "type;", 5) == 0) {
		p += 5;
		restorechar = strrchr(p, ';');
		if (restorechar) *restorechar = '\0';
	}

	/* Check if there is a proxy spec in there */
	urlstart = p;
	p += 4; /* Skip leading "http" */
	p = strstr((p+4), "/http");
	if (p) {
		/* There IS a proxy spec first. */

		p++; /* Move p to "http" */
		if (proxy) {
			char *userpwd;
			char *p1;

			*(p-1) = '\0'; /* Proxy setting stops before "/http" */

			/* See if there is username:password in the proxy spec */
			userpwd = urlstart + strlen("http://");
			p1 = strchr(userpwd, '@');
			if (p1) {
				/* We do have a username:password */
				*p1 = '\0';
				strcpy(proxyuserpwdresult, userpwd);
				if (proxyuserpwd) *proxyuserpwd = proxyuserpwdresult;

				sprintf(proxyresult, "http://%s", (p1+1));
				*p1 = '@';
			}
			else {
				strcpy(proxyresult, urlstart);
			}
			*proxy = proxyresult;
			*(p-1) = '/';
		}
		urlstart = p;
	}

	/* Drop the special httpsX/http1X stuff */
	if (strncmp(urlstart, "https2:", 7) == 0) {
		urlstart += 7;
		sprintf(result, "https:%s", urlstart);
	} else if (strncmp(urlstart, "https3:", 7) == 0) {
		urlstart += 7;
		sprintf(result, "https:%s", urlstart);
	} else if (strncmp(urlstart, "httpsm:", 7) == 0) {
		urlstart += 7;
		sprintf(result, "https:%s", urlstart);
	} else if (strncmp(urlstart, "httpsh:", 7) == 0) {
		urlstart += 7;
		sprintf(result, "https:%s", urlstart);
	} else if (strncmp(urlstart, "https:", 6)  == 0) {
		urlstart += 6;
		sprintf(result, "https:%s", urlstart);
	} else if (strncmp(urlstart, "http10:", 7)   == 0) {
		urlstart += 7;
		sprintf(result, "http:%s", urlstart);
	} else if (strncmp(urlstart, "http11:", 7)   == 0) {
		urlstart += 7;
		sprintf(result, "http:%s", urlstart);
	} else if (strncmp(urlstart, "http:", 5)   == 0) {
		strcpy(result, urlstart);
	} else if (strncmp(urlstart, "ftp:", 4)    == 0) {
		strcpy(result, urlstart);
	}

	if (restorechar) *restorechar = ';';


	/* 
	 * Now see if the hostname inside the URL has an IP spec in it
	 * It could be 
	 *    http://www.foo.com=12.34.56.78/baz/index.html
	 *    http://www.foo.com:1234=5.6.7.8/baz/index.html
	 */
	urlstart = strstr(result, "://");
	if (urlstart) {
		urlstart += 3;
		p = strchr(urlstart, '/');
	}
	if (urlstart && p) {
		char *ipstart;

		*p = '\0';
		ipstart = strchr(urlstart, '=');
		if (ipstart) {
			strcpy(ipresult, (ipstart+1));
			*p = '/';
			memmove(ipstart, p, strlen(p)+1);
			if (ip) *ip = ipresult;

			if (hosthdr) {
				strcpy(hosthdrresult, "Host: ");
				urlip(result, ipresult, hosthdrresult+strlen(hosthdrresult));
				*hosthdr = hosthdrresult;
			}
		}
		else {
			*p = '/';
		}
	}

	return result;
}


int generate_static(void)
{
	return ( (strcmp(getenv("BBLOGSTATUS"), "STATIC") == 0) ? 1 : 0);
}


int stdout_on_file(char *filename)
{
	struct stat st_fd, st_out;

	if (!isatty(1) && (fstat(1, &st_out) == 0) && (stat(filename, &st_fd) != 0)) {
		if ((st_out.st_ino == st_fd.st_ino) && (st_out.st_dev == st_fd.st_dev)) {
			return 1;
		}
	}

	return 0;
}



void sigsegv_handler(int signum)
{
	/*
	 * This is a signal handler. Only a very limited number of 
	 * library routines can be safely used here, according to
	 * Posix: http://www.opengroup.org/onlinepubs/007904975/functions/xsh_chap02_04.html#tag_02_04_03
	 * Do not use string, stdio etc. - just basic system calls.
	 * That is why we need to setup all of the strings in advance.
	 */

	signal(signum, SIG_DFL);

	/* 
	 * Try to fork a child to send in an alarm message.
	 * If the fork fails, then just attempt to exec() the BB command
	 */
	if (fork() <= 0) {
		execl(signal_bbcmd, "bbgen-signal", signal_bbdisp, signal_msg, NULL);
	}

	/* Dump core and abort */
	chdir(signal_bbtmp);
	abort();
}

void setup_signalhandler(char *programname)
{
	/*
	 * After lengthy debugging and perusing of mail archives:
	 * Need to ignore SIGPIPE since FreeBSD (and others?) can throw this
	 * on a write() instead of simply returning -EPIPE like any sane
	 * OS would.
	 */
	signal(SIGPIPE, SIG_IGN);

#ifdef RLIMIT_CORE
	/*
	 * Try to allow ourselves to generate core files
	 */
	{
		struct rlimit lim;

		getrlimit(RLIMIT_CORE, &lim);
		lim.rlim_cur = RLIM_INFINITY;
		setrlimit(RLIMIT_CORE, &lim);
	}
#endif
	if (getenv("BB") == NULL) return;
	if (getenv("BBDISP") == NULL) return;

	/*
	 * Used inside signal-handler. Must be setup in
	 * advance.
	 */
	strcpy(signal_bbcmd, getenv("BB"));
	strcpy(signal_bbdisp, getenv("BBDISP"));
	strcpy(signal_bbtmp, getenv("BBTMP"));
	sprintf(signal_msg, "status %s.%s red - Program crashed\n\nFatal signal caught!\n", 
		(getenv("MACHINE") ? getenv("MACHINE") : "BBDISPLAY"), programname);

	signal(SIGSEGV, sigsegv_handler);
#ifdef SIGBUS
	signal(SIGBUS, sigsegv_handler);
#endif
}


int hexvalue(unsigned char c)
{
	switch (c) {
	  case '0': return 0;
	  case '1': return 1;
	  case '2': return 2;
	  case '3': return 3;
	  case '4': return 4;
	  case '5': return 5;
	  case '6': return 6;
	  case '7': return 7;
	  case '8': return 8;
	  case '9': return 9;
	  case 'a': return 10;
	  case 'A': return 10;
	  case 'b': return 11;
	  case 'B': return 11;
	  case 'c': return 12;
	  case 'C': return 12;
	  case 'd': return 13;
	  case 'D': return 13;
	  case 'e': return 14;
	  case 'E': return 14;
	  case 'f': return 15;
	  case 'F': return 15;
	}

	return -1;
}

char *urlunescape(char *url)
{
	char *pin, *pout, *result;

	pin = url;
	pout = result = (char *) malloc(strlen(pin) + 1);
	while (*pin) {
		if (*pin == '+') {
			*pout = ' ';
			pin++;
		}
		else if (*pin == '%') {
			pin++;
			if ((strlen(pin) >= 2) && isxdigit((int)*pin) && isxdigit((int)*(pin+1))) {
				*pout = 16*hexvalue(*pin) + hexvalue(*(pin+1));
				pin += 2;
			}
			else {
				*pout = '%';
				pin++;
			}
		}
		else {
			*pout = *pin;
			pin++;
		}

		pout++;
	}

	*pout = '\0';

	return result;
}

char *urldecode(char *envvar)
{
	if (getenv(envvar) == NULL) return NULL;

	return urlunescape(getenv(envvar));
}

char *urlencode(char *s)
{
	static char result[1024];
	char *inp, *outp;

	outp = result;
	for (inp = s; (inp && *inp && (outp-result < sizeof(result)) ); inp++) {
		if ( ( (*inp >= 'a') && (*inp <= 'z') ) ||
		     ( (*inp >= 'A') && (*inp <= 'Z') ) ||
		     ( (*inp >= '0') && (*inp <= '9') ) ) {
			*outp = *inp;
			outp++;
		}
		else {
			sprintf(outp, "%%%0x", *inp);
			outp += 3;
		}
	}

	*outp = '\0';
	return result;
}

int urlvalidate(char *query, char *validchars)
{
	char *p;
	int valid;

	if (validchars == NULL) validchars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ,.-:&_%=*+/ ";

	for (p=query, valid=1; (valid && *p); p++) {
		valid = (strchr(validchars, *p) != NULL);
	}

	return valid;
}

time_t sslcert_expiretime(char *timestr)
{
	int res;
	time_t t1, t2;
	struct tm *t;
	struct tm exptime;
	time_t gmtofs, result;

	/* expire date: 2004-01-02 08:04:15 GMT */
	res = sscanf(timestr, "%4d-%2d-%2d %2d:%2d:%2d", 
		     &exptime.tm_year, &exptime.tm_mon, &exptime.tm_mday,
		     &exptime.tm_hour, &exptime.tm_min, &exptime.tm_sec);
	if (res != 6) {
		errprintf("Cannot interpret certificate time %s\n", timestr);
		return 0;
	}

	/* tm_year is 1900 based; tm_mon is 0 based */
	exptime.tm_year -= 1900; exptime.tm_mon -= 1;
	result = mktime(&exptime);

	if (result > 0) {
		/* 
		 * Calculate the difference between localtime and GMT 
		 */
		t = gmtime(&result); t->tm_isdst = 0; t1 = mktime(t);
		t = localtime(&result); t->tm_isdst = 0; t2 = mktime(t);
		gmtofs = (t2-t1);

		result += gmtofs;
	}
	else {
		/*
		 * mktime failed - probably it expires after the
		 * Jan 19,2038 rollover for a 32-bit time_t.
		 */

		result = INT_MAX;
	}

	dprintf("Output says it expires: %s", timestr);
	dprintf("I think it expires at (localtime) %s\n", asctime(localtime(&result)));

	return result;
}

unsigned int IPtou32(int ip1, int ip2, int ip3, int ip4)
{
	return ((ip1 << 24) | (ip2 << 16) | (ip3 << 8) | (ip4));
}

char *u32toIP(unsigned int ip32)
{
	int ip1, ip2, ip3, ip4;
	static char result[16];

	ip1 = ((ip32 >> 24) & 0xFF);
	ip2 = ((ip32 >> 16) & 0xFF);
	ip3 = ((ip32 >> 8) & 0xFF);
	ip4 = (ip32 & 0xFF);

	sprintf(result, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
	return result;
}

void addtobuffer(char **buf, int *buflen, char *newtext)
{
	if (*buf == NULL) {
		*buflen = 4096;
		*buf = (char *) malloc(*buflen);
		**buf = '\0';
	}
	else if ((strlen(*buf) + strlen(newtext)) > *buflen) {
		*buflen += strlen(newtext) + 4096;
		*buf = (char *) realloc(*buf, *buflen);
	}

	strcat(*buf, newtext);
}


int run_command(char *cmd, char *errortext, char **banner, int *bannerbytes, int showcmd)
{
	FILE	*cmdpipe;
	char	l[1024];
	int	result;
	int	piperes;
	int	bannersize = 0;

	result = 0;
	if (banner) { 
		bannersize = 4096;
		*banner = (char *) malloc(bannersize); 
		**banner = '\0';
		if (showcmd) sprintf(*banner, "Command: %s\n\n", cmd); 
	}
	cmdpipe = popen(cmd, "r");
	if (cmdpipe == NULL) {
		errprintf("Could not open pipe for command %s\n", cmd);
		if (banner) strcat(*banner, "popen() failed to run command\n");
		return -1;
	}

	while (fgets(l, sizeof(l), cmdpipe)) {
		if (banner) {
			if ((strlen(l) + strlen(*banner)) > bannersize) {
				bannersize += strlen(l) + 4096;
				*banner = (char *) realloc(*banner, bannersize);
			}
			strcat(*banner, l);
		}
		if (errortext && (strstr(l, errortext) != NULL)) result = 1;
	}
	piperes = pclose(cmdpipe);
	if (!WIFEXITED(piperes) || (WEXITSTATUS(piperes) != 0)) {
		/* Something failed */
		result = 1;
	}

	if (bannerbytes) *bannerbytes = strlen(*banner);
	return result;
}

