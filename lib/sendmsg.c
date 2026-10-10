/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for communicating with the Xymon daemon               */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include "config.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdio.h>

#include <limits.h>
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <sys/ipc.h>
#include <sys/msg.h>

#include "libxymon.h"
#include "xymontls.h"

#define SENDRETRIES 2

/* These commands go to all Xymon servers */
static char *multircptcmds[] = { "status", "combo", "extcombo", "meta", "data", "notify", "enable", "disable", "drop", "rename", "client", NULL };
static char errordetails[1024];

/* Stuff for combo message handling */
int		xymonmsgcount = 0;	/* Number of messages transmitted */
int		xymonstatuscount = 0;	/* Number of status items reported */
int		xymonnocombocount = 0;	/* Number of status items reported outside combo msgs */
static int	xymonmsgqueued;		/* Anything in the buffer ? */
static strbuffer_t *xymonmsg = NULL;	/* Complete combo message buffer */
static strbuffer_t *msgbuf = NULL;	/* message buffer for one status message */
static int	msgcolor;		/* color of status message in msgbuf */
static int	combo_is_local = 0;
static int      maxmsgspercombo = 100;	/* 0 = no limit. 100 is a reasonable default. */
static int      sleepbetweenmsgs = 0;
static int      xymondportnumber = 0;
static char     *xymonproxyhost = NULL;
static int      xymonproxyport = 0;
static char	*proxysetting = NULL;
static char	*comboofsstr = NULL;
static int	comboofssz = 0;
static int	*combooffsets = NULL;

static int	xymonmetaqueued;		/* Anything in the buffer ? */
static strbuffer_t *metamsg = NULL;	/* Complete meta message buffer */
static strbuffer_t *metabuf = NULL;	/* message buffer for one meta message */

static int backfeedqueue = -1;
static int max_backfeedsz = 16384;

int dontsendmessages = 0;

void setproxy(char *proxy)
{
	if (proxysetting) xfree(proxysetting);
	proxysetting = strdup(proxy);
}

/*
 * Split a recipient's address in place: "host:port", "[IPv6]:port",
 * "[IPv6]", or a bare IPv6 address, which has more than one ':' and so
 * cannot carry a port. Returns the host, without brackets; *port is set
 * only when a port is given.
 */
static char *split_hostport(char *s, int *port)
{
	char *p;

	if (*s == '[') {
		p = strchr(s, ']');
		if (!p) return s;	/* unbalanced: left for the lookup to refuse */
		*p = '\0';
		if (*(p+1) == ':') *port = atoi(p+2);
		return s+1;
	}

	p = strchr(s, ':');
	if (p && (strchr(p+1, ':') == NULL)) {
		*p = '\0';
		*port = atoi(p+1);
	}
	return s;
}

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

		if (proxysetting == NULL) proxysetting = getenv("http_proxy");
		if (proxysetting) {
			char *h = strdup(proxysetting);

			if (strncmp(h, "http://", 7) == 0) h += strlen("http://");
			xymonproxyport = 8080;
			xymonproxyhost = split_hostport(h, &xymonproxyport);
		}
	}
	else {
		/* 
		 * Non-HTTP transport - lookup portnumber in both XYMONDPORT env.
		 * and the "xymond" entry from /etc/services.
		 */
		default_port = 1984;

		if (xgetenv("XYMONDPORT")) xymondportnumber = atoi(xgetenv("XYMONDPORT"));
	
	
		/* Next is /etc/services "bbd" entry */
		if ((xymondportnumber <= 0) || (xymondportnumber > 65535)) {
			struct servent *svcinfo;

			svcinfo = getservbyname("bbd", NULL);
			if (!svcinfo) svcinfo = getservbyname("bb", NULL);
			if (svcinfo) xymondportnumber = ntohs(svcinfo->s_port);
		}
	}

	/* Last resort: The default value */
	if ((xymondportnumber <= 0) || (xymondportnumber > 65535)) {
		xymondportnumber = default_port;
	}

	dbgprintf("Transport setup is:\n");
	dbgprintf("xymondportnumber = %d\n", xymondportnumber);
	dbgprintf("xymonproxyhost = %s\n", (xymonproxyhost ? xymonproxyhost : "NONE"));
	dbgprintf("xymonproxyport = %d\n", xymonproxyport);
}

/*
 * Keep n bytes of the reply: write them to respfd, or append them to
 * *respstr. Returns 1 when that was the last of it -- a caller that did not
 * ask for the full response stops at the first piece holding no newline.
 */
static int keep_response(char *outp, int n, FILE *respfd, char **respstr, int *respstrsz, int *respstrlen, int fullresponse)
{
	if (respfd) {
		fwrite(outp, n, 1, respfd);
	}
	else if (respstr) {
		char *respend;

		if (*respstrsz == 0) {
			*respstrsz = (n+32768);
			*respstr = (char *)malloc(*respstrsz);
		}
		else if ((n + *respstrlen) >= *respstrsz) {
			*respstrsz += (n+32768);
			*respstr = (char *)realloc(*respstr, *respstrsz);
		}
		respend = (*respstr) + *respstrlen;
		memcpy(respend, outp, n);
		*(respend + n) = '\0';
		*respstrlen += n;
	}
	return (!fullresponse && (memchr(outp, '\n', n) == NULL));
}

#ifdef HAVE_OPENSSL
#include <openssl/err.h>

/*
 * The conversation over TLS, on a connected socket: the handshake, the
 * message, close_notify -- TLS's half-close, as shutdown(SHUT_WR) is the
 * plaintext one -- and the reply until the server's close_notify. The
 * settings are XYMON_TLS_CA, _CERT, _KEY, _SNI and _VERIFY. A failure is
 * the send's failure: there is no retry in plaintext.
 *
 * The server's close_notify is read even when no reply is wanted: it says
 * the message was taken. Under TLS 1.3 our side of the handshake is done
 * before the server has checked our certificate, so a refusal arrives only
 * here, as an alert, and would otherwise look like a message sent.
 */
static int tls_conversation(int sockfd, char *host, char *label, char *message, int timeout,
			    FILE *respfd, char **respstr, int *respstrsz, int *respstrlen, int fullresponse)
{
	static SSL_CTX *ctx = NULL;
	char err[512], buf[32768], reason[256];
	unsigned long e;
	char *v = xgetenv("XYMON_TLS_VERIFY"), *sni = xgetenv("XYMON_TLS_SNI");
	xymontls_verify_t verify = XYMONTLS_VERIFY_FULL;
	SSL *ssl;
	struct timeval tv;
	int n, done = 0, result = XYMONSEND_OK;
	size_t sent = 0, len = strlen(message);

	if (v && (strcmp(v, "peer") == 0)) verify = XYMONTLS_VERIFY_PEER;
	else if (v && (strcmp(v, "none") == 0)) verify = XYMONTLS_VERIFY_NONE;

	if (!ctx) {
		char *ca = xgetenv("XYMON_TLS_CA"), *cert = xgetenv("XYMON_TLS_CERT"), *key = xgetenv("XYMON_TLS_KEY");

		ctx = xymontls_client_ctx((ca && *ca) ? ca : NULL, (cert && *cert) ? cert : NULL,
					  (key && *key) ? key : NULL, verify, err, sizeof(err));
		if (!ctx) {
			snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "TLS to Xymon daemon@%s: %s", label, err);
			return XYMONSEND_ECONNFAILED;
		}
	}

	/* The exchange runs blocking, bounded by the timeout like the plaintext one */
	fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) & ~O_NONBLOCK);
	tv.tv_sec = timeout; tv.tv_usec = 0;
	if (timeout) {
		setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	}

	ssl = SSL_new(ctx);
	SSL_set_fd(ssl, sockfd);
	if (!xymontls_client_expect(ssl, ((sni && *sni) ? sni : host), verify) || (SSL_connect(ssl) != 1)) {
		long vr = SSL_get_verify_result(ssl);
		snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "TLS handshake with Xymon daemon@%s failed%s%s",
			 label, (vr != X509_V_OK) ? ": " : "", (vr != X509_V_OK) ? X509_verify_cert_error_string(vr) : "");
		SSL_free(ssl);
		return XYMONSEND_ECONNFAILED;
	}

	while (sent < len) {
		n = SSL_write(ssl, message + sent, (int)(len - sent));
		if (n <= 0) {
			snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "Write error while sending message to Xymon daemon@%s over TLS", label);
			SSL_free(ssl);
			return XYMONSEND_EWRITEERROR;
		}
		sent += n;
	}
	SSL_shutdown(ssl);	/* close_notify: the message is complete */

	while ((n = SSL_read(ssl, buf, sizeof(buf)-1)) > 0) {
		if (done || (!respfd && !respstr)) continue;	/* not wanted, read on to close_notify */
		buf[n] = '\0';
		done = keep_response(buf, n, respfd, respstr, respstrsz, respstrlen, fullresponse);
	}
	if (SSL_get_error(ssl, n) != SSL_ERROR_ZERO_RETURN) {
		e = ERR_get_error();
		if (e) ERR_error_string_n(e, reason, sizeof(reason));
		else snprintf(reason, sizeof(reason), "the connection ended without close_notify");
		snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)),
			 "Xymon daemon@%s did not take the message over TLS: %s", label, reason);
		result = XYMONSEND_EREADERROR;
	}
	ERR_clear_error();

	SSL_free(ssl);
	return result;
}
#endif

static int sendtoxymond(char *recipient, char *message, FILE *respfd, char **respstr, int fullresponse, int timeout)
{
	struct addrinfo hints, *addrs = NULL, *ai = NULL;
	char portstr[16];
	char rcptlabel[300];
	int	sockfd = -1;
	fd_set	readfds;
	fd_set	writefds;
	int	res, isconnected, wdone, rdone;
	struct timeval tmo;
	char *msgptr = message;
	char *p;
	char *rcptbuf = NULL, *rcptip = NULL;
	int rcptport = 0;
	int connretries = SENDRETRIES;
	SBUF_DEFINE(httpmessage);
	char recvbuf[32768];
	int haveseenhttphdrs = 1;
	int usetls = 0;
	int respstrsz = 0;
	int respstrlen = 0;
	int result = XYMONSEND_OK;

	if (dontsendmessages && !respfd && !respstr) {
		fprintf(stdout, "%s\n", message);
		fflush(stdout);
		return XYMONSEND_OK;
	}

	setup_transport(recipient);

	dbgprintf("Recipient listed as '%s'\n", recipient);

	if (strncmp(recipient, "http://", strlen("http://")) != 0) {
		/*
		 * Standard communications, directly to Xymon daemon: "host[:port]",
		 * or "xymon://" before it, or "xymons://" for TLS -- on XYMONDTLSPORT
		 * unless a port is given.
		 */
		char *r = recipient, *slash;

		if (strncmp(r, "xymons://", 9) == 0) { usetls = 1; r += 9; }
		else if (strncmp(r, "xymon://", 8) == 0) r += 8;
		rcptbuf = strdup(r);
		if ((slash = strchr(rcptbuf, '/')) != NULL) *slash = '\0';
		rcptport = (usetls ? atoi(xgetenv("XYMONDTLSPORT")) : xymondportnumber);
		rcptip = split_hostport(rcptbuf, &rcptport);
		dbgprintf("Standard protocol%s on port %d\n", (usetls ? " over TLS" : ""), rcptport);
#ifndef HAVE_OPENSSL
		if (usetls) {
			snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "Cannot send to %s: this build has no TLS support", recipient);
			xfree(rcptbuf);
			return XYMONSEND_ECONNFAILED;
		}
#endif
	}
	else {
		char *posturl = NULL;
		char *posthost = NULL;

		if (xymonproxyhost == NULL) {
			char *p;

			/*
			 * No proxy. "recipient" is "http://host[:port]/url/for/post"
			 * Strip off "http://", and point "posturl" to the part after the hostname.
			 * If a portnumber is present, strip it off and update rcptport.
			 */
			rcptbuf = strdup(recipient+strlen("http://"));
			rcptport = xymondportnumber;

			p = strchr(rcptbuf, '/');
			if (p) {
				posturl = strdup(p);
				*p = '\0';
			}

			rcptip = split_hostport(rcptbuf, &rcptport);

			/* The Host: header brackets an IPv6 address, as a URL does */
			posthost = (char *)malloc(strlen(rcptip) + 3);
			snprintf(posthost, strlen(rcptip) + 3, (strchr(rcptip, ':') ? "[%s]" : "%s"), rcptip);

			dbgprintf("HTTP protocol directly to host %s\n", posthost);
		}
		else {
			char *p;

			/*
			 * With proxy. The full "recipient" must be in the POST request.
			 */
			rcptbuf = strdup(xymonproxyhost);
			rcptip = rcptbuf;
			rcptport = xymonproxyport;

			posturl = strdup(recipient);

			p = strchr(recipient + strlen("http://"), '/');
			if (p) {
				*p = '\0';
				posthost = strdup(recipient + strlen("http://"));
				*p = '/';

				/* Drop the port; keep an IPv6 address's brackets */
				if (*posthost == '[') {
					p = strchr(posthost, ']');
					if (p) *(p+1) = '\0';
				}
				else {
					p = strchr(posthost, ':');
					if (p) *p = '\0';
				}
			}

			dbgprintf("HTTP protocol via proxy to host %s\n", posthost);
		}

		if ((posturl == NULL) || (posthost == NULL)) {
			snprintf(errordetails + strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "Unable to parse HTTP recipient");
			if (posturl) xfree(posturl);
			if (posthost) xfree(posthost);
			if (rcptbuf) xfree(rcptbuf);
			return XYMONSEND_EBADURL;
		}

		SBUF_MALLOC(httpmessage, strlen(message)+strlen(posthost)+1024);
		msgptr = httpmessage;
		snprintf(httpmessage, httpmessage_buflen, 
			 "POST %s HTTP/1.0\nMIME-version: 1.0\nContent-Type: application/octet-stream\nContent-Length: %d\nHost: %s\n\n%s",
			 posturl, (int)strlen(message), posthost, message);

		if (posturl) xfree(posturl);
		if (posthost) xfree(posthost);
		haveseenhttphdrs = 0;

		dbgprintf("HTTP message is:\n%s\n", httpmessage);
	}

	snprintf(rcptlabel, sizeof(rcptlabel), (strchr(rcptip, ':') ? "[%s]:%d" : "%s:%d"), rcptip, rcptport);

	/*
	 * A name or an address of either family. A name may resolve to several
	 * addresses, IPv6 and IPv4 among them: each is tried in the resolver's
	 * order until one connects.
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(portstr, sizeof(portstr), "%d", rcptport);
	if (getaddrinfo(rcptip, portstr, &hints, &addrs) != 0) {
		snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "Cannot determine IP address of message recipient %s", rcptip);
		result = XYMONSEND_EIPUNKNOWN;
		goto done;
	}
	ai = addrs;

retry_connect:
	dbgprintf("Will connect to address %s port %d (family %d)\n", rcptip, rcptport, ai->ai_family);

	/* Get a non-blocking socket */
	sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (sockfd == -1) {
		if (ai->ai_next) { ai = ai->ai_next; goto retry_connect; }
		result = XYMONSEND_ENOSOCKET; goto done;
	}
	res = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if (res != 0) { result = XYMONSEND_ECANNOTDONONBLOCK; goto done; }

	res = connect(sockfd, ai->ai_addr, ai->ai_addrlen);
	if ((res == -1) && (errno != EINPROGRESS)) {
		if (ai->ai_next) {
			dbgprintf("connect to Xymon daemon@%s failed (%s) - trying its next address\n", rcptlabel, strerror(errno));
			close(sockfd);
			ai = ai->ai_next;
			goto retry_connect;
		}
		snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "connect to Xymon daemon@%s failed (%s)", rcptlabel, strerror(errno));
		result = XYMONSEND_ECONNFAILED;
		goto done;
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
			snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "Select failure while sending to Xymon daemon@%s", rcptlabel);
			result = XYMONSEND_ESELFAILED;
			goto done;
		}
		else if (res == 0) {
			/* Timeout! */
			shutdown(sockfd, SHUT_RDWR);
			close(sockfd);

			if (!isconnected && ai->ai_next) {
				dbgprintf("Timeout while connecting to Xymon daemon@%s - trying its next address\n", rcptlabel);
				ai = ai->ai_next;
				goto retry_connect;
			}
			if (!isconnected && (connretries > 0)) {
				dbgprintf("Timeout while talking to Xymon daemon@%s - retrying\n", rcptlabel);
				connretries--;
				ai = addrs;
				sleep(1);
				goto retry_connect;	/* Yuck! */
			}

			result = XYMONSEND_ETIMEOUT;
			goto done;
		}
		else {
			if (!isconnected) {
				/* Havent seen our connect() status yet - must be now */
				int connres;
				socklen_t connressize = sizeof(connres);

				res = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &connres, &connressize);
				dbgprintf("Connect status is %d\n", connres);
				isconnected = (connres == 0);
				if (!isconnected && ai->ai_next) {
					dbgprintf("Could not connect to Xymon daemon@%s (%s) - trying its next address\n", rcptlabel, strerror(connres));
					close(sockfd);
					ai = ai->ai_next;
					goto retry_connect;
				}
				if (!isconnected) {
					snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "Could not connect to Xymon daemon@%s (%s)",
						  rcptlabel, strerror(connres));
					result = XYMONSEND_ECONNFAILED;
					goto done;
				}
#ifdef HAVE_OPENSSL
				if (usetls) {
					result = tls_conversation(sockfd, rcptip, rcptlabel, msgptr, timeout,
								  respfd, respstr, &respstrsz, &respstrlen, fullresponse);
					goto done;
				}
#endif
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
					 * is consistent with what we get from the normal Xymon daemon
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
						rdone = keep_response(outp, n, respfd, respstr, &respstrsz, &respstrlen, fullresponse);
					}
				}
				else rdone = 1;
				if (rdone) shutdown(sockfd, SHUT_RD);
			}

			if (!wdone && FD_ISSET(sockfd, &writefds)) {
				/* Send some data */
				res = write(sockfd, msgptr, strlen(msgptr));
				if (res == -1) {
					snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "Write error while sending message to Xymon daemon@%s", rcptlabel);
					result = XYMONSEND_EWRITEERROR;
					goto done;
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

done:
	dbgprintf("Closing connection\n");
	shutdown(sockfd, SHUT_RDWR);
	if (sockfd > 0) close(sockfd);
	if (addrs) freeaddrinfo(addrs);
	xfree(rcptbuf);
	if (httpmessage) xfree(httpmessage);
	return result;
}

static int sendtomany(char *onercpt, char *morercpts, char *msg, int timeout, sendreturn_t *response)
{
	int allservers = 1, first = 1, result = XYMONSEND_OK;
	char *xymondlist, *rcpt;

	/*
	 * Even though this is the "sendtomany" routine, we need to decide if the
	 * request should go to all servers, or just a single server. The default 
	 * is to send to all servers - but commands that trigger a response can
	 * only go to a single server.
	 *
	 * "schedule" is special - when scheduling an action there is no response, but 
	 * when it is the blank "schedule" command there will be a response. So a 
	 * schedule action goes to all Xymon servers, the blank "schedule" goes to a single
	 * server.
	 */

	// errprintf("sendtomany: onercpt=%s\n", onercpt);

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
		// errprintf("sendtomany: msgcmd=%s\n", msgcmd);
		for (i = 0; (multircptcmds[i] && strcmp(multircptcmds[i], msgcmd)); i++) ;
		xfree(msgcmd);

		allservers = (multircptcmds[i] != NULL);
	}

	// errprintf("sendtomany: allservers=%d\n", allservers);

	if (allservers && !morercpts) {
		snprintf(errordetails+strlen(errordetails), (sizeof(errordetails) - strlen(errordetails)), "No recipients listed! XYMSRV was %s, XYMSERVERS %s",
			  onercpt, textornull(morercpts));
		return XYMONSEND_EBADIP;
	}

	if (strcmp(onercpt, "0.0.0.0") != 0) 
		xymondlist = strdup(onercpt);
	else
		xymondlist = strdup(morercpts);

	rcpt = strtok(xymondlist, " \t");
	while (rcpt) {
		int oneres;

		if (first) {
			/* We grab the result from the first server */
			char *respstr = NULL;

			if (response) {
				oneres =  sendtoxymond(rcpt, msg,
						    response->respfd,
						    (response->respstr ? &respstr : NULL),
						    (response->respfd || response->respstr),
						    timeout);
			}
			else {
				oneres =  sendtoxymond(rcpt, msg, NULL, NULL, 0, timeout);
			}

			if (oneres == XYMONSEND_OK) {
				if (respstr && response && response->respstr) {
					addtobuffer(response->respstr, respstr);
					xfree(respstr);
				}
				first = 0;
			}
		}
		else {
			/* Secondary servers do not yield a response */
			oneres =  sendtoxymond(rcpt, msg, NULL, NULL, 0, timeout);
		}

		/* Save any error results */
		if (result == XYMONSEND_OK) result = oneres;

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

	xfree(xymondlist);

	return result;
}

sendreturn_t *newsendreturnbuf(int fullresponse, FILE *respfd)
{
	sendreturn_t *result;

	result = (sendreturn_t *)calloc(1, sizeof(sendreturn_t));
	result->fullresponse = fullresponse;
	result->respfd = respfd;
	if (!respfd) {
		/* No response file, so return it in a strbuf */
		result->respstr = newstrbuffer(0);
	}
	result->haveseenhttphdrs = 1;

	return result;
}

void freesendreturnbuf(sendreturn_t *s)
{
	if (!s) return;
	if (s->respstr) freestrbuffer(s->respstr);
	xfree(s);
}

char *getsendreturnstr(sendreturn_t *s, int takeover)
{
	char *result = NULL;

	if (!s) return NULL;
	if (!s->respstr) return NULL;
	result = STRBUF(s->respstr);
	if (takeover) {
		/*
		 * We cannot leave respstr as NULL, because later calls 
		 * to sendmessage() might re-use this sendreturn_t struct
		 * and expect to get the data back. So allocate a new
		 * responsebuffer for future use - if it isn't used, it
		 * will be freed by freesendreturnbuf().
		 */
		s->respstr = newstrbuffer(0);
	}

	return result;
}


int sendmessage_init_local(void)
{
        backfeedqueue = setup_feedback_queue(CHAN_CLIENT);
	if (backfeedqueue == -1) return -1;

	max_backfeedsz = 1024*shbufsz(C_FEEDBACK_QUEUE)-1;
	return max_backfeedsz;
}

void sendmessage_finish_local(void)
{
        close_feedback_queue(backfeedqueue, CHAN_CLIENT);
}

sendresult_t sendmessage_local(char *msg)
{
	int n, done = 0;
	#if defined(__OpenBSD__) || defined(__dietlibc__)
		unsigned long msglen;
	#else
		msglen_t msglen;
	#endif

	if (backfeedqueue == -1) {
		return sendmessage(msg, NULL, XYMON_TIMEOUT, NULL);
	}

	/* Make sure we dont overflow the message buffer */
	msglen = strlen(msg);
	if (msglen > max_backfeedsz) {
		errprintf("Truncating backfeed channel message from %d to %d\n", msglen, max_backfeedsz);
		*(msg+max_backfeedsz) = '\0';
		msglen = max_backfeedsz;
	}

	/* This will block if queue is full, but that is OK */
	do {
		n = msgsnd(backfeedqueue, msg, msglen+1, 0);
		if ((n == 0) || ((n == -1) && (errno != EINTR))) done = 1;
	} while (!done);

	if (n == -1) {
		errprintf("Sending via backfeed channel failed: %s\n", strerror(errno));
		return XYMONSEND_ECONNFAILED;
	}

	return XYMONSEND_OK;
}


sendresult_t sendmessage(char *msg, char *recipient, int timeout, sendreturn_t *response)
{
	static char *xymsrv = NULL;
	int res = 0;

	*errordetails = '\0';

 	if ((xymsrv == NULL) && xgetenv("XYMSRV")) xymsrv = strdup(xgetenv("XYMSRV"));
	if (recipient == NULL) recipient = xymsrv;
	if ((recipient == NULL) && xgetenv("XYMSERVERS")) {
		recipient = "0.0.0.0";
	} else if (recipient == NULL) {
		errprintf("No recipient for message\n");
		return XYMONSEND_EBADIP;
	}

	res = sendtomany(recipient, xgetenv("XYMSERVERS"), msg, timeout, response);

	if (res != XYMONSEND_OK) {
		char *statustext = "";
		char *eoln;

		switch (res) {
		  case XYMONSEND_OK            : statustext = "OK"; break;
		  case XYMONSEND_EBADIP        : statustext = "Bad IP address"; break;
		  case XYMONSEND_EIPUNKNOWN    : statustext = "Cannot resolve hostname"; break;
		  case XYMONSEND_ENOSOCKET     : statustext = "Cannot get a socket"; break;
		  case XYMONSEND_ECANNOTDONONBLOCK   : statustext = "Non-blocking I/O failed"; break;
		  case XYMONSEND_ECONNFAILED   : statustext = "Connection failed"; break;
		  case XYMONSEND_ESELFAILED    : statustext = "select(2) failed"; break;
		  case XYMONSEND_ETIMEOUT      : statustext = "timeout"; break;
		  case XYMONSEND_EWRITEERROR   : statustext = "write error"; break;
		  case XYMONSEND_EREADERROR    : statustext = "read error"; break;
		  case XYMONSEND_EBADURL       : statustext = "Bad URL"; break;
		  default:                statustext = "Unknown error"; break;
		};

		eoln = strchr(msg, '\n'); if (eoln) *eoln = '\0';
		if (strcmp(recipient, "0.0.0.0") == 0) recipient = xgetenv("XYMSERVERS");
		errprintf("Whoops ! Failed to send message (%s)\n", statustext);
		errprintf("->  %s\n", errordetails);
		errprintf("->  Recipient '%s', timeout %d\n", recipient, timeout);
		errprintf("->  1st line: '%s'\n", msg);
		if (eoln) *eoln = '\n';
	}

	/* Give it a break */
	if (sleepbetweenmsgs) usleep(sleepbetweenmsgs);
	xymonmsgcount++;
	return res;
}


/* Routines for handling combo message transmission */
static void combo_params(void)
{
	static int issetup = 0;

	if (issetup) return;

	issetup = 1;

	if (xgetenv("MAXMSGSPERCOMBO")) maxmsgspercombo = atoi(xgetenv("MAXMSGSPERCOMBO"));
	if (maxmsgspercombo == 0) {
		/* Force it to 100 */
		dbgprintf("MAXMSGSPERCOMBO is 0, setting it to 100\n");
		maxmsgspercombo = 100;
	}

	if (xgetenv("SLEEPBETWEENMSGS")) sleepbetweenmsgs = atoi(xgetenv("SLEEPBETWEENMSGS"));

	comboofssz = 10*maxmsgspercombo;
	comboofsstr = (char *)malloc(comboofssz+1);
	combooffsets = (int *)malloc((maxmsgspercombo+1)*sizeof(int));
}

void combo_start(void)
{
	int n;

	combo_params();

	memset(comboofsstr, ' ', comboofssz);
	memcpy(comboofsstr, "extcombo", 8);
	*(comboofsstr + comboofssz) = '\0';

	memset(combooffsets, 0, maxmsgspercombo*sizeof(int));
	combooffsets[0] = comboofssz;

	if (xymonmsg == NULL) xymonmsg = newstrbuffer(0);
	clearstrbuffer(xymonmsg);
	addtobufferraw(xymonmsg, comboofsstr, comboofssz);
	xymonmsgqueued = 0;
	combo_is_local = 0;
}

void combo_start_local(void)
{
	combo_start();
	combo_is_local = 1;
}

void meta_start(void)
{
	if (metamsg == NULL) metamsg = newstrbuffer(0);
	clearstrbuffer(metamsg);
	xymonmetaqueued = 0;
}

static void combo_flush(void)
{
	int i;
	char *outp;

	if (!xymonmsgqueued) {
		dbgprintf("Flush, but xymonmsg is empty\n");
		return;
	}

	outp = strchr(STRBUF(xymonmsg), ' ');
	for (i = 0; (i <= xymonmsgqueued); i++) {
		outp += snprintf(outp, (STRBUFSZ(xymonmsg) - (outp - STRBUF(xymonmsg))), " %d", combooffsets[i]);
	}
	*outp = '\n';
	
	if (debug) {
		char *p1, *p2;

		dbgprintf("Flushing combo message\n");
		p1 = p2 = STRBUF(xymonmsg);

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

	if (combo_is_local) {
		sendmessage_local(STRBUF(xymonmsg));
		combo_start_local();
	}
	else {
		sendmessage(STRBUF(xymonmsg), NULL, XYMON_TIMEOUT, NULL);
		combo_start();
	}
}

static void meta_flush(void)
{
	if (!xymonmetaqueued) {
		dbgprintf("Flush, but xymonmeta is empty\n");
		return;
	}

	sendmessage(STRBUF(metamsg), NULL, XYMON_TIMEOUT, NULL);
	meta_start();	/* Get ready for the next */
}

void combo_add(strbuffer_t *buf)
{
	if (combo_is_local) {
		/* Check if message fits into the backfeed message buffer */
		if ( (STRBUFLEN(xymonmsg) + STRBUFLEN(buf)) >= max_backfeedsz) {
			combo_flush();
		}
	}
	else {
		/* Check if there is room for the message + 2 newlines */
		if (maxmsgspercombo && (xymonmsgqueued >= maxmsgspercombo)) {
			combo_flush();
		}
	}

	addtostrbuffer(xymonmsg, buf);
	combooffsets[++xymonmsgqueued] = STRBUFLEN(xymonmsg);
}

static void meta_add(strbuffer_t *buf)
{
	/* Check if there is room for the message + 2 newlines */
	if (maxmsgspercombo && (xymonmetaqueued >= maxmsgspercombo)) {
		/* Nope ... flush buffer */
		meta_flush();
	}
	else {
		/* Yep ... add delimiter before new status (but not before the first!) */
		if (xymonmetaqueued) addtobuffer(metamsg, "\n\n");
	}

	addtostrbuffer(metamsg, buf);
	xymonmetaqueued++;
}

void combo_end(void)
{
	combo_flush();
	combo_is_local = 0;
	dbgprintf("%d status messages merged into %d transmissions\n", xymonstatuscount, xymonmsgcount);
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
	xymonstatuscount++;
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


