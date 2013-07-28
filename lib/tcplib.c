/*----------------------------------------------------------------------------*/
/*                                                                            */
/* This file implements a TCP-based communications library, while trying to   */
/* hide all of the messy details.                                             */
/* It supports:                                                               */
/*   - IPv4 connections                                                       */
/*   - IPv6 connections                                                       */
/*   - SSL/TLS encrypted connections                                          */
/*   - server mode (listening for connections)                                */
/*   - client mode (initiates connections)                                    */
/*   - Parallel handling of multiple connections                              */
/*                                                                            */
/* Copyright (C) 2011-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>

#include "config.h"
#include "tcplib.h"

#ifdef HAVE_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/bio.h>

/* SSL context (holds certificate, SSL protocol version etc) for server-mode operation */
/* Note: Since this is global, we are limited to one server instance per process. */
static SSL_CTX *serverctx = NULL;	
#endif

/* Listen socket list */
static tcpconn_t *lsocks = NULL;

/* Active connections list */
static tcpconn_t *conns = NULL;

enum io_action_t { IO_READ, IO_WRITE };

void (*userinfo)(time_t, const char *id, char *msg) = NULL;
enum infolevel_t userinfolevel = INFO_WARN;

char *conn_callback_names[CONN_CB_CLEANUP+1] = {
	"New connection",
	"Connect start",
	"Connect complete",
	"Connect failed",
	"SSL handshake OK",
	"SSL handshake failed",
	"Read check",
	"Write check",
	"Read",
	"Write",
	"Timeout",
	"Closed",
	"Cleanup"
};

char *conn_state_names[CONN_DEAD+1] = {
	"Plaintext",
	"SSL init",
	"SSL connecting",
	"Plaintext connecting",
	"SSL accept read",
	"SSL accept write",
	"SSL connect read",
	"SSL connect write",
	"SSL starttls read",
	"SSL starttls write",
	"SSL read",
	"SSL write",
	"SSL user ready",
	"Closing",
	"Dead",
};

void conn_register_infohandler(void (*cb)(time_t, const char *id, char *msg), enum infolevel_t level)
{
	userinfo = cb;
	userinfolevel = level;
}

void conn_info(const char *funcid, enum infolevel_t level, const char *fmt, ...)
{
	char timestr[30];
	char msg[4096];
	va_list args;
	time_t now;

	if (level > userinfolevel) return;

	now = time(NULL);
	strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", localtime(&now));

	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

	if (userinfo) {
		userinfo(now, funcid, msg);
	}
	else {
		fprintf(stderr, "%s %s(%d): %s", timestr, funcid, level, msg);
		fflush(stderr);
	}
}

static void conn_ssllibrary_init(void)
{
	static int haverun = 0;

	if (haverun) return;

	haverun = 1;

#ifdef HAVE_OPENSSL
	SSL_load_error_strings();
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	conn_info("conn_ssllibrary_init", INFO_DEBUG, "Library init done\n");
#endif
}

void conn_getntimer(struct timespec *tp)
{
#if (_POSIX_TIMERS > 0) && defined(_POSIX_MONOTONIC_CLOCK)
	if (clock_gettime(CLOCK_MONOTONIC, tp) == 0) 
		return;
	else
#endif
	{
		struct timeval t;
		struct timezone tz;

		gettimeofday(&t, &tz);
		tp->tv_sec = t.tv_sec;
		tp->tv_nsec = 1000*t.tv_usec;
	}
}

long conn_elapsedus(struct timespec *tstart, struct timespec *tnow)
{
	struct timespec tdiff;

	if (tnow) {
		tdiff.tv_sec = tnow->tv_sec;
		tdiff.tv_nsec = tnow->tv_nsec;
	}
	else
		conn_getntimer(&tdiff);

	if (tdiff.tv_nsec < tstart->tv_nsec) {
		tdiff.tv_sec--;
		tdiff.tv_nsec += 1000000000;
	}
	tdiff.tv_sec  -= tstart->tv_sec;
	tdiff.tv_nsec -= tstart->tv_nsec;

	return tdiff.tv_sec*1000000 + tdiff.tv_nsec/1000;
}


/* Convert a network address to printable form */
static char *conn_print_address_and_port(tcpconn_t *conn, int includeport)
{
	/* IPv6 address needs 46 bytes: 8 numbers (4 bytes each), 7 colons, 1 colon before portnumber, 5 digits for portnumber, terminating NUL */
	/* This is also INET6_ADDRSTRLEN */
	static char addrstring[46];

	*addrstring = '\0';
	inet_ntop(conn->family, conn->peer_sin, addrstring, sizeof(addrstring));
	if (includeport) {
		switch (conn->family) {
#ifdef IPV4_SUPPORT
		  case AF_INET:
			{
				struct sockaddr_in *sin = (struct sockaddr_in *)conn->peer;
				sprintf(addrstring + strlen(addrstring), ":%d", ntohs(sin->sin_port));
			}
			break;
#endif

#ifdef IPV6_SUPPORT
		  case AF_INET6:
			{
				struct sockaddr_in6 *sin = (struct sockaddr_in6 *)conn->peer;
				sprintf(addrstring + strlen(addrstring), ":%d", ntohs(sin->sin6_port));
			}
			break;
#endif

		  default:
			break;
		}
	}

	return addrstring;
}

char *conn_print_address(tcpconn_t *conn)
{
	return conn_print_address_and_port(conn, 1);
}

char *conn_print_ip(tcpconn_t *conn)
{
	return conn_print_address_and_port(conn, 0);
}

#ifdef HAVE_OPENSSL
static time_t convert_asn1_tstamp(ASN1_UTCTIME *tstamp)
{
	/*
	 * tstamp->data is a string is in a YYYYMMDDhhmmss format.
	 * tstamp->length is the length of the string.
	 *
	 * It may be followed by 'Z' to indicate it is in UTC time.
	 * YYYY may be just 'YY', 'ss' may be omitted.
	 */

	struct tm tm;
	int i, gmt;
	time_t result = 0;

	int tslen = tstamp->length;
	char *tstr = (char *)tstamp->data;

	if (tslen < 10) return 0;
	for (i=0; (i < tslen-1); i++) {
		if ((tstr[i] > '9') || (tstr[i] < '0')) return 0;
	}

	memset(&tm, 0, sizeof(tm));
	gmt = (tstr[tslen-1] == 'Z') ? 1 : 0;

	if (tslen >= 14) {
		/* YYYYMMDDhhmmss format */
		tm.tm_year = (tstr[0]-'0')*1000 + (tstr[1]-'0')*100 + (tstr[2]-'0')*10 + (tstr[3]-'0');
		tstr += 4; tslen -= 4;
	}
	else {
		/* YYMMDDhhmmss format */
		tm.tm_year = 1900 + (tstr[0]-'0')*10 + (tstr[1]-'0');
		if (tm.tm_year < 1970) tm.tm_year += 100;
		tstr += 2; tslen -= 2;
	}
	tm.tm_year -= 1900;

	tm.tm_mon = (tstr[0]-'0')*10 + (tstr[1]-'0');
	if ((tm.tm_mon > 12) || (tm.tm_mon < 1)) return 0;
	tstr += 2; tslen -= 2;
	tm.tm_mon -= 1;

	tm.tm_mday = (tstr[0]-'0')*10 + (tstr[1]-'0');
	tstr += 2; tslen -= 2;

	tm.tm_hour = (tstr[0]-'0')*10 + (tstr[1]-'0');
	tstr += 2; tslen -= 2;

	tm.tm_min = (tstr[0]-'0')*10 + (tstr[1]-'0');
	tstr += 2; tslen -= 2;

	if (tslen >= 2) {
		tm.tm_sec = (tstr[0]-'0')*10 + (tstr[1]-'0');
		tstr += 2; tslen -= 2;
	}
	else {
		tm.tm_sec = 0;
	}

	result = mktime(&tm);	/* Returns a local timestamp */
	if (gmt) {
		/* Calculate the difference between localtime and UTC */
		struct tm *t;
		time_t t1, t2, utcofs;

		/* Use tm_isdst=0 here, since we only want the difference between local and UTC. So no messing with DST while calculating that. */
		t = gmtime(&result); t->tm_isdst = 0; t1 = mktime(t);
		t = localtime(&result); t->tm_isdst = 0; t2 = mktime(t);
		utcofs = (t2-t1);
		result += utcofs;
	}

	return result;
}
#endif

char *conn_peer_certificate(tcpconn_t *conn, time_t *certstart, time_t *certend, char **issuer, char **fulltext)
{
	char *result = NULL;

#ifdef HAVE_OPENSSL
	X509 *peercert;
	ASN1_UTCTIME *tstamp;

	peercert = SSL_get_peer_certificate(conn->ssl);
	if (!peercert) return NULL;

	/* X509_NAME_oneline malloc's space for the result when called with a NULL buffer */
	result = X509_NAME_oneline(X509_get_subject_name(peercert), NULL, 0);
	if (issuer) {
		*issuer = X509_NAME_oneline(X509_get_issuer_name(peercert), NULL, 0);
	}
	if (certstart) {
		tstamp = X509_get_notBefore(peercert);
		*certstart = convert_asn1_tstamp(tstamp);
	}
	if (certend) {
		tstamp = X509_get_notAfter(peercert);
		*certend = convert_asn1_tstamp(tstamp);
	}

	if (fulltext) {
		BIO *o = BIO_new(BIO_s_mem());
		long slen;
		char *sdata;

		X509_print_ex(o, peercert, XN_FLAG_COMPAT, X509_FLAG_COMPAT);

		slen = BIO_get_mem_data(o, &sdata);
		*fulltext = malloc(slen+1);
		memcpy(*fulltext, sdata, slen);
		*((*fulltext)+slen) = '\0';

		BIO_set_close(o, BIO_CLOSE);
		BIO_free(o);
	}

	X509_free(peercert);
#endif

	return result;
}


static void conn_cleanup(tcpconn_t *conn)
{
#ifdef HAVE_OPENSSL
	if ((conn->sock > 0) && conn->ssl) SSL_shutdown(conn->ssl);
	if (conn->ssl) SSL_free(conn->ssl);
	if (conn->ctx) SSL_CTX_free(conn->ctx);
	conn->ssl = NULL;
	conn->ctx = NULL;
#endif

	if (conn->sock > 0) { close(conn->sock); conn->sock = -1; }
	if (conn->peer) { free(conn->peer); conn->peer = NULL; }
	conn->elapsedus = conn_elapsedus(&conn->starttime, NULL);
	conn->usercallback(conn, CONN_CB_CLOSED, conn->userdata);

	conn->connstate = CONN_DEAD;
}

/* Create a listener socket, for one IP family. */
static int listen_port(tcpconn_t *ls, int portnumber, int backlog, char *localaddr)
{
	const char *funcid = "listen_port";
	int opt;

	ls->sock = socket(ls->family, SOCK_STREAM, IPPROTO_TCP);
	if (ls->sock == -1) {
		conn_info(funcid, INFO_ERROR, "Cannot create listen socket (%s)\n", strerror(errno));
		return -1;
	}
	opt = 1;
	setsockopt(ls->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	switch (ls->family) {
#ifdef IPV4_SUPPORT
	  case AF_INET:
		  {
			struct sockaddr_in *sin4 = (struct sockaddr_in *)ls->peer;
			sin4->sin_family = ls->family;
			sin4->sin_port = htons(portnumber);
			ls->peer_sin = &sin4->sin_addr;
			if (!(localaddr && (inet_pton(ls->family, localaddr, &sin4->sin_addr) != 0))) {
				memset(ls->peer_sin, 0, sizeof(struct sockaddr_in));
			}
			break;
		  }
#endif

#ifdef IPV6_SUPPORT
	  case AF_INET6: 
		{
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ls->peer;
#ifdef HAVE_V6ONLY
			opt = 1;
			setsockopt(ls->sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif
			sin6->sin6_family = ls->family;
			sin6->sin6_port = htons(portnumber);
			ls->peer_sin = &sin6->sin6_addr;
			if (!(localaddr && (inet_pton(AF_INET6, localaddr, &sin6->sin6_addr) != 0))) {
				memset(ls->peer_sin, 0, sizeof(struct sockaddr_in6));
			}
			break;
		}
#endif

	  default:
		return -1;
	}

	fcntl(ls->sock, F_SETFL, O_NONBLOCK);

	if (bind(ls->sock, ls->peer, ls->peersz) == -1) {
		conn_info(funcid, INFO_ERROR, "Cannot bind to %s (%s)\n", conn_print_address(ls), strerror(errno));
		return -1;
	}
	if (listen(ls->sock, backlog) == -1) {
		conn_info(funcid, INFO_ERROR, "Cannot listen on %s (%s)\n", conn_print_address(ls), strerror(errno));
		return -1;
	}

	return ls->sock;
}

/*
 * Setup listener. 
 *
 * This sets up a listener on both IPv4 and IPv6, using the same port number.
 * The listener socket(s) are put in the "lsocks" list and the conn_process_listeners()
 * routine will scan them to see if there are new inbound connections.
 */
int conn_listen(int portnumber, int backlog, int maxlifetime,
		enum sslhandling_t  sslhandling, char *local4, char *local6, 
		enum conn_cbresult_t (*usercallback)(tcpconn_t *, enum conn_callback_t, void *))
{
	const char *funcid = "conn_listen";
	tcpconn_t *ls;

#ifdef IPV4_SUPPORT
	ls = (tcpconn_t *)calloc(1, sizeof(tcpconn_t));
	ls->connstate = ((sslhandling == CONN_SSL_YES) ? CONN_SSL_INIT : CONN_PLAINTEXT);
#ifdef HAVE_OPENSSL
	ls->sslhandling = sslhandling;
#endif
	ls->usercallback = usercallback;
	ls->maxlifetime = maxlifetime;
	ls->family = AF_INET;
	ls->peersz = sizeof(struct sockaddr_in);
	ls->peer = (struct sockaddr *)calloc(1, ls->peersz);
	if (listen_port(ls, portnumber, backlog, local4) == -1) {
		conn_cleanup(ls);
		ls = NULL;
	}
	else {
		conn_info(funcid, INFO_INFO, "Listening on IPv4 %s\n", conn_print_address(ls));
		ls->next = lsocks;
		lsocks = ls;
	}
#endif

#ifdef IPV6_SUPPORT
	ls = (tcpconn_t *)calloc(1, sizeof(tcpconn_t));
	ls->connstate = ((sslhandling == CONN_SSL_YES) ? CONN_SSL_INIT : CONN_PLAINTEXT);
#ifdef HAVE_OPENSSL
	ls->sslhandling = sslhandling;
#endif
	ls->usercallback = usercallback;
	ls->maxlifetime = maxlifetime;
	ls->family = AF_INET6;
	ls->peersz = sizeof(struct sockaddr) + sizeof(struct sockaddr_in6);
	ls->peer = (struct sockaddr *)calloc(1, ls->peersz);
	if (listen_port(ls, portnumber, backlog, local6) == -1) {
		conn_cleanup(ls);
		ls = NULL;
	}
	else {
		conn_info(funcid, INFO_INFO, "Listening on IPv6 %s\n", conn_print_address(ls));
		ls->next = lsocks;
		lsocks = ls;
	}
#endif

	return (lsocks != NULL);
}


static void try_ssl_accept(tcpconn_t *conn)
{
#ifdef HAVE_OPENSSL
	const char *funcid = "try_ssl_accept";
	int sslresult;

	conn->connstate = CONN_SSL_INIT;

	sslresult = SSL_accept(conn->ssl);
	if (sslresult == 1) {
		conn->usercallback(conn, CONN_CB_SSLHANDSHAKE_OK, conn->userdata);
		conn->connstate = CONN_SSL_READY;
		conn_info(funcid, INFO_INFO, "SSL handshake completed with %s\n", conn_print_address(conn));
	}
	else if (sslresult == 0) {
		/* SSL accept failed */
		conn->usercallback(conn, CONN_CB_SSLHANDSHAKE_FAILED, conn->userdata);
		SSL_get_error(conn->ssl, sslresult);
		conn_info(funcid, INFO_WARN, "SSL accept failed from %s\n", conn_print_address(conn));
		conn->connstate = CONN_CLOSING;
	}
	else if (sslresult == -1) {
		switch (SSL_get_error(conn->ssl, sslresult)) {
		  case SSL_ERROR_WANT_READ: conn->connstate = CONN_SSL_ACCEPT_READ; break;
		  case SSL_ERROR_WANT_WRITE: conn->connstate = CONN_SSL_ACCEPT_WRITE; break;
		  default:
			conn->usercallback(conn, CONN_CB_SSLHANDSHAKE_FAILED, conn->userdata);
			conn_info(funcid, INFO_WARN, "SSL error during handshake with %s\n", conn_print_address(conn));
			conn->connstate = CONN_CLOSING;
			break;
		}
	}
#endif
}

static void try_ssl_connect(tcpconn_t *conn)
{
#ifdef HAVE_OPENSSL
	const char *funcid = "try_ssl_connect";
	int sslresult;

	conn->connstate = CONN_SSL_INIT;

	sslresult = SSL_connect(conn->ssl);
	if (sslresult == 1) {
		conn->usercallback(conn, CONN_CB_SSLHANDSHAKE_OK, conn->userdata);
		conn->connstate = CONN_SSL_READY;
		conn_info(funcid, INFO_INFO, "SSL connection established with %s\n", conn_print_address(conn));
	}
	else if (sslresult == 0) {
		/* SSL connect failed */
		conn->usercallback(conn, CONN_CB_SSLHANDSHAKE_FAILED, conn->userdata);
		SSL_get_error(conn->ssl, sslresult);
		conn_info(funcid, INFO_ERROR, "SSL connection failed to %s\n", conn_print_address(conn));
		conn->connstate = CONN_CLOSING;
	}
	else if (sslresult == -1) {
		switch (SSL_get_error(conn->ssl, sslresult)) {
		  case SSL_ERROR_WANT_READ: conn->connstate = CONN_SSL_CONNECT_READ; break;
		  case SSL_ERROR_WANT_WRITE: conn->connstate = CONN_SSL_CONNECT_WRITE; break;
		  default: 
			{
				char sslerrmsg[256];
				ERR_error_string(ERR_get_error(), sslerrmsg);
				conn_info(funcid, INFO_ERROR, "SSL error during connection setup with %s: %s\n", 
					  conn_print_address(conn), sslerrmsg);
			}
			conn->usercallback(conn, CONN_CB_SSLHANDSHAKE_FAILED, conn->userdata);
			conn->connstate = CONN_CLOSING;
			break;
		}
	}
#endif
}

static void try_ssl_starttls(tcpconn_t *conn)
{
#ifdef HAVE_OPENSSL
	const char *funcid = "try_ssl_starttls";
	int sslresult;
	char sslerrmsg[256];

	conn->connstate = CONN_SSL_INIT;

	sslresult = SSL_do_handshake(conn->ssl);

	if (sslresult == 1) {
		conn->usercallback(conn, CONN_CB_SSLHANDSHAKE_OK, conn->userdata);
		conn->connstate = CONN_SSL_READY;
		conn_info(funcid, INFO_INFO, "SSL connection established with %s\n", conn_print_address(conn));
	}
	else if (sslresult == 0) {
		/* SSL handshake failed */
		conn->usercallback(conn, CONN_CB_SSLHANDSHAKE_FAILED, conn->userdata);
		SSL_get_error(conn->ssl, sslresult);
		ERR_error_string(ERR_get_error(), sslerrmsg);
		conn_info(funcid, INFO_ERROR, "SSL connection failed to %s: %s\n", conn_print_address(conn), sslerrmsg);
		conn->connstate = CONN_CLOSING;
	}
	else if (sslresult == -1) {
		switch (SSL_get_error(conn->ssl, sslresult)) {
		  case SSL_ERROR_WANT_READ: conn->connstate = CONN_SSL_STARTTLS_READ; break;
		  case SSL_ERROR_WANT_WRITE: conn->connstate = CONN_SSL_STARTTLS_WRITE; break;
		  default: 
			{
				char sslerrmsg[256];
				ERR_error_string(ERR_get_error(), sslerrmsg);
				conn_info(funcid, INFO_ERROR, "SSL error during starttls with %s: %s\n", 
					  conn_print_address(conn), sslerrmsg);
			}
			conn->usercallback(conn, CONN_CB_SSLHANDSHAKE_FAILED, conn->userdata);
			conn->connstate = CONN_CLOSING;
			break;
		}
	}
#endif
}

int conn_starttls(tcpconn_t *conn)
{
	const char *funcid = "conn_starttls";

#ifdef HAVE_OPENSSL
	conn_info(funcid, INFO_DEBUG, "Initiating STARTTLS in %s mode\n",
		  (conn->sslhandling == CONN_SSL_STARTTLS_SERVER) ? "server" : "client");

	/* The SSL ctx and ssl settings have been setup when the socket was created */
	if (conn->sslhandling == CONN_SSL_STARTTLS_SERVER) {
		if (serverctx) {
			conn->ctx = NULL;	/* NULL, because we dont want it freed in case of an error */
			conn->ssl = SSL_new(serverctx);
		}
		else {
			conn_info(funcid, INFO_ERROR, 
				  "starttls failed, SSL certificate not prepared\n");
			return 1;
		}
	}

	if (SSL_set_fd(conn->ssl, conn->sock) != 1) {
		char sslerrmsg[256];
		ERR_error_string(ERR_get_error(), sslerrmsg);
		conn_info(funcid, INFO_ERROR, "starttls failed for %s: %s\n", conn_print_address(conn), sslerrmsg);
		return 1;
	}

	switch (conn->sslhandling) {
	  case CONN_SSL_STARTTLS_SERVER:
		SSL_set_accept_state(conn->ssl);
		break;
	  case CONN_SSL_STARTTLS_CLIENT:
		SSL_set_connect_state(conn->ssl);
		break;
	  default:
		conn_info(funcid, INFO_ERROR, "starttls failed, not requested when socket was created\n");
		return 1;
	}

	try_ssl_starttls(conn);
	return 0;
#else
	return 1;
#endif
}

/* 
 * Accept an incoming connection.
 *
 * When this succeeds, the useralloc() callback is
 * invoked to let the application allocate any
 * application-specific data for the connection.
 */
tcpconn_t *conn_accept(tcpconn_t *ls)
{
	const char *funcid = "conn_accept";
	tcpconn_t *newconn;
	socklen_t sin_len = ls->peersz;

	newconn = (tcpconn_t *)calloc(1, sizeof(tcpconn_t));
	newconn->connstate = ls->connstate;
#ifdef HAVE_OPENSSL
	newconn->sslhandling = ls->sslhandling;
#endif
	newconn->usercallback = ls->usercallback;
	newconn->maxlifetime = ls->maxlifetime;
	newconn->family = ls->family;
	newconn->peer = (struct sockaddr *)malloc(sin_len);
	newconn->sock = accept(ls->sock, newconn->peer, &sin_len);
	if (newconn->sock == -1) {
		conn_cleanup(newconn);
		if ((errno != EAGAIN) && (errno != EINTR)) conn_info(funcid, INFO_WARN, "accept failed (%d: %s)\n", errno, strerror(errno));
		return NULL;
	}

	/* Make the new socket non-blocking */
	fcntl(newconn->sock, F_SETFL, O_NONBLOCK);

	switch (newconn->family) {
#ifdef IPV4_SUPPORT
	  case AF_INET:
		newconn->peer_sin = &((struct sockaddr_in *)newconn->peer)->sin_addr;
		break;
#endif

#ifdef IPV6_SUPPORT
	  case AF_INET6:
		newconn->peer_sin = &((struct sockaddr_in6 *)newconn->peer)->sin6_addr;
		break;
#endif
	  default:
		break;
	}

	if (ls->usercallback(newconn, CONN_CB_NEWCONNECTION, NULL) != CONN_CBRESULT_OK) {
		/* User rejects connection */
		newconn->connstate = CONN_CLOSING;
	}

#ifdef HAVE_OPENSSL
	if (newconn->connstate == CONN_SSL_INIT) {
		/* 
		 * We have a connection, but the SSL handshake has not happened yet. 
		 * Add the connection to the SSL connection pool, and start the
		 * handshake by calling try_ssl_accept()
		 */
		newconn->ctx = NULL;	/* NULL, because we dont want it freed in case of an error */
		newconn->ssl = SSL_new(serverctx);
		SSL_set_fd(newconn->ssl, newconn->sock);
		try_ssl_accept(newconn);
	}
#endif

	if (newconn->connstate == CONN_CLOSING) {
		conn_cleanup(newconn);
		newconn = NULL;
	}

	if (newconn) {
		conn_getntimer(&newconn->starttime);
		newconn->next = conns;
		conns = newconn;
		conn_info(funcid, INFO_INFO, "Incoming connection from %s\n", conn_print_address(newconn));
	}

	return newconn;
}

/*
 * This routine handles SSL (re)negotiation, i.e. when an SSL I/O operation
 * returns SSL_ERROR_WANT_READ/WRITE.
 */
static int try_ssl_io(tcpconn_t *conn, enum io_action_t action, void *buf, size_t sz)
{
	const char *funcid = "try_ssl_io";
	int n = 0;

#ifdef HAVE_OPENSSL
	switch (conn->connstate) {
	  case CONN_SSL_ACCEPT_READ:
	  case CONN_SSL_ACCEPT_WRITE:
		/* Re-try an SSL_accept() after having done SSL handshake */
		try_ssl_accept(conn);
		n = 0;
		break;

	  case CONN_SSL_CONNECT_READ:
	  case CONN_SSL_CONNECT_WRITE:
		/* Re-try an SSL_connect() after having done SSL handshake */
		try_ssl_connect(conn);
		n = 0;
		break;

	  case CONN_SSL_STARTTLS_READ:
	  case CONN_SSL_STARTTLS_WRITE:
		/* Re-try an SSL_handshake() after having done SSL handshake */
		try_ssl_starttls(conn);
		n = 0;
		break;

	  case CONN_SSL_READ:
	  case CONN_SSL_WRITE:
	  case CONN_SSL_READY:
		/* Try the real I/O. If we're told to re-do the handshake, then change state accordingly. */
		if (action == IO_READ)
			n = SSL_read(conn->ssl, buf, sz);
		else
			n = SSL_write(conn->ssl, buf, sz);

		if (n == 0) {
			/* Peer closed connection */
			conn_info(funcid, INFO_INFO, "Connection closed by peer: %s\n", conn_print_address(conn));
			conn->connstate = CONN_CLOSING;
		}
		else if (n < 0) {
			/* SSL error. Catch the re-negotiate request; if another error close the connection */
			switch (SSL_get_error(conn->ssl, n)) {
			  case SSL_ERROR_WANT_READ: conn->connstate = CONN_SSL_READ; break;
			  case SSL_ERROR_WANT_WRITE: conn->connstate = CONN_SSL_WRITE; break;
			  default: 
				{
					char sslerrmsg[256];
					ERR_error_string(ERR_get_error(), sslerrmsg);
					conn_info(funcid, INFO_WARN, "SSL error while talking to %s: %s\n",
						  conn_print_address(conn), sslerrmsg);
				}
				conn->connstate = CONN_CLOSING;
				break;
			}
			n = 0;
		}
		break;

	  default:
		break;
	}

	if (conn->connstate == CONN_CLOSING) {
		conn_cleanup(conn);
		n = -1;
	}
#endif

	return n;
}


/*
 * Read data from the socket.
 */
int conn_read(tcpconn_t *conn, void *buf, size_t sz)
{
	const char *funcid = "conn_read";
	int n;

	switch (conn->connstate) {
	  case CONN_PLAINTEXT:
		n = read(conn->sock, buf, sz);
		if ((n == -1) && ((errno == EAGAIN) || (errno == EINTR))) {
			n = 0;
		}
		else if (n < 0) {
			conn_info(funcid, INFO_DEBUG, "read() returned no data: %s\n", strerror(errno));
			conn->connstate = CONN_CLOSING;
		}
		break;

	  default:
		n = try_ssl_io(conn, IO_READ, buf, sz);
		break;
	}

	if (conn->connstate == CONN_CLOSING) {
		conn_info(funcid, INFO_INFO, "Closing connection with %s\n", conn_print_address(conn));
		conn_cleanup(conn);
		n = -1;
	}

	return n;
}

/*
 * Write data to the socket.
 */
int conn_write(tcpconn_t *conn, void *buf, size_t count)
{
	const char *funcid = "conn_write";
	int n;

	if (count <= 0) return 0;

	switch (conn->connstate) {
	  case CONN_PLAINTEXT:
		n = write(conn->sock, buf, count);
		if ((n == -1) && ((errno == EAGAIN) || (errno == EINTR))) {
			n = 0;
			break; /* Do nothing */
		}
		else if (n < 0) {
			conn_info(funcid, INFO_DEBUG, "write failed: %s\n", strerror(errno));
			conn->connstate = CONN_CLOSING;
		}
		break;

	  default:
		n = try_ssl_io(conn, IO_WRITE, buf, count);
		break;
	}

	if (conn->connstate == CONN_CLOSING) {
		conn_info(funcid, INFO_INFO, "Closing connection with %s\n", conn_print_address(conn));
		conn_cleanup(conn);
		n = -1;
	}

	return n;
}

/*
 * Remove dead connections from the connection list.
 */
int conn_trimactive(void)
{
	tcpconn_t *newhead = NULL, *current;
	int result = 0;

	while (conns) {
		current = conns;
		conns = conns->next;

		if (current->connstate != CONN_DEAD) {
			current->next = newhead;
			newhead = current;
			result++;
		}
		else {
			current->usercallback(current, CONN_CB_CLEANUP, current->userdata);
			free(current);
		}
	}

	conns = newhead;
	return result;
}


void clear_fdsets(fd_set *fdr, fd_set *fdw, int *maxfd)
{
	FD_ZERO(fdr);
	FD_ZERO(fdw);
	*maxfd = -1;
}

void add_fd(int sock, fd_set *fds, int *maxfd)
{
	FD_SET(sock, fds);
	if (sock > *maxfd) *maxfd = sock;
}


/*
 * Setup the FD sets for select(). Simple enough when reading/writing data,
 * but the other states have special needs.
 *
 * readcheck() and writecheck() are callback-routines where application signals
 * that it wants to read/write data.
 */
int conn_fdset(fd_set *fdread, fd_set *fdwrite)
{
	const char *funcid = "conn_fdset";

	int maxfd, wantread, wantwrite;
	tcpconn_t *walk;

	clear_fdsets(fdread, fdwrite, &maxfd);
	for (walk = lsocks; (walk); walk = walk->next) {
		/* Listener sockets wait for READ events = connection arrivals */
		add_fd(walk->sock, fdread, &maxfd);
	}

	for (walk = conns; (walk); walk = walk->next) {
		switch (walk->connstate) {
		  case CONN_CLOSING:
		  case CONN_DEAD:
			break;

		  case CONN_PLAINTEXT:
		  case CONN_SSL_READY:
			wantread = (walk->usercallback(walk, CONN_CB_READCHECK, walk->userdata) == CONN_CBRESULT_OK); if (wantread) add_fd(walk->sock, fdread, &maxfd);
			wantwrite = (walk->usercallback(walk, CONN_CB_WRITECHECK, walk->userdata) == CONN_CBRESULT_OK); if (wantwrite) add_fd(walk->sock, fdwrite, &maxfd);
			if (!wantread && !wantwrite) {
				/* Must be done with this socket */
				walk->connstate = CONN_CLOSING;
				conn_cleanup(walk);
			}
			break;

		  case CONN_SSL_INIT:
			/*
			 * Starting an SSL handshake, we want to read or write data.
			 * 
			 * NOTE: This really should not happen, since all SSL I/O
			 * operations explicitly call try_ssl_X(), which invokes the
			 * SSL I/O operation and then changes state to CONN_SSL_X_READ/WRITE
			 */
			add_fd(walk->sock, fdread, &maxfd);
			add_fd(walk->sock, fdwrite, &maxfd);
			break;

		  case CONN_SSL_ACCEPT_READ:
		  case CONN_SSL_CONNECT_READ:
		  case CONN_SSL_STARTTLS_READ:
		  case CONN_SSL_READ:
			/* We're doing SSL handshake and the library needs to read data */
			add_fd(walk->sock, fdread, &maxfd);
			break;

		  case CONN_SSL_ACCEPT_WRITE:
		  case CONN_SSL_CONNECT_WRITE:
		  case CONN_SSL_STARTTLS_WRITE:
		  case CONN_SSL_WRITE:
			/* We're doing SSL handshake and the library needs to write data */
		  case CONN_SSL_CONNECTING:
		  case CONN_PLAINTEXT_CONNECTING:
			/* We're waiting for an outbound connection to complete = ready for writing */
			add_fd(walk->sock, fdwrite, &maxfd);
			break;
		}
	}

	return maxfd;
}


/*
 * Do a cycle of all the active connections after select() has found out
 * which connections are doing something.
 *
 * The userread() callback gets invoked when there is data to read from the socket.
 * It is passed the connection-specific data-pointer, and the callback-routine must
 * then read the data, usually via conn_read().
 *
 * The userwrite() callback gets invoked when it is possible to write data to the
 * socket. As for reading, it is passed the connection-specific data pointer, and
 * the callback-routine must then write the data, usually via conn_write().
 *
 * The only funny thing about this is that an outbound connection that is established
 * is handled here; since we do async I/O, the connect() call is also asynchronous and
 * a new connection shows up here as being ready for writing.
 */
void conn_process_active(fd_set *fdread, fd_set *fdwrite)
{
	const char *funcid = "conn_process_active";
	tcpconn_t *walk;
	int connres;
	socklen_t connressize;
	struct timespec tnow;
	
	conn_info(funcid, INFO_DEBUG, "Processing all active connections\n");

	conn_getntimer(&tnow);

	for (walk = conns; (walk); walk = walk->next) {
		enum conn_cbresult_t cbres = CONN_CBRESULT_OK;

		if (FD_ISSET(walk->sock, fdread)) {
			cbres = walk->usercallback(walk, CONN_CB_READ, walk->userdata);
			if (walk->connstate == CONN_DEAD) continue;

			if (cbres == CONN_CBRESULT_STARTTLS)
				conn_starttls(walk);
		}

		if (FD_ISSET(walk->sock, fdwrite)) {
			switch (walk->connstate) {
			  case CONN_PLAINTEXT_CONNECTING:
			  case CONN_SSL_CONNECTING:
				/* We have the connect() result now */
				connressize = sizeof(connres);
				getsockopt(walk->sock, SOL_SOCKET, SO_ERROR, &connres, &connressize);
				if (connres != 0) {
					walk->errcode = connres;
					conn_info(funcid, INFO_DEBUG, "connect() to %s failed: status %d\n", 
						  conn_print_address(walk), connres);
					walk->usercallback(walk, CONN_CB_CONNECT_FAILED, walk->userdata);
					conn_cleanup(walk);
				}
				else {
					walk->usercallback(walk, CONN_CB_CONNECT_COMPLETE, walk->userdata);
					if (walk->connstate == CONN_PLAINTEXT_CONNECTING) {
						walk->connstate = CONN_PLAINTEXT;
					}
					else {
						/* Connected, but havent done SSL handshake yet */
						try_ssl_connect(walk);
					}

					if ((walk->connstate == CONN_PLAINTEXT) || (walk->connstate == CONN_SSL_READY)) {
						if (walk->usercallback(walk, CONN_CB_WRITECHECK, walk->userdata) == CONN_CBRESULT_OK)
							cbres = walk->usercallback(walk, CONN_CB_WRITE, walk->userdata);
					}
				}
				break;

			  default:
				cbres = walk->usercallback(walk, CONN_CB_WRITE, walk->userdata);
				break;
			}

			if (walk->connstate == CONN_DEAD) continue;

			if (cbres == CONN_CBRESULT_STARTTLS)
				conn_starttls(walk);
		}

		if (walk->maxlifetime && (conn_elapsedus(&walk->starttime, &tnow) > walk->maxlifetime)) {
			walk->usercallback(walk, CONN_CB_TIMEOUT, walk->userdata);
		}
		continue;
	}
}

/*
 * Handle listener sockets - i.e. pick up all inbound connections.
 */
void conn_process_listeners(fd_set *fdread)
{
	const char *funcid = "conn_process_listeners";
	tcpconn_t *walk;

	conn_info(funcid, INFO_DEBUG, "Processing all listen-sockets\n");
	for (walk = lsocks; (walk); walk = walk->next) {
		if (FD_ISSET(walk->sock, fdread)) conn_accept(walk);
	}
}


#ifdef HAVE_OPENSSL
/* 
 * SSL helper routine to get the password for a certificate. 
 * Tries to read the password from a file called the same as the
 * certificate, except with ".pass" instead of ".cert"
 */
static int cert_password_cb(char *buf, int size, int rwflag, void *userdata)
{
	const char *funcid = "cert_password_cb";
	char *keyfn = (char *)userdata;
	char *passfn, *p;
	FILE *passfd;
	char passphrase[1024];

	*buf = '\0';

	passfn = (char *)malloc(strlen(keyfn) + 6);
	strcpy(passfn, keyfn);
	p = strrchr(passfn, '.');
	if (p && (p < strrchr(passfn, '/'))) p = NULL;  /* Dont add .pass in the middle of a file path */
	if (p) strcpy(p, ".pass"); else strcat(passfn, ".pass");

	passfd = fopen(passfn, "r");
	if (passfd) {
		fgets(passphrase, sizeof(passphrase)-1, passfd);
		p = strchr(passphrase, '\n'); if (p) *p = '\0';
		fclose(passfd);
	}
	else {
		conn_info(funcid, INFO_WARN, "Cannot open certificate password file %s: %s\n", passfn, strerror(errno));
		*passphrase = '\0';
	}
	free(passfn);

	strncpy(buf, passphrase, size);
	buf[size - 1] = '\0';

	/* Clear this buffer for security! Dont want passphrases in core dumps... */
	memset(passphrase, 0, sizeof(passphrase));

	return strlen(buf);
}

/*
 * SSL helper routine - loads an SSL certificate in to the SSL context.
 */
static int try_ssl_certload(SSL_CTX *ctx, char *certfn, char *keyfn)
{
	const char *funcid = "try_ssl_certload";
	int status;

	if (!certfn) return -1;

	SSL_CTX_set_default_passwd_cb(ctx, cert_password_cb);
	SSL_CTX_set_default_passwd_cb_userdata(ctx, (keyfn ? keyfn : certfn));

	status = SSL_CTX_use_certificate_file(ctx, certfn , SSL_FILETYPE_PEM);
	if (status == 1) status = SSL_CTX_use_PrivateKey_file(ctx, (keyfn ? keyfn : certfn), SSL_FILETYPE_PEM);

	if (status != 1) {
		char sslerrmsg[256];
		ERR_error_string(ERR_get_error(), sslerrmsg);
		conn_info(funcid, INFO_ERROR, "Cannot load SSL server certificate/key %s/%s: %s\n", 
			  certfn, (keyfn ? keyfn : "builtin"), sslerrmsg);
		return -1;
	}
	return 0;
}
#endif


/*
 * Setup a server instance, listening on a specific port.
 *
 * If a server certificate, keyfile, and an SSL port number are provided,
 * setup an SSL-enabled listener also.
 *
 * It will attempt to listen on all supported IP families.
 *
 * local4 and local6 can be used to bind to a specific local
 * adresses in the IPv4 and IPv6 adress family.
 */
void conn_init_server(int portnumber, int backlog, int maxlifetime,
		      char *certfn, char *keyfn, int sslportnumber, char *rootcafn, int requireclientcert,
		      char *local4, char *local6,
		      enum conn_cbresult_t (*usercallback)(tcpconn_t *, enum conn_callback_t, void *))
{
	static char *funcid = "conn_init_server";
	int sslavailable = 0;

	signal(SIGPIPE, SIG_IGN);	/* socket I/O needs to ignore SIGPIPE */

#ifdef HAVE_OPENSSL
	conn_ssllibrary_init();

	serverctx = SSL_CTX_new(SSLv23_server_method());
	SSL_CTX_set_options(serverctx, (SSL_OP_NO_SSLv2 | SSL_OP_ALL));
	SSL_CTX_set_quiet_shutdown(serverctx, 1);

	if (certfn) {
		sslavailable = (try_ssl_certload(serverctx, certfn, keyfn) == 0);
		if (!sslavailable) {
			conn_info(funcid, INFO_INFO, "No server certificate - disabling SSL connections\n");
		}
	}

	if (sslavailable && rootcafn) {
		int mode = SSL_VERIFY_PEER|SSL_VERIFY_CLIENT_ONCE;

		conn_info(funcid, INFO_INFO, "Enabled client certificate verification\n");

		if (requireclientcert) mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

		if (SSL_CTX_load_verify_locations(serverctx, rootcafn, NULL) != 1)
			conn_info(funcid, INFO_WARN, "Cannot open rootca file %s\n", rootcafn);
		else {
			SSL_CTX_set_client_CA_list(serverctx, SSL_load_client_CA_file(rootcafn));
			SSL_CTX_set_verify(serverctx, mode, NULL);
		}
	}
#endif

	if (portnumber) conn_listen(portnumber, backlog, maxlifetime, (sslavailable ? CONN_SSL_STARTTLS_SERVER : CONN_SSL_NO), local4, local6, usercallback);
	if (sslavailable && sslportnumber) conn_listen(sslportnumber, backlog, maxlifetime, CONN_SSL_YES, local4, local6, usercallback);
}


/*
 * Initialise for a client. This does almost nothing except load the SSL library.
 */
void conn_init_client(void)
{
	signal(SIGPIPE, SIG_IGN);

	conn_ssllibrary_init();
}


/*
 * Setup an outbound (client) connection.
 *
 * This routine handles connecting to IP:port, optionally with SSL encryption (perhaps using a client certificate).
 * IP can be either IPv4 or IPv6, but not must be numeric.
 *
 * If all goes well, the useralloc() callback is invoked for the application to provide the connection-
 * specific application-data.
 *
 * The connection is added to the list of active connections, and will then be handled when running
 * conn_process_active().
 */
tcpconn_t *conn_prepare_connection(char *ip, int portnumber, enum conn_socktype_t socktype, 
				   char *localaddr, enum sslhandling_t sslhandling, char *certfn, char *keyfn, long maxlifetime,
				   enum conn_cbresult_t (*usercallback)(tcpconn_t *, enum conn_callback_t, void *), void *userdata)
{
	const char *funcid = "conn_prepare_connection";
	tcpconn_t *newconn = NULL;
	int have_addr = 0, n;
	struct sockaddr *local = NULL;
	socklen_t locallen = 0;

	newconn = (tcpconn_t *)calloc(1, sizeof(tcpconn_t));
	newconn->usercallback = usercallback;
	newconn->userdata = userdata;

#ifdef IPV4_SUPPORT
	if (!have_addr) {
		struct in_addr addr;

		if (inet_pton(AF_INET, ip, &addr) == 1) {
			struct sockaddr_in *sin4 = (struct sockaddr_in *)calloc(1, sizeof(struct sockaddr_in));

			sin4->sin_family = AF_INET;
			sin4->sin_port = htons(portnumber);
			memcpy(&sin4->sin_addr, &addr, sizeof(struct in_addr));

			newconn->family = AF_INET;
			newconn->peersz = sizeof(struct sockaddr_in);
			newconn->peer = (struct sockaddr *)sin4;
			newconn->peer_sin = &sin4->sin_addr;
			conn_info(funcid, INFO_DEBUG, "Will connect to IPv4 %s\n", conn_print_address(newconn));

			if (localaddr) {
				if (inet_pton(AF_INET, localaddr, &addr) == 1) {
					sin4 = (struct sockaddr_in *)calloc(1, sizeof(struct sockaddr_in));
					sin4->sin_family = AF_INET;
					sin4->sin_port = 0;
					memcpy(&sin4->sin_addr, &addr, sizeof(struct in_addr));
					local = (struct sockaddr *)sin4;
					locallen = sizeof(struct sockaddr_in);
				}
				else {
					conn_info(funcid, INFO_WARN, "Invalid local IPv4 address %s\n", localaddr);
				}
			}

			have_addr = 1;
		}
	}
#endif

#ifdef IPV6_SUPPORT
	if (!have_addr) {
		char portstr[10];
		struct addrinfo hints, *addr;

		sprintf(portstr, "%d", portnumber);
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET6;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_NUMERICHOST;
#ifdef HAVE_AI_NUMERICSERV
		hints.ai_flags |= AI_NUMERICSERV;
#endif
		hints.ai_protocol = IPPROTO_TCP;
		have_addr = (getaddrinfo(ip, portstr, &hints, &addr) == 0);

		if (have_addr) {
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)calloc(1, addr->ai_addrlen);
			memcpy(sin6, addr->ai_addr, addr->ai_addrlen);

			newconn->family = AF_INET6;
			newconn->peersz = addr->ai_addrlen;
			newconn->peer = (struct sockaddr *)sin6;
			newconn->peer_sin = &sin6->sin6_addr;
			conn_info(funcid, INFO_DEBUG, "Will connect to IPv6 %s\n", conn_print_address(newconn));

			have_addr = 1;
			freeaddrinfo(addr);

			if (localaddr) {
				if (inet_pton(AF_INET6, localaddr, &addr) == 1) {
					sin6 = (struct sockaddr_in6 *)calloc(1, sizeof(struct sockaddr_in6));
					sin6->sin6_family = AF_INET;
					sin6->sin6_port = 0;
					memcpy(&sin6->sin6_addr, &addr, sizeof(struct in6_addr));
					local = (struct sockaddr *)sin6;
					locallen = sizeof(struct sockaddr_in6);
				}
				else {
					conn_info(funcid, INFO_WARN, "Invalid local IPv6 address %s\n", localaddr);
				}
			}

		}
	}
#endif

	if (!have_addr) {
		conn_info(funcid, INFO_ERROR, "Invalid destination address %s\n", ip);
		conn_cleanup(newconn);
		free(newconn);
		return NULL;
	}

	switch (socktype) {
	  case CONN_SOCKTYPE_STREAM: newconn->sock = socket(newconn->family, SOCK_STREAM, 0); break;
	  case CONN_SOCKTYPE_DGRAM: newconn->sock = socket(newconn->family, SOCK_DGRAM, 0); break;
	}

	if (newconn->sock == -1) {
		/* Couldn't get a socket */
		conn_info(funcid, INFO_ERROR, "No socket available (%s)\n", strerror(errno));
		conn_cleanup(newconn);
		free(newconn);
		return NULL;
	}

	if (local) {
		n = bind(newconn->sock, local, locallen);
		if (n != 0) {
			conn_info(funcid, INFO_WARN, "Cannot bind connection to local address %s: %s\n", localaddr, strerror(errno));
		}
	}

	fcntl(newconn->sock, F_SETFL, O_NONBLOCK);

#ifdef HAVE_OPENSSL
	if (sslhandling != CONN_SSL_NO) {
		newconn->sslhandling = sslhandling;
		newconn->ctx = SSL_CTX_new(SSLv23_client_method());
		if (!newconn->ctx) {
			char sslerrmsg[256];

			ERR_error_string(ERR_get_error(), sslerrmsg);
			conn_info(funcid, INFO_ERROR, "SSL_CTX_new failed: %s\n", sslerrmsg);
			conn_cleanup(newconn);
			free(newconn);
			return NULL;
		}

		SSL_CTX_set_options(newconn->ctx, (SSL_OP_NO_SSLv2 | SSL_OP_ALL));
		SSL_CTX_set_quiet_shutdown(newconn->ctx, 1);
		if (certfn) {
			if (try_ssl_certload(newconn->ctx, certfn, keyfn) != 0) {
				conn_info(funcid, INFO_ERROR, "Client certificate %s (key %s) not available\n", certfn, (keyfn ? keyfn : "included in certfile"));
				conn_cleanup(newconn);
				free(newconn);
				return NULL;
			}
		}

		newconn->ssl = SSL_new(newconn->ctx);
		if (!newconn->ssl) {
			char sslerrmsg[256];

			ERR_error_string(ERR_get_error(), sslerrmsg);
			conn_info(funcid, INFO_ERROR, "SSL_new failed: %s\n", sslerrmsg);
			conn_cleanup(newconn);
			free(newconn);
			return NULL;
		}

		if (certfn) {
			/* Verify that the certificate is working */
			X509 *x509 = SSL_get_certificate(newconn->ssl);
			if(x509 != NULL) {
				EVP_PKEY *pktmp = X509_get_pubkey(x509);
				EVP_PKEY_copy_parameters(pktmp, SSL_get_privatekey(newconn->ssl));
				EVP_PKEY_free(pktmp);
			}

			if (!SSL_CTX_check_private_key(newconn->ctx)) {
				conn_info(funcid, INFO_ERROR, "Private/public key mismatch for client certificate\n");
				conn_cleanup(newconn);
				free(newconn);
				return NULL;
			}
		}

		if (sslhandling == CONN_SSL_YES) {
			if (SSL_set_fd(newconn->ssl, newconn->sock) != 1) {
				char sslerrmsg[256];

				ERR_error_string(ERR_get_error(), sslerrmsg);
				conn_info(funcid, INFO_ERROR, "SSL_set_fd failed: %s\n", sslerrmsg);
				conn_cleanup(newconn);
				free(newconn);
				return NULL;
			}
		}
	}
#endif
	newconn->usercallback(newconn, CONN_CB_CONNECT_START, newconn->userdata);
	n = connect(newconn->sock, newconn->peer, newconn->peersz);
	if (n == 0) {
		newconn->usercallback(newconn, CONN_CB_CONNECT_COMPLETE, newconn->userdata);

		if (sslhandling == CONN_SSL_YES)
			try_ssl_connect(newconn);
		else
			newconn->connstate = CONN_PLAINTEXT;
	}
	else if ((n == -1) && (errno == EINPROGRESS)) {
		newconn->connstate = ((sslhandling == CONN_SSL_YES) ? CONN_SSL_CONNECTING : CONN_PLAINTEXT_CONNECTING);
	}
	else {
		newconn->usercallback(newconn, CONN_CB_CONNECT_FAILED, newconn->userdata);
		conn_info(funcid, INFO_ERROR, "connect to %s failed: %s\n", conn_print_address(newconn), strerror(errno));
		conn_cleanup(newconn);
		free(newconn);
		return NULL;
	}

	if (newconn) {
		newconn->maxlifetime = maxlifetime;
		conn_getntimer(&newconn->starttime);
		newconn->next = conns;
		conns = newconn;
	}

	return newconn;
}

void conn_close_connection(tcpconn_t *conn, char *direction)
{
	if (!conn || (conn->sock <= 0)) return;

	switch (conn->connstate) {
	  case CONN_DEAD:
	  case CONN_CLOSING:
		break;
	
	  case CONN_PLAINTEXT:
		/* Unencrypted connections support shutdown() */
		if (!direction || (strcasecmp(direction, "rw") == 0)) {
			conn->connstate = CONN_CLOSING;
			conn_cleanup(conn);
		}
		else if (strcasecmp(direction, "w") == 0) {
			shutdown(conn->sock, SHUT_WR);
		}
		else if (strcasecmp(direction, "r") == 0) {
			shutdown(conn->sock, SHUT_RD);
		}
		break;

	  default:
		/* Encrypted connections can only do a full close */
		if (!direction || (strcasecmp(direction, "rw") == 0)) {
#ifdef HAVE_OPENSSL
			if (conn->ssl) SSL_shutdown(conn->ssl);
#endif
			conn->connstate = CONN_CLOSING;
			conn_cleanup(conn);
		}
	}
}


int conn_active(void)
{
	return (conns != NULL);
}

void conn_deinit(void)
{
	tcpconn_t *walk;

	for (walk = lsocks; (walk); walk = walk->next) {
		conn_close_connection(walk, NULL);
	}
	for (walk = conns; (walk); walk = walk->next) {
		conn_close_connection(walk, NULL);
	}

#ifdef HAVE_OPENSSL
	if (serverctx) SSL_CTX_free(serverctx);
	EVP_cleanup();
	ERR_free_strings();
#endif
}

int conn_lookup_portnumber(char *svcname, int defaultport)
{
	struct servent *svcinfo;

	svcinfo = getservbyname(svcname, NULL);
	return (svcinfo ? ntohs(svcinfo->s_port) : defaultport);
}

char *conn_lookup_ip(char *hostname, int *portnumber)
{
	struct addrinfo *addr;
	char *portname;
	static char addrstring[46];
	int res;

	*addrstring = '\0';

	portname = strrchr(hostname, '/');
	if (portname) {
		*portname = '\0';
		portname++;
	}
	res = getaddrinfo(hostname, portname, NULL, &addr);
	if (portname) *portname = '/';

#ifdef IPV4_SUPPORT
	if ((res != 0) && !portname) {
		/* Maybe it's a v4 address with ":portnumber" added */
		portname = strrchr(hostname, ':');
		if (portname) {
			*portname = '\0';
			portname++;
			res = getaddrinfo(hostname, portname, NULL, &addr);
			*portname = ':';
		}
	}
#endif

	if (res == 0) {
		switch (addr->ai_family) {
#ifdef IPV4_SUPPORT
		  case AF_INET: 
			{
				struct sockaddr_in *sin4 = (struct sockaddr_in *)addr->ai_addr;
				inet_ntop(addr->ai_family, &sin4->sin_addr, addrstring, sizeof(addrstring));
				if (portnumber) *portnumber = ntohs(sin4->sin_port);
			}
			break;
#endif

#ifdef IPV6_SUPPORT
		  case AF_INET6:
			{
				struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr->ai_addr;
				inet_ntop(addr->ai_family, &sin6->sin6_addr, addrstring, sizeof(addrstring));
				if (portnumber) *portnumber = ntohs(sin6->sin6_port);
			}
			break;
#endif
		}

		freeaddrinfo(addr);

		return addrstring;
	}
	else
		return NULL;
}


int conn_is_ip(char *ip)
{
	int res = 0;

#ifdef IPV4_SUPPORT
	if (!res) {
		struct in_addr addr;
		res = (inet_pton(AF_INET, ip, &addr) == 1) ? 4 : 0;
	}
#endif

#ifdef IPV6_SUPPORT
	if (!res) {
		struct in6_addr addr;
		res = (inet_pton(AF_INET6, ip, &addr) == 1) ? 6 : 0;
	}
#endif

	return res;
}


int conn_null_ip(char *ip)
{
#ifdef IPV4_SUPPORT
	{
		struct in_addr addr;
		if ((inet_pton(AF_INET, ip, &addr) == 1) && (addr.s_addr == INADDR_ANY)) return 1;
	}
#endif

#ifdef IPV6_SUPPORT
	{
		struct in6_addr addr;
		if ((inet_pton(AF_INET6, ip, &addr) == 1) && (memcmp(&addr.s6_addr, in6addr_any.s6_addr, sizeof(in6addr_any.s6_addr))== 0)) return 1;
	}
#endif

	return 0;
}

