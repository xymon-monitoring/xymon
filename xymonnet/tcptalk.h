/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __TCPTALK_H__
#define __TCPTALK_H__

#define NTPTRIES 4

typedef struct myconn_netparams_t {
	char *destinationip, *sourceip;		/* The actual IP we will use for the connection, either IPv4 or IPv6 */
	int destinationport;
	enum conn_socktype_t socktype;
	enum { SSLVERSION_NOSSL, SSLVERSION_DEFAULT, SSLVERSION_V2, SSLVERSION_V3, SSLVERSION_TLS1 } sslver;
	int (*callback)(tcpconn_t *, enum conn_callback_t, void *);
} myconn_netparams_t;

/*
 * This struct holds the application-level data for a network test. All of the connection-specific data are
 * stored in the tcpconn_t structure that is maintained by tcplib.
 */
typedef struct myconn_t {
	char *testspec;
	myconn_netparams_t netparams;
	enum { TALK_PROTO_PLAIN, TALK_PROTO_NTP, TALK_PROTO_HTTP, TALK_PROTO_DNS } talkprotocol;
	char **dialog;				/* SEND/EXPECT/READ/CLOSE steps */

	/* Results and statistics */
	enum { TALK_CONN_FAILED, TALK_CONN_TIMEOUT, TALK_OK, TALK_BADDATA, TALK_BADSSLHANDSHAKE, TALK_INTERRUPTED } talkresult;
	strbuffer_t *textlog;			/* Logs the actual data exchanged */
	unsigned int bytesread;
	unsigned int byteswritten;
	int elapsedms;
	char *peercertificate;
	time_t peercertificateexpiry;

	/* Plain-text protocols */
	int step;				/* Current step in dialog */
	char *readbuf, *writebuf;		/* I/O buffers */
	char *readp, *writep;			/* Temp pointers while reading/sending one step */
	int readbufsz;

	/* NTP */
	unsigned int ntp_sendtime[2];
	float ntpdiff[NTPTRIES];
	int ntpstratum;
	float ntpoffset;

	/* Telnet */
	int istelnet;	/* 0 = No telnet, 1 = Read telnet options, -N = Write N bytes of telnet option response */

	/* HTTP */
	enum { HTTPDATA_HEADERS, HTTPDATA_BODY } httpdatastate;
	strbuffer_t *httpheaders;
	strbuffer_t *httpbody;
	int httpstatus;
	unsigned int httpcontentleft;	/* How many more bytes left to read of the content */
	enum { HTTP_CHUNK_NOTCHUNKED, HTTP_CHUNK_NOTCHUNKED_NOCLEN,
	       HTTP_CHUNK_INIT, HTTP_CHUNK_GETLEN, HTTP_CHUNK_SKIPLENCR, 
	       HTTP_CHUNK_DATA, HTTP_CHUNK_SKIPENDCR, 
	       HTTP_CHUNK_DONE, HTTP_CHUNK_NOMORE } httpchunkstate;
	int httpleftinchunk;
	int httplastbodyread;

	/* DNS */
	void *dnschannel;
	enum { DNS_NOTDONE, DNS_QUERY_READY, DNS_QUERY_ACTIVE, DNS_QUERY_COMPLETED, DNS_FINISHED } dnsstatus;
	struct myconn_t *dnsnext;
	struct timespec dnsstarttime;

	struct myconn_t *next;
} myconn_t;

extern int client_callback(tcpconn_t *connection, enum conn_callback_t id, void *userdata);
extern char **tcp_set_dialog(myconn_t *rec, char *service);

#endif

