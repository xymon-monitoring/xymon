/*----------------------------------------------------------------------------*/
/* Xymon webpage generator tool.                                              */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: useradm.c 6588 2010-11-14 17:21:19Z storner $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "libxymon.h"

static void errormsg(char *msg)
{
	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

static int idcompare(const void *p1, const void *p2)
{
	return strcmp(* (char * const *) p1, * (char * const *) p2);
}

#define ACT_NONE 0
#define ACT_CREATE 1
#define ACT_DELETE 2

char *adduser_name = NULL;
char *adduser_password = NULL;
char *deluser_name = NULL;

int parse_query(void)
{
	cgidata_t *cgidata, *cwalk;
	int returnval = ACT_NONE;

	cgidata = cgi_request();
	if (cgi_method != CGI_POST) return ACT_NONE;

	if (cgidata == NULL) errormsg(cgi_error());

	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcmp(cwalk->name, "USERNAME") == 0) {
			adduser_name = cwalk->value;
		}
		else if (strcmp(cwalk->name, "PASSWORD") == 0) {
			adduser_password = cwalk->value;
		}
		else if (strcmp(cwalk->name, "USERLIST") == 0) {
			deluser_name = cwalk->value;
		}
		else if (strcmp(cwalk->name, "SendCreate") == 0) {
			returnval = ACT_CREATE;
		}
		else if (strcmp(cwalk->name, "SendDelete") == 0) {
			returnval = ACT_DELETE;
		}

		cwalk = cwalk->next;
	}

	return returnval;
}

int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
	char *hffile = "useradm";
	char *passfile = NULL;
	FILE *fd;
	char *infomsg = NULL;

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
		else if (argnmatch(argv[argi], "--passwdfile=")) {
			char *p = strchr(argv[argi], '=');
			passfile = strdup(p+1);
		}
	}

	if (passfile == NULL) {
		passfile = (char *)malloc(strlen(xgetenv("XYMONHOME")) + 20);
		sprintf(passfile, "%s/etc/xymonpasswd", xgetenv("XYMONHOME"));
	}

	switch (parse_query()) {
	  case ACT_NONE:	/* Show the form */
		break;


	  case ACT_CREATE:	/* Add a user */
		{
			char *cmd;
			int n, ret;

			const size_t bufsz = 1024 + strlen(passfile) + strlen(adduser_name) + strlen(adduser_password);
			cmd = (char *)malloc(bufsz);
			snprintf(cmd, bufsz, "htpasswd -b '%s' '%s' '%s'",
				 passfile, adduser_name, adduser_password);
			n = system(cmd); ret = WEXITSTATUS(n);
			if ((n == -1) || (ret != 0)) {
				infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('Update FAILED'); </SCRIPT>\n";

			}
			else {
				infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('User added/updated'); </SCRIPT>\n";
			}

			xfree(cmd);
		}
		break;


	  case ACT_DELETE:	/* Delete a user */
		{
			char *cmd;
			int n, ret;

			const size_t bufsz = 1024 + strlen(passfile) + strlen(deluser_name);
			cmd = (char *)malloc(bufsz);
			snprintf(cmd, bufsz, "htpasswd -D '%s' '%s'",
					passfile, deluser_name);
			n = system(cmd); ret = WEXITSTATUS(n);
			if ((n == -1) || (ret != 0)) {
				infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('Update delete FAILED'); </SCRIPT>\n";

			}
			else {
				infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('User deleted'); </SCRIPT>\n";
			}

			xfree(cmd);
		}
		break;
	}

	sethostenv_clearlist(NULL);
	sethostenv_addtolist(NULL, "", "", NULL, 1); /* Have a blank entry first so we won't delete one by accident */
	fd = fopen(passfile, "r");

	if (fd != NULL) {
		char l[1024];
		char *id, *delim;
		int usercount;
		char **userlist;
		int i;

		usercount = 0;
		userlist = (char **)calloc(usercount+1, sizeof(char *));

		while (fgets(l, sizeof(l), fd)) {
			id = l; delim = strchr(l, ':'); 
			if (delim) {
				*delim = '\0';
				usercount++;
				userlist = (char **)realloc(userlist, (usercount+1)*sizeof(char *));
				userlist[usercount-1] = strdup(id);
				userlist[usercount] = NULL;
			}
		}

		fclose(fd);

		qsort(&userlist[0], usercount, sizeof(char *), idcompare);
		for (i=0; (userlist[i]); i++) sethostenv_addtolist(NULL, userlist[i], userlist[i], NULL, 0);
	}

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	showform(stdout, hffile, "useradm_form", COL_BLUE, getcurrenttime(NULL), infomsg, NULL);

	return 0;
}

