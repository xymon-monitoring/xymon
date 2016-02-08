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
#include <unistd.h>

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

	/* We only want to accept posts from certain pages */
	
	if (returnval != ACT_NONE) {
		char cgisource[1024]; char *p;
		p = csp_header("chpasswd"); if (p) fprintf(stdout, "%s", p);
		snprintf(cgisource, sizeof(cgisource), "%s/%s", xgetenv("SECURECGIBINURL"), programname);
		if (!cgi_refererok(cgisource)) { fprintf(stdout, "Location: %s.sh?\n\n", cgisource); return 0; }
	}

	return returnval;
}

int main(int argc, char *argv[])
{
	int argi, event;
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

	event = parse_query();

	if (adduser_name && !issimpleword(adduser_name)) {
		event = ACT_NONE;
		adduser_name = strdup("");
		infomsg = "<strong><big><font color='#FF0000'>Invalid USERNAME. Letters, numbers, dashes, and periods only.</font></big></strong>\n";
	}

	switch (event) {
	  case ACT_NONE:	/* Show the form */
		break;

	  case ACT_UPDATE:	/* Change a user password*/
		{
			char *cmd;
			int n, ret;

			if ( (strlen(loggedinuser) == 0) || (strlen(loggedinuser) != strlen(adduser_name)) || (strcmp(loggedinuser, adduser_name) != 0) ) {
				infomsg = "Username mismatch! You may only change your own password.";
				break;
			}

			if ( (strlen(adduser_name) == 0)) {
				infomsg = "User not logged in";
			}
			else if ( (strlen(adduser_password1) == 0) || (strlen(adduser_password2) == 0)) {
				infomsg = "New password cannot be blank";
			}
			else if (strcmp(adduser_password1, adduser_password2) != 0) {
				infomsg = "New passwords dont match";
			}
			else if (strlen(adduser_name) != strspn(adduser_name,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.,@/=^") ) {
				infomsg = "Username has invalid characters!";
			}
			else if (strlen(adduser_password1) != strspn(adduser_password1,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.,@/=^") ) {
				infomsg = "Password has invalid characters! Use alphanumerics and/or _ - . , @ / = ^";
			}
			else {
				pid_t childpid;
				int n, ret;

				childpid = fork();
				if (childpid < 0) {
				        /* Fork failed */
				        errprintf("Could not fork child\n");
				        exit(1);
				}
				else if (childpid == 0) {
				        /* child */
				        char *cmd;
				        char **cmdargs;
				
				        cmdargs = (char **) calloc(4 + 2, sizeof(char *));
				        cmdargs[0] = cmd = strdup("htpasswd");
				        cmdargs[1] = "-bv";
				        cmdargs[2] = strdup(passfile);
				        cmdargs[3] = strdup(adduser_name);
				        cmdargs[4] = strdup(adduser_password);
				        cmdargs[5] = '\0';
				
				        execvp(cmd, cmdargs);
				        exit(127);
				}
				
				/* parent waits for htpasswd to finish */
				if ((waitpid(childpid, &n, 0) == -1) || (WEXITSTATUS(n) != 0)) {
					infomsg = "Existing Password incorrect";
					break;
				}

				childpid = fork();
				if (childpid < 0) {
				        /* Fork failed */
				        errprintf("Could not fork child\n");
				        exit(1);
				}
				else if (childpid == 0) {
				        /* child */
				        char *cmd;
				        char **cmdargs;
				
				        cmdargs = (char **) calloc(4 + 2, sizeof(char *));
				        cmdargs[0] = cmd = strdup("htpasswd");
				        cmdargs[1] = "-b";
				        cmdargs[2] = strdup(passfile);
				        cmdargs[3] = strdup(adduser_name);
				        cmdargs[4] = strdup(adduser_password1);
				        cmdargs[5] = '\0';
				
				        execvp(cmd, cmdargs);
				        exit(127);
				}
				
				/* parent waits for htpasswd to finish */
				if ((waitpid(childpid, &n, 0) == -1) || (WEXITSTATUS(n) != 0)) {
					infomsg = "Update FAILED";

				}
				else {
					infomsg = "<strong><big>Password changed</big></strong>\n";
				}
			}
		}
		break;

	}

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	showform(stdout, hffile, "chpasswd_form", COL_BLUE, getcurrenttime(NULL), infomsg, NULL);

	return 0;
}

