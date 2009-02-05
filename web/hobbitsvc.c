/*----------------------------------------------------------------------------*/
/* Hobbit status-log viewer CGI.                                              */
/*                                                                            */
/* This CGI tool shows an HTML version of a status log.                       */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc.c,v 1.73 2006-08-07 10:05:39 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libbbgen.h"
#include "version.h"
#include "hobbitsvc-info.h"
#include "hobbitsvc-trends.h"

/* Commandline params */
static enum { SRC_HOBBITD, SRC_HISTLOGS, SRC_CLIENTLOGS } source = SRC_HOBBITD;
static int wantserviceid = 1;
static char *multigraphs = ",disk,inode,qtree,quotas,snapshot,TblSpace,if_load,";

/* CGI params */
static char *hostname = NULL;
static char *service = NULL;
static char *tstamp = NULL;
static char *nkprio = NULL, *nkttgroup = NULL, *nkttextra = NULL;
static enum { FRM_STATUS, FRM_CLIENT } outform = FRM_STATUS;
static char *clienturi = NULL;

static char errortxt[1000];
static char *hostdatadir = NULL;


static void errormsg(char *msg)
{
	snprintf(errortxt, sizeof(errortxt),
		 "Content-type: %s\n\n<html><head><title>Invalid request</title></head>\n<body>%s</body></html>\n", 
		 xgetenv("HTMLCONTENTTYPE"), msg);

	errortxt[sizeof(errortxt)-1] = '\0';
}

static int parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		if (strcasecmp(cwalk->name, "HOST") == 0) {
			hostname = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "SERVICE") == 0) {
			service = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "HOSTSVC") == 0) {
			/* For backwards compatibility */
			char *p = strrchr(cwalk->value, '.');
			if (p) {
				*p = '\0';
				hostname = strdup(cwalk->value);
				service = strdup(p+1);
				for (p=strchr(hostname, ','); (p); p = strchr(p, ',')) *p = '.';
			}
		}
		else if (strcasecmp(cwalk->name, "TIMEBUF") == 0) {
			/* Only for the historical logs */
			tstamp = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "CLIENT") == 0) {
			char *p;

			hostname = strdup(cwalk->value);
			p = hostname; while ((p = strchr(p, ',')) != NULL) *p = '.';
			service = strdup("");
			outform = FRM_CLIENT;
		}
		else if (strcasecmp(cwalk->name, "SECTION") == 0) {
			service = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "NKPRIO") == 0) {
			nkprio = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "NKTTGROUP") == 0) {
			nkttgroup = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "NKTTEXTRA") == 0) {
			nkttextra = strdup(cwalk->value);
		}

		cwalk = cwalk->next;
	}

	if (!hostname || !service) {
		errormsg("Invalid request");
		return 1;
	}

	if (outform == FRM_STATUS) {
		char *p, *req;

		req = getenv("SCRIPT_NAME");
		clienturi = (char *)malloc(strlen(req) + 10 + strlen(hostname));
		strcpy(clienturi, req);
		p = strchr(clienturi, '?'); if (p) *p = '\0'; else p = clienturi + strlen(clienturi);
		sprintf(p, "?CLIENT=%s", hostname);
	}

	return 0;
}

void loadhostdata(char *hostname, char **ip, char **displayname)
{
	void *hinfo = NULL;

	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());

	if ((hinfo = hostinfo(hostname)) == NULL) {
		errormsg("No such host");
		exit(1);
	}

	*ip = bbh_item(hinfo, BBH_IP);
	*displayname = bbh_item(hinfo, BBH_DISPLAYNAME);
	if (!(*displayname)) *displayname = hostname;
}

int do_request(void)
{
	int color = 0;
	char timesincechange[100];
	time_t logtime = 0, acktime = 0, disabletime = 0;
	char *log = NULL, *firstline = NULL, *sender = NULL, *clientid = NULL, *flags = NULL;	/* These are free'd */
	char *restofmsg = NULL, *ackmsg = NULL, *dismsg = NULL, *acklist=NULL;	/* These are just used */
	int ishtmlformatted = 0;
	int clientavail = 0;
	char *ip, *displayname;

	if (parse_query() != 0) return 1;

	{
		char *s;
		
		s = xgetenv("CLIENTLOGS"); 
		if (s) {
			hostdatadir = (char *)malloc(strlen(s) + strlen(hostname) + 12);
			sprintf(hostdatadir, "%s/%s", s, hostname);
		}
		else {
			s = xgetenv("BBVAR");
			hostdatadir = (char *)malloc(strlen(s) + strlen(hostname) + 12);
			sprintf(hostdatadir, "%s/hostdata/%s", s, hostname);
		}
	}

	if (outform == FRM_CLIENT) {
		if (source == SRC_HOBBITD) {
			char *hobbitdreq;
			int hobbitdresult;

			hobbitdreq = (char *)malloc(1024 + strlen(hostname) + (service ? strlen(service) : 0));
			sprintf(hobbitdreq, "clientlog %s", hostname);
			if (service && *service) sprintf(hobbitdreq + strlen(hobbitdreq), " section=%s", service);

			hobbitdresult = sendmessage(hobbitdreq, NULL, NULL, &log, 1, BBTALK_TIMEOUT);
			if (hobbitdresult != BB_OK) {
				char errtxt[4096];
				sprintf(errtxt, "Status not available: Req=%s, result=%d\n", hobbitdreq, hobbitdresult);
				errormsg(errtxt);
				return 1;
			}
		}
		else if (source == SRC_HISTLOGS) {
			char logfn[PATH_MAX];
			FILE *fd;

			sprintf(logfn, "%s/%s", hostdatadir, tstamp);
			fd = fopen(logfn, "r");
			if (fd) {
				struct stat st;
				int n;

				fstat(fileno(fd), &st);
				log = (char *)malloc(st.st_size + 1);
				n = fread(log, 1, st.st_size, fd);
				if (n >= 0) *(log+n) = '\0'; else *log = '\0';
				fclose(fd);
			}
		}

		restofmsg = (log ? log : strdup("<No data>\n"));
	}
	else if ((strcmp(service, xgetenv("TRENDSCOLUMN")) == 0) || (strcmp(service, xgetenv("INFOCOLUMN")) == 0)) {
		loadhostdata(hostname, &ip, &displayname);
		ishtmlformatted = 1;
		sethostenv(displayname, ip, service, colorname(COL_GREEN), hostname);
		sethostenv_refresh(600);
		color = COL_GREEN;
		logtime = time(NULL);
		strcpy(timesincechange, "0 minutes");

		if (strcmp(service, xgetenv("TRENDSCOLUMN")) == 0) {
			log = restofmsg = generate_trends(hostname);
		}
		else if (strcmp(service, xgetenv("INFOCOLUMN")) == 0) {
			log = restofmsg = generate_info(hostname);
		}
	}
	else if (source == SRC_HOBBITD) {
		char hobbitdreq[1024];
		int hobbitdresult;
		char *items[20];
		int icount;
		time_t logage, clntstamp;
		char *sumline, *msg, *p;

		sprintf(hobbitdreq, "hobbitdlog host=%s test=%s fields=hostname,testname,color,flags,lastchange,logtime,validtime,acktime,disabletime,sender,cookie,ackmsg,dismsg,client,acklist,BBH_IP,BBH_DISPLAYNAME,clntstamp", hostname, service);
		hobbitdresult = sendmessage(hobbitdreq, NULL, NULL, &log, 1, BBTALK_TIMEOUT);
		if ((hobbitdresult != BB_OK) || (log == NULL) || (strlen(log) == 0)) {
			errormsg("Status not available\n");
			return 1;
		}

		sumline = log; p = strchr(log, '\n'); *p = '\0';
		msg = (p+1); p = strchr(msg, '\n');
		if (!p) {
			firstline = strdup(msg);
			restofmsg = NULL;
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

		/*
		 * hostname,		[0]
		 * testname,		[1]
		 * color,		[2]
		 * flags,		[3]
		 * lastchange,		[4]
		 * logtime,		[5]
		 * validtime,		[6]
		 * acktime,		[7]
		 * disabletime,		[8]
		 * sender,		[9]
		 * cookie,		[10]
		 * ackmsg,		[11]
		 * dismsg,		[12]
		 * client,		[13]
		 * acklist		[14]
		 * BBH_IP		[15]
		 * BBH_DISPLAYNAME	[16]
		 * clienttstamp         [17]
		 */
		color = parse_color(items[2]);
		flags = strdup(items[3]);
		logage = time(NULL) - atoi(items[4]);
		timesincechange[0] = '\0'; p = timesincechange;
		if (logage > 86400) p += sprintf(p, "%d days,", (int) (logage / 86400));
		p += sprintf(p, "%d hours, %d minutes", (int) ((logage % 86400) / 3600), (int) ((logage % 3600) / 60));
		logtime = atoi(items[5]);
		if (items[7] && strlen(items[7])) acktime = atoi(items[7]);
		if (items[8] && strlen(items[8])) disabletime = atoi(items[8]);
		sender = strdup(items[9]);

		if (items[11] && strlen(items[11])) ackmsg = items[11];
		if (ackmsg) nldecode(ackmsg);

		if (items[12] && strlen(items[12])) dismsg = items[12];
		if (dismsg) nldecode(dismsg);

		if (items[13]) clientavail = (*items[13] == 'Y');

		acklist = ((items[14] && *items[14]) ? strdup(items[14]) : NULL);

		ip = (items[15] ? items[15] : "");
		displayname = ((items[16]  && *items[16]) ? items[16] : hostname);
		clntstamp = ((items[17]  && *items[17]) ? atol(items[17]) : 0);

		sethostenv(displayname, ip, service, colorname(COL_GREEN), hostname);
		sethostenv_refresh(60);
	}
	else if (source == SRC_HISTLOGS) {
		char logfn[PATH_MAX];
		struct stat st;
		int fd;
		/*
		 * Some clients (Unix disk reports) dont have a newline before the
		 * "Status unchanged in ..." text. Most do, but at least Solaris and
		 * AIX do not. So just look for the text, not the newline.
		 */
		char *statusunchangedtext = "Status unchanged in ";
		char *receivedfromtext = "Message received from ";
		char *clientidtext = "Client data ID ";
		char *p, *unchangedstr, *receivedfromstr, *clientidstr, *hostnamedash;
		int n;

		if (!tstamp) errormsg("Invalid request");

		loadhostdata(hostname, &ip, &displayname);
		hostnamedash = strdup(hostname);
		p = hostnamedash; while ((p = strchr(p, '.')) != NULL) *p = '_';
		p = hostnamedash; while ((p = strchr(p, ',')) != NULL) *p = '_';
		sprintf(logfn, "%s/%s/%s/%s", xgetenv("BBHISTLOGS"), hostnamedash, service, tstamp);
		xfree(hostnamedash);
		p = tstamp; while ((p = strchr(p, '_')) != NULL) *p = ' ';
		sethostenv_histlog(tstamp);

		if (stat(logfn, &st) == -1) {
			errormsg("Historical status log not available\n");
			return 1;
		}

		fd = open(logfn, O_RDONLY);
		if (fd < 0) {
			errormsg("Unable to access historical logfile\n");
			return 1;
		}
		log = (char *)malloc(st.st_size+1);
		n = read(fd, log, st.st_size);
		if (n >= 0) *(log+n) = '\0'; else *log = '\0';
		close(fd);

		p = strchr(log, '\n'); 
		if (!p) {
			firstline = strdup(log);
			restofmsg = NULL;
		}
		else { 
			*p = '\0'; 
			firstline = strdup(log); 
			restofmsg = (p+1);
			*p = '\n'; 
		}


		color = parse_color(log);

		p = strstr(log, "<!-- [flags:"); 
		if (p) {
			p += strlen("<!-- [flags:");
			n = strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
			flags = (char *)malloc(n+1);
			strncpy(flags, p, n);
			*(flags + n) = '\0';
		}

		timesincechange[0] = '\0';

		p = clientidstr = strstr(restofmsg, clientidtext);
		if (p) {
			p += strlen(clientidtext);
			n = strspn(p, "0123456789");
			clientid = (char *)malloc(n+1);
			strncpy(clientid, p, n);
			*(clientid+n) = '\0';
		}

		p = unchangedstr = strstr(restofmsg, statusunchangedtext);
		if (p) {
			p += strlen(statusunchangedtext);
			n = strcspn(p, "\n"); if (n >= sizeof(timesincechange)) n = sizeof(timesincechange);
			strncpy(timesincechange, p, n);
			timesincechange[n] = '\0';
		}

		p = receivedfromstr = strstr(restofmsg, receivedfromtext); 
		if (p) {
			p += strlen(receivedfromtext);
			n = strspn(p, "0123456789.");
			sender = (char *)malloc(n+1);
			strncpy(sender, p, n);
			*(sender+n) = '\0';
		}

		/* Kill the "Status unchanged ..." and "Message received ..." lines */
		if (unchangedstr) *unchangedstr = '\0';
		if (receivedfromstr) *receivedfromstr = '\0';
	}

	if (outform == FRM_CLIENT) {
		fprintf(stdout, "Content-type: text/plain\n\n");
		fprintf(stdout, "%s", restofmsg);
	}
	else {
		if (clientid && (source == SRC_HISTLOGS)) {
			char logfn[PATH_MAX];
			struct stat st;

			sprintf(logfn, "%s/%s", hostdatadir, clientid);
			clientavail = (stat(logfn, &st) == 0);

			if (clientavail) {
				sprintf(clienturi + strlen(clienturi), "&amp;TIMEBUF=%s", clientid);
			}
		}

		fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		generate_html_log(hostname, 
			  displayname,
			  service, 
			  ip,
		          color, 
			  (sender ? sender : "Hobbit"), 
			  (flags ? flags : ""),
		          logtime, timesincechange, 
		          (firstline ? firstline : ""), 
			  (restofmsg ? restofmsg : ""), 
			  acktime, ackmsg, acklist,
			  disabletime, dismsg,
		          (source == SRC_HISTLOGS), 
			  wantserviceid, 
			  ishtmlformatted,
			  (source == SRC_HOBBITD),
			  multigraphs, (clientavail ? clienturi : NULL),
			  nkprio, nkttgroup, nkttextra,
			  stdout);
	}

	/* Cleanup CGI params */
	if (hostname) xfree(hostname);
	if (service) xfree(service);
	if (tstamp) xfree(tstamp);

	/* Cleanup main vars */
	if (clientid) xfree(clientid);
	if (sender) xfree(sender);
	if (flags) xfree(flags);
	if (firstline) xfree(firstline);
	if (log) xfree(log);

	return 0;
}


int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;

	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--historical") == 0) {
			source = SRC_HISTLOGS;
		}
		else if (strcmp(argv[argi], "--hobbitd") == 0) {
			source = SRC_HOBBITD;
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
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--no-svcid") == 0) {
			wantserviceid = 0;
		}
		else if (argnmatch(argv[argi], "--templates=")) {
			char *p = strchr(argv[argi], '=');
			sethostenv_template(p+1);
		}
		else if (argnmatch(argv[argi], "--multigraphs=")) {
			char *p = strchr(argv[argi], '=');
			multigraphs = (char *)malloc(strlen(p+1) + 3);
			sprintf(multigraphs, ",%s,", p+1);
		}
		else if (strcmp(argv[argi], "--no-disable") == 0) {
			showenadis = 0;
		}
		else if (strcmp(argv[argi], "--no-jsvalidation") == 0) {
			usejsvalidation = 0;
		}
		else if (strcmp(argv[argi], "--old-nk-config") == 0) {
			newnkconfig = 0;
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
	}

	redirect_cgilog("hobbitsvc");

	*errortxt = '\0';
	hostname = service = tstamp = NULL;
	if (do_request() != 0) {
		fprintf(stdout, "%s", errortxt);
		return 1;
	}

	return 0;
}

