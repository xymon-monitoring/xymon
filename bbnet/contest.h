/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* This is used to implement the testing of a TCP service.                    */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
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

/*
 * Flag bits for known TCP services
 */
#define TCP_GET_BANNER 0x0001
#define TCP_TELNET     0x0002
#define TCP_SSL        0x0004

#ifdef BBGEN_SSL

/*
 * OpenSSL defs. We require OpenSSL 0.9.5 or later
 * as some of the routines we use are not available
 * in earlier versions.
 */
#include <openssl/ssl.h>
#include <openssl/rand.h>

#if !defined(OPENSSL_VERSION_NUMBER) || (OPENSSL_VERSION_NUMBER < 0x00905000L)
#error SSL-protocol testing requires OpenSSL version 0.9.5 or later
#endif

#else
/*
 * bbgen without support for SSL protocols.
 */
#undef TCP_SSL
#define TCP_SSL 0x0000
#define SSL_CTX void
#define SSL void
#endif


typedef struct svcinfo_t {
	char *svcname;
	char *sendtxt;
	char *exptext;
	unsigned int flags;
} svcinfo_t;

typedef struct test_t {
	struct sockaddr_in addr;        /* Address (IP+port) to test */
	struct svcinfo_t *svcinfo;      /* svcinfo_t for service */
	int  fd;                        /* Socket filedescriptor */

	/* Connection info */
	int  connres;                   /* connect() status returned */
	int  open;                      /* Result - is it open? */
	struct timeval timestart;	/* Starttime of connection attempt */
	struct timeval duration;	/* Duration of connection attempt */

	/* For grabbing banners */
	int  silenttest;		/* Banner grabbing can be disabled per test */
	int  readpending;               /* Temp status while reading banner */
	unsigned char *banner;          /* Banner text from service */

	/* For testing SSL-wrapped services */
	SSL_CTX *sslctx;		/* SSL context pointer */
	SSL  *ssldata;			/* SSL data (socket) pointer */
	char *certinfo;			/* Certificate info (subject+expiretime) */
	time_t certexpires;		/* Expiretime in time_t format */
	int sslrunning;			/* Track state of an SSL session */

	/* For testing telnet services */
	unsigned char *telnetbuf;	/* Buffer for telnet option negotiation */
	int telnetbuflen;		/* Length of telnetbuf - it's binary, so no strlen */
	int telnetnegotiate;		/* Flag telling if telnet option negotiation is being done */

	struct test_t *next;
} test_t;

extern test_t *add_tcp_test(char *ip, int portnum, char *service, int silent);
extern void do_tcp_tests(int timeout, int concurrency);
extern void show_tcp_test_results(void);
extern int tcp_got_expected(test_t *test);

#endif

