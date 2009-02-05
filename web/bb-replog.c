/*----------------------------------------------------------------------------*/
/* Hobbit report-mode statuslog viewer.                                       */
/*                                                                            */
/* This tool generates the report status log for a single status, with the    */
/* availability percentages etc needed for a report-mode view.                */
/*                                                                            */
/* Copyright (C) 2003-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-replog.c,v 1.39 2006-05-03 21:12:33 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libbbgen.h"

char *hostname = NULL;
char *displayname = NULL;
char *ip = NULL;
char *reporttime = NULL;
char *service = NULL;
time_t st, end;
int style;
int color;
double reportgreenlevel = 99.995;
double reportwarnlevel = 98.0;
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
			char *p = strrchr(cwalk->value, '.');

			if (p) { *p = '\0'; service = strdup(p+1); }
			hostname = strdup(cwalk->value);
			while ((p = strchr(hostname, ','))) *p = '.';
		}
		else if (strcasecmp(cwalk->name, "HOST") == 0) {
			hostname = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "SERVICE") == 0) {
			service = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "REPORTTIME") == 0) {
			reporttime = (char *) malloc(strlen(cwalk->value)+strlen("REPORTTIME=")+1);
			sprintf(reporttime, "REPORTTIME=%s", cwalk->value);
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
			char *colstr = (char *) malloc(strlen(cwalk->value)+2);
			sprintf(colstr, "%s ", cwalk->value);
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
	char histlogfn[PATH_MAX];
	FILE *fd;
	char textrepfullfn[PATH_MAX], textrepfn[1024], textrepurl[PATH_MAX];
	FILE *textrep;
	reportinfo_t repinfo;
	int argi;
	char *envarea = NULL;
	void *hinfo;

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

	redirect_cgilog("bb-replog");

	cgidata = cgi_request();
	parse_query();
	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
        if ((hinfo = hostinfo(hostname)) == NULL) {
		errormsg("No such host");
		return 1;
	}
	ip = bbh_item(hinfo, BBH_IP);
	displayname = bbh_item(hinfo, BBH_DISPLAYNAME);
	if (!displayname) displayname = hostname;

	sprintf(histlogfn, "%s/%s.%s", xgetenv("BBHIST"), commafy(hostname), service);
	fd = fopen(histlogfn, "r");
	if (fd == NULL) {
		errormsg("Cannot open history file");
	}

	color = parse_historyfile(fd, &repinfo, hostname, service, st, end, 0, reportwarnlevel, reportgreenlevel, reporttime);
	fclose(fd);

	sprintf(textrepfn, "avail-%s-%s-%u-%u.txt", hostname, service, (unsigned int)getcurrenttime(NULL), (int)getpid());
	sprintf(textrepfullfn, "%s/%s", xgetenv("BBREP"), textrepfn);
	sprintf(textrepurl, "%s/%s", xgetenv("BBREPURL"), textrepfn);
	textrep = fopen(textrepfullfn, "w");

	/* Now generate the webpage */
	printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	generate_replog(stdout, textrep, textrepurl, 
			hostname, service, color, style, 
			ip, displayname,
			st, end, reportwarnlevel, reportgreenlevel, 
			&repinfo);

	if (textrep) fclose(textrep);
	return 0;
}

