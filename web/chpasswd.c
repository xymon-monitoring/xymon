/*----------------------------------------------------------------------------*/
/* Xymon webpage generator tool.                                              */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: chpasswd.c 6588 2010-11-14 17:21:19Z storner $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "libxymon.h"

static void errormsg(int status, char *msg)
{
	printf("Status: %d\n", status);
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
#define ACT_DELETE 2
#define ACT_UPDATE 3

char *adduser_name = NULL;
char *adduser_password = NULL;
char *adduser_password1 = NULL;
char *adduser_password2 = NULL;
char *deluser_name = NULL;

int parse_query(void)
{
	cgidata_t *cgidata, *cwalk;
	int returnval = ACT_NONE;

	cgidata = cgi_request();
	if (cgi_method != CGI_POST) return ACT_NONE;

	if (cgidata == NULL) errormsg(400, cgi_error());

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
		else if (strcmp(cwalk->name, "PASSWORD1") == 0) {
			adduser_password1 = cwalk->value;
		}
		else if (strcmp(cwalk->name, "PASSWORD2") == 0) {
			adduser_password2 = cwalk->value;
		}
		else if (strcmp(cwalk->name, "USERLIST") == 0) {
			deluser_name = cwalk->value;
		}
		else if (strcmp(cwalk->name, "SendDelete") == 0) {
			returnval = ACT_DELETE;
		}
		else if (strcmp(cwalk->name, "SendUpdate") == 0) {
			returnval = ACT_UPDATE;
		}

		cwalk = cwalk->next;
	}

	return returnval;
}

int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
	char *hffile = "chpasswd";
	char *passfile = NULL;
	FILE *fd;
	char *infomsg = NULL;
	char *loggedinuser = NULL;

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

	loggedinuser = getenv("REMOTE_USER");
        if (!loggedinuser) errormsg(401, "User authentication must be enabled and you must be logged in to use this CGI");

	switch (parse_query()) {
	  case ACT_NONE:	/* Show the form */
		break;

	  case ACT_UPDATE:	/* Change a user password*/
		{
			char *cmd;
			int n, ret;

			if ( (strlen(loggedinuser) == 0) || (strlen(loggedinuser) != strlen(adduser_name)) || (strcmp(loggedinuser, adduser_name) != 0) ) {
				infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('Username mismatch! You may only change your own password.'); </SCRIPT>\n";
				break;
			}

			if ( (strlen(adduser_name) == 0)) {
				infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('I dont know who you are!'); </SCRIPT>\n";
			}
			else if ( (strlen(adduser_password1) == 0) || (strlen(adduser_password2) == 0)) {
				infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('New password cannot be blank'); </SCRIPT>\n";
			}
			else if (strcmp(adduser_password1, adduser_password2) != 0) {
				infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('New passwords dont match'); </SCRIPT>\n";
			}
			else if (strlen(adduser_name) != strspn(adduser_name,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.,@/=^") ) {
				infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('Username has invalid characters!'); </SCRIPT>\n";
			}
			else if (strlen(adduser_password1) != strspn(adduser_password1,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.,@/=^") ) {
				infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('Password has invalid characters! Use alphanumerics and/or _ - . , @ / = ^'); </SCRIPT>\n";
			}
			else {
				const size_t bufsz = 1024 + strlen(passfile) + strlen(adduser_name) + strlen(adduser_password);

				cmd = (char *)malloc(bufsz);
				snprintf(cmd, bufsz, "htpasswd -bv '%s' '%s' '%s'",
					 passfile, adduser_name, adduser_password);
				n = system(cmd); ret = WEXITSTATUS(n);
				if ((n == -1) || (ret != 0)) {
					infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('Existing Password incorrect'); </SCRIPT>\n";

				}
				else {

					xfree(cmd);
					cmd = (char *)malloc(bufsz);
					snprintf(cmd, bufsz, "htpasswd -b '%s' '%s' '%s'",
					 	passfile, adduser_name, adduser_password1);
					n = system(cmd); ret = WEXITSTATUS(n);
					if ((n == -1) || (ret != 0)) {
						infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('Update FAILED'); </SCRIPT>\n";

					}
					else {
						infomsg = "<SCRIPT LANGUAGE=\"Javascript\" type=\"text/javascript\"> alert('Password changed'); </SCRIPT>\n";
					}
				}

				xfree(cmd);
			}
		}
		break;

	}

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	showform(stdout, hffile, "chpasswd_form", COL_BLUE, getcurrenttime(NULL), infomsg, NULL);

	return 0;
}

