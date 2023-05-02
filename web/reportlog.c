/*----------------------------------------------------------------------------*/
/* Xymon report-mode statuslog viewer.                                        */
/*                                                                            */
/* This tool generates the report status log for a single status, with the    */
/* availability percentages etc needed for a report-mode view.                */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: reportlog.c 8072 2019-08-05 20:21:55Z jccleaver $";

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include "libxymon.h"

char *hostname = NULL;
char *displayname = NULL;
char *ip = NULL;
SBUF_DEFINE(reporttime);
char *service = NULL;
time_t st, end;
int style;
int color;
double reportgreenlevel = 99.995;
double reportwarnlevel = 98.0;
int    reportwarnstops = -1;
cgidata_t *cgidata = NULL;

static void errormsg(char *msg)
{
	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

static void parse_query(void)
{
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcasecmp(cwalk->name, "HOSTSVC") == 0) {
			char *p = cwalk->value + strspn(cwalk->value, "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:,.\\/_-");
			*p = '\0';

			p = strrchr(cwalk->value, '.');
			if (p) { *p = '\0'; service = strdup(p+1); }
			hostname = strdup(basename(cwalk->value));
			while ((p = strchr(hostname, ','))) *p = '.';
		}
		else if (strcasecmp(cwalk->name, "HOST") == 0) {
			char *p = cwalk->value + strspn(cwalk->value, "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:,._-");
			*p = '\0';
			hostname = strdup(basename(cwalk->value));
		}
		else if (strcasecmp(cwalk->name, "SERVICE") == 0) {
			char *p = cwalk->value + strspn(cwalk->value, "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:\\/_-");
			*p = '\0';
			service = strdup(basename(cwalk->value));
		}
		else if (strcasecmp(cwalk->name, "REPORTTIME") == 0) {
			SBUF_MALLOC(reporttime, strlen(cwalk->value)+strlen("REPORTTIME=")+1);
			snprintf(reporttime, reporttime_buflen, "REPORTTIME=%s", cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "WARNPCT") == 0) {
			reportwarnlevel = atof(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "STYLE") == 0) {
			if (strcmp(cwalk->value, "crit") == 0) style = STYLE_CRIT;
			else if (strcmp(cwalk->value, "nongr") == 0) style = STYLE_NONGR;
			else style = STYLE_OTHER;
		}
		else if (strcasecmp(cwalk->name, "ST") == 0) {
			/* Must be after "STYLE" */
			st = atol(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "END") == 0) {
			end = atol(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "COLOR") == 0) {
			SBUF_DEFINE(colstr);

			SBUF_MALLOC(colstr, strlen(cwalk->value)+2);
			snprintf(colstr, colstr_buflen, "%s ", cwalk->value);
			color = parse_color(colstr);
			xfree(colstr);
		}
		else if (strcasecmp(cwalk->name, "RECENTGIFS") == 0) {
			use_recentgifs = atoi(cwalk->value);
		}

		cwalk = cwalk->next;
	}
}

int main(int argc, char *argv[])
{
	SBUF_DEFINE(histlogfn);
	FILE *fd;
	SBUF_DEFINE(textrepfn);
	SBUF_DEFINE(textrepfullfn);
	SBUF_DEFINE(textrepurl);
	FILE *textrep;
	reportinfo_t repinfo;
	int argi;
	char *envarea = NULL;
	void *hinfo;

	SBUF_MALLOC(histlogfn, PATH_MAX);

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
	}

	redirect_cgilog("reportlog");

	cgidata = cgi_request();
	parse_query();
	load_hostinfo(hostname);
        if ((hinfo = hostinfo(hostname)) == NULL) {
		errormsg("No such host");
		return 1;
	}
	ip = xmh_item(hinfo, XMH_IP);
	displayname = xmh_item(hinfo, XMH_DISPLAYNAME);
	if (!displayname) displayname = hostname;

	snprintf(histlogfn, histlogfn_buflen, "%s/%s.%s", xgetenv("XYMONHISTDIR"), commafy(hostname), service);
	fd = fopen(histlogfn, "r");
	if (fd == NULL) {
		errormsg("Cannot open history file");
	}

	color = parse_historyfile(fd, &repinfo, hostname, service, st, end, 0, reportwarnlevel, reportgreenlevel, reportwarnstops, reporttime);
	fclose(fd);

	SBUF_MALLOC(textrepfn, 1024 + strlen(hostname) + strlen(service));
	snprintf(textrepfn, textrepfn_buflen, "avail-%s-%s-%u-%lu.txt", hostname, service, (unsigned int)getcurrenttime(NULL), (unsigned long)getpid());
	SBUF_MALLOC(textrepfullfn, 1024 + strlen(xgetenv("XYMONREPDIR")) + strlen(textrepfn));
	snprintf(textrepfullfn, textrepfullfn_buflen, "%s/%s", xgetenv("XYMONREPDIR"), textrepfn);
	SBUF_MALLOC(textrepurl, 1024 + strlen(xgetenv("XYMONREPURL")) + strlen(textrepfn));
	snprintf(textrepurl, textrepurl_buflen, "%s/%s", xgetenv("XYMONREPURL"), textrepfn);
	textrep = fopen(textrepfullfn, "w");

	/* Now generate the webpage */
	printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	generate_replog(stdout, textrep, textrepurl, 
			hostname, service, color, style, 
			ip, displayname,
			st, end, reportwarnlevel, reportgreenlevel, reportwarnstops, 
			&repinfo);

	if (textrep) fclose(textrep);
	return 0;
}

