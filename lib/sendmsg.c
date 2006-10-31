/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for sending and receiving data to/from the BB daemon  */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: sendmsg.c,v 1.84 2006-10-31 11:53:40 henrik Exp $";

#include "config.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdio.h>

#include "libbbgen.h"

#define BBSENDRETRIES 2

/* These commands go to BBDISPLAYS */
static char *multircptcmds[] = { "status", "combo", "meta", "data", "notify", "enable", "disable", "drop", "rename", "client", NULL };

/* Stuff for combo message handling */
int		bbmsgcount = 0;		/* Number of messages transmitted */
int		bbstatuscount = 0;	/* Number of status items reported */
int		bbnocombocount = 0;	/* Number of status items reported outside combo msgs */
static int	bbmsgqueued;		/* Anything in the buffer ? */
static strbuffer_t *bbmsg = NULL;	/* Complete combo message buffer */
static strbuffer_t *msgbuf = NULL;	/* message buffer for one status message */
static int	msgcolor;		/* color of status message in msgbuf */
static int      maxmsgspercombo = 100;	/* 0 = no limit. 100 is a reasonable default. */
static int      sleepbetweenmsgs = 0;
static int      bbdportnumber = 0;
static char     *bbdispproxyhost = NULL;
static int      bbdispproxyport = 0;
static char	*proxysetting = NULL;

static int	bbmetaqueued;		/* Anything in the buffer ? */
static strbuffer_t *metamsg = NULL;	/* Complete meta message buffer */
static strbuffer_t *metabuf = NULL;	/* message buffer for one meta message */

int dontsendmessages = 0;


void setproxy(char *proxy)
{
	if (proxysetting) xfree(proxysetting);
	proxysetting = strdup(proxy);
}

static void setup_transport(char *recipient)
{
	static int transport_is_setup = 0;
	int default_port;

	if (transport_is_setup) return;
	transport_is_setup = 1;

	if (strcmp(recipient, "local") == 0) {
		dbgprintf("Using local Unix domain socket transport\n");
		return;
	}

	if (strncmp(recipient, "http://", 7) == 0) {
		/*
		 * Send messages via http. This requires e.g. a CGI on the webserver to
		 * receive the POST we do here.
		 */
		default_port = 80;

		if (proxysetting == NULL) proxysetting = getenv("http_proxy");
		if (proxysetting) {
			char *p;

			bbdispproxyhost = strdup(proxysetting);
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
		default_port = 1984;

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

	dbgprintf("Transport setup is:\n");
	dbgprintf("bbdportnumber = %d\n", bbdportnumber),
	dbgprintf("bbdispproxyhost = %s\n", (bbdispproxyhost ? bbdispproxyhost : "NONE"));
	dbgprintf("bbdispproxyport = %d\n", bbdispproxyport);
}

static int sendtobbd(char *recipient, char *message, FILE *respfd, char **respstr, int fullresponse, int timeout)
{
	struct in_addr addr;
	struct sockaddr_un localaddr;
	struct sockaddr_in saddr;
	enum { C_IP, C_UNIX } conntype = C_IP;
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
	char recvbuf[32768];
	int haveseenhttphdrs = 1;
	int respstrsz = 0;
	int respstrlen = 0;

	if (dontsendmessages && !respfd && !respstr) {
		printf("%s\n", message);
		return BB_OK;
	}

	setup_transport(recipient);

	dbgprintf("Recipient listed as '%s'\n", recipient);

	if ((strcmp(recipient, "local") == 0) || (strcmp(recipient, "127.0.0.1") == 0) || (strcmp(recipient, xgetenv("BBDISP")) == 0)) {
		/* Connect via local unix domain socket $BBTMP/hobbitd_if */
		dbgprintf("Unix domain protocol\n");
		conntype = C_UNIX;
	}
	else if (strncmp(recipient, "http://", strlen("http://")) != 0) {
		/* Standard BB communications, directly to bbd */
		rcptip = strdup(recipient);
		rcptport = bbdportnumber;
		p = strchr(rcptip, ':');
		if (p) {
			*p = '\0'; p++; rcptport = atoi(p);
		}
		dbgprintf("Standard BB protocol on port %d\n", rcptport);
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
			rcptip = strdup(recipient+strlen("http://"));
			rcptport = bbdportnumber;

			p = strchr(rcptip, '/');
			if (p) {
				posturl = strdup(p);
				*p = '\0';
			}

			p = strchr(rcptip, ':');
			if (p) {
				*p = '\0';
				p++;
				rcptport = atoi(p);
			}

			posthost = strdup(rcptip);

			dbgprintf("BB-HTTP protocol directly to host %s\n", posthost);
		}
		else {
			char *p;

			/*
			 * With proxy. The full "recipient" must be in the POST request.
			 */
			rcptip = strdup(bbdispproxyhost);
			rcptport = bbdispproxyport;

			posturl = strdup(recipient);

			p = strchr(recipient + strlen("http://"), '/');
			if (p) {
				*p = '\0';
				posthost = strdup(recipient + strlen("http://"));
				*p = '/';

				p = strchr(posthost, ':');
				if (p) *p = '\0';
			}

			dbgprintf("BB-HTTP protocol via proxy to host %s\n", posthost);
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

		if (posturl) xfree(posturl);
		if (posthost) xfree(posthost);
		haveseenhttphdrs = 0;

		dbgprintf("BB-HTTP message is:\n%s\n", httpmessage);
	}

	if (conntype == C_IP) {
		if (inet_aton(rcptip, &addr) == 0) {
			/* recipient is not an IP - do DNS lookup */

			struct hostent *hent;
			char hostip[IP_ADDR_STRLEN];

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
	}

retry_connect:
	dbgprintf("Will connect to address %s port %d\n", rcptip, rcptport);

	if (conntype == C_IP) {
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
	}
	else {
		memset(&localaddr, 0, sizeof(localaddr));
		localaddr.sun_family = AF_UNIX;
		sprintf(localaddr.sun_path, "%s/hobbitd_if", xgetenv("BBTMP"));

		/* Get a non-blocking socket */
		sockfd = socket(PF_UNIX, SOCK_STREAM, 0);
		if (sockfd == -1) return BB_ENOSOCKET;
		res = fcntl(sockfd, F_SETFL, O_NONBLOCK);
		if (res != 0) return BB_ECANNOTDONONBLOCK;

		res = connect(sockfd, (struct sockaddr *)&localaddr, sizeof(localaddr));
	}

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
				dbgprintf("Timeout while talking to bbd@%s:%d - retrying\n", rcptip, rcptport);
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
				dbgprintf("Connect status is %d\n", connres);
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

				n = recv(sockfd, recvbuf, sizeof(recvbuf)-1, 0);
				if (n > 0) {
					dbgprintf("Read %d bytes\n", n);
					recvbuf[n] = '\0';

					/*
					 * When running over a HTTP transport, we must strip
					 * off the HTTP headers we get back, so the response
					 * is consistent with what we get from the normal bbd
					 * transport.
					 * (Non-http transport sets "haveseenhttphdrs" to 1)
					 */
					if (!haveseenhttphdrs) {
						outp = strstr(recvbuf, "\r\n\r\n");
						if (outp) {
							outp += 4;
							n -= (outp - recvbuf);
							haveseenhttphdrs = 1;
						}
						else n = 0;
					}
					else outp = recvbuf;

					if (n > 0) {
						if (respfd) {
							fwrite(outp, n, 1, respfd);
						}
						else if (respstr) {
							char *respend;

							if (respstrsz == 0) {
								respstrsz = (n+sizeof(recvbuf));
								*respstr = (char *)malloc(respstrsz);
							}
							else if ((n+respstrlen) >= respstrsz) {
								respstrsz += (n+sizeof(recvbuf));
								*respstr = (char *)realloc(*respstr, respstrsz);
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
					dbgprintf("Sent %d bytes\n", res);
					msgptr += res;
					wdone = (strlen(msgptr) == 0);
					if (wdone) shutdown(sockfd, SHUT_WR);
				}
			}
		}
	}

	dbgprintf("Closing connection\n");
	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);
	if (rcptip) xfree(rcptip);
	if (httpmessage) xfree(httpmessage);
	return BB_OK;
}

static int sendtomany(char *onercpt, char *morercpts, char *msg, FILE *respfd, char **respstr, int fullresponse, int timeout)
{
	int allservers = 1, first = 1, result = BB_OK;
	char *bbdlist, *rcpt;

	/*
	 * Even though this is the "sendtomany" routine, we need to decide if the
	 * request should go to all servers, or just a single server. The default 
	 * is to send to all servers - but commands that trigger a response can
	 * only go to a single server.
	 *
	 * "schedule" is special - when scheduling an action there is no response, but 
	 * when it is the blank "schedule" command there will be a response. So a 
	 * schedule action goes to all BBDISPLAYS, the blank "schedule" goes to a single
	 * server.
	 */

	if (strcmp(onercpt, "0.0.0.0") != 0) 
		allservers = 0;
	else if (strncmp(msg, "schedule", 8) == 0)
		/* See if it's just a blank "schedule" command */
		allservers = (strcmp(msg, "schedule") != 0);
	else {
		char *msgcmd;
		int i;

		/* See if this is a multi-recipient command */
		i = strspn(msg, "abcdefghijklmnopqrstuvwxyz");
		msgcmd = (char *)malloc(i+1);
		strncpy(msgcmd, msg, i); *(msgcmd+i) = '\0';
		for (i = 0; (multircptcmds[i] && strcmp(multircptcmds[i], msgcmd)); i++) ;
		xfree(msgcmd);

		allservers = (multircptcmds[i] != NULL);
	}

	if (allservers && !morercpts) {
		errprintf("No recipients listed! BBDISP was %s, BBDISPLAYS %s\n",
			  onercpt, textornull(morercpts));
		return BB_EBADIP;
	}

	if (strcmp(onercpt, "0.0.0.0") != 0) 
		bbdlist = strdup(onercpt);
	else
		bbdlist = strdup(morercpts);

	rcpt = strtok(bbdlist, " \t");
	while (rcpt) {
		int oneres;

		if (first) {
			/* We grab the result from the first server */
			oneres =  sendtobbd(rcpt, msg, respfd, respstr, fullresponse, timeout);
			if (oneres == BB_OK) first = 0;
		}
		else {
			/* Secondary servers do not yield a response */
			oneres =  sendtobbd(rcpt, msg, NULL, NULL, 0, timeout);
		}

		/* Save any error results */
		if (result == BB_OK) result = oneres;

		/*
		 * Handle more servers IF we're doing all servers, OR
		 * we are still at the first one (because the previous
		 * ones failed).
		 */
		if (allservers || first) 
			rcpt = strtok(NULL, " \t");
		else 
			rcpt = NULL;
	}

	xfree(bbdlist);

	return result;
}


int sendmessage(char *msg, char *recipient, FILE *respfd, char **respstr, int fullresponse, int timeout)
{
	static char *bbdisp = NULL;
	int res = 0;

 	if ((bbdisp == NULL) && xgetenv("BBDISP")) bbdisp = strdup(xgetenv("BBDISP"));
	if (recipient == NULL) recipient = bbdisp;
	if (recipient == NULL) {
		errprintf("No recipient for message\n");
		return BB_EBADIP;
	}

	res = sendtomany((recipient ? recipient : bbdisp), xgetenv("BBDISPLAYS"), msg, respfd, respstr, fullresponse, timeout);

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
static void combo_params(void)
{
	static int issetup = 0;

	if (issetup) return;

	issetup = 1;

	if (xgetenv("BBMAXMSGSPERCOMBO")) maxmsgspercombo = atoi(xgetenv("BBMAXMSGSPERCOMBO"));
	if (maxmsgspercombo == 0) {
		/* Force it to 100 */
		dbgprintf("BBMAXMSGSPERCOMBO is 0, setting it to 100\n");
		maxmsgspercombo = 100;
	}

	if (xgetenv("BBSLEEPBETWEENMSGS")) sleepbetweenmsgs = atoi(xgetenv("BBSLEEPBETWEENMSGS"));
}

void combo_start(void)
{
	combo_params();

	if (bbmsg == NULL) bbmsg = newstrbuffer(0);
	clearstrbuffer(bbmsg);
	addtobuffer(bbmsg, "combo\n");
	bbmsgqueued = 0;
}

void meta_start(void)
{
	if (metamsg == NULL) metamsg = newstrbuffer(0);
	clearstrbuffer(metamsg);
	bbmetaqueued = 0;
}

static void combo_flush(void)
{

	if (!bbmsgqueued) {
		dbgprintf("Flush, but bbmsg is empty\n");
		return;
	}

	if (debug) {
		char *p1, *p2;

		dbgprintf("Flushing combo message\n");
		p1 = p2 = STRBUF(bbmsg);

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

	sendmessage(STRBUF(bbmsg), NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
	combo_start();	/* Get ready for the next */
}

static void meta_flush(void)
{
	if (!bbmetaqueued) {
		dbgprintf("Flush, but bbmeta is empty\n");
		return;
	}

	sendmessage(STRBUF(metamsg), NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
	meta_start();	/* Get ready for the next */
}

static void combo_add(strbuffer_t *buf)
{
	/* Check if there is room for the message + 2 newlines */
	if (maxmsgspercombo && (bbmsgqueued >= maxmsgspercombo)) {
		/* Nope ... flush buffer */
		combo_flush();
	}
	else {
		/* Yep ... add delimiter before new status (but not before the first!) */
		if (bbmsgqueued) addtobuffer(bbmsg, "\n\n");
	}

	addtostrbuffer(bbmsg, buf);
	bbmsgqueued++;
}

static void meta_add(strbuffer_t *buf)
{
	/* Check if there is room for the message + 2 newlines */
	if (maxmsgspercombo && (bbmetaqueued >= maxmsgspercombo)) {
		/* Nope ... flush buffer */
		meta_flush();
	}
	else {
		/* Yep ... add delimiter before new status (but not before the first!) */
		if (bbmetaqueued) addtobuffer(metamsg, "\n\n");
	}

	addtostrbuffer(metamsg, buf);
	bbmetaqueued++;
}

void combo_end(void)
{
	combo_flush();
	dbgprintf("%d status messages merged into %d transmissions\n", bbstatuscount, bbmsgcount);
}

void meta_end(void)
{
	meta_flush();
}

void init_status(int color)
{
	if (msgbuf == NULL) msgbuf = newstrbuffer(0);
	clearstrbuffer(msgbuf);
	msgcolor = color;
	bbstatuscount++;
}

void init_meta(char *metaname)
{
	if (metabuf == NULL) metabuf = newstrbuffer(0);
	clearstrbuffer(metabuf);
}

void addtostatus(char *p)
{
	addtobuffer(msgbuf, p);
}

void addtostrstatus(strbuffer_t *p)
{
	addtostrbuffer(msgbuf, p);
}

void addtometa(char *p)
{
	addtobuffer(metabuf, p);
}

void finish_status(void)
{
	if (debug) {
		char *p = strchr(STRBUF(msgbuf), '\n');

		if (p) *p = '\0';
		dbgprintf("Adding to combo msg: %s\n", STRBUF(msgbuf));
		if (p) *p = '\n';
	}

	combo_add(msgbuf);
}

void finish_meta(void)
{
	meta_add(metabuf);
}


