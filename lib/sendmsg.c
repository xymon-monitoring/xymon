/*----------------------------------------------------------------------------*/
/* Big Brother "bbgen" toolkit - routines to send messages to bbd             */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: sendmsg.c,v 1.11 2003-08-27 20:18:18 henrik Exp $";

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

int dontsendmessages = 0;

static int sendtobbd(char *recipient, char *message)
{
	struct in_addr addr;
	struct sockaddr_in saddr;
	int	sockfd;
	fd_set	writefds;
	int	res, isconnected, done;
	struct timeval tmo;
	char *msgptr = message;

	if (dontsendmessages) {
		printf("%s\n", message);
		return BB_OK;
	}

	if (inet_aton(recipient, &addr) == 0) {
		/* recipient is not an IP - do DNS lookup */

		struct hostent *hent;
		char hostip[16];

		hent = gethostbyname(recipient);
		if (hent) {
			sprintf(hostip, "%d.%d.%d.%d",
				(unsigned char) hent->h_addr_list[0][0],
				(unsigned char) hent->h_addr_list[0][1],
				(unsigned char) hent->h_addr_list[0][2],
				(unsigned char) hent->h_addr_list[0][3]);
			if (inet_aton(hostip, &addr) == 0) return BB_EBADIP;
		}
		else {
			errprintf("Cannot determine IP address of message recipient %s\n", recipient);
			return BB_EIPUNKNOWN;
		}
	}
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = addr.s_addr;
	saddr.sin_port = htons(BBDPORTNUMBER);

	/* Get a non-blocking socket */
	sockfd = socket(PF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) return BB_ENOSOCKET;
	res = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if (res != 0) return BB_ECANNOTDONONBLOCK;

	res = connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
	if ((res == -1) && (errno != EINPROGRESS)) {
		close(sockfd);
		return BB_ECONNFAILED;
	}

	isconnected = done = 0;
	while (!done) {
		FD_ZERO(&writefds);
		FD_SET(sockfd, &writefds);
		tmo.tv_sec = 5;  tmo.tv_usec = 0; /* 5 seconds timeout to connect to bbd */
		res = select(sockfd+1, NULL, &writefds, NULL, &tmo);
		if (res == -1) {
			errprintf("Select failure while sending to bbd!\n");
			shutdown(sockfd, SHUT_RDWR); close(sockfd);
			return BB_ESELFAILED;
		}
		else if (res == 0) {
			/* Timeout! */
			errprintf("Timeout while talking to bbd!\n");
			close(sockfd);
			return BB_ETIMEOUT;
		}
		else if (FD_ISSET(sockfd, &writefds)) {
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
			else {
				/* Send some data */
				res = write(sockfd, msgptr, strlen(msgptr));
				if (res == -1) {
					shutdown(sockfd, SHUT_RDWR); close(sockfd);
					errprintf("Write error while sending message to bbd\n");
					return BB_EWRITEERROR;
				}
				else {
					msgptr += res;
					done = (strlen(msgptr) == 0);
				}
			}
		}
		else {
			/* Should not happen */
			dprintf("Huh - how did I get here ??\n");
		}
	}

	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);
	return BB_OK;
}

static int sendtomany(char *onercpt, char *morercpts, char *msg)
{
	int result = 0;

	if (strcmp(onercpt, "0.0.0.0") != 0)
		result = sendtobbd(onercpt, msg);
	else if (morercpts) {
		char *bbdlist, *rcpt;

		bbdlist = malcop(morercpts);
		rcpt = strtok(bbdlist, " \t");
		while (rcpt) {
			result += sendtobbd(rcpt, msg);
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


int sendmessage(char *msg, char *recipient)
{
	static char *bbdisp = NULL;
	int res = 0;

	if ((bbdisp == NULL) && (recipient == NULL)) bbdisp = malcop(getenv("BBDISP"));

	if ((strncmp(msg, "status", 6) == 0) || (strncmp(msg, "combo", 5) == 0)) {
		res = sendstatus((recipient ? recipient : bbdisp), msg);
	}
	else {
		res = sendtobbd(recipient, msg);
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

	sendmessage(bbmsg, NULL);
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
			sendmessage(msgbuf, NULL);
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
	int result;

	result = sendmessage(argv[2], argv[1]);
	return result;
}
#endif

