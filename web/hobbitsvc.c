/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "bb-hostsvc.sh" script                       */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc.c,v 1.12 2004-10-24 22:05:24 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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

void nldecode(unsigned char *msg)
{
	unsigned char *inp = msg;
	unsigned char *outp = msg;
	int n;

	if (msg == NULL) return;

	while (*inp) {
		n = strcspn(inp, "\\");
		if ((n > 0) && (inp != outp)) {
			memmove(outp, inp, n);
			inp += n;
			outp += n;
		}

		if (*inp == '\\') {
			inp++;
			switch (*inp) {
			  case 'p': *outp = '|';  outp++; inp++; break;
			  case 'r': *outp = '\r'; outp++; inp++; break;
			  case 'n': *outp = '\n'; outp++; inp++; break;
			  case 't': *outp = '\t'; outp++; inp++; break;
			  case '\\': *outp = '\\'; outp++; inp++; break;
			}
		}
		else if (*inp) {
			*outp = *inp;
			outp++; inp++;
		}
	}
	*outp = '\0';
}

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
char *tstamp = "";

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
		else if (argnmatch(token, "HOST")) {
			hostname = malcop(val);
		}
		else if (argnmatch(token, "SERVICE")) {
			service = malcop(val);
		}
		else if (argnmatch(token, "TIMEBUF")) {
			tstamp = malcop(val);
		}

		token = strtok(NULL, "&");
	}

        free(query);

	if (strcmp(displayname, "") == 0) displayname = hostname;
}

void historybutton(char *cgibinurl, char *hostname, char *service, char *ip) 
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


enum source_t { SRC_BBLOGS, SRC_BBGEND, SRC_HISTLOGS };
enum histbutton_t { HIST_TOP, HIST_BOTTOM, HIST_NONE };

int main(int argc, char *argv[])
{
	char bbgendreq[200];
	char *log = NULL;
	int bbgendresult;
	char *msg;
	char *sumline, *firstline, *restofmsg, *p;
	char *items[20];
	int argi, icount, linecount;
	int color;
	char timesincechange[100];
	time_t logtime = 0;
	char *sender;
	char *flags;
	char *ackmsg = NULL, *dismsg = NULL;
	larrdsvc_t *larrd = NULL;
	char *cgibinurl, *colfont, *rowfont;
	enum source_t source = SRC_BBLOGS;
	enum histbutton_t histlocation = HIST_BOTTOM;

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
	{
		getenv_default("USEBBGEND", "FALSE", NULL);
		if (strcmp(getenv("USEBBGEND"), "TRUE") == 0) source = SRC_BBGEND;
	}

	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--historical") == 0) {
			source = SRC_HISTLOGS;
		}
		else if (strcmp(argv[argi], "--bbgend") == 0) {
			source = SRC_BBGEND;
		}
		else if (strncmp(argv[argi], "--history=", 10) == 0) {
			char *val = strchr(argv[argi], '=')+1;

			if (strcmp(val, "none") == 0)
				histlocation = HIST_NONE;
			else if (strcmp(val, "top") == 0)
				histlocation = HIST_TOP;
			else if (strcmp(val, "bottom") == 0)
				histlocation = HIST_BOTTOM;
		}
	}

	envcheck(reqenv);
	parse_query();

	if (source == SRC_BBGEND) {
		time_t logage;

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

		memset(items, 0, sizeof(items));
		p = gettok(sumline, "|"); icount = 0;
		while (p && (icount < 20)) {
			items[icount++] = p;
			p = gettok(NULL, "|");
		}

		/* hostname|testname|color|testflags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|ackmsg|dismsg */
		color = parse_color(items[2]);
		flags = items[3];
		logage = time(NULL) - atoi(items[4]);
		timesincechange[0] = '\0'; p = timesincechange;
		if (logage > 86400) p += sprintf(p, "%d days,", (int) (logage / 86400));
		p += sprintf(p, "%d hours, %d minutes", (int) ((logage % 86400) / 3600), (int) ((logage % 3600) / 60));
		logtime = atoi(items[5]);
		sender = items[9];

		if (items[11] && strlen(items[11])) ackmsg = items[11];
		if (ackmsg) nldecode(ackmsg);

		if (items[12] && strlen(items[12])) dismsg = items[12];
		if (dismsg) nldecode(dismsg);
	}
	else {
		char logfn[MAX_PATH];
		struct stat st;
		int fd;
		char *receivedfromtext = "\nMessage received from ";
		char *statusunchangedtext = "\nStatus unchanged in ";
		char *p, *unchangedstr, *receivedfromstr;
		int n;

		if (source == SRC_BBLOGS) {
			sprintf(logfn, "%s/%s.%s", getenv("BBLOGS"), commafy(hostname), service);
		}
		else if (source == SRC_HISTLOGS) {
			char *hostnamedash = strdup(hostname);
			p = hostnamedash; while ((p = strchr(p, '.')) != NULL) *p = '_';
			p = hostnamedash; while ((p = strchr(p, ',')) != NULL) *p = '_';
			sprintf(logfn, "%s/%s/%s/%s", getenv("BBHISTLOGS"), hostnamedash, service, tstamp);
			free(hostnamedash);
			p = tstamp; while ((p = strchr(p, '_')) != NULL) *p = ' ';
			sethostenv_histlog(tstamp);
		}

		if (stat(logfn, &st) == -1) {
			errormsg("No such host/service\n");
			return 1;
		}

		fd = open(logfn, O_RDONLY);
		if (fd < 0) {
			errormsg("Unable to access logfile\n");
			return 1;
		}
		log = (char *)malloc(st.st_size+1);
		read(fd, log, st.st_size);
		close(fd);
		firstline = log;
		restofmsg = strchr(log, '\n'); 
		if (restofmsg) {
			*restofmsg = '\0';
			restofmsg++;
		}

		color = parse_color(log);

		p = strstr(log, "<!-- [flags:"); 
		if (p) {
			p += strlen("<!-- [flags:");
			n = strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
			flags = (char *)malloc(n+1);
			strncpy(flags, p, n);
		}
		else {
			flags = "";
		}

		timesincechange[0] = '\0';
		p = unchangedstr = strstr(restofmsg, statusunchangedtext);
		if (p) {
			p += strlen(statusunchangedtext);
			n = strcspn(p, "\n");
			strncpy(timesincechange, p, n);
			timesincechange[n] = '\0';
		}

		p = receivedfromstr = strstr(restofmsg, receivedfromtext); 
		if (p) {
			p += strlen(receivedfromtext);
			n = strspn(p, "0123456789.");
			sender = (char *)malloc(n);
			strncpy(sender, p, n);
			*(sender+n) = '\0';
		}
		else {
			sender = NULL;
		}

		/* Kill the "Status unchanged ..." and "Message received ..." lines */
		if (unchangedstr) *unchangedstr = '\0';
		if (receivedfromstr) *receivedfromstr = '\0';
	}

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
	if (logtime) sethostenv_snapshot(logtime);
	fprintf(stdout, "Content-type: text/html\n\n");

	headfoot(stdout, ((source == SRC_HISTLOGS) ? "histlog" : "hostsvc"), "", "header", color);

	fprintf(stdout, "<br><br><a name=\"begindata\">&nbsp;</a>\n");

	if ((source != SRC_HISTLOGS) && (histlocation == HIST_TOP)) historybutton(cgibinurl, hostname, service, ip);

	fprintf(stdout, "<CENTER><TABLE ALIGN=CENTER BORDER=0>\n");
	fprintf(stdout, "<TR><TH><FONT %s>%s - %s</FONT><BR><HR WIDTH=\"60%%\"></TH></TR>\n", rowfont, displayname, service);
	fprintf(stdout, "<TR><TD><H3>%s</H3>\n", skipword(firstline));	/* Drop the color */
	fprintf(stdout, "<PRE>\n");

	do {
		int color;

		p = strchr(restofmsg, '&');
		if (p) {
			*p = '\0';
			fprintf(stdout, "%s", restofmsg);

			color = parse_color(p+1);
			if (color == -1) {
				fprintf(stdout, "&");
				restofmsg = p+1;
			}
			else {
				fprintf(stdout, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0>",
                                                        getenv("BBSKIN"), dotgiffilename(color, 0, 0),
							colorname(color),
                                                        getenv("DOTHEIGHT"), getenv("DOTWIDTH"));

				restofmsg = p+1+strlen(colorname(color));
			}
		}
		else {
			fprintf(stdout, "%s", restofmsg);
			restofmsg = NULL;
		}
	} while (restofmsg);

	fprintf(stdout, "\n</PRE>\n");
	fprintf(stdout, "</TD></TR></TABLE>\n");

	fprintf(stdout, "<br><br>\n");
	fprintf(stdout, "<table align=\"center\" border=0>\n");
	fprintf(stdout, "<tr><td align=\"center\"><font %s>", colfont);
	if (strlen(timesincechange)) fprintf(stdout, "Status unchanged in %s<br>\n", timesincechange);
	if (sender) fprintf(stdout, "Status message received from %s<br>\n", sender);
	if (ackmsg) fprintf(stdout, "Current acknowledgment: %s<br>\n", ackmsg);
	fprintf(stdout, "</font></td></tr>\n");
	fprintf(stdout, "</table>\n");

	/* larrd stuff here */
	if (source != SRC_HISTLOGS) larrd = find_larrd(service, flags);
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

	if ((source != SRC_HISTLOGS) && (histlocation == HIST_BOTTOM)) historybutton(cgibinurl, hostname, service, ip);
	
	headfoot(stdout, ((source == SRC_HISTLOGS) ? "histlog" : "hostsvc"), "", "footer", color);
	return 0;
}

