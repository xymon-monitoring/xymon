/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* This is used to implement the testing of a TCP service.                    */
/*                                                                            */
/* Copyright (C) 2003-2010 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __CONTEST_H_
#define __CONTEST_H_

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef HAVE_OPENSSL

/*
 * OpenSSL defs. We require OpenSSL 0.9.5 or later
 * as some of the routines we use are not available
 * in earlier versions.
 */
#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#if !defined(OPENSSL_VERSION_NUMBER) || (OPENSSL_VERSION_NUMBER < 0x00905000L)
#error SSL-protocol testing requires OpenSSL version 0.9.5 or later
#endif

#else
/*
 * xymonnet without support for SSL protocols.
 */
#undef TCP_SSL
#define TCP_SSL 0x0000
#define SSL_CTX void
#define SSL void
#endif

#include "libxymon.h"

extern char *ssl_library_version;
extern char *ciphershigh;
extern char *ciphersmedium;
extern unsigned int warnbytesread;
extern int shuffletests;

#define SSLVERSION_DEFAULT 0
#define SSLVERSION_V2      1
#define SSLVERSION_V3      2
#define SSLVERSION_TLS1    3

typedef struct {
	char *cipherlist;
	int  sslversion;
	char *clientcert;
} ssloptions_t;

typedef int (*f_callback_data)(unsigned char *buffer, unsigned int size, void *privdata);
typedef void (*f_callback_final)(void *privdata);

#define CONTEST_ENOERROR   0
#define CONTEST_ETIMEOUT   1
#define CONTEST_ENOCONN    2
#define CONTEST_EDNS       3
#define CONTEST_EIO        4
#define CONTEST_ESSL       5

typedef struct tcptest_t {
	struct sockaddr_in addr;        /* Address (IP+port) to test */
	char *srcaddr;
	struct svcinfo_t *svcinfo;      /* svcinfo_t for service */
	long int randomizer;
	int  fd;                        /* Socket filedescriptor */
	time_t lastactive;
	time_t cutoff;
	char *tspec;
	unsigned int bytesread;
	unsigned int byteswritten;

	/* Connection info */
	int  connres;                   /* connect() status returned */
	int  open;                      /* Result - is it open? */
	int  errcode;                   /* Pick up any errors */
	struct timespec timestart;	/* Starttime of connection attempt */
	struct timespec duration;	/* Duration of connection attempt */
	struct timespec totaltime;	/* Duration of the full transfer */

	/* Data we send */
	unsigned char *sendtxt;
	unsigned int sendlen;

	/* For grabbing banners */
	int  silenttest;		/* Banner grabbing can be disabled per test */
	int  readpending;               /* Temp status while reading banner */
	unsigned char *banner;          /* Banner text from service */
	unsigned int bannerbytes;       /* Number of bytes in banner */

	/* For testing SSL-wrapped services */
	ssloptions_t *ssloptions;	/* Specific SSL options requested by user */
	SSL_CTX *sslctx;		/* SSL context pointer */
	SSL  *ssldata;			/* SSL data (socket) pointer */
	char *certinfo;			/* Certificate info (subject+expiretime) */
	time_t certexpires;		/* Expiretime in time_t format */
	char *certsubject;
	int mincipherbits;              /* Bits in the weakest encryption supported */
	int sslrunning;			/* Track state of an SSL session */
	int sslagain;			/* SSL read/write needs more data */

	/* For testing telnet services */
	unsigned char *telnetbuf;	/* Buffer for telnet option negotiation */
	int telnetbuflen;		/* Length of telnetbuf - it's binary, so no strlen */
	int telnetnegotiate;		/* Flag telling if telnet option negotiation is being done */

	/* For testing http services */
	void *priv;
	f_callback_data datacallback;
	f_callback_final finalcallback;

	struct tcptest_t *next;
} tcptest_t;

#define CONTENTCHECK_NONE   0
#define CONTENTCHECK_REGEX  1
#define CONTENTCHECK_DIGEST 2
#define CONTENTCHECK_NOREGEX 3
#define CONTENTCHECK_CONTENTTYPE 4

#define HTTPVER_ANY 0
#define HTTPVER_10  1
#define HTTPVER_11  2

#define CHUNK_NOTCHUNKED 0
#define CHUNK_INIT       1
#define CHUNK_GETLEN     2
#define CHUNK_SKIPLENCR  3
#define CHUNK_DATA       4
#define CHUNK_SKIPENDCR  5
#define CHUNK_DONE       6
#define CHUNK_NOMORE     7

typedef struct {
	tcptest_t	*tcptest;

	char		*url;			/* URL to request, stripped of configuration artefacts */
	int		parsestatus;
	weburl_t	weburl;

	int		gotheaders;
	int		contlen;
	int		chunkstate;
	unsigned int	leftinchunk;

	unsigned char	*headers;		/* HTTP headers from server */
	unsigned int	hdrlen;
	unsigned char	*output;		/* Data from server */
	unsigned int	outlen;

	long		httpstatus;		/* HTTP status from server */
	char		*contenttype;		/* Content-type: header from server */

	int		contentcheck;		/* 0=no content check, 1=regex check, 2=digest check */
	void		*exp;			/* data for content match (digest, or regexp data) */
	digestctx_t	*digestctx;		/* OpenSSL data for digest handling */
	char		*digest;		/* Digest of the data received from the server */
	long		contstatus;		/* Pseudo HTTP status for content check */

	/* Used during status-reporting */
	int		httpcolor;		/* Color of this HTTP test */
	char		*errorcause;
	char		*faileddeps;		/* List of failed dependency checks */
} http_data_t;

extern unsigned long tcp_stats_read;
extern unsigned long tcp_stats_written;
extern unsigned int tcp_stats_total;
extern unsigned int tcp_stats_http;
extern unsigned int tcp_stats_plain;
extern unsigned int tcp_stats_connects;

extern char *init_tcp_services(void);
extern int default_tcp_port(char *svcname);
extern void dump_tcp_services(void);
extern tcptest_t *add_tcp_test(char *ip, int port, char *service, ssloptions_t *sslopt, char *srcip,
			    char *tspec, int silent, unsigned char *reqmsg, 
			    void *priv, f_callback_data datacallback, f_callback_final finalcallback);
extern void do_tcp_tests(int timeout, int concurrency);
extern void show_tcp_test_results(void);
extern int tcp_got_expected(tcptest_t *test);

#endif

