/*----------------------------------------------------------------------------*/
/* Hobbit report-mode statuslog viewer.                                       */
/*                                                                            */
/* This tool generates the report status log for a single status, with the    */
/* availability percentages etc needed for a report-mode view.                */
/*                                                                            */
/* Copyright (C) 2003-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-replog.c,v 1.36 2006-02-08 22:09:47 henrik Exp $";

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

static void errormsg(char *msg)
{
	printf("Content-type: text/html\n\n");
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

static void parse_query(void)
{
	char *query, *token;

	if (xgetenv("QUERY_STRING") == NULL) {
		errormsg("Invalid request");
		return;
	}
	else query = urldecode("QUERY_STRING");

	if (!urlvalidate(query, NULL)) {
		errormsg("Invalid request");
		return;
	}

	token = strtok(query, "&");
	while (token) {
		char *val;
		
		val = strchr(token, '='); if (val) { *val = '\0'; val++; }

		if (argnmatch(token, "HOSTSVC")) {
			char *p = strrchr(val, '.');

			if (p) { *p = '\0'; service = strdup(p+1); }
			hostname = strdup(val);
			while ((p = strchr(hostname, ','))) *p = '.';
		}
		else if (argnmatch(token, "HOST")) {
			hostname = strdup(val);
		}
		else if (argnmatch(token, "SERVICE")) {
			service = strdup(val);
		}
		else if (argnmatch(token, "REPORTTIME")) {
			reporttime = (char *) malloc(strlen(val)+strlen("REPORTTIME=")+1);
			sprintf(reporttime, "REPORTTIME=%s", val);
		}
		else if (argnmatch(token, "WARNPCT")) {
			reportwarnlevel = atof(val);
		}
		else if (argnmatch(token, "STYLE")) {
			if (strcmp(val, "crit") == 0) style = STYLE_CRIT;
			else if (strcmp(val, "nongr") == 0) style = STYLE_NONGR;
			else style = STYLE_OTHER;
		}
		else if (argnmatch(token, "ST")) {
			/* Must be after "STYLE" */
			st = atol(val);
		}
		else if (argnmatch(token, "END")) {
			end = atol(val);
		}
		else if (argnmatch(token, "COLOR")) {
			char *colstr = (char *) malloc(strlen(val)+2);
			sprintf(colstr, "%s ", val);
			color = parse_color(colstr);
			xfree(colstr);
		}
		else if (argnmatch(token, "RECENTGIFS")) {
			use_recentgifs = atoi(val);
		}

		token = strtok(NULL, "&");
	}

	xfree(query);
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
	namelist_t *hinfo;

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

	sprintf(textrepfn, "avail-%s-%s-%u-%u.txt", hostname, service, (unsigned int)time(NULL), (int)getpid());
	sprintf(textrepfullfn, "%s/%s", xgetenv("BBREP"), textrepfn);
	sprintf(textrepurl, "%s/%s", xgetenv("BBREPURL"), textrepfn);
	textrep = fopen(textrepfullfn, "w");

	/* Now generate the webpage */
	printf("Content-Type: text/html\n\n");

	generate_replog(stdout, textrep, textrepurl, 
			hostname, service, color, style, 
			ip, displayname,
			st, end, reportwarnlevel, reportgreenlevel, 
			&repinfo);

	if (textrep) fclose(textrep);
	return 0;
}

