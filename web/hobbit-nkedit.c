/*----------------------------------------------------------------------------*/
/* Hobbit CGI for administering the hobbit-nkview.cfg file                    */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-nkedit.c,v 1.1 2006-01-20 16:14:12 henrik Exp $";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libbbgen.h"

static enum { NKEDIT_FIND, NKEDIT_UPDATE, NKEDIT_DELETE } editaction = NKEDIT_FIND;
static char *hostname = NULL;
static char *service = NULL;

static void parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		if (strcasecmp(cwalk->name, "Find") == 0) {
			editaction = NKEDIT_FIND;
		}
		else if (strcasecmp(cwalk->name, "Update") == 0) {
			editaction = NKEDIT_UPDATE;
		}
		else if (strcasecmp(cwalk->name, "HOSTNAME") == 0) {
			hostname = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "SERVICE") == 0) {
			service = strdup(cwalk->value);
		}

		cwalk = cwalk->next;
	}
}

void findrecord(char *hostname, char *service)
{
	int formfile;
	char formfn[PATH_MAX];
	nkconf_t *rec = NULL;
	int cloned = 0;

	if (hostname && service) {
		char *key = (char *)malloc(strlen(hostname) + strlen(service) + 2);
		char *realkey;

		sprintf(key, "%s|%s", hostname, service);
		rec = get_nkconfig(key, NKCONF_FIRSTMATCH, &realkey);
		if (strcmp(key, realkey) != 0) {
			char *p;

			xfree(key);
			key = strdup(realkey);
			hostname = realkey;
			p = strchr(realkey, '|');
			if (p) {
				*p = '\0';
				service = p+1;
			}

			cloned = 1;
		}
	}
	else {
		hostname = service = "";
	}

	if (rec) sethostenv_nkedit(rec->priority, rec->ttgroup, rec->starttime, rec->endtime, rec->nktime, rec->ttextra);
	else sethostenv_nkedit(0, NULL, 0, 0, NULL, NULL);

	sprintf(formfn, "%s/web/nkedit_form", xgetenv("BBHOME"));
	formfile = open(formfn, O_RDONLY);

	if (formfile >= 0) {
		char *inbuf;
		struct stat st;

		fstat(formfile, &st);
		inbuf = (char *) malloc(st.st_size + 1);
		read(formfile, inbuf, st.st_size);
		inbuf[st.st_size] = '\0';
		close(formfile);

		printf("Content-Type: text/html\n\n");
		sethostenv(hostname, "", service, colorname(COL_BLUE), NULL);

		headfoot(stdout, "nkedit", "", "header", COL_BLUE);
		if (cloned) {
			fprintf(stdout, "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('Cloned record - showing origin host');</SCRIPT>\n");
		}
		output_parsed(stdout, inbuf, COL_BLUE, "nkedit", time(NULL));
		headfoot(stdout, "nkedit", "", "footer", COL_BLUE);

		xfree(inbuf);
	}
}


int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
	char *operator = NULL;

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
	}

	redirect_cgilog("hobbit-nkedit");
	parse_query();

	/* Get the login username */
	operator = getenv("REMOTE_USER");
	if (!operator) {
	}

	load_nkconfig(NULL);

	switch (editaction) {
	  case NKEDIT_FIND:
		findrecord(hostname, service);
		break;

	  case NKEDIT_UPDATE:
		break;

	  case NKEDIT_DELETE:
		break;
	}

	return 0;
}

