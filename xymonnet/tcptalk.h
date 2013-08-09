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
	enum conn_cbresult_t (*callback)(tcpconn_t *, enum conn_callback_t, void *);

	/* For SSL connections */
	enum { SSLVERSION_DEFAULT, SSLVERSION_V2, SSLVERSION_V3, SSLVERSION_TLS1 } sslver;
	enum sslhandling_t sslhandling;
	char *sslcertfn, *sslkeyfn, *sslname;

	/* For DNS lookups of destinationip */
	char *lookupstring;
	int af_index;
	enum { LOOKUP_COMPLETED, LOOKUP_NEEDED, LOOKUP_ACTIVE, LOOKUP_FAILED } lookupstatus;
	struct timespec lookupstart;
} myconn_netparams_t;

/*
 * This struct holds the application-level data for a network test. All of the connection-specific data are
 * stored in the tcpconn_t structure that is maintained by tcplib.
 */
typedef struct myconn_t {
	char *testspec;
	myconn_netparams_t netparams;
	enum { TALK_PROTO_PLAIN, TALK_PROTO_NTP, TALK_PROTO_HTTP, TALK_PROTO_DNSQUERY, TALK_PROTO_PING, TALK_PROTO_LDAP, TALK_PROTO_EXTERNAL } talkprotocol;
	char **dialog;				/* SEND/EXPECT/READ/CLOSE steps */
	int dialogtoken;
	listitem_t *listitem;
	void *hostinfo;
	int timeout, interval;
	unsigned long testid;
	time_t teststarttime, testendtime;

	/* Results and statistics */
	enum { TALK_CONN_FAILED, TALK_CONN_TIMEOUT, TALK_OK, TALK_BADDATA, TALK_BADSSLHANDSHAKE, TALK_INTERRUPTED, TALK_CANNOT_RESOLVE, TALK_MODULE_FAILED, TALK_RESULT_LAST } talkresult;
	strbuffer_t *textlog;			/* Logs the actual data exchanged */
	unsigned int bytesread;
	unsigned int byteswritten;
	int elapsedus, dnselapsedus;
	char *peercertificate;
	time_t peercertificateexpiry;
	char *peercertificateissuer;
	char *peercertificatedetails;

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
	struct timespec dnsstarttime;

	/* External tests */
	pid_t workerpid;
} myconn_t;

typedef struct net_test_options_t {
	enum { NET_TEST_STANDARD, NET_TEST_TELNET, NET_TEST_HTTP, NET_TEST_NTP, NET_TEST_DNS, NET_TEST_PING, NET_TEST_LDAP, NET_TEST_EXTERNAL } testtype;
	int timeout, interval;
	char *sourceip;
	unsigned long testid;
} net_test_options_t;

enum dns_strategy_t { DNS_STRATEGY_STANDARD, DNS_STRATEGY_IP, DNS_STRATEGY_HOSTNAME };
extern void set_dns_strategy(enum dns_strategy_t strategy);

extern void test_is_done(myconn_t *rec);
extern void *add_net_test(char *testspec, char **dialog, int dtoken, net_test_options_t *options,
			 myconn_netparams_t *netparams, void *hostinfo);
extern listhead_t *run_net_tests(int concurrency, char *sourceip4, char *sourceip6);
extern void init_tcp_testmodule(void);

extern char *myconn_talkresult_names[];

#endif

