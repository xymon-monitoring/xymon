#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"
#include "sendmsg.h"

#ifdef CGI
/*
 * This program is invoked via CGI with QUERY_STRING containing:
 *
 *      HOSTSVC=www,sample,com.conn
 */

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

char *hostname = "";
char *service = "";

char *reqenv[] = {
	"BBDISP",
	"BBSKIN",
	"BBWEB",
	"MKBBROWFONT",
	"MKBBCOLFONT",
	NULL };


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

	if (getenv("QUERY_STRING") == NULL) {
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

			if (p) { *p = '\0'; service = malcop(p+1); }
			hostname = malcop(val);
			while ((p = strchr(hostname, ','))) *p = '.';
		}

		token = strtok(NULL, "&");
	}

        free(query);
}


int main(int argc, char *argv[])
{
	char bbgendreq[200];
	char *log = NULL;
	int bbgendresult;
	char *msg;
	char *sumline, *firstline, *restofmsg, *p;
	char *items[20];
	int icount;
	int color;
	time_t logage;
	char timesincechange[100];
	char *sender;

	envcheck(reqenv);
	parse_query();

	sprintf(bbgendreq, "bbgendlog %s.%s", hostname, service);
	bbgendresult = sendmessage(bbgendreq, NULL, NULL, &log, 1);
	if ((bbgendresult != BB_OK) || (log == NULL) || (strlen(log) == 0)) {
		errormsg("Status not available\n");
		return 1;
	}

	sumline = log; p = strchr(log, '\n'); *p = '\0';
	msg = (p+1); p = strchr(msg, '\n');
	if (!p) {
		firstline = msg;
		restofmsg = "";
	}
	else { 
		*p = '\0'; 
		firstline = strdup(msg); 
		restofmsg = (p+1);
		*p = '\n'; 
	}

	p = strtok(sumline, "|"); icount = 0;
	while (p && (icount < 20)) {
		items[icount++] = p;
		p = strtok(NULL, "|");
	}

	color = parse_color(items[2]);
	logage = time(NULL) - atoi(items[4]);
	timesincechange[0] = '\0'; p = timesincechange;
	if (logage > 86400) p += sprintf(p, "%d days,", (int) (logage / 86400));
	p += sprintf(p, "%d hours, %d minutes", (int) ((logage % 86400) / 3600), (int) ((logage % 3600) / 60));
	sender = items[6];

	sethostenv(hostname, "", service, colorname(color));
	fprintf(stdout, "Content-type: text/html\n\n");

	headfoot(stdout, "hostsvc", "", "header", color);

	fprintf(stdout, "<br><br><a name=\"begindata\">&nbsp;</a>\n");

	fprintf(stdout, "<CENTER><TABLE ALIGN=CENTER BORDER=0>\n");
	fprintf(stdout, "<TR><TH><FONT %s>%s - %s</FONT><BR><HR WIDTH=\"60%%\"></TH></TR>\n", 
		getenv("MKBBROWFONT"), hostname, service);
	fprintf(stdout, "<TR><TD><H3>%s</H3>\n", firstline);
	fprintf(stdout, "<PRE>\n%s\n</PRE>\n", restofmsg);
	fprintf(stdout, "</TD></TR></TABLE>\n");

	fprintf(stdout, "<br><br>\n");
	fprintf(stdout, "<table align=\"center\" border=0>\n");
	fprintf(stdout, "<tr><td align=\"center\"><font %s>", getenv("MKBBCOLFONT"));
	fprintf(stdout, "Status unchanged in %s<br>\n", timesincechange);
	fprintf(stdout, "Status message received from %s\n", sender);
	fprintf(stdout, "</font></td></tr>\n");
	fprintf(stdout, "</table>\n");

	/* larrd stuff here */

	headfoot(stdout, "hostsvc", "", "footer", color);
	return 0;
}
#endif

