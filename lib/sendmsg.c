/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for sending and receiving data to/from the BB daemon  */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: sendmsg.c,v 1.50 2005-01-18 22:25:59 henrik Exp $";

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
#include <stdio.h>

#include "libbbgen.h"

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
static char	*proxysetting = NULL;

static int	bbmetaqueued;		/* Anything in the buffer ? */
static char	metamsg[MAXMSG];
static char	metabuf[MAXMSG-50];	/* message buffer for one status message */

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

		if (proxysetting == NULL) proxysetting = xgetenv("http_proxy");
		if (proxysetting) {
			char *p;

			bbdispproxyhost = xstrdup(proxysetting);
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

		if (xgetenv("BBPORT")) bbdportnumber = atoi(xgetenv("BBPORT"));
	
	
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

	dprintf("Transport setup is:\n");
	dprintf("bbdportnumber = %d\n", bbdportnumber),
	dprintf("bbdispproxyhost = %s\n", (bbdispproxyhost ? bbdispproxyhost : "NONE"));
	dprintf("bbdispproxyport = %d\n", bbdispproxyport);
}

static int sendtobbd(char *recipient, char *message, FILE *respfd, char **respstr, int fullresponse, int timeout)
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
	int haveseenhttphdrs = 1;
	int respstrsz = 0;
	int respstrlen = 0;

	if (dontsendmessages && !respfd && !respstr) {
		printf("%s\n", message);
		return BB_OK;
	}

	setup_transport(recipient);

	dprintf("Recipient listed as '%s'\n", recipient);

	if (strncmp(recipient, "http://", strlen("http://")) != 0) {
		/* Standard BB communications, directly to bbd */
		rcptip = xstrdup(recipient);
		rcptport = bbdportnumber;
		p = strchr(rcptip, ':');
		if (p) {
			*p = '\0'; p++; rcptport = atoi(p);
		}
		dprintf("Standard BB protocol on port %d\n", rcptport);
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
			rcptip = xstrdup(recipient+strlen("http://"));
			rcptport = bbdportnumber;

			p = strchr(rcptip, '/');
			if (p) {
				posturl = xstrdup(p);
				*p = '\0';
			}

			p = strchr(rcptip, ':');
			if (p) {
				*p = '\0';
				p++;
				rcptport = atoi(p);
			}

			posthost = xstrdup(rcptip);

			dprintf("BB-HTTP protocol directly to host %s\n", posthost);
		}
		else {
			char *p;

			/*
			 * With proxy. The full "recipient" must be in the POST request.
			 */
			rcptip = xstrdup(bbdispproxyhost);
			rcptport = bbdispproxyport;

			posturl = xstrdup(recipient);

			p = strchr(recipient + strlen("http://"), '/');
			if (p) {
				*p = '\0';
				posthost = xstrdup(recipient + strlen("http://"));
				*p = '/';

				p = strchr(posthost, ':');
				if (p) *p = '\0';
			}

			dprintf("BB-HTTP protocol via proxy to host %s\n", posthost);
		}

		if ((posturl == NULL) || (posthost == NULL)) {
			errprintf("Unable to parse HTTP recipient\n");
			return BB_EBADURL;
		}

		bufp = msgptr = httpmessage = xmalloc(strlen(message)+1024);
		bufp += sprintf(httpmessage, "POST %s HTTP/1.0\n", posturl);
		bufp += sprintf(bufp, "MIME-version: 1.0\n");
		bufp += sprintf(bufp, "Content-Type: application/octet-stream\n");
		bufp += sprintf(bufp, "Content-Length: %d\n", strlen(message));
		bufp += sprintf(bufp, "Host: %s\n", posthost);
		bufp += sprintf(bufp, "\n%s", message);

		if (posturl) xfree(posturl);
		if (posthost) xfree(posthost);
		haveseenhttphdrs = 0;

		dprintf("BB-HTTP message is:\n%s\n", httpmessage);
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

retry_connect:
	dprintf("Will connect to address %s port %d\n", rcptip, rcptport);

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = addr.s_addr;
	saddr.sin_port = htons(rcptport);

	/* Get a non-blocking socket */
	sockfd = socket(PF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) return BB_ENOSOCKET;
	res = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if (res != 0) return BB_ECANNOTDONONBLOCK;

	res = connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
	if ((res == -1) && (errno != EINPROGRESS)) {
		close(sockfd);
		errprintf("connect to bbd failed - %s\n", strerror(errno));
		return BB_ECONNFAILED;
	}

	rdone = ((respfd == NULL) && (respstr == NULL));
	isconnected = wdone = 0;
	while (!wdone || !rdone) {
		FD_ZERO(&writefds);
		FD_ZERO(&readfds);
		if (!rdone) FD_SET(sockfd, &readfds);
		if (!wdone) FD_SET(sockfd, &writefds);
		tmo.tv_sec = timeout;  tmo.tv_usec = 0;
		res = select(sockfd+1, &readfds, &writefds, NULL, (timeout ? &tmo : NULL));
		if (res == -1) {
			errprintf("Select failure while sending to bbd@%s:%d!\n", rcptip, rcptport);
			shutdown(sockfd, SHUT_RDWR);
			close(sockfd);
			return BB_ESELFAILED;
		}
		else if (res == 0) {
			/* Timeout! */
			shutdown(sockfd, SHUT_RDWR);
			close(sockfd);

			if (!isconnected && (connretries > 0)) {
				dprintf("Timeout while talking to bbd@%s:%d - retrying\n", rcptip, rcptport);
				connretries--;
				sleep(1);
				goto retry_connect;	/* Yuck! */
			}

			return BB_ETIMEOUT;
		}
		else {
			if (!isconnected) {
				/* Havent seen our connect() status yet - must be now */
				int connres;
				socklen_t connressize = sizeof(connres);

				res = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &connres, &connressize);
				dprintf("Connect status is %d\n", connres);
				isconnected = (connres == 0);
				if (!isconnected) {
					shutdown(sockfd, SHUT_RDWR);
					close(sockfd);
					errprintf("Could not connect to bbd@%s:%d - %s\n", 
						  rcptip, rcptport, strerror(connres));
					return BB_ECONNFAILED;
				}
			}

			if (!rdone && FD_ISSET(sockfd, &readfds)) {
				char *outp;
				int n;

				n = recv(sockfd, response, MAXMSG-1, 0);
				if (n > 0) {
					dprintf("Read %d bytes\n", n);
					response[n] = '\0';

					/*
					 * When running over a HTTP transport, we must strip
					 * off the HTTP headers we get back, so the response
					 * is consistent with what we get from the normal bbd
					 * transport.
					 * (Non-http transport sets "haveseenhttphdrs" to 1)
					 */
					if (!haveseenhttphdrs) {
						outp = strstr(response, "\r\n\r\n");
						if (outp) {
							outp += 4;
							n -= (outp - response);
							haveseenhttphdrs = 1;
						}
						else n = 0;
					}
					else outp = response;

					if (n > 0) {
						if (respfd) {
							fwrite(outp, n, 1, respfd);
						}
						else if (respstr) {
							char *respend;

							if (respstrsz == 0) {
								respstrsz = (n+MAXMSG);
								*respstr = (char *)xmalloc(respstrsz);
							}
							else if ((n+respstrlen) >= respstrsz) {
								respstrsz += (n+MAXMSG);
								*respstr = (char *)xrealloc(*respstr, respstrsz);
							}
							respend = (*respstr) + respstrlen;
							memcpy(respend, outp, n);
							*(respend + n) = '\0';
							respstrlen += n;
						}
						if (!fullresponse) {
							rdone = (strchr(outp, '\n') == NULL);
						}
					}
				}
				else rdone = 1;
				if (rdone) shutdown(sockfd, SHUT_RD);
			}

			if (!wdone && FD_ISSET(sockfd, &writefds)) {
				/* Send some data */
				res = write(sockfd, msgptr, strlen(msgptr));
				if (res == -1) {
					shutdown(sockfd, SHUT_RDWR); close(sockfd);
					errprintf("Write error while sending message to bbd@%s:%d\n", rcptip, rcptport);
					return BB_EWRITEERROR;
				}
				else {
					dprintf("Sent %d bytes\n", res);
					msgptr += res;
					wdone = (strlen(msgptr) == 0);
					if (wdone) shutdown(sockfd, SHUT_WR);
				}
			}
		}
	}

	dprintf("Closing connection\n");
	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);
	xfree(rcptip);
	if (httpmessage) xfree(httpmessage);
	return BB_OK;
}

static int sendtomany(char *onercpt, char *morercpts, char *msg, int timeout)
{
	int result = 0;

	if (strcmp(onercpt, "0.0.0.0") != 0)
		result = sendtobbd(onercpt, msg, NULL, NULL, 0, timeout);
	else if (morercpts) {
		char *bbdlist, *rcpt;

		bbdlist = xstrdup(morercpts);
		rcpt = strtok(bbdlist, " \t");
		while (rcpt) {
			result += sendtobbd(rcpt, msg, NULL, NULL, 0, timeout);
			rcpt = strtok(NULL, " \t");
		}

		xfree(bbdlist);
	}
	else {
		errprintf("No recipients for message - oneaddr=%s, moreaddrs is NULL\n", onercpt);
		result = 100;
	}

	return result;
}

static int sendstatus(char *bbdisp, char *msg, int timeout)
{
	int statusresult, pageresult;
	char statuscolor[256];
	char *pagelevels;

	statusresult = sendtomany(bbdisp, xgetenv("BBDISPLAYS"), msg, timeout);

	/* If no BBPAGE defined, drop paging */
	if (xgetenv("BBPAGE") == NULL) return statusresult;

	/* If we're using hobbitd, drop the page message */
	if (strcmp(getenv_default("USEHOBBITD", "FALSE", NULL), "TRUE") == 0) return statusresult;

	/* Check if we should send a "page" message also */
	pagelevels = xstrdup(xgetenv("PAGELEVELS") ? xgetenv("PAGELEVELS") : PAGELEVELSDEFAULT);
	sscanf(msg, "%*s %*s %255s", statuscolor);
	if (strstr(pagelevels, statuscolor)) {
		/* Reformat the message into a "page" message */
		char *pagemsg = (char *) xmalloc(strlen(msg)+1);
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
		pageresult = sendtomany(xgetenv("BBPAGE"), xgetenv("BBPAGERS"), pagemsg, timeout);
		xfree(pagemsg);
	}

	xfree(pagelevels);
	return statusresult;
}


int sendmessage(char *msg, char *recipient, FILE *respfd, char **respstr, int fullresponse, int timeout)
{
	static char *bbdisp = NULL;
	int res = 0;

 	if ((bbdisp == NULL) && xgetenv("BBDISP")) bbdisp = xstrdup(xgetenv("BBDISP"));
	if (recipient == NULL) recipient = bbdisp;
	if (recipient == NULL) {
		errprintf("No recipient for message\n");
		return BB_EBADIP;
	}

	if ((strncmp(msg, "status", 6) == 0) || (strncmp(msg, "combo", 5) == 0)) {
		res = sendstatus((recipient ? recipient : bbdisp), msg, timeout);
	}
	else if (strncmp(msg, "data", 4) == 0) {
		res = sendtomany((recipient ? recipient : bbdisp), xgetenv("BBDISPLAYS"), msg, timeout);
	}
	else {
		res = sendtobbd(recipient, msg, respfd, respstr, fullresponse, timeout);
	}

	if (res != BB_OK) {
		char *statustext = "";

		switch (res) {
		  case BB_OK            : statustext = "OK"; break;
		  case BB_EBADIP        : statustext = "Bad IP address"; break;
		  case BB_EIPUNKNOWN    : statustext = "Cannot resolve hostname"; break;
		  case BB_ENOSOCKET     : statustext = "Cannot get a socket"; break;
		  case BB_ECANNOTDONONBLOCK   : statustext = "Non-blocking I/O failed"; break;
		  case BB_ECONNFAILED   : statustext = "Connection failed"; break;
		  case BB_ESELFAILED    : statustext = "select(2) failed"; break;
		  case BB_ETIMEOUT      : statustext = "timeout"; break;
		  case BB_EWRITEERROR   : statustext = "write error"; break;
		  case BB_EBADURL       : statustext = "Bad URL"; break;
		  default:                statustext = "Unknown error"; break;
		};

		errprintf("Whoops ! bb failed to send message - %s\n", statustext, res);
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

	if ((maxmsgspercombo == 0) && xgetenv("BBMAXMSGSPERCOMBO")) 
		maxmsgspercombo = atoi(xgetenv("BBMAXMSGSPERCOMBO"));
	if ((sleepbetweenmsgs == 0) && xgetenv("BBSLEEPBETWEENMSGS")) 
		sleepbetweenmsgs = atoi(xgetenv("BBSLEEPBETWEENMSGS"));
}

void meta_start(void)
{
	metamsg[0] = '\0';
	bbmetaqueued = 0;
}

static void combo_flush(void)
{

	if (!bbmsgqueued) {
		dprintf("Flush, but bbmsg is empty\n");
		return;
	}

	if (debug) {
		char *p1, *p2;

		dprintf("Flushing combo message\n");
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

	sendmessage(bbmsg, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
	combo_start();	/* Get ready for the next */
}

static void meta_flush(void)
{
	if (!bbmetaqueued) {
		dprintf("Flush, but bbmeta is empty\n");
		return;
	}

	sendmessage(metamsg, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
	meta_start();	/* Get ready for the next */
}

static void combo_add(char *buf)
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

static void meta_add(char *buf)
{
	/* Check if there is room for the message + 2 newlines */
	if ( ((strlen(metamsg) + strlen(buf) + 200) >= MAXMSG) || 
	     (maxmsgspercombo && (bbmetaqueued >= maxmsgspercombo)) ) {
		/* Nope ... flush buffer */
		meta_flush();
	}
	else {
		/* Yep ... add delimiter before new status (but not before the first!) */
		if (bbmetaqueued) strcat(metamsg, "\n\n");
	}

	strcat(metamsg, buf);
	bbmetaqueued++;
}

void combo_end(void)
{
	combo_flush();
	dprintf("%d status messages merged into %d transmissions\n", bbstatuscount, bbmsgcount);
}

void meta_end(void)
{
	meta_flush();
}

void init_status(int color)
{
	msgbuf[0] = '\0';
	msgcolor = color;
	bbstatuscount++;
}

void init_meta(char *metaname)
{
	metabuf[0] = '\0';
}

void addtostatus(char *p)
{
	if ((strlen(msgbuf) + strlen(p)) < sizeof(msgbuf))
		strcat(msgbuf, p);
	else {
		strncat(msgbuf, p, sizeof(msgbuf)-strlen(msgbuf)-1);
	}
}

void addtometa(char *p)
{
	if ((strlen(metabuf) + strlen(p)) < sizeof(metabuf))
		strcat(metabuf, p);
	else {
		strncat(metabuf, p, sizeof(metabuf)-strlen(metabuf)-1);
	}
}

void finish_status(void)
{
	int usehobbitd = (strcmp(getenv_default("USEHOBBITD", "FALSE", NULL), "TRUE") == 0);

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
			if (usehobbitd) {
				/* hobbitd takes anything in combos */
				combo_add(msgbuf);
			}
			else {
				/* Old bbd: Red, yellow and purple messages go out NOW. Or we get no alarms ... */
				bbnocombocount++;
				sendmessage(msgbuf, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
			}
			break;
	}
}

void finish_meta(void)
{
	meta_add(metabuf);
}


#if defined(STANDALONE) || defined(CGI)

int main(int argc, char *argv[])
{
	int argi;
	int showhelp = 0;
	int result = 1;
	int cgimode = 0;
	int timeout = BBTALK_TIMEOUT;
	char *recipient = NULL;
	char *msg = NULL;
	FILE *respfd = stdout;
	char *response = NULL;

#ifdef CGI
	cgimode = 1;
	recipient = "127.0.0.1";
	msg = "";
#else
	cgimode = 0;
	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strncmp(argv[argi], "--proxy=", 8) == 0) {
			char *p = strchr(argv[argi], '=');

			if (p) {
				p++;
				proxysetting = p;
			}
		}
		else if (strcmp(argv[argi], "--help") == 0) {
			showhelp = 1;
		}
		else if (strcmp(argv[argi], "--str") == 0) {
			respfd = NULL;
		}
		else if (strncmp(argv[argi], "--timeout=", 10) == 0) {
			char *p = strchr(argv[argi], '=');

			timeout = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1);
		}
		else if (strcmp(argv[argi], "-?") == 0) {
			showhelp = 1;
		}
		else if (strncmp(argv[argi], "-", 1) == 0) {
			fprintf(stderr, "Unknown option %s\n", argv[argi]);
		}
		else {
			/* No more options - pickup recipient and msg */
			if (recipient == NULL) {
				recipient = argv[argi];
			}
			else if (msg == NULL) {
				msg = argv[argi];
			}
			else {
				showhelp=1;
			}
		}
	}

	if ((recipient == NULL) || (msg == NULL) || showhelp) {
		fprintf(stderr, "Usage: %s [--debug] [--proxy=http://ip.of.the.proxy:port/] RECIPIENT DATA\n", argv[0]);
		fprintf(stderr, "  RECIPIENT: IP-address, hostname or URL\n");
		fprintf(stderr, "  DATA: Message to send, or \"-\" to read from stdin\n");
		return 1;
	}
#endif

	if (cgimode || (strcmp(msg, "@") == 0)) {
		char msg[MAXMSG];
		char *bufp = msg;
		int spaceleft = sizeof(msg)-1;

		do {
			if (fgets(bufp, spaceleft, stdin)) {
				spaceleft -= strlen(bufp);
				bufp += strlen(bufp);
			}
			else {
				spaceleft = 0;
			}
		} while (spaceleft > 0);

		if (cgimode) printf("Content-Type: application/octet-stream\n\n");
		result = sendmessage(msg, recipient, stdout, NULL, 1, timeout);
	}
	else if (strcmp(msg, "-") == 0) {
		char msg[MAXMSG];

		while (fgets(msg, sizeof(msg), stdin)) {
			result = sendmessage(msg, recipient, NULL, NULL, 0, timeout);
		}
	}
	else {
		if (strncmp(msg, "query ", 6) == 0) {
			result = sendmessage(msg, recipient, respfd, (respfd ? NULL : &response), 0, timeout);
		}
		else if (strncmp(msg, "config ", 7) == 0) {
			result = sendmessage(msg, recipient, respfd, (respfd ? NULL : &response), 1, timeout);
		}
		else if (strncmp(msg, "hobbitdlog ", 10) == 0) {
			result = sendmessage(msg, recipient, respfd, (respfd ? NULL : &response), 1, timeout);
		}
		else if (strncmp(msg, "hobbitdxlog ", 11) == 0) {
			result = sendmessage(msg, recipient, respfd, (respfd ? NULL : &response), 1, timeout);
		}
		else if (strncmp(msg, "hobbitdboard", 11) == 0) {
			result = sendmessage(msg, recipient, respfd, (respfd ? NULL : &response), 1, timeout);
		}
		else if (strncmp(msg, "hobbitdxboard", 12) == 0) {
			result = sendmessage(msg, recipient, respfd, (respfd ? NULL : &response), 1, timeout);
		}
		else if (strncmp(msg, "hobbitdlist", 10) == 0) {
			result = sendmessage(msg, recipient, respfd, (respfd ? NULL : &response), 1, timeout);
		}
		else {
			result = sendmessage(msg, recipient, NULL, NULL, 0, timeout);
		}

		if (response) {
			printf("Buffered response is '%s'\n", response);
		}
	}

	return result;
}
#endif

