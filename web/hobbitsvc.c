#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"
#include "sendmsg.h"

typedef struct {
   char *bbsvcname;
   char *larrdsvcname;
   char *larrdpartname;
} larrdsvc_t;

static larrdsvc_t *larrdsvcs = NULL;

larrdsvc_t *find_larrd(char *service, char *flags)
{
	larrdsvc_t *result;
	larrdsvc_t *lrec;

	if (strchr(flags, 'R')) {
		/* Dont do LARRD for reverse tests, since they have no data */
		return NULL;
	}

	if (larrdsvcs == NULL) {
		char *lenv = getenv("LARRDS");
		int lcount = 0;
		char *ldef, *p;

		p = lenv; do { lcount++; p = strchr(p+1, ','); } while (p);
		larrdsvcs = (larrdsvc_t *)calloc(sizeof(larrdsvc_t), (lcount+1));

		lrec = larrdsvcs; ldef = strtok(lenv, ",");
		while (ldef) {
			p = strchr(ldef, '=');
			if (p) {
				*p = '\0'; 
				lrec->bbsvcname = strdup(ldef);
				lrec->larrdsvcname = strdup(p+1);
			}
			else {
				lrec->bbsvcname = lrec->larrdsvcname = strdup(ldef);
			}

			if (strcmp(ldef, "disk") == 0) lrec->larrdpartname = "disk_part";

			ldef = strtok(NULL, ",");
			lrec++;
		}
	}

	lrec = larrdsvcs; while (lrec->bbsvcname && strcmp(lrec->bbsvcname, service)) lrec++;
	return (lrec->bbsvcname ? lrec : NULL);
}

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
char *ip = "";
char *displayname = "";

char *reqenv[] = {
	"BBDISP",
	"BBHOME",
	NULL 
};


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
		else if (argnmatch(token, "IP")) {
			ip = malcop(val);
		}
		else if (argnmatch(token, "DISPLAYNAME")) {
			displayname = malcop(val);
		}

		token = strtok(NULL, "&");
	}

        free(query);

	if (strcmp(displayname, "") == 0) displayname = hostname;
}


int main(int argc, char *argv[])
{
	char bbgendreq[200];
	char *log = NULL;
	int bbgendresult;
	char *msg;
	char *sumline, *firstline, *restofmsg, *p;
	char *items[20];
	int icount, linecount;
	int color;
	time_t logage;
	char timesincechange[100];
	char *sender;
	char *flags;
	larrdsvc_t *larrd = NULL;

	char *cgibinurl, *colfont, *rowfont;

	getenv_default("LARRDS", "cpu=la,content=http,http,conn,fping=conn,ftp,ssh,telnet,nntp,pop,pop-2,pop-3,pop2,pop3,smtp,imap,disk,vmstat,memory,iostat,netstat,citrix,bbgen,bbtest,bbproxy,time=ntpstat,vmio,temperature", NULL);
	getenv_default("NONHISTS", "info,larrd,trends,graphs", NULL);
	getenv_default("BBREL", "bbgen", NULL);
	getenv_default("BBRELDATE", VERSION, NULL);
	getenv_default("CGIBINURL", "/cgi-bin", &cgibinurl);
	getenv_default("MKBBCOLFONT", "COLOR=teal SIZE=-1\"", &colfont);
	getenv_default("MKBBROWFONT", "SIZE=+1 COLOR=\"#FFFFCC\" FACE=\"Tahoma, Arial, Helvetica\"", &rowfont);
	getenv_default("BBWEB", "/bb", NULL);
	{
		char *dbuf = malloc(strlen(getenv("BBWEB")) + 6);
		sprintf(dbuf, "%s/gifs", getenv("BBWEB"));
		getenv_default("BBSKIN", dbuf, NULL);
		free(dbuf);
	}

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

	p = gettok(sumline, "|"); icount = 0;
	while (p && (icount < 20)) {
		items[icount++] = p;
		p = gettok(NULL, "|");
	}

	color = parse_color(items[2]);
	flags = items[3];
	logage = time(NULL) - atoi(items[4]);
	timesincechange[0] = '\0'; p = timesincechange;
	if (logage > 86400) p += sprintf(p, "%d days,", (int) (logage / 86400));
	p += sprintf(p, "%d hours, %d minutes", (int) ((logage % 86400) / 3600), (int) ((logage % 3600) / 60));
	sender = items[6];

	/* Count how many lines are in the status message. This is needed by LARRD later */
	linecount = 0; p = restofmsg;
	do {
		/* First skip all whitespace and blank lines */
		while ((*p) && (isspace((int)*p) || iscntrl((int)*p))) p++;
		if (*p) {
			/* We found something that is not blank, so one more line */
			linecount++;
			/* Then skip forward to the EOLN */
			p = strchr(p, '\n');
		}
	} while (p && (*p));

	sethostenv(displayname, ip, service, colorname(color));
	fprintf(stdout, "Content-type: text/html\n\n");

	headfoot(stdout, "hostsvc", "", "header", color);

	fprintf(stdout, "<br><br><a name=\"begindata\">&nbsp;</a>\n");

	fprintf(stdout, "<CENTER><TABLE ALIGN=CENTER BORDER=0>\n");
	fprintf(stdout, "<TR><TH><FONT %s>%s - %s</FONT><BR><HR WIDTH=\"60%%\"></TH></TR>\n", rowfont, displayname, service);
	fprintf(stdout, "<TR><TD><H3>%s</H3>\n", firstline);
	fprintf(stdout, "<PRE>\n%s\n</PRE>\n", restofmsg);
	fprintf(stdout, "</TD></TR></TABLE>\n");

	fprintf(stdout, "<br><br>\n");
	fprintf(stdout, "<table align=\"center\" border=0>\n");
	fprintf(stdout, "<tr><td align=\"center\"><font %s>", colfont);
	fprintf(stdout, "Status unchanged in %s<br>\n", timesincechange);
	fprintf(stdout, "Status message received from %s\n", sender);
	fprintf(stdout, "</font></td></tr>\n");
	fprintf(stdout, "</table>\n");

	/* larrd stuff here */
	larrd = find_larrd(service, flags);
	if (larrd) {
		/* 
		 * If this service uses part-names (currently, only disk does),
		 * then setup a link for each of the part graphs.
		 */
		if (larrd->larrdpartname) {
			int start;

			fprintf(stdout, "<!-- linecount=%d -->\n", linecount);
			for (start=0; (start < linecount); start += 6) {
				fprintf(stdout,"<BR><BR><CENTER><A HREF=\"%s/larrd-grapher.cgi?host=%s&amp;service=%s&%s=%d..%d&amp;disp=%s\"><IMG SRC=\"%s/larrd-grapher.cgi?host=%s&amp;service=%s&amp;%s=%d..%d&amp;graph=hourly&ampdisp=%s\" ALT=\"&nbsp;\" BORDER=0></A><BR></CENTER>\n",
					cgibinurl, hostname, larrd->larrdsvcname, larrd->larrdpartname,
					start, (((start+5) < linecount) ? start+5 : linecount-1), displayname,
					cgibinurl, hostname, larrd->larrdsvcname, larrd->larrdpartname,
					start, (((start+5) < linecount) ? start+5 : linecount-1), displayname);
			}
		}
		else {
				fprintf(stdout,"<BR><BR><CENTER><A HREF=\"%s/larrd-grapher.cgi?host=%s&amp;service=%s&amp;disp=%s\"><IMG SRC=\"%s/larrd-grapher.cgi?host=%s&amp;service=%s&amp;disp=%s&amp;graph=hourly\"ALT=\"&nbsp;\" BORDER=0></A><BR></CENTER>\n",
					cgibinurl, hostname, larrd->larrdsvcname, displayname,
					cgibinurl, hostname, larrd->larrdsvcname, displayname);
		}
	}

	{
		char *tmp1 = (char *)malloc(strlen(getenv("NONHISTS"))+3);
		char *tmp2 = (char *)malloc(strlen(service)+3);

		sprintf(tmp1, ",%s,", getenv("NONHISTS"));
		sprintf(tmp2, ",%s,", service);
		if (strstr(tmp1, tmp2) == NULL) {
			fprintf(stdout,"<BR><BR><CENTER><FORM ACTION=\"%s/bb-hist.sh\"> \
				<INPUT TYPE=SUBMIT VALUE=\"HISTORY\"> \
				<INPUT TYPE=HIDDEN NAME=\"HISTFILE\" VALUE=\"%s.%s\"> \
				<INPUT TYPE=HIDDEN NAME=\"ENTRIES\" VALUE=\"50\"> \
				<INPUT TYPE=HIDDEN NAME=\"IP\" VALUE=\"%s\"> \
				</FORM></CENTER>\n",
				cgibinurl, hostname, service, ip);
		}

		free(tmp2);
		free(tmp1);
	}

	headfoot(stdout, "hostsvc", "", "footer", color);
	return 0;
}

