/*----------------------------------------------------------------------------*/
/* Big Brother "bbgen" toolkit - routines to send messages to bbd             */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: sendmsg.c,v 1.18 2004-07-26 22:26:53 henrik Exp $";

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#if !defined(HPUX)
#include <sys/select.h>
#endif
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "sendmsg.h"

#define BBSENDRETRIES 2

/* Stuff for combo message handling */
int		bbmsgcount = 0;		/* Number of messages transmitted */
int		bbstatuscount = 0;	/* Number of status items reported */
int		bbnocombocount = 0;	/* Number of status items reported outside combo msgs */
static int	bbmsgqueued;		/* Anything in the buffer ? */
static char	bbmsg[MAXMSG];		/* Complete combo message buffer */
static char	msgbuf[MAXMSG-50];	/* message buffer for one status message */
static int	msgcolor;		/* color of status message in msgbuf */
static int      maxmsgspercombo = 0;	/* 0 = no limit */
static int      sleepbetweenmsgs = 0;
static int      bbdportnumber = 0;
static char     *bbdispproxyhost = NULL;
static int      bbdispproxyport = 0;

int dontsendmessages = 0;

static void setup_transport(char *recipient)
{
	static int transport_is_setup = 0;
	int default_port;

	if (transport_is_setup) return;
	transport_is_setup = 1;

	if (strncmp(recipient, "http://", 7) == 0) {
		/*
		 * Send messages via http. This requires e.g. a CGI on the webserver to
		 * receive the POST we do here.
		 */
		default_port = 80;

		if (getenv("http_proxy")) {
			char *p;

			bbdispproxyhost = malcop(getenv("http_proxy"));
			if (strncmp(bbdispproxyhost, "http://", 7) == 0) bbdispproxyhost += strlen("http://");
 
			p = strchr(bbdispproxyhost, ':');
			if (p) {
				*p = '\0';
				p++;
				bbdispproxyport = atoi(p);
			}
			else {
				bbdispproxyport = 8080;
			}
		}
	}
	else {
		/* 
		 * Non-HTTP transport - lookup portnumber in both BBPORT env.
		 * and the "bbd" entry from /etc/services.
		 */
		default_port = BBDPORTNUMBER;

		if (getenv("BBPORT")) bbdportnumber = atoi(getenv("BBPORT"));
	
	
		/* Next is /etc/services "bbd" entry */
		if ((bbdportnumber <= 0) || (bbdportnumber > 65535)) {
			struct servent *svcinfo;

			svcinfo = getservbyname("bbd", NULL);
			if (svcinfo) bbdportnumber = ntohs(svcinfo->s_port);
		}
	}

	/* Last resort: The default value */
	if ((bbdportnumber <= 0) || (bbdportnumber > 65535)) {
		bbdportnumber = default_port;
	}
}

static int sendtobbd(char *recipient, char *message, FILE *respfd, int fullresponse)
{
	struct in_addr addr;
	struct sockaddr_in saddr;
	int	sockfd;
	fd_set	readfds;
	fd_set	writefds;
	int	res, isconnected, wdone, rdone;
	struct timeval tmo;
	char *msgptr = message;
	char *p;
	char *rcptip = NULL;
	int rcptport = 0;
	int connretries = BBSENDRETRIES;
	char *httpmessage = NULL;
	char response[MAXMSG];

	if (dontsendmessages) {
		printf("%s\n", message);
		return BB_OK;
	}

	setup_transport(recipient);

	if (strncmp(recipient, "http://", strlen("http://")) != 0) {
		/* Standard BB communications, directly to bbd */
		rcptip = malcop(recipient);
		rcptport = bbdportnumber;
		p = strchr(rcptip, ':');
		if (p) {
			*p = '\0'; p++; rcptport = atoi(p);
		}
	}
	else {
		char *bufp;
		char *posturl = NULL;
		char *posthost = NULL;

		if (bbdispproxyhost == NULL) {
			char *p;

			/*
			 * No proxy. "recipient" is "http://host[:port]/url/for/post"
			 * Strip off "http://", and point "posturl" to the part after the hostname.
			 * If a portnumber is present, strip it off and update rcptport.
			 */
			rcptip = malcop(recipient+strlen("http://"));
			rcptport = bbdportnumber;

			p = strchr(rcptip, '/');
			if (p) {
				posturl = malcop(p);
				*p = '\0';
			}

			p = strchr(rcptip, ':');
			if (p) {
				*p = '\0';
				p++;
				rcptport = atoi(p);
			}

			posthost = malcop(rcptip);
		}
		else {
			char *p;

			/*
			 * With proxy. The full "recipient" must be in the POST request.
			 */
			rcptip = malcop(bbdispproxyhost);
			rcptport = bbdispproxyport;

			posturl = malcop(recipient);

			p = strchr(recipient + strlen("http://"), '/');
			if (p) {
				*p = '\0';
				posthost = malcop(recipient + strlen("http://"));
				*p = '/';

				p = strchr(posthost, ':');
				if (p) *p = '\0';
			}
		}

		if ((posturl == NULL) || (posthost == NULL)) {
			errprintf("Unable to parse HTTP recipient\n");
			return BB_EBADURL;
		}

		bufp = msgptr = httpmessage = malloc(strlen(message)+1024);
		bufp += sprintf(httpmessage, "POST %s HTTP/1.0\n", posturl);
		bufp += sprintf(bufp, "MIME-version: 1.0\n");
		bufp += sprintf(bufp, "Content-Type: application/octet-stream\n");
		bufp += sprintf(bufp, "Content-Length: %d\n", strlen(message));
		bufp += sprintf(bufp, "Host: %s\n", posthost);
		bufp += sprintf(bufp, "\n%s", message);

		if (posturl) free(posturl);
		if (posthost) free(posthost);
	}

	if (inet_aton(rcptip, &addr) == 0) {
		/* recipient is not an IP - do DNS lookup */

		struct hostent *hent;
		char hostip[16];

		hent = gethostbyname(rcptip);
		if (hent) {
			memcpy(&addr, *(hent->h_addr_list), sizeof(struct in_addr));
			strcpy(hostip, inet_ntoa(addr));

			if (inet_aton(hostip, &addr) == 0) return BB_EBADIP;
		}
		else {
			errprintf("Cannot determine IP address of message recipient %s\n", rcptip);
			return BB_EIPUNKNOWN;
		}
	}
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = addr.s_addr;
	saddr.sin_port = htons(rcptport);

	/* Get a non-blocking socket */
	sockfd = socket(PF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) return BB_ENOSOCKET;
	res = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if (res != 0) return BB_ECANNOTDONONBLOCK;

retry_connect:
	res = connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
	if ((res == -1) && (errno != EINPROGRESS)) {
		close(sockfd);
		return BB_ECONNFAILED;
	}

	rdone = (respfd == NULL);
	isconnected = wdone = 0;
	while (!wdone || !rdone) {
		FD_ZERO(&writefds);
		FD_ZERO(&readfds);
		if (!rdone) FD_SET(sockfd, &readfds);
		if (!wdone) FD_SET(sockfd, &writefds);
		tmo.tv_sec = 5;  tmo.tv_usec = 0; /* 5 seconds timeout to connect to bbd */
		res = select(sockfd+1, &readfds, &writefds, NULL, &tmo);
		if (res == -1) {
			errprintf("Select failure while sending to bbd!\n");
			shutdown(sockfd, SHUT_RDWR); close(sockfd);
			return BB_ESELFAILED;
		}
		else if (res == 0) {
			/* Timeout! */
			close(sockfd);

			if (!isconnected && (connretries > 0)) {
				connretries--;
				errprintf("Timeout while talking to bbd - retrying\n");
				sleep(1);
				goto retry_connect;	/* Yuck! */
			}

			errprintf("Timeout while talking to bbd!\n");
			return BB_ETIMEOUT;
		}
		else {
			if (!isconnected) {
				/* Havent seen our connect() status yet - must be now */
				int connres;
				socklen_t connressize = sizeof(connres);

				res = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &connres, &connressize);
				isconnected = (connres == 0);
				if (!isconnected) {
					close(sockfd);
					errprintf("Could not connect to bbd!\n");
					return BB_ECONNFAILED;
				}
			}

			if (!rdone && FD_ISSET(sockfd, &readfds)) {
				int n = recv(sockfd, response, MAXMSG-1, 0);
				if (n > 0) {
					response[n] = '\0';
					fwrite(response, n, 1, respfd);
					if (!fullresponse) rdone = (strchr(response, '\n') == NULL);
				}
				else rdone = 1;
			}

			if (!wdone && FD_ISSET(sockfd, &writefds)) {
				/* Send some data */
				res = write(sockfd, msgptr, strlen(msgptr));
				if (res == -1) {
					shutdown(sockfd, SHUT_RDWR); close(sockfd);
					errprintf("Write error while sending message to bbd\n");
					return BB_EWRITEERROR;
				}
				else {
					msgptr += res;
					wdone = (strlen(msgptr) == 0);
				}
			}
		}
	}

	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);
	free(rcptip);
	if (httpmessage) free(httpmessage);
	return BB_OK;
}

static int sendtomany(char *onercpt, char *morercpts, char *msg)
{
	int result = 0;

	if (strcmp(onercpt, "0.0.0.0") != 0)
		result = sendtobbd(onercpt, msg, NULL, 0);
	else if (morercpts) {
		char *bbdlist, *rcpt;

		bbdlist = malcop(morercpts);
		rcpt = strtok(bbdlist, " \t");
		while (rcpt) {
			result += sendtobbd(rcpt, msg, NULL, 0);
			rcpt = strtok(NULL, " \t");
		}

		free(bbdlist);
	}
	else {
		errprintf("No recipients for message - oneaddr=%s, moreaddrs is NULL\n", onercpt);
		result = 100;
	}

	return result;
}

int sendstatus(char *bbdisp, char *msg)
{
	int statusresult, pageresult;
	char statuscolor[256];
	char *pagelevels;

	statusresult = sendtomany(bbdisp, getenv("BBDISPLAYS"), msg);

	/* If no BBPAGE defined, drop paging */
	if (getenv("BBPAGE") == NULL) return statusresult;

	/* Check if we should send a "page" message also */
	pagelevels = malcop(getenv("PAGELEVELS") ? getenv("PAGELEVELS") : PAGELEVELSDEFAULT);
	sscanf(msg, "%*s %*s %255s", statuscolor);
	if (strstr(pagelevels, statuscolor)) {
		/* Reformat the message into a "page" message */
		char *pagemsg = (char *) malloc(strlen(msg)+1);
		char *firstnl;
		char *inp, *outp;

		/* Split message into first line and the rest */
		firstnl = strchr(msg, '\n');
		if (firstnl) { *firstnl = '\0';  firstnl++; }

		/* Start the page message with the "page" keyword */
		strcpy(pagemsg, "page ");
		outp = pagemsg + strlen(pagemsg);

		/* Skip past the "status" word (incl. any duration string) and copy from there */
		inp = skipwhitespace(skipword(msg));
		while (inp && (*inp)) {
			if (strncmp(inp, "<!--", 4) == 0) {
				/* HTML comments must be stripped from the page message */
				inp = strstr(inp, "-->");
				if (inp) inp += 3;
			}
			else {
				*outp = *inp;
				outp++; inp++;
			}
		}
		*outp = '\0';

		if (firstnl) { *firstnl = '\n'; strcat(pagemsg, firstnl); }
		pageresult = sendtomany(getenv("BBPAGE"), getenv("BBPAGERS"), pagemsg);
		free(pagemsg);
	}

	free(pagelevels);
	return statusresult;
}


int sendmessage(char *msg, char *recipient, FILE *respfd, int fullresponse)
{
	static char *bbdisp = NULL;
	int res = 0;

	if ((bbdisp == NULL) && (recipient == NULL)) bbdisp = malcop(getenv("BBDISP"));

	if ((strncmp(msg, "status", 6) == 0) || (strncmp(msg, "combo", 5) == 0)) {
		res = sendstatus((recipient ? recipient : bbdisp), msg);
	}
	else {
		res = sendtobbd(recipient, msg, respfd, fullresponse);
	}

	if (res != BB_OK) {
		errprintf("%s: Whoops ! bb failed to send message - returns status %d\n", timestamp, res);
	}

	/* Give it a break */
	if (sleepbetweenmsgs) usleep(sleepbetweenmsgs);
	bbmsgcount++;
	return res;
}


/* Routines for handling combo message transmission */
void combo_start(void)
{
	strcpy(bbmsg, "combo\n");
	bbmsgqueued = 0;

	if ((maxmsgspercombo == 0) && getenv("BBMAXMSGSPERCOMBO")) 
		maxmsgspercombo = atoi(getenv("BBMAXMSGSPERCOMBO"));
	if ((sleepbetweenmsgs == 0) && getenv("BBSLEEPBETWEENMSGS")) 
		sleepbetweenmsgs = atoi(getenv("BBSLEEPBETWEENMSGS"));
}

void combo_flush(void)
{

	if (!bbmsgqueued) {
		dprintf("Flush, but bbmsg is empty\n");
		return;
	}

	if (debug) {
		char *p1, *p2;

		printf("Flushing combo message\n");
		p1 = p2 = bbmsg;

		do {
			p2++;
			p1 = strstr(p2, "\nstatus ");
			if (p1) {
				p1++; /* Skip the newline */
				p2 = strchr(p1, '\n');
				if (p2) *p2='\0';
				printf("      %s\n", p1);
				if (p2) *p2='\n';
			}
		} while (p1 && p2);
	}

	sendmessage(bbmsg, NULL, NULL, 0);
	combo_start();	/* Get ready for the next */
}

void combo_add(char *buf)
{
	/* Check if there is room for the message + 2 newlines */
	if ( ((strlen(bbmsg) + strlen(buf) + 200) >= MAXMSG) || 
	     (maxmsgspercombo && (bbmsgqueued >= maxmsgspercombo)) ) {
		/* Nope ... flush buffer */
		combo_flush();
	}
	else {
		/* Yep ... add delimiter before new status (but not before the first!) */
		if (bbmsgqueued) strcat(bbmsg, "\n\n");
	}

	strcat(bbmsg, buf);
	bbmsgqueued++;
}

void combo_end(void)
{
	combo_flush();
	dprintf("%s: %d status messages merged into %d transmissions\n", 
		timestamp, bbstatuscount, bbmsgcount);
}


void init_status(int color)
{
	msgbuf[0] = '\0';
	msgcolor = color;
	bbstatuscount++;
}

void addtostatus(char *p)
{
	if ((strlen(msgbuf) + strlen(p)) < sizeof(msgbuf))
		strcat(msgbuf, p);
	else {
		strncat(msgbuf, p, sizeof(msgbuf)-strlen(msgbuf)-1);
	}
}

void finish_status(void)
{
	if (debug) {
		char *p = strchr(msgbuf, '\n');

		if (p) *p = '\0';
		dprintf("Adding to combo msg: %s\n", msgbuf);
		if (p) *p = '\n';
	}

	switch (msgcolor) {
		case COL_GREEN:
		case COL_BLUE:
		case COL_CLEAR:
			combo_add(msgbuf);
			break;
		default:
			/* Red, yellow and purple messages go out NOW. Or we get no alarms ... */
			bbnocombocount++;
			sendmessage(msgbuf, NULL, NULL, 0);
			break;
	}
}
#ifdef STANDALONE

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

int main(int argc, char *argv[])
{
	int result = 1;

	if (argc < 3) {
		fprintf(stderr, "Invalid call\n\n");
		fprintf(stderr, "Usage: %s RECIPIENT DATA\n", argv[0]);
		fprintf(stderr, "  RECIPIENT: IP-address, hostname or URL\n");
		fprintf(stderr, "  DATA: Message to send, or \"-\" to read from stdin\n");
		return 1;
	}

	if (strcmp(argv[2], "-") == 0) {
		char msg[MAXMSG];

		while (fgets(msg, sizeof(msg), stdin)) {
			result = sendmessage(msg, argv[1], NULL, 0);
		}
	}
	else {
		if (strncmp(argv[2], "query ", 6) == 0) {
			result = sendmessage(argv[2], argv[1], stdout, 0);
		}
		else if (strncmp(argv[2], "config ", 7) == 0) {
			result = sendmessage(argv[2], argv[1], stdout, 1);
		}
		else {
			result = sendmessage(argv[2], argv[1], NULL, 0);
		}
	}

	return result;
}
#endif

