/*----------------------------------------------------------------------------*/
/* Hobbit backend script for disabling/enabling tests.                        */
/*                                                                            */
/* Copyright (C) 2003-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-enadis.c,v 1.3 2005-04-16 09:54:44 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include "libbbgen.h"

enum { ACT_NONE, ACT_FILTER, ACT_ENABLE, ACT_DISABLE, ACT_SCHED_DISABLE, ACT_SCHED_CANCEL } action = ACT_NONE;
int hostcount = 0;
char **hostnames  = NULL;
int disablecount = 0;
char **disabletest = NULL;
char *enabletest = NULL;
int duration = 0;
int scale = 1;
char *disablemsg = "No reason given";
time_t schedtime = 0;
int cancelid = 0;
int preview = 0;

char *hostpattern = NULL;
char *pagepattern = NULL;
char *ippattern = NULL;

void errormsg(char *msg)
{
        printf("Content-type: text/html\n\n");
        printf("<html><head><title>Invalid request</title></head>\n");
        printf("<body>%s</body></html>\n", msg);
        exit(1);
}

void parse_post(void)
{
	char l[4096];
        char *token;
	struct tm schedtm;

	memset(&schedtm, 0, sizeof(schedtm));

	while (fgets(l, sizeof(l), stdin)) {
		l[sizeof(l)-1] = '\0';
		token = strtok(l, "&");

		while (token) {
			char *val;

			val = strchr(token, '='); if (val) { *val = '\0'; val++; }
			if (val) val = urlunescape(val);

			/*
			 * When handling the "go", the "Disable now" and "Schedule disable"
			 * radio buttons mess things up. So ignore the "go" if we have seen a
			 * "filter" request already.
			 */
			if ((strcmp(token, "go") == 0) && (action != ACT_FILTER)) {
				if      (strcasecmp(val, "enable") == 0)           action = ACT_ENABLE;
				else if (strcasecmp(val, "disable now") == 0)      action = ACT_DISABLE;
				else if (strcasecmp(val, "schedule disable") == 0) action = ACT_SCHED_DISABLE;
				else if (strcasecmp(val, "cancel") == 0)           action = ACT_SCHED_CANCEL;
				else if (strcasecmp(val, "apply filters") == 0)    action = ACT_FILTER;
			}
			else if (strcmp(token, "duration") == 0) {
				duration = atoi(val);
			}
			else if (strcmp(token, "scale") == 0) {
				scale = atoi(val);
			}
			else if (strcmp(token, "cause") == 0) {
				disablemsg = strdup(val);
			}
			else if (strcmp(token, "hostname") == 0) {
				if (hostnames == NULL) {
					hostnames = (char **)malloc(2 * sizeof(char *));
					hostnames[0] = strdup(val);
					hostnames[1] = NULL;
					hostcount = 1;
				}
				else {
					hostnames = (char **)realloc(hostnames, (hostcount + 2) * sizeof(char *));
					hostnames[hostcount] = strdup(val);
					hostnames[hostcount+1] = NULL;
					hostcount++;
				}
			}
			else if (strcmp(token, "enabletest") == 0) {
				enabletest = strdup(val);
			}
			else if (strcmp(token, "disabletest") == 0) {
				if (disabletest == NULL) {
					disabletest = (char **)malloc(2 * sizeof(char *));
					disabletest[0] = strdup(val);
					disabletest[1] = NULL;
					disablecount = 1;
				}
				else {
					disabletest = (char **)realloc(disabletest, (disablecount + 2) * sizeof(char *));
					disabletest[disablecount] = strdup(val);
					disabletest[disablecount+1] = NULL;
					disablecount++;
				}
			}
			else if (strcmp(token, "year") == 0) {
				schedtm.tm_year = atoi(val) - 1900;
			}
			else if (strcmp(token, "month") == 0) {
				schedtm.tm_mon = atoi(val) - 1;
			}
			else if (strcmp(token, "day") == 0) {
				schedtm.tm_mday = atoi(val);
			}
			else if (strcmp(token, "hour") == 0) {
				schedtm.tm_hour = atoi(val);
			}
			else if (strcmp(token, "minute") == 0) {
				schedtm.tm_min = atoi(val);
			}
			else if (strcmp(token, "canceljob") == 0) {
				cancelid = atoi(val);
			}
			else if (strcmp(token, "preview") == 0) {
				preview = (strcasecmp(val, "on") == 0);
			}
			else if ((strcmp(token, "hostpattern") == 0) && val && strlen(val)) {
				hostpattern = strdup(val);
			}
			else if ((strcmp(token, "pagepattern") == 0) && val && strlen(val)) {
				pagepattern = strdup(val);
			}
			else if ((strcmp(token, "ippattern") == 0)   && val && strlen(val)) {
				ippattern = strdup(val);
			}

			token = strtok(NULL, "&");
		}
	}

	schedtm.tm_isdst = -1;
	schedtime = mktime(&schedtm);
}

void do_one_host(char *hostname, char *fullmsg)
{
	char hobbitcmd[4096];
	int i, result;

	switch (action) {
	  case ACT_ENABLE:
		sprintf(hobbitcmd, "enable %s.%s", commafy(hostname), enabletest);
		result = (preview ? 0 : sendmessage(hobbitcmd, NULL, NULL, NULL, 0, BBTALK_TIMEOUT));
		printf("<tr><td>Enabling host <b>%s</b> test <b>%s</b> : %s</td></tr>\n", hostname, enabletest, ((result == BB_OK) ? "OK" : "Failed"));
		break;

	  case ACT_DISABLE:
		for (i=0; (i < disablecount); i++) {
			sprintf(hobbitcmd, "disable %s.%s %d %s", 
				commafy(hostname), disabletest[i], duration*scale, fullmsg);
			result = (preview ? 0 : sendmessage(hobbitcmd, NULL, NULL, NULL, 0, BBTALK_TIMEOUT));
			printf("<tr><td>Disabling host <b>%s</b> test <b>%s</b>: %s</td></tr>\n", 
				hostname, disabletest[i], ((result == BB_OK) ? "OK" : "Failed"));
		}
		break;

	  case ACT_SCHED_DISABLE:
		for (i=0; (i < disablecount); i++) {
			sprintf(hobbitcmd, "schedule %d disable %s.%s %d %s", 
				(int) schedtime, commafy(hostname), disabletest[i], duration*scale, fullmsg);
			result = (preview ? 0 : sendmessage(hobbitcmd, NULL, NULL, NULL, 0, BBTALK_TIMEOUT));
			printf("<tr><td>Scheduling disable of host <b>%s</b> test <b>%s</b> at <b>%s</b>: %s</td></tr>\n", 
				hostname, disabletest[i], ctime(&schedtime), ((result == BB_OK) ? "OK" : "Failed"));
		}
		break;

	  case ACT_SCHED_CANCEL:
		sprintf(hobbitcmd, "schedule cancel %d", cancelid);
		result = (preview ? 0 : sendmessage(hobbitcmd, NULL, NULL, NULL, 0, BBTALK_TIMEOUT));
		printf("<tr><td>Canceling job <b>%d</b> : %s</td></tr>\n", cancelid, ((result == BB_OK) ? "OK" : "Failed"));
		break;

	  default:
		errprintf("No action\n");
		break;
	}
}

int main(int argc, char *argv[])
{
	int argi, i;
	int waittime = 3;
	char *username = getenv("REMOTE_USER");
	char *userhost = getenv("REMOTE_HOST");
	char *userip   = getenv("REMOTE_ADDR");
	char *fullmsg = "No cause specified";

	if ((username == NULL) || (strlen(username) == 0)) username = "unknown";
	if ((userhost == NULL) || (strlen(userhost) == 0)) userhost = userip;
	
	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
			waittime = 15;
		}
	}

	if (strcmp(getenv("REQUEST_METHOD"), "POST") == 0) parse_post();
	else action = ACT_FILTER;

	if (action == ACT_FILTER) {
		/* Present the query form */
		int formfile;
		char formfn[PATH_MAX];

		sethostenv_filter(hostpattern, pagepattern, ippattern);

		load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());

		sprintf(formfn, "%s/web/maint_form", xgetenv("BBHOME"));
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
			sethostenv("", "", "", colorname(COL_BLUE));

			headfoot(stdout, "maint", "", "header", COL_BLUE);
			output_parsed(stdout, inbuf, COL_BLUE, "report", time(NULL));
			headfoot(stdout, "maint", "", "footer", COL_BLUE);

			xfree(inbuf);
		}
		return 0;
	}

	fullmsg = (char *)malloc(strlen(username) + strlen(userhost) + strlen(disablemsg) + 1024);
	sprintf(fullmsg, "\nDisabled by: %s @ %s\nReason: %s\n", username, userhost, disablemsg);

	if (preview) waittime = 60;

	/*
	 * Ready ... go build the webpage.
	 */
	printf("Content-Type: text/html\n\n");
	printf("<html>\n");
	printf("<head>\n<meta http-equiv=\"refresh\" content=\"%d; URL=%s\"></head>\n", 
		waittime, xgetenv("HTTP_REFERER"));

        /* It's ok with these hardcoded values, as they are not used for this page */
	sethostenv("", "", "", colorname(COL_BLUE));
	headfoot(stdout, "maint", "", "header", COL_BLUE);

	if (debug) {
		printf("<pre>\n");
		switch (action) {
		  case ACT_NONE   : dprintf("Action = none\n"); break;

		  case ACT_FILTER : dprintf("Action = filter\n"); break;

		  case ACT_ENABLE : dprintf("Action = enable, Test = %s\n", textornull(enabletest)); 
				    break;

		  case ACT_DISABLE: dprintf("Action = disable\n"); 
				    dprintf("Tests = ");
				    for (i=0; (i < disablecount); i++) printf("%s ", disabletest[i]);
				    printf("\n");
				    dprintf("Duration = %d, scale = %d\n", duration, scale);
				    dprintf("Cause = %s\n", textornull(disablemsg));
				    break;

		  case ACT_SCHED_DISABLE:
				    dprintf("Action = schedule\n");
				    dprintf("Time = %s\n", ctime(&schedtime));
				    dprintf("Tests = ");
				    for (i=0; (i < disablecount); i++) printf("%s ", disabletest[i]);
				    printf("\n");
				    dprintf("Duration = %d, scale = %d\n", duration, scale);
				    dprintf("Cause = %s\n", textornull(disablemsg));
				    break;

		  case ACT_SCHED_CANCEL:
				    dprintf("Action = cancel\n");
				    dprintf("ID = %d\n", cancelid);
				    break;
		}
		printf("</pre>\n");
	}

	printf("<table align=\"center\" summary=\"Actions performed\" width=\"60%%\">\n");
	if (action == ACT_SCHED_CANCEL) {
		do_one_host(NULL, NULL);
	}
	else {
		for (i = 0; (i < hostcount); i++) do_one_host(hostnames[i], fullmsg);
	}
	printf("<tr><td><br>Please wait while refreshing status list ...</td></tr>\n");
	printf("</table>\n");

	headfoot(stdout, "maint", "", "footer", COL_BLUE);

	return 0;
}

