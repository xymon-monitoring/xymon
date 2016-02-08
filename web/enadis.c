/*----------------------------------------------------------------------------*/
/* Xymon backend script for disabling/enabling tests.                         */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

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

#include "libxymon.h"

enum { ACT_NONE, ACT_FILTER, ACT_ENABLE, ACT_DISABLE, ACT_SCHED_DISABLE, ACT_SCHED_CANCEL } action = ACT_NONE;
enum { DISABLE_FOR, DISABLE_UNTIL } disableend = DISABLE_FOR; /* disable until a date OR disable for a duration */

int hostcount = 0;
char **hostnames  = NULL;
int disablecount = 0;
char **disabletest = NULL;
int enablecount = 0;
char **enabletest = NULL;
int duration = 0;
int scale = 1;
char *disablemsg = "No reason given";
time_t schedtime = 0;
time_t endtime = 0;
time_t nowtime = 0;
time_t starttime = 0;
int cancelid = 0;
int preview = 0;

char *hostpattern = NULL;
char *pagepattern = NULL;
char *ippattern = NULL;
char *classpattern = NULL;

void errormsg(char *msg)
{
        printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
        printf("<html><head><title>Invalid request</title></head>\n");
        printf("<body>%s</body></html>\n", msg);
        exit(1);
}

void parse_cgi(void)
{
	cgidata_t *postdata, *pwalk;
	struct tm schedtm;
	struct tm endtm;
	struct tm nowtm;

	memset(&schedtm, 0, sizeof(schedtm));
	memset(&endtm, 0, sizeof(endtm));

	postdata = cgi_request();
	if (cgi_method == CGI_GET) return;


	/* We only want to accept posts from certain pages: svcstatus (for info), and ourselves */
	/* At some point in the future, moving info lookups to their own page would be a good idea */
	{
		char cgisource[1024]; char *p;
		p = csp_header("enadis"); if (p) fprintf(stdout, "%s", p);
		snprintf(cgisource, sizeof(cgisource), "%s/%s", xgetenv("SECURECGIBINURL"), "enadis");
		if (!cgi_refererok(cgisource)) {
			snprintf(cgisource, sizeof(cgisource), "%s/%s", xgetenv("CGIBINURL"), "svcstatus");
			if (!cgi_refererok(cgisource)) {
				dbgprintf("Not coming from self or svcstatus; abort\n");
				return;	/* Just display, don't do anything */
			}
		}
	}


	if (!postdata) {
		errormsg(cgi_error());
	}

	pwalk = postdata;
	while (pwalk) {
		/*
		 * When handling the "go", the "Disable now" and "Schedule disable"
		 * radio buttons mess things up. So ignore the "go" if we have seen a
		 * "filter" request already.
		 */
		if ((strcmp(pwalk->name, "go") == 0) && (action != ACT_FILTER)) {
			if      (strcasecmp(pwalk->value, "enable") == 0)           action = ACT_ENABLE;
			else if (strcasecmp(pwalk->value, "disable now") == 0)      action = ACT_DISABLE;
			else if (strcasecmp(pwalk->value, "schedule disable") == 0) action = ACT_SCHED_DISABLE;
			else if (strcasecmp(pwalk->value, "cancel") == 0)           action = ACT_SCHED_CANCEL;
			else if (strcasecmp(pwalk->value, "apply filters") == 0)    action = ACT_FILTER;
		}
		else if ((strcmp(pwalk->name, "go2") == 0) && (action != ACT_FILTER)) { 
			if (strcasecmp(pwalk->value, "Disable until") == 0) disableend = DISABLE_UNTIL;
		}
		else if (strcmp(pwalk->name, "duration") == 0) {
			duration = atoi(pwalk->value);
		}
		else if (strcmp(pwalk->name, "untilok") == 0) {
			if (strcasecmp(pwalk->value, "on") == 0) {
				duration = -1;
				scale = 1;
			}
		}
		else if (strcmp(pwalk->name, "scale") == 0) {
			scale = atoi(pwalk->value);
		}
		else if (strcmp(pwalk->name, "cause") == 0) {
			disablemsg = strdup(pwalk->value);
		}
		else if (strcmp(pwalk->name, "hostname") == 0) {
			if (hostnames == NULL) {
				hostnames = (char **)malloc(2 * sizeof(char *));
				hostnames[0] = strdup(pwalk->value);
				hostnames[1] = NULL;
				hostcount = 1;
			}
			else {
				hostnames = (char **)realloc(hostnames, (hostcount + 2) * sizeof(char *));
				hostnames[hostcount] = strdup(pwalk->value);
				hostnames[hostcount+1] = NULL;
				hostcount++;
			}
		}
		else if (strcmp(pwalk->name, "enabletest") == 0) {
			char *val = pwalk->value;

			if (strcmp(val, "ALL") == 0) val = "*";

			if (enabletest == NULL) {
				enabletest = (char **)malloc(2 * sizeof(char *));
				enabletest[0] = strdup(val);
				enabletest[1] = NULL;
				enablecount = 1;
			}
			else {
				enabletest = (char **)realloc(enabletest, (enablecount + 2) * sizeof(char *));
				enabletest[enablecount] = strdup(val);
				enabletest[enablecount+1] = NULL;
				enablecount++;
			}
		}
		else if (strcmp(pwalk->name, "disabletest") == 0) {
			char *val = pwalk->value;

			if (strcmp(val, "ALL") == 0) val = "*";

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
		else if (strcmp(pwalk->name, "year") == 0) {
			schedtm.tm_year = atoi(pwalk->value) - 1900;
		}
		else if (strcmp(pwalk->name, "month") == 0) {
			schedtm.tm_mon = atoi(pwalk->value) - 1;
		}
		else if (strcmp(pwalk->name, "day") == 0) {
			schedtm.tm_mday = atoi(pwalk->value);
		}
		else if (strcmp(pwalk->name, "hour") == 0) {
			schedtm.tm_hour = atoi(pwalk->value);
		}
		else if (strcmp(pwalk->name, "minute") == 0) {
			schedtm.tm_min = atoi(pwalk->value);
		}

		/* Until start */
		else if (strcmp(pwalk->name, "endyear") == 0) {
			endtm.tm_year = atoi(pwalk->value) - 1900;
		}
		else if (strcmp(pwalk->name, "endmonth") == 0) {
			endtm.tm_mon = atoi(pwalk->value) - 1;
		}
		else if (strcmp(pwalk->name, "endday") == 0) {
			endtm.tm_mday = atoi(pwalk->value);
		}
		else if (strcmp(pwalk->name, "endhour") == 0) {
			endtm.tm_hour = atoi(pwalk->value);
		}
		else if (strcmp(pwalk->name, "endminute") == 0) {
			endtm.tm_min = atoi(pwalk->value);
		}
		/* Until end */

		else if (strcmp(pwalk->name, "canceljob") == 0) {
			cancelid = atoi(pwalk->value);
		}
		else if (strcmp(pwalk->name, "preview") == 0) {
			preview = (strcasecmp(pwalk->value, "on") == 0);
		}
		else if ((strcmp(pwalk->name, "hostpattern") == 0) && pwalk->value && strlen(pwalk->value)) {
			hostpattern = strdup(pwalk->value);
		}
		else if ((strcmp(pwalk->name, "pagepattern") == 0) && pwalk->value && strlen(pwalk->value)) {
			pagepattern = strdup(pwalk->value);
		}
		else if ((strcmp(pwalk->name, "ippattern") == 0)   && pwalk->value && strlen(pwalk->value)) {
			ippattern = strdup(pwalk->value);
		}
		else if ((strcmp(pwalk->name, "classpattern") == 0)   && pwalk->value && strlen(pwalk->value)) {
			classpattern = strdup(pwalk->value);
		}

		pwalk = pwalk->next;
	}

	schedtm.tm_isdst = -1;
	schedtime = mktime(&schedtm);
	endtm.tm_isdst = -1;
	endtime = mktime(&endtm);
}

void do_one_host(char *hostname, char *fullmsg, char *username)
{
	char *xymoncmd = (char *)malloc(1024);
	int i, result;
	
	if (disableend == DISABLE_UNTIL)   {
		nowtime = time(NULL);
		starttime = nowtime;
		if (action == ACT_SCHED_DISABLE) starttime = schedtime;
		if (duration > 0) {
			/* Convert to minutes unless "until OK" */
			duration = (int) difftime (endtime, starttime) / 60; 
		}
		scale = 1;
	}

	switch (action) {
	  case ACT_ENABLE:
		for (i=0; (i < enablecount); i++) {
			if (preview) result = 0;
			else {
				xymoncmd = (char *)realloc(xymoncmd, 1024 + 2*strlen(hostname) + 2*strlen(enabletest[i]) + strlen(username));
				sprintf(xymoncmd, "enable %s.%s", commafy(hostname), enabletest[i]);
				result = sendmessage(xymoncmd, NULL, XYMON_TIMEOUT, NULL);
				sprintf(xymoncmd, "notify %s.%s\nMonitoring of %s:%s has been ENABLED by %s\n", 
					commafy(hostname), enabletest[i], 
					hostname, enabletest[i], username);
				sendmessage(xymoncmd, NULL, XYMON_TIMEOUT, NULL);
			}

			if (preview) {
				printf("<tr><td>Enabling host <b>%s</b>", htmlquoted(hostname));
				printf(" test <b>%s</b>", htmlquoted(enabletest[i]));
				printf(": %s</td></tr>\n", ((result == XYMONSEND_OK) ? "OK" : "Failed"));
			}
		}
		break;

	  case ACT_DISABLE:
		for (i=0; (i < disablecount); i++) {
			if (preview) result = 0;
			else {
				xymoncmd = (char *)realloc(xymoncmd, 1024 + 2*strlen(hostname) + 2*strlen(disabletest[i]) + strlen(fullmsg) + strlen(username));
				sprintf(xymoncmd, "disable %s.%s %d %s", 
					commafy(hostname), disabletest[i], duration*scale, fullmsg);
				result = sendmessage(xymoncmd, NULL, XYMON_TIMEOUT, NULL);
				sprintf(xymoncmd, "notify %s.%s\nMonitoring of %s:%s has been DISABLED by %s for %d minutes\n%s", 
					commafy(hostname), disabletest[i], 
					hostname, disabletest[i], username, duration*scale, fullmsg);
				result = sendmessage(xymoncmd, NULL, XYMON_TIMEOUT, NULL);
			}

			if (preview) {
				printf("<tr><td>Disabling host <b>%s</b>", htmlquoted(hostname));
				printf(" test <b>%s</b>", htmlquoted(disabletest[i]));
				printf(": %s</td></tr>\n", ((result == XYMONSEND_OK) ? "OK" : "Failed"));
			}
		}
		break;

	  case ACT_SCHED_DISABLE:
		for (i=0; (i < disablecount); i++) {
			xymoncmd = (char *)realloc(xymoncmd, 1024 + 2*strlen(hostname) + strlen(disabletest[i]) + strlen(fullmsg));
			sprintf(xymoncmd, "schedule %d disable %s.%s %d %s", 
				(int) schedtime, commafy(hostname), disabletest[i], duration*scale, fullmsg);
			result = (preview ? 0 : sendmessage(xymoncmd, NULL, XYMON_TIMEOUT, NULL));

			if (preview) {
				printf("<tr><td>Scheduling disable of host <b>%s</b>", htmlquoted(hostname));
				printf("test <b>%s</b>", htmlquoted(disabletest[i]));
				printf(" at <b>%s</b>: %s</td></tr>\n", ctime(&schedtime), ((result == XYMONSEND_OK) ? "OK" : "Failed"));
			}
		}
		break;

	  case ACT_SCHED_CANCEL:
		sprintf(xymoncmd, "schedule cancel %d", cancelid);
		result = (preview ? 0 : sendmessage(xymoncmd, NULL, XYMON_TIMEOUT, NULL));

		if (preview) {
			printf("<tr><td>Canceling job <b>%d</b> : %s</td></tr>\n", cancelid, ((result == XYMONSEND_OK) ? "OK" : "Failed"));
		}
		break;

	  default:
		errprintf("No action\n");
		break;
	}

	xfree(xymoncmd);
}

int main(int argc, char *argv[])
{
	int argi, i;
	char *username = getenv("REMOTE_USER");
	char *userhost = getenv("REMOTE_HOST");
	char *userip   = getenv("REMOTE_ADDR");
	char *fullmsg = "No cause specified";
	char *envarea = NULL;
	int  obeycookies = 1;
	char *accessfn = NULL;

	if ((username == NULL) || (strlen(username) == 0)) username = "unknown";
	if ((userhost == NULL) || (strlen(userhost) == 0)) userhost = userip;
	
	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--no-cookies") == 0) {
			obeycookies = 0;
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--access=")) {
			char *p = strchr(argv[argi], '=');
			accessfn = strdup(p+1);
		}
	}

	redirect_cgilog("enadis");

	parse_cgi();
	if (debug) preview = 1;

	if (cgi_method == CGI_GET) {
		/*
		 * It's a GET, so the initial request.
		 * If we have a pagepath cookie, use that as the initial
		 * host-name filter.
		 */
		char *pagepath;

		action = ACT_FILTER;
		pagepath = get_cookie("pagepath");
		if (obeycookies && pagepath && *pagepath) pagepattern = strdup(pagepath);
	}

	if (action == ACT_FILTER) {
		/* Present the query form */

		load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
		sethostenv("", "", "", colorname(COL_BLUE), NULL);
		sethostenv_filter(hostpattern, pagepattern, ippattern, classpattern);
		printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		showform(stdout, "maint", "maint_form", COL_BLUE, getcurrenttime(NULL), NULL, NULL);
		return 0;
	}

	fullmsg = (char *)malloc(1024 + strlen(username) + strlen(userhost) + strlen(disablemsg));
	sprintf(fullmsg, "\nDisabled by: %s @ %s\nReason: %s\n", username, userhost, disablemsg);

	/*
	 * Ready ... go build the webpage.
	 */
	printf("Content-Type: %s\n", xgetenv("HTMLCONTENTTYPE"));
	if (!preview) {
		char *returl;
		// dbgprintf("Not a preview: sending to %s\n", textornull(getenv("HTTP_REFERER")));
		/* We're done -- figure out where to send them */
		if (getenv("HTTP_REFERER")) printf("Location: %s\n\n", getenv("HTTP_REFERER"));
		else {
			returl = (char *)malloc(1024);
			snprintf(returl, sizeof(returl), "%s/%s", xgetenv("SECURECGIBINURL"), "enadis.sh");
			printf("Location: %s?\n\n", returl);
		}
	}
	else {
		printf("\n");
	}

        /* It's ok with these hardcoded values, as they are not used for this page */
	sethostenv("", "", "", colorname(COL_BLUE), NULL);
	if (preview) headfoot(stdout, "maintact", "", "header", COL_BLUE);

	if (debug) {
		printf("<pre>\n");
		switch (action) {
		  case ACT_NONE   : dbgprintf("Action = none\n"); break;

		  case ACT_FILTER : dbgprintf("Action = filter\n"); break;

		  case ACT_ENABLE : dbgprintf("Action = enable\n"); 
				    dbgprintf("Tests = ");
				    for (i=0; (i < enablecount); i++) printf("%s ", enabletest[i]);
				    printf("\n");
				    break;

		  case ACT_DISABLE: dbgprintf("Action = disable\n"); 
				    dbgprintf("Tests = ");
				    for (i=0; (i < disablecount); i++) printf("%s ", disabletest[i]);
				    printf("\n");
				    if (disableend == DISABLE_UNTIL) {
				    	dbgprintf("Disable until: endtime = %d, duration = %d, scale = %d\n", endtime, duration, scale);
				     }	
			            else {
				    	dbgprintf("Duration = %d, scale = %d\n", duration, scale);
				    }
				    dbgprintf("Cause = %s\n", textornull(disablemsg));
				    break;

		  case ACT_SCHED_DISABLE:
				    dbgprintf("Action = schedule\n");
				    dbgprintf("Time = %s\n", ctime(&schedtime));
				    dbgprintf("Tests = ");
				    for (i=0; (i < disablecount); i++) printf("%s ", disabletest[i]);
				    printf("\n");
				    if (disableend == DISABLE_UNTIL) {
						  dbgprintf("Disable until: endtime = %d, duration = %d, scale = %d\n", endtime, duration, scale);
					  }	
			            else {
				    		  dbgprintf("Duration = %d, scale = %d\n", duration, scale);
				    }
				    dbgprintf("Cause = %s\n", textornull(disablemsg));
				    break;

		  case ACT_SCHED_CANCEL:
				    dbgprintf("Action = cancel\n");
				    dbgprintf("ID = %d\n", cancelid);
				    break;
		}
		printf("</pre>\n");
	}

	if (preview) printf("<table align=\"center\" summary=\"Actions performed\" width=\"60%%\">\n");


	if (action == ACT_SCHED_CANCEL) {
		do_one_host(NULL, NULL, username);
	}
	else {
		/* Load the host data (for access control) */
		if (accessfn) {
			load_web_access_config(accessfn);

			for (i = 0; (i < hostcount); i++) {
				if (web_access_allowed(getenv("REMOTE_USER"), hostnames[i], NULL, WEB_ACCESS_CONTROL)) {
					do_one_host(hostnames[i], fullmsg, username);
				}
			}
		}
		else {
			for (i = 0; (i < hostcount); i++) do_one_host(hostnames[i], fullmsg, username);
		}
	}
	if (preview) {
		printf("<tr><td align=center><br><br><form method=\"GET\" ACTION=\"%s\"><input type=submit value=\"Continue\"></form></td></tr>\n", xgetenv("HTTP_REFERER"));
		printf("</table>\n");

		headfoot(stdout, "maintact", "", "footer", COL_BLUE);
	}

	return 0;
}

