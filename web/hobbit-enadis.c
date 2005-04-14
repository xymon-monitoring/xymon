/*----------------------------------------------------------------------------*/
/* Hobbit backend script for disabling/enabling tests.                        */
/*                                                                            */
/* Copyright (C) 2003-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-enadis.c,v 1.2 2005-04-14 13:01:23 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include "libbbgen.h"

enum { ACT_NONE, ACT_ENABLE, ACT_DISABLE, ACT_SCHED_DISABLE, ACT_SCHED_CANCEL } action = ACT_NONE;
char *hostname = NULL;
char *enabletest = NULL;
int duration = 0;
int scale = 1;
int disablecount = 0;
char **disabletest = NULL;
char *disablemsg = "No reason given";
time_t schedtime = 0;
int cancelid = 0;

void errormsg(char *msg)
{
        printf("Content-type: text/html\n\n");
        printf("<html><head><title>Invalid request</title></head>\n");
        printf("<body>%s</body></html>\n", msg);
        exit(1);
}

void parse_query(void)
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

			if (strcmp(token, "go") == 0) {
				if (strcasecmp(val, "enable") == 0) action = ACT_ENABLE;
				else if (strcasecmp(val, "disable now") == 0) action = ACT_DISABLE;
				else if (strcasecmp(val, "schedule disable") == 0) action = ACT_SCHED_DISABLE;
				else if (strcasecmp(val, "cancel") == 0) action = ACT_SCHED_CANCEL;
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
				hostname = strdup(val);
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

			token = strtok(NULL, "&");
		}
	}

	schedtm.tm_isdst = -1;
	schedtime = mktime(&schedtm);
}


int main(int argc, char *argv[])
{
	int argi, i, result;
	char hobbitcmd[4096];
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
		}
	}

	parse_query();
	fullmsg = (char *)malloc(strlen(username) + strlen(userhost) + strlen(disablemsg) + 1024);
	sprintf(fullmsg, "\nDisabled by: %s @ %s\nReason: %s\n", username, userhost, disablemsg);

	/*
	 * Ready ... go build the webpage.
	 */
	printf("Content-Type: text/html\n\n");
	printf("<html>\n");
	printf("<head>\n<meta http-equiv=\"refresh\" content=\"%d; URL=%s\"></head>\n", 
		(debug ? 15 : 5), xgetenv("HTTP_REFERER"));

        /* It's ok with these hardcoded values, as they are not used for this page */
	sethostenv("", "", "", colorname(COL_BLUE));
	headfoot(stdout, "maint", "", "header", COL_BLUE);

	if (debug) {
		printf("<pre>\n");
		dprintf("Hostname = %s\n", hostname);
		switch (action) {
		  case ACT_NONE   : dprintf("Action = none\n"); break;

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

	switch (action) {
	  case ACT_NONE:
		break;

	  case ACT_ENABLE:
		sprintf(hobbitcmd, "enable %s.%s", commafy(hostname), enabletest);
		result = sendmessage(hobbitcmd, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
		printf("<tr><td>Enabling host <b>%s</b> test <b>%s</b> : %s</td></tr>\n", hostname, enabletest, ((result == BB_OK) ? "OK" : "Failed"));
		break;

	  case ACT_DISABLE:
		for (i=0; (i < disablecount); i++) {
			sprintf(hobbitcmd, "disable %s.%s %d %s", 
				commafy(hostname), disabletest[i], duration*scale, fullmsg);
			result = sendmessage(hobbitcmd, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
			printf("<tr><td>Disabling host <b>%s</b> test <b>%s</b>: %s</td></tr>\n", 
				hostname, disabletest[i], ((result == BB_OK) ? "OK" : "Failed"));
		}
		break;

	  case ACT_SCHED_DISABLE:
		for (i=0; (i < disablecount); i++) {
			sprintf(hobbitcmd, "schedule %d disable %s.%s %d %s", 
				(int) schedtime, commafy(hostname), disabletest[i], duration*scale, fullmsg);
			result = sendmessage(hobbitcmd, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
			printf("<tr><td>Scheduling disable of host <b>%s</b> test <b>%s</b> at <b>%s</b>: %s</td></tr>\n", 
				hostname, disabletest[i], ctime(&schedtime), ((result == BB_OK) ? "OK" : "Failed"));
		}
		break;

	  case ACT_SCHED_CANCEL:
		sprintf(hobbitcmd, "schedule cancel %d", cancelid);
		result = sendmessage(hobbitcmd, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
		printf("<tr><td>Canceling job <b>%d</b> : %s</td></tr>\n", cancelid, ((result == BB_OK) ? "OK" : "Failed"));
		break;
	}

	printf("<tr><td><br>Please wait while refreshing status list ...</td></tr>\n");
	printf("</table>\n");

	headfoot(stdout, "maint", "", "footer", COL_BLUE);

	return 0;
}

