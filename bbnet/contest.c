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

static char rcsid[] = "$Id: contest.c,v 1.64 2004-09-11 07:13:37 henrik Exp $";

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#if !defined(HPUX)		/* HP-UX has select() and friends in sys/types.h */
#include <sys/select.h>		/* Someday I'll move to GNU Autoconf for this ... */
#endif
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <ctype.h>

#include "bbtest-net.h"
#include "contest.h"
#include "httptest.h"

#include "bbgen.h"
#include "debug.h"
#include "util.h"
#include "dns.h"

/* BSD uses RLIMIT_OFILE */
#if defined(RLIMIT_OFILE) && !defined(RLIMIT_NOFILE)
#define RLIMIT_NOFILE RLIMIT_OFILE
#endif

#define MAX_TELNET_CYCLES 5		/* Max loops with telnet options before aborting banner */
#define SSLSETUP_PENDING -1		/* Magic value for tcptest_t->sslrunning while handshaking */

/* See http://www.openssl.org/docs/apps/ciphers.html for cipher strings */
char *ciphersmedium = "MEDIUM";	/* Must be formatted for openssl library */
char *ciphershigh = "HIGH";	/* Must be formatted for openssl library */

unsigned int tcp_stats_total    = 0;
unsigned int tcp_stats_http     = 0;
unsigned int tcp_stats_plain    = 0;
unsigned int tcp_stats_connects = 0;
unsigned long tcp_stats_read    = 0;
unsigned long tcp_stats_written = 0;

static tcptest_t *thead = NULL;

/*
 * Services we know how to handle:
 * This defines what to send to them to shut down the 
 * session nicely, and whether we want to grab the
 * banner or not.
 */
static svcinfo_t default_svcinfo[] = {
	/*           ------------- data to send ------   ---- green data ------ flags */
	/* name      databytes            length          databytes offset len        */
	{ "ftp",     "quit\r\n",          0,                  "220",	0, 0,	(TCP_GET_BANNER), 21 },
	{ "ssh",     NULL,                0,                  "SSH",	0, 0,	(TCP_GET_BANNER), 22 },
	{ "ssh1",    NULL,                0,                  "SSH",	0, 0,	(TCP_GET_BANNER), 22 },
	{ "ssh2",    NULL,                0,                  "SSH",	0, 0,	(TCP_GET_BANNER), 22 },
	{ "telnet",  NULL,                0,                  NULL,	0, 0,	(TCP_GET_BANNER|TCP_TELNET), 23 },
	{ "smtp",    "mail\r\nquit\r\n",  0,                  "220",	0, 0,	(TCP_GET_BANNER), 25 }, /* Send "MAIL" to avoid sendmail NOQUEUE logs */
	{ "pop",     "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 110 },
	{ "pop2",    "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 109 },
	{ "pop-2",   "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 109 },
	{ "pop3",    "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 110 },
	{ "pop-3",   "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 110 },
	{ "imap",    "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 143 },
	{ "imap2",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 143 },
	{ "imap3",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 220 },
	{ "imap4",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 143 },
	{ "nntp",    "quit\r\n",          0,                  "200",	0, 0,	(TCP_GET_BANNER), 119 },
	{ "ldap",    NULL,                0,                  NULL,     0, 0,	(0), 389 },
	{ "rsync",   NULL,                0,                  "@RSYNCD",0, 0,	(TCP_GET_BANNER), 873 },
	{ "bbd",     "dummy",             0,                  NULL,	0, 0,	(0), 1984 },
	{ "ftps",    "quit\r\n",          0,                  "220",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 990 },
	{ "telnets", NULL,                0,                  NULL, 	0, 0,	(TCP_GET_BANNER|TCP_TELNET|TCP_SSL), 992 },
	{ "smtps",   "mail\r\nquit\r\n",  0,                  "220",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 0 }, /* Non-standard - IANA */
	{ "pop3s",   "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 995 },
	{ "imaps",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 993 },
	{ "nntps",   "quit\r\n",          0,                  "200",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 563 },
	{ "ldaps",   NULL,                0,                  NULL,     0, 0,	(TCP_SSL), 636 },
	{ "clamd",   "PING\r\n",          0,                  "PONG",   0, 0,	(0), 3310 },
	{ NULL,      NULL,                0,                  NULL,	0, 0,	(0), 0 }	/* Default behaviour: Just try a connect */
};

static svcinfo_t *svcinfo = default_svcinfo;
static svcinfo_t svcinfo_http  = { "http", NULL, 0, NULL, 0, 0, (TCP_GET_BANNER|TCP_HTTP), 80 };
static svcinfo_t svcinfo_https = { "https", NULL, 0, NULL, 0, 0, (TCP_GET_BANNER|TCP_HTTP|TCP_SSL), 443 };
static ssloptions_t default_sslopt = { NULL, SSLVERSION_DEFAULT };

typedef struct svclist_t {
	struct svcinfo_t *rec;
	struct svclist_t *next;
} svclist_t;


static char *binview(unsigned char *buf, int buflen)
{
	static char hexchars[16] = "0123456789ABCDEF";
	static char result[MAX_LINE_LEN];
	unsigned char *inp, *outp;
	int i;

	if (buf && (buflen == 0)) buflen = strlen(buf);
	for (inp=buf, i=0, outp=result; (i<buflen); i++,inp++) {
		if (isprint(*inp)) {
			*outp = *inp;
			outp++;
		}
		else if (*inp == '\r') {
			*outp = '\\'; outp++;
			*outp = 'r'; outp++;
		}
		else if (*inp == '\n') {
			*outp = '\\'; outp++;
			*outp = 'n'; outp++;
		}
		else if (*inp == '\t') {
			*outp = '\\'; outp++;
			*outp = 't'; outp++;
		}
		else {
			*outp = '\\'; outp++;
			*outp = 'x'; outp++;
			*outp = hexchars[*inp / 16]; outp++;
			*outp = hexchars[*inp % 16]; outp++;
		}
	}
	*outp = '\0';

	return result;
}

char *init_tcp_services(void)
{
	char filename[MAX_PATH];
	FILE *fd = NULL;
	char buf[MAX_LINE_LEN];
	svclist_t *head = NULL;
	svclist_t *item = NULL;
	svclist_t *first = NULL;
	svclist_t *walk;
	char *bbnetsvcs = NULL;
	char *searchstring;
	int svcnamebytes = 0;
	int svccount = 1;
	int i;

	filename[0] = '\0';
	if (getenv("BBHOME")) {
		sprintf(filename, "%s/etc/", getenv("BBHOME"));
	}
	strcat(filename, "bb-services");

	fd = fopen(filename, "r");
	if (fd == NULL) {
		errprintf("Cannot open TCP service-definitions file %s - using defaults\n", filename);
		return malcop(getenv("BBNETSVCS"));
	}

	head = (svclist_t *)malloc(sizeof(svclist_t));
	head->rec = (svcinfo_t *)calloc(1, sizeof(svcinfo_t));
	head->next = NULL;

	while (fd && fgets(buf, sizeof(buf), fd)) {
		char *l;

		l = strchr(buf, '\n'); if (l) *l = '\0';
		l = skipwhitespace(buf);

		if (strncmp(l, "service ", 8) == 0) {
			char *svcname;

			l = skipwhitespace(l+7);
			svcname = strtok(l, "|");
			first = NULL;
			while (svcname) {
				item = (svclist_t *) malloc(sizeof(svclist_t));
				item->rec = (svcinfo_t *)calloc(1, sizeof(svcinfo_t));
				item->rec->svcname = malcop(svcname);
				svcnamebytes += (strlen(svcname) + 1);
				item->next = head;
				head = item;
				svcname = strtok(NULL, "|");
				if (first == NULL) first = item;
				svccount++;
			}
		}
		else if (strncmp(l, "send ", 5) == 0) {
			if (item) {
				getescapestring(skipwhitespace(l+4), &item->rec->sendtxt, &item->rec->sendlen);
				for (walk = item; (walk != first); walk = walk->next) {
					walk->next->rec->sendtxt = item->rec->sendtxt;
					walk->next->rec->sendlen = item->rec->sendlen;
				}
			}
		}
		else if (strncmp(l, "expect ", 7) == 0) {
			if (item) {
				getescapestring(skipwhitespace(l+7), &item->rec->exptext, &item->rec->explen);
				for (walk = item; (walk != first); walk = walk->next) {
					walk->next->rec->exptext = item->rec->exptext;
					walk->next->rec->explen = item->rec->explen;
					walk->next->rec->expofs = 0; /* HACK - not used right now */
				}
			}
		}
		else if (strncmp(l, "options ", 8) == 0) {
			if (item) {
				char *opt;

				item->rec->flags = 0;
				l = skipwhitespace(l+7);
				opt = strtok(l, ",");
				while (opt) {
					if      (strcmp(opt, "ssl") == 0)    item->rec->flags += TCP_SSL;
					else if (strcmp(opt, "banner") == 0) item->rec->flags += TCP_GET_BANNER;
					else if (strcmp(opt, "telnet") == 0) item->rec->flags += TCP_TELNET;
					else errprintf("Unknown option: %s\n", opt);

					opt = strtok(NULL, ",");
				}
				for (walk = item; (walk != first); walk = walk->next) {
					walk->next->rec->flags = item->rec->flags;
				}
			}
		}
		else if (strncmp(l, "port ", 5) == 0) {
			if (item) {
				item->rec->port = atoi(skipwhitespace(l+4));
				for (walk = item; (walk != first); walk = walk->next) {
					walk->next->rec->port = item->rec->port;
				}
			}
		}
	}

	if (fd) fclose(fd);

	svcinfo = (svcinfo_t *) malloc(svccount * sizeof(svcinfo_t));
	for (walk=head, i=0; (walk); walk = walk->next, i++) {
		svcinfo[i].svcname = walk->rec->svcname;
		svcinfo[i].sendtxt = walk->rec->sendtxt;
		svcinfo[i].sendlen = walk->rec->sendlen;
		svcinfo[i].exptext = walk->rec->exptext;
		svcinfo[i].explen  = walk->rec->explen;
		svcinfo[i].expofs  = walk->rec->expofs;
		svcinfo[i].flags   = walk->rec->flags;
		svcinfo[i].port    = walk->rec->port;
	}

	searchstring = malcop(getenv("BBNETSVCS"));
	bbnetsvcs = (char *) malloc(strlen(getenv("BBNETSVCS")) + svcnamebytes + 1);
	strcpy(bbnetsvcs, getenv("BBNETSVCS"));
	for (i=0; (svcinfo[i].svcname); i++) {
		char *p;

		strcpy(searchstring, getenv("BBNETSVCS"));
		p = strtok(searchstring, " ");
		while (p && (strcmp(p, svcinfo[i].svcname) != 0)) p = strtok(NULL, " ");

		if (p == NULL) {
			strcat(bbnetsvcs, " ");
			strcat(bbnetsvcs, svcinfo[i].svcname);
		}
	}
	free(searchstring);

	if (debug) {
		dump_tcp_services();
		printf("BBNETSVCS set to : %s\n", bbnetsvcs);
	}

	return bbnetsvcs;
}

int default_tcp_port(char *svcname)
{
	int svcidx;
	int result = 0;

	for (svcidx=0; (svcinfo[svcidx].svcname && (strcmp(svcname, svcinfo[svcidx].svcname) != 0)); svcidx++) ;
	if (svcinfo[svcidx].svcname) result = svcinfo[svcidx].port;

	return result;
}

void dump_tcp_services(void)
{
	int i;

	printf("Service list dump\n");
	for (i=0; (svcinfo[i].svcname); i++) {
		printf(" Name      : %s\n", svcinfo[i].svcname);
		printf("   Sendtext: %s\n", binview(svcinfo[i].sendtxt, svcinfo[i].sendlen));
		printf("   Sendlen : %d\n", svcinfo[i].sendlen);
		printf("   Exp.text: %s\n", binview(svcinfo[i].exptext, svcinfo[i].explen));
		printf("   Exp.len : %d\n", svcinfo[i].explen);
		printf("   Exp.ofs : %d\n", svcinfo[i].expofs);
		printf("   Flags   : %d\n", svcinfo[i].flags);
		printf("   Port    : %d\n", svcinfo[i].port);
	}
	printf("\n");
}


static int tcp_callback(unsigned char *buf, unsigned int len, void *priv)
{
	/*
	 * The default data callback function for simple TCP tests.
	 */

	tcptest_t *item = (tcptest_t *) priv;

	if (item->banner == NULL) {
		item->banner = (unsigned char *)malloc(len+1);
	}
	else {
		item->banner = (unsigned char *)realloc(item->banner, item->bannerbytes+len+1);
	}

	memcpy(item->banner+item->bannerbytes, buf, len);
	item->bannerbytes += len;
	*(item->banner + item->bannerbytes) = '\0';

	return 1;	/* We always just grab the first bit of data for TCP tests */
}


tcptest_t *add_tcp_test(char *ip, int port, char *service, ssloptions_t *sslopt,
			int silent, unsigned char *reqmsg, 
		     void *priv, f_callback_data datacallback, f_callback_final finalcallback)
{
	tcptest_t *newtest;

	dprintf("Adding tcp test IP=%s, port=%d, service=%s, silent=%d\n", textornull(ip), port, service, silent);

	if (port == 0) {
		errprintf("Trying to scan port 0 for service %s\n", service);
		errprintf("You probably need to define the %s service in /etc/services\n", service);
		return NULL;
	}

	tcp_stats_total++;
	newtest = (tcptest_t *) malloc(sizeof(tcptest_t));

	newtest->fd = -1;
	newtest->open = 0;
	newtest->connres = -1;
	newtest->errcode = CONTEST_ENOERROR;
	newtest->duration.tv_sec = newtest->duration.tv_usec = 0;
	newtest->totaltime.tv_sec = newtest->totaltime.tv_usec = 0;

	memset(&newtest->addr, 0, sizeof(newtest->addr));
	newtest->addr.sin_family = PF_INET;
	newtest->addr.sin_port = htons(port);
	if ((ip == NULL) || (strlen(ip) == 0) || (inet_aton(ip, (struct in_addr *) &newtest->addr.sin_addr.s_addr) == 0)) {
		newtest->errcode = CONTEST_EDNS;
	}

	if (strcmp(service, "http") == 0) {
		newtest->svcinfo = &svcinfo_http;
		tcp_stats_http++;
	}
	else if (strcmp(service, "https") == 0) {
		newtest->svcinfo = &svcinfo_https;
		tcp_stats_http++;
	}
	else {
		int i;

		for (i=0; (svcinfo[i].svcname && (strcmp(service, svcinfo[i].svcname) != 0)); i++) ;
		newtest->svcinfo = &svcinfo[i];
		tcp_stats_plain++;
	}

	newtest->sendtxt = (reqmsg ? reqmsg : newtest->svcinfo->sendtxt);
	newtest->sendlen = (reqmsg ? strlen(reqmsg) : newtest->svcinfo->sendlen);

	newtest->silenttest = silent;
	newtest->readpending = 0;
	newtest->telnetnegotiate = (((newtest->svcinfo->flags & TCP_TELNET) && !silent) ? MAX_TELNET_CYCLES : 0);
	newtest->telnetbuf = NULL;
	newtest->telnetbuflen = 0;

	newtest->ssloptions = (sslopt ? sslopt : &default_sslopt);
	newtest->sslctx = NULL;
	newtest->ssldata = NULL;
	newtest->certinfo = NULL;
	newtest->certexpires = 0;
	newtest->sslrunning = ((newtest->svcinfo->flags & TCP_SSL) ? SSLSETUP_PENDING : 0);
	newtest->sslagain = 0;

	newtest->banner = NULL;
	newtest->bannerbytes = 0;

	if (datacallback == NULL) {
		/*
		 * Use the default callback-routine, which expects 
		 * "priv" to point at the test item.
		 */
		newtest->priv = newtest;
		newtest->datacallback = tcp_callback;
	}
	else {
		/*
		 * Custom callback - handles data output by itself.
		 */
		newtest->priv = priv;
		newtest->datacallback = datacallback;
	}

	newtest->finalcallback = finalcallback;

	if (newtest->errcode == CONTEST_ENOERROR) {
		newtest->next = thead;
		thead = newtest;
	}

	return newtest;
}


static void get_connectiontime(tcptest_t *item, struct timeval *timestamp)
{
	tvdiff(&item->timestart, timestamp, &item->duration);
}

static void get_totaltime(tcptest_t *item, struct timeval *timestamp)
{
	tvdiff(&item->timestart, timestamp, &item->totaltime);
}

static int do_telnet_options(tcptest_t *item)
{
	/*
	 * Handle telnet options.
	 *
	 * This code was taken from the sources for "netcat" version 1.10
	 * by "Hobbit" <hobbit@avian.org>.
	 */

	unsigned char *obuf;
	int remain;
	unsigned char y;
	unsigned char *inp;
	unsigned char *outp;
	int result = 0;

	if (item->telnetbuflen == 0) {
		dprintf("Ignoring telnet option with length 0\n");
		return 0;
	}

	obuf = (unsigned char *)malloc(item->telnetbuflen);
	y = 0;
	inp = item->telnetbuf;
	remain = item->telnetbuflen;
	outp = obuf;

	while (remain > 0) {
		if ((remain < 3) || (*inp != 255)) {                     /* IAC? */
			/*
			 * End of options. 
			 * We probably have the banner in the remainder of the
			 * buffer, so copy it over, and return it.
			 */
			item->banner = malcop(inp);
			item->bannerbytes = strlen(inp);
			item->telnetbuflen = 0;
			free(obuf);
			return 0;
		}
	        *outp = 255; outp++;
		inp++; remain--;
		if ((*inp == 251) || (*inp == 252))     /* WILL or WONT */
			y = 254;                          /* -> DONT */
		if ((*inp == 253) || (*inp == 254))     /* DO or DONT */
			y = 252;                          /* -> WONT */
		if (y) {
			*outp = y; outp++;
			inp++; remain--;
			*outp = *inp; outp++;		/* copy actual option byte */
			y = 0;
			result = 1;
		} /* if y */
		inp++; remain--;
	} /* while remain */

	item->telnetbuflen = (outp-obuf);
	if (item->telnetbuflen) memcpy(item->telnetbuf, obuf, item->telnetbuflen);
	item->telnetbuf[item->telnetbuflen] = '\0';
	free(obuf);
	return result;
}

#if TCP_SSL <= 0

char *ssl_library_version = NULL;

/*
 * Define stub routines for plain socket operations without SSL
 */
static void setup_ssl(tcptest_t *item)
{
	dprintf("SSL service checked as simple TCP test - bbtest-net compiled without SSL\n");
	item->sslrunning = 0;
}

static int socket_write(tcptest_t *item, unsigned char *outbuf, int outlen)
{
	return write(item->fd, outbuf, outlen);
}

static int socket_read(tcptest_t *item, unsigned char *inbuf, int inbufsize)
{
	return read(item->fd, inbuf, inbufsize);
}

static void socket_shutdown(tcptest_t *item)
{
	shutdown(item->fd, SHUT_RDWR);
}

#else

char *ssl_library_version = OPENSSL_VERSION_TEXT;

static int cert_password_cb(char *buf, int size, int rwflag, void *userdata)
{
	FILE *passfd;
	char *p;
	char passfn[MAX_PATH];
	char passphrase[1024];
	tcptest_t *item = (tcptest_t *)userdata;

	memset(passphrase, 0, sizeof(passphrase));

	/*
	 * Private key passphrases are stored in the file named same as the
	 * certificate itself, but with extension ".pass"
	 */
	sprintf(passfn, "%s/certs/%s", getenv("BBHOME"), item->ssloptions->clientcert);
	p = strrchr(passfn, '.'); if (p == NULL) p = passfn+strlen(passfn);
	strcpy(p, ".pass");

	passfd = fopen(passfn, "r");
	if (passfd) {
		fgets(passphrase, sizeof(passphrase)-1, passfd);
		p = strchr(passphrase, '\n'); if (p) *p = '\0';
		fclose(passfd);
	}

	strncpy(buf, passphrase, size);
	buf[size - 1] = '\0';

	/* Clear this buffer for security! Dont want passphrases in core dumps... */
	memset(passphrase, 0, sizeof(passphrase));

	return strlen(buf);
}

static char *bbgen_ASN1_UTCTIME(ASN1_UTCTIME *tm)
{
	static char result[256];
	char *asn1_string;
	int gmt=0;
	int i;
	int year=0,month=0,day=0,hour=0,minute=0,second=0;

	i=tm->length;
	asn1_string=(char *)tm->data;

	if (i < 10) return NULL;
	if (asn1_string[i-1] == 'Z') gmt=1;
	for (i=0; i<10; i++) {
		if ((asn1_string[i] > '9') || (asn1_string[i] < '0')) return NULL;
	}

	year=(asn1_string[0]-'0')*10+(asn1_string[1]-'0');
	if (year < 50) year+=100;

	month=(asn1_string[2]-'0')*10+(asn1_string[3]-'0');
	if ((month > 12) || (month < 1)) return NULL;

	day=(asn1_string[4]-'0')*10+(asn1_string[5]-'0');
	hour=(asn1_string[6]-'0')*10+(asn1_string[7]-'0');
	minute=(asn1_string[8]-'0')*10+(asn1_string[9]-'0');
	if ( (asn1_string[10] >= '0') && (asn1_string[10] <= '9') &&
	     (asn1_string[11] >= '0') && (asn1_string[11] <= '9')) {
		second= (asn1_string[10]-'0')*10+(asn1_string[11]-'0');
	}

	sprintf(result, "%04d-%02d-%02d %02d:%02d:%02d %s",
		year+1900, month, day, hour, minute, second, (gmt?"GMT":""));

	return result;
}


static void setup_ssl(tcptest_t *item)
{
	static int ssl_init_complete = 0;
	struct servent *sp;
	char portinfo[100];
	X509 *peercert;
	char *certcn, *certstart, *certend;
	int err;

	item->sslrunning = 1;

	if (!ssl_init_complete) {
		/* Setup entropy */
		if (RAND_status() != 1) {
			char path[MAX_PATH];	/* Path for the random file */

			/* load entropy from files */
			RAND_load_file(RAND_file_name(path, sizeof (path)), -1);

			/* load entropy from egd sockets */
			RAND_egd("/var/run/egd-pool");
			RAND_egd("/dev/egd-pool");
			RAND_egd("/etc/egd-pool");
			RAND_egd("/var/spool/prngd/pool");

			/* shuffle $RANDFILE (or ~/.rnd if unset) */
			RAND_write_file(RAND_file_name(path, sizeof (path)));
			if (RAND_status() != 1) {
				errprintf("Failed to find enough entropy on your system");
				item->errcode = CONTEST_ESSL;
				return;
			}
		}

		SSL_load_error_strings();
		SSL_library_init();
		ssl_init_complete = 1;
	}

	if (item->sslctx == NULL) {
		switch (item->ssloptions->sslversion) {
		  case SSLVERSION_V2:
			item->sslctx = SSL_CTX_new(SSLv2_client_method()); break;
		  case SSLVERSION_V3:
			item->sslctx = SSL_CTX_new(SSLv3_client_method()); break;
		  case SSLVERSION_TLS1:
			item->sslctx = SSL_CTX_new(TLSv1_client_method()); break;
		  default:
			item->sslctx = SSL_CTX_new(SSLv23_client_method()); break;
		}

		if (!item->sslctx) {
			char sslerrmsg[256];

			ERR_error_string(ERR_get_error(), sslerrmsg);
			errprintf("Cannot create SSL context - IP %s, service %s: %s\n", 
				   inet_ntoa(item->addr.sin_addr), item->svcinfo->svcname, sslerrmsg);
			item->sslrunning = 0;
			item->errcode = CONTEST_ESSL;
			return;
		}

		/* Workaround SSL bugs */
		SSL_CTX_set_options(item->sslctx, SSL_OP_ALL);
		SSL_CTX_set_quiet_shutdown(item->sslctx, 1);

		/* Limit set of ciphers, if user wants to */
		if (item->ssloptions->cipherlist) 
			SSL_CTX_set_cipher_list(item->sslctx, item->ssloptions->cipherlist);

		if (item->ssloptions->clientcert) {
			int status;
			char certfn[MAX_PATH];

			SSL_CTX_set_default_passwd_cb(item->sslctx, cert_password_cb);
			SSL_CTX_set_default_passwd_cb_userdata(item->sslctx, item);

			sprintf(certfn, "%s/certs/%s", getenv("BBHOME"), item->ssloptions->clientcert);
			status = SSL_CTX_use_certificate_chain_file(item->sslctx, certfn);
			if (status == 1) {
				status = SSL_CTX_use_PrivateKey_file(item->sslctx, certfn, SSL_FILETYPE_PEM);
			}

			if (status != 1) {
				char sslerrmsg[256];

				ERR_error_string(ERR_get_error(), sslerrmsg);
				errprintf("Cannot load SSL client certificate/key %s: %s\n", 
					  item->ssloptions->clientcert, sslerrmsg);
				item->sslrunning = 0;
				item->errcode = CONTEST_ESSL;
				return;
			}
		}
	}

	if (item->ssldata == NULL) {
		item->ssldata = SSL_new(item->sslctx);
		if (!item->ssldata) {
			char sslerrmsg[256];

			ERR_error_string(ERR_get_error(), sslerrmsg);
			errprintf("SSL_new failed - IP %s, service %s: %s\n", 
				   inet_ntoa(item->addr.sin_addr), item->svcinfo->svcname, sslerrmsg);
			item->sslrunning = 0;
			SSL_CTX_free(item->sslctx);
			item->errcode = CONTEST_ESSL;
			return;
		}

		/* Verify that the client certificate is working */
		if (item->ssloptions->clientcert) {
			X509 *x509;

			x509 = SSL_get_certificate(item->ssldata);
			if(x509 != NULL) {
				EVP_PKEY *pktmp = X509_get_pubkey(x509);
				EVP_PKEY_copy_parameters(pktmp,SSL_get_privatekey(item->ssldata));
				EVP_PKEY_free(pktmp);
			}

			if (!SSL_CTX_check_private_key(item->sslctx)) {
				errprintf("Private/public key mismatch for certificate %s\n", item->ssloptions->clientcert);
				item->sslrunning = 0;
				item->errcode = CONTEST_ESSL;
				return;
			}
		}

		/* SSL setup is done. Now attach the socket FD to the SSL protocol handler */
		if (SSL_set_fd(item->ssldata, item->fd) != 1) {
			char sslerrmsg[256];

			ERR_error_string(ERR_get_error(), sslerrmsg);
			errprintf("Could not initiate SSL on connection - IP %s, service %s: %s\n", 
				   inet_ntoa(item->addr.sin_addr), item->svcinfo->svcname, sslerrmsg);
			item->sslrunning = 0;
			SSL_free(item->ssldata); 
			SSL_CTX_free(item->sslctx);
			item->errcode = CONTEST_ESSL;
			return;
		}
	}

	sp = getservbyport(item->addr.sin_port, "tcp");
	if (sp) {
		sprintf(portinfo, "%s (%d/tcp)", sp->s_name, item->addr.sin_port);
	}
	else {
		sprintf(portinfo, "%d/tcp", item->addr.sin_port);
	}
	if ((err = SSL_connect(item->ssldata)) != 1) {
		char sslerrmsg[256];

		switch (SSL_get_error (item->ssldata, err)) {
		  case SSL_ERROR_WANT_READ:
		  case SSL_ERROR_WANT_WRITE:
			item->sslrunning = SSLSETUP_PENDING;
			break;
		  case SSL_ERROR_SYSCALL:
			ERR_error_string(ERR_get_error(), sslerrmsg);
			errprintf("IO error in SSL_connect to %s on host %s: %s\n",
				  portinfo, inet_ntoa(item->addr.sin_addr), sslerrmsg);
			item->errcode = CONTEST_ESSL;
			item->sslrunning = 0; SSL_free(item->ssldata); SSL_CTX_free(item->sslctx);
			break;
		  case SSL_ERROR_SSL:
			ERR_error_string(ERR_get_error(), sslerrmsg);
			errprintf("Unspecified SSL error in SSL_connect to %s on host %s: %s\n",
				  portinfo, inet_ntoa(item->addr.sin_addr), sslerrmsg);
			item->errcode = CONTEST_ESSL;
			item->sslrunning = 0; SSL_free(item->ssldata); SSL_CTX_free(item->sslctx);
			break;
		  default:
			ERR_error_string(ERR_get_error(), sslerrmsg);
			errprintf("Unknown error %d in SSL_connect to %s on host %s: %s\n",
				  err, portinfo, inet_ntoa(item->addr.sin_addr), sslerrmsg);
			item->errcode = CONTEST_ESSL;
			item->sslrunning = 0; SSL_free(item->ssldata); SSL_CTX_free(item->sslctx);
			break;
		}

		return;
	}

	/* If we get this far, the SSL handshake has completed. So grab the certificate */
	peercert = SSL_get_peer_certificate(item->ssldata);
	if (!peercert) {
		errprintf("Cannot get peer certificate for %s on host %s\n",
			  portinfo, inet_ntoa(item->addr.sin_addr));
		item->errcode = CONTEST_ESSL;
		item->sslrunning = 0; SSL_free(item->ssldata); SSL_CTX_free(item->sslctx);
		return;
	}

	certcn = X509_NAME_oneline(X509_get_subject_name(peercert), NULL, 0);
	certstart = malcop(bbgen_ASN1_UTCTIME(X509_get_notBefore(peercert)));
	certend = malcop(bbgen_ASN1_UTCTIME(X509_get_notAfter(peercert)));

	item->certinfo = (char *) malloc(strlen(certcn)+strlen(certstart)+strlen(certend)+100);
	sprintf(item->certinfo, 
		"Server certificate:\n\tsubject:%s\n\tstart date: %s\n\texpire date:%s\n", 
		certcn, certstart, certend);
	item->certexpires = sslcert_expiretime(certend);
	free(certcn); free(certstart); free(certend);

	X509_free(peercert);
}

static int socket_write(tcptest_t *item, char *outbuf, int outlen)
{
	int res = 0;

	if (item->sslrunning) {
		res = SSL_write(item->ssldata, outbuf, outlen);
		if (res < 0) {
			switch (SSL_get_error (item->ssldata, res)) {
			  case SSL_ERROR_WANT_READ:
			  case SSL_ERROR_WANT_WRITE:
				  res = 0;
				  break;
			}
		}
	}
	else {
		res = write(item->fd, outbuf, outlen);
	}

	return res;
}

static int socket_read(tcptest_t *item, char *inbuf, int inbufsize)
{
	int res = 0;

	if (item->svcinfo->flags & TCP_SSL) {
		if (item->sslrunning) {
			item->sslagain = 0;
			res = SSL_read(item->ssldata, inbuf, inbufsize);
			if (res < 0) {
				switch (SSL_get_error (item->ssldata, res)) {
				  case SSL_ERROR_WANT_READ:
				  case SSL_ERROR_WANT_WRITE:
					  item->sslagain = 1;
					  break;
				}
			}
		}
		else {
			/* SSL setup failed - flag 0 bytes read. */
			res = 0;
		}
	}
	else res = read(item->fd, inbuf, inbufsize);

	return res;
}

static void socket_shutdown(tcptest_t *item)
{
	if (item->sslrunning) {
		SSL_shutdown(item->ssldata);
		SSL_free(item->ssldata);
		SSL_CTX_free(item->sslctx);
	}
	shutdown(item->fd, SHUT_RDWR);
}
#endif


void do_tcp_tests(int timeout, int concurrency)
{
	int		selres;
	fd_set		readfds, writefds;
	struct timeval	tmo, timestamp, cutoff;

	int		activesockets = 0; /* Number of allocated sockets */
	int		pending = 0;	   /* Total number of tests */
	tcptest_t	*nextinqueue;      /* Points to the next item to start testing */
	tcptest_t	*firstactive;      /* Points to the first item currently being tested */
					   /* Thus, active connections are between firstactive..nextinqueue */
	tcptest_t	*item;
	int		sockok;
	int		maxfd;
	int		res;
	socklen_t	connressize;
	char		msgbuf[4096];

	struct timezone tz;

	/* If timeout or concurrency are 0, set them to reasonable defaults */
	if (timeout == 0) timeout = 10;	/* seconds */
	if (concurrency == 0) {
		struct rlimit lim;

		concurrency = (FD_SETSIZE / 4);

		getrlimit(RLIMIT_NOFILE, &lim);
		if (lim.rlim_cur < concurrency) {
			concurrency = lim.rlim_cur - 10;
		}
	}
	if (concurrency > (FD_SETSIZE-10)) {
		concurrency = FD_SETSIZE - 10;	/* Allow a bit for stdin, stdout and such */
		errprintf("bbtest-net: concurrency reduced to FD_SETSIZE-10 (%d)\n", concurrency);
	}

	/* How many tests to do ? */
	for (item = thead; (item); item = item->next) pending++; 
	firstactive = nextinqueue = thead;
	dprintf("About to do %d TCP tests\n", pending);

	while (pending > 0) {
		/*
		 * First, see if we need to allocate new sockets and initiate connections.
		 */
		for (sockok=1; (sockok && nextinqueue && (activesockets < concurrency)); nextinqueue=nextinqueue->next) {

			/*
			 * We need to allocate a new socket that has O_NONBLOCK set.
			 */
			nextinqueue->fd = socket(PF_INET, SOCK_STREAM, 0);
			sockok = (nextinqueue->fd != -1);
			if (sockok) {
				res = fcntl(nextinqueue->fd, F_SETFL, O_NONBLOCK);
				if (res == 0) {
					/*
					 * Initiate the connection attempt ... 
					 */
					gettimeofday(&nextinqueue->timestart, &tz);
					res = connect(nextinqueue->fd, (struct sockaddr *)&nextinqueue->addr, sizeof(nextinqueue->addr));
					cutoff.tv_sec = nextinqueue->timestart.tv_sec + timeout + 1;
					cutoff.tv_usec = 0;

					/*
					 * Did it work ?
					 */
					if ((res == 0) || ((res == -1) && (errno == EINPROGRESS))) {
						/* This is OK - EINPROGRES and res=0 pick up status in select() */
						activesockets++;
						tcp_stats_connects++;
					}
					else if (res == -1) {
						/* connect() failed. Flag the item as "not open" */
						nextinqueue->connres = errno;
						nextinqueue->open = 0;
						nextinqueue->errcode = CONTEST_ENOCONN;
						close(nextinqueue->fd);
						nextinqueue->fd = -1;
						pending--;

						switch (nextinqueue->connres) {
						   /* These may happen if connection is refused immediately */
						   case ECONNREFUSED : break;
						   case EHOSTUNREACH : break;
						   case ENETUNREACH  : break;
						   case EHOSTDOWN    : break;

						   /* Not likely ... */
						   case ETIMEDOUT    : break;

						   /* These should not happen. */
						   case EBADF        : errprintf("connect returned EBADF!\n"); break;
						   case ENOTSOCK     : errprintf("connect returned ENOTSOCK!\n"); break;
						   case EADDRNOTAVAIL: errprintf("connect returned EADDRNOTAVAIL!\n"); break;
						   case EAFNOSUPPORT : errprintf("connect returned EAFNOSUPPORT!\n"); break;
						   case EISCONN      : errprintf("connect returned EISCONN!\n"); break;
						   case EADDRINUSE   : errprintf("connect returned EADDRINUSE!\n"); break;
						   case EFAULT       : errprintf("connect returned EFAULT!\n"); break;
						   case EALREADY     : errprintf("connect returned EALREADY!\n"); break;
						   default           : errprintf("connect returned %d, errno=%d\n", res, errno);
						}
					}
					else {
						/* Should NEVER happen. connect returns 0 or -1 */
						errprintf("Strange result from connect: %d, errno=%d\n", res, errno);
					}
				}
				else {
					/* Could net set to non-blocking mode! Hmmm ... */
					sockok = 0;
					errprintf("Cannot set O_NONBLOCK\n");
				}
			}
			else {
				/* Could not get a socket */
				switch (errno) {
				   case EPROTONOSUPPORT: errprintf("Cannot get socket - EPROTONOSUPPORT\n"); break;
				   case EAFNOSUPPORT   : errprintf("Cannot get socket - EAFNOSUPPORT\n"); break;
				   case EMFILE         : errprintf("Cannot get socket - EMFILE\n"); break;
				   case ENFILE         : errprintf("Cannot get socket - ENFILE\n"); break;
				   case EACCES         : errprintf("Cannot get socket - EACCESS\n"); break;
				   case ENOBUFS        : errprintf("Cannot get socket - ENOBUFS\n"); break;
				   case ENOMEM         : errprintf("Cannot get socket - ENOMEM\n"); break;
				   case EINVAL         : errprintf("Cannot get socket - EINVAL\n"); break;
				   default             : errprintf("Cannot get socket - errno=%d\n", errno); break;
				}
				errprintf("Try running with a lower --concurrency setting (currently: %d)\n", concurrency);
			}
		}

		/* Ready to go - we have a bunch of connections being established */
		dprintf("%d tests pending - %d active tests\n", pending, activesockets);

		/*
		 * Setup the FDSET's
		 */
		FD_ZERO(&readfds); FD_ZERO(&writefds); maxfd = -1;
		for (item=firstactive; (item != nextinqueue); item=item->next) {
			if (item->fd > -1) {
				/*
				 * WRITE events are used to signal that a
				 * connection is ready, or it has been refused.
				 * READ events are only interesting for sockets
				 * that have already been found to be open, and
				 * thus have the "readpending" flag set.
				 *
				 * So: On any given socket, we want either a 
				 * write-event or a read-event - never both.
				 */
				if (item->readpending)
					FD_SET(item->fd, &readfds);
				else 
					FD_SET(item->fd, &writefds);

				if (item->fd > maxfd) maxfd = item->fd;
			}
		}

		/*
		 * Wait for something to happen: connect, timeout, banner arrives ...
		 */
		gettimeofday(&timestamp, &tz);
		tvdiff(&timestamp, &cutoff, &tmo);
		if ((tmo.tv_sec < 0) || (tmo.tv_usec < 0)) {
			/*
			 * This is actually OK, and it does happen occasionally.
			 * It just means that we passed the cutoff-threshold.
			 * So set selres=0 (timeout) without doing the select,
			 * and we will act as correctly.
			 */
			dprintf("select timeout is < 0: %d.%06d (cutoff=%d.%06d, timestamp=%d.%06d)\n", 
					tmo.tv_sec, tmo.tv_usec,
					cutoff.tv_sec, cutoff.tv_usec,
					timestamp.tv_sec, timestamp.tv_usec);
			selres = 0;
		}
		else if (maxfd < 0) {
			errprintf("select - no active fd's found, but pending is %d\n", pending);
			selres = 0;
		}
		else {
			dprintf("Doing select\n");
			selres = select((maxfd+1), &readfds, &writefds, NULL, &tmo);
			dprintf("select returned %d\n", selres);
		}

		if (selres == -1) {
			int selerr = errno;

			/*
			 * select() failed - this is BAD!
			 */
			switch (selerr) {
			   case EINTR : errprintf("select failed - EINTR\n"); break;
			   case EBADF : errprintf("select failed - EBADF\n"); break;
			   case EINVAL: errprintf("select failed - EINVAL, maxfd=%d, tmo=%u.%06u\n", maxfd, 
						(unsigned int)tmo.tv_sec, (unsigned int)tmo.tv_usec); 
					break;
			   case ENOMEM: errprintf("select failed - ENOMEM\n"); break;
			   default    : errprintf("Unknown select() error %d\n", selerr); break;
			}

			/* Leave this mess ... */
			errprintf("Aborting TCP tests with %d tests pending\n", pending);
			return;
		}

		/* Fetch the timestamp so we can tell how long the connect took */
		gettimeofday(&timestamp, &tz);

		/* Now find out which connections had something happen to them */
		for (item=firstactive; (item != nextinqueue); item=item->next) {
			if (item->fd > -1) {		/* Only active sockets have this */
				if ((selres == 0) || (timestamp.tv_sec >= cutoff.tv_sec)) {
					/* 
					 * Timeout on all active connection attempts.
					 * Close all sockets.
					 */
					if (item->readpending) {
						/* Final read timeout - just shut this socket */
						socket_shutdown(item);
						item->errcode = CONTEST_ETIMEOUT;
					}
					else {
						/* Connection timeout */
						item->open = 0;
						item->errcode = CONTEST_ETIMEOUT;
					}
					get_totaltime(item, &timestamp);
					close(item->fd);
					item->fd = -1;
					activesockets--;
					pending--;
					if (item == firstactive) firstactive = item->next;
				}
				else {
					if (FD_ISSET(item->fd, &writefds)) {
						int do_talk = 1;
						unsigned char *outbuf = NULL;
						unsigned int outlen = 0;

						if (!item->open) {
							/*
							 * First time here.
							 *
							 * Active response on this socket - either OK, or 
							 * connection refused.
							 * We determine what happened by getting the SO_ERROR status.
							 * (cf. select_tut(2) manpage).
							 */
							connressize = sizeof(item->connres);
							res = getsockopt(item->fd, SOL_SOCKET, SO_ERROR, &item->connres, &connressize);
							item->open = (item->connres == 0);
							if (!item->open) item->errcode = CONTEST_ENOCONN;
							do_talk = item->open;
							get_connectiontime(item, &timestamp);
						}

						if (item->open && (item->svcinfo->flags & TCP_SSL)) {
							/* 
							 * Setup the SSL connection, if not done already.
							 *
							 * NB: This can be triggered many times, as setup_ssl()
							 * may need more data from the remote and return with
							 * item->sslrunning == SSLSETUP_PENDING
							 */
							if (item->sslrunning == SSLSETUP_PENDING) {
								setup_ssl(item);
								if (item->sslrunning == 1) {
									/*
									 * Update connectiontime to include
									 * time for SSL handshake.
									 */
									get_connectiontime(item, &timestamp);
								}
							}
							do_talk = (item->sslrunning == 1);
						}

						/*
						 * Connection succeeded - port is open, if SSL then the
						 * SSL handshake is complete. 
						 *
						 * If we have anything to send then send it.
						 * If we want the banner, set the "readpending" flag to initiate
						 * select() for read()'s.
						 */

						item->readpending = (do_talk && !item->silenttest && (item->svcinfo->flags & TCP_GET_BANNER));
						if (do_talk) {
							if (item->telnetnegotiate && item->telnetbuflen) {
								/*
								 * Return the telnet negotiate data response
								 */
								outbuf = item->telnetbuf;
								outlen = item->telnetbuflen;
							}
							else if (item->sendtxt && !item->silenttest) {
								outbuf = item->sendtxt;
								outlen = (item->sendlen ? item->sendlen : strlen(outbuf));
							}

							if (outbuf && outlen) {
								/*
								 * It may be that we cannot write all of the
								 * data we want to. Tough ... 
								 */
								res = socket_write(item, outbuf, outlen);
								tcp_stats_written += res;
								if (res == -1) {
									/* Write failed - this socket is done. */
									dprintf("write failed\n");
									item->readpending = 0;
									item->errcode = CONTEST_EIO;
								}
								else if (item->svcinfo->flags & TCP_HTTP) {
									/*
									 * HTTP tests require us to send the full buffer.
									 * So adjust sendtxt/sendlen accordingly.
									 * If no more to send, switch to read-mode.
									 */
									item->sendtxt += res;
									item->sendlen -= res;
									item->readpending = (item->sendlen == 0);
								}
							}
						}

						/* If closed and/or no bannergrabbing, shut down socket */
						if (item->sslrunning != SSLSETUP_PENDING) {
							if (!item->open || !item->readpending) {
								if (item->open) {
									socket_shutdown(item);
								}
								close(item->fd);
								get_totaltime(item, &timestamp);
								if (item->finalcallback) item->finalcallback(item->priv);
								item->fd = -1;
								activesockets--;
								pending--;
								if (item == firstactive) firstactive = item->next;
							}
						}
					}
					else if (FD_ISSET(item->fd, &readfds)) {
						/*
						 * Data ready to read on this socket. Grab the
						 * banner - we only do one read (need the socket
						 * for other tests), so if the banner takes more
						 * than one cycle to arrive, too bad!
						 */
						int wantmoredata = 0;
						int datadone = 0;

						/*
						 * We may be in the process of setting up an SSL connection
						 */
						if (item->sslrunning == SSLSETUP_PENDING) setup_ssl(item);
						if (item->sslrunning == SSLSETUP_PENDING) break;  /* Loop again waiting for more data */

						/*
						 * Connection is ready - plain or SSL. Read data.
						 */
						res = socket_read(item, msgbuf, sizeof(msgbuf)-1);
						tcp_stats_read += res;
						dprintf("read %d bytes from socket\n", res);

						if ((res > 0) && item->datacallback) {
							datadone = item->datacallback(msgbuf, res, item->priv);
						}

						if ((res > 0) && item->telnetnegotiate) {
							/*
							 * telnet data has telnet options first.
							 * We must negotiate the session before we
							 * get the banner.
							 */
							item->telnetbuf = item->banner;
							item->telnetbuflen = res;

							/*
							 * Safety measure: Dont loop forever doing
							 * telnet options.
							 * This puts a maximum on how many times
							 * we go here.
							 */
							item->telnetnegotiate--;
							if (!item->telnetnegotiate) {
								dprintf("Max. telnet negotiation (%d) reached for host %s\n", 
									MAX_TELNET_CYCLES,
									inet_ntoa(item->addr.sin_addr));
							}

							if (do_telnet_options(item)) {
								/* Still havent seen the session banner */
								item->banner = NULL;
								item->bannerbytes = 0;
								item->readpending = 0;
								wantmoredata = 1;
							}
							else {
								/* No more options - we have the banner */
								item->telnetnegotiate = 0;
							}
						}

						if ((item->svcinfo->flags & TCP_HTTP) && 
						    ((res > 0) || item->sslagain)     &&
						    (!datadone) ) {
							/*
							 * HTTP : Grab the entire response.
							 */
							wantmoredata = 1;
						}

						if (!wantmoredata) {
							if (item->open) {
								socket_shutdown(item);
							}
							item->readpending = 0;
							close(item->fd);
							get_totaltime(item, &timestamp);
							if (item->finalcallback) item->finalcallback(item->priv);
							item->fd = -1;
							activesockets--;
							pending--;
							if (item == firstactive) firstactive = item->next;
						}
					}
				}
			}
		}  /* end for loop */
	} /* end while (pending) */

	dprintf("TCP tests completed normally\n");
}


void show_tcp_test_results(void)
{
	tcptest_t *item;

	for (item = thead; (item); item = item->next) {
		printf("Address=%s:%d, open=%d, res=%d, err=%d, connecttime=%u.%06u, totaltime=%u.%06u, ",
				inet_ntoa(item->addr.sin_addr), 
				ntohs(item->addr.sin_port),
				item->open, item->connres, item->errcode,
				(unsigned int)item->duration.tv_sec, (unsigned int)item->duration.tv_usec,
				(unsigned int)item->totaltime.tv_sec, (unsigned int)item->totaltime.tv_usec);

		if (item->banner && (item->bannerbytes == strlen(item->banner))) {
			printf("banner='%s' (%d bytes)",
				textornull(item->banner),
				item->bannerbytes);
		}
		else {
			int i;
			unsigned char *p;

			for (i=0, p=item->banner; i < item->bannerbytes; i++, p++) {
				printf("%c", (isprint(*p) ? *p : '.'));
			}
		}

		if (item->certinfo) {
			printf(", certinfo='%s' (%u %s)", 
				item->certinfo, (unsigned int)item->certexpires,
				((item->certexpires > time(NULL)) ? "valid" : "expired"));
		}
		printf("\n");

		if ((item->svcinfo == &svcinfo_http) || (item->svcinfo == &svcinfo_https)) {
			http_data_t *httptest = (http_data_t *) item->priv;

			printf("httpstatus = %ld, open=%d, errcode=%d, parsestatus=%d\n",
				httptest->httpstatus, httptest->tcptest->open, httptest->tcptest->errcode, httptest->parsestatus);
			printf("Response:\n");
			if (httptest->headers) printf("%s\n", httptest->headers); else printf("(no headers)\n");
			if (httptest->contentcheck == CONTENTCHECK_DIGEST) printf("Content digest: %s\n", httptest->digest);
			if (httptest->output) printf("%s", httptest->output);
		}
	}
}

int tcp_got_expected(tcptest_t *test)
{
	if (test == NULL) return 1;

	if (test->svcinfo && test->svcinfo->exptext) {
		int compbytes; /* Number of bytes to compare */


		/* Did we get enough data? */
		if (test->banner == NULL) {
			dprintf("tcp_got_expected: No data in banner\n");
			return 0;
		}

		compbytes = (test->svcinfo->explen ? test->svcinfo->explen : strlen(test->svcinfo->exptext));
		if ((test->svcinfo->expofs + compbytes) > test->bannerbytes) {
			dprintf("tcp_got_expected: Not enough data\n");
			return 0;
		}

		return (memcmp(test->svcinfo->exptext+test->svcinfo->expofs, test->banner, compbytes) == 0);
	}
	else
		return 1;
}

#ifdef STANDALONE

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

int main(int argc, char *argv[])
{
	int argi;
	char *argp, *p;
	testitem_t *thead = NULL;
	int timeout = 0;
	int concurrency = 0;

	if (getenv("BBNETSVCS") == NULL) putenv("BBNETSVCS=");
	init_tcp_services();

	for (argi=1; (argi<argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strncmp(argv[argi], "--timeout=", 10) == 0) {
			p = strchr(argv[argi], '=');
			timeout = atoi(p+1);
			if (timeout < 0) timeout = 0;
		}
		else if (strncmp(argv[argi], "--concurrency=", 14) == 0) {
			p = strchr(argv[argi], '=');
			concurrency = atoi(p+1);
			if (concurrency < 0) concurrency = 0;
		}
		else {
			char *ip;
			char *port;
			char *testspec;

			argp = argv[argi]; ip = port = testspec = NULL;

			p = strchr(argp, '/');
			ip = argp;
			if (p) {
				*p = '\0'; argp = (p+1); 
				p = strchr(argp, '/');
				if (p) {
					port = argp; *p = '\0'; argp = (p+1);
				}
				else {
					port = "0";
				}
				testspec = argp;
			}

			if (ip && port && testspec) {
				if ( 	(strncmp(argp, "http", 4) == 0) ||
					(strncmp(argp, "cont;", 5) == 0) ||
					(strncmp(argp, "cont=", 5) == 0) ||
					(strncmp(argp, "post;", 5) == 0) ||
					(strncmp(argp, "post=", 5) == 0) ||
					(strncmp(argp, "nocont;", 7) == 0) ||
					(strncmp(argp, "nocont=", 7) == 0) ||
					(strncmp(argp, "nopost;", 7) == 0) ||
					(strncmp(argp, "nopost=", 7) == 0) ||
					(strncmp(argp, "type;", 5) == 0)   ||
					(strncmp(argp, "type=", 5) == 0) ) {

					testitem_t *testitem = calloc(1, sizeof(testitem_t));
					testedhost_t *hostitem = calloc(1, sizeof(testedhost_t));
					http_data_t *httptest;

					testitem->host = hostitem;
					testitem->testspec = testspec;
					strcpy(hostitem->ip, ip);
					add_url_to_dns_queue(testspec);
					add_http_test(testitem);

					testitem->next = NULL;
					thead = testitem;

					httptest = (http_data_t *)testitem->privdata;
					if (httptest && httptest->tcptest) {
						printf("TCP connection goes to %s:%d\n",
							inet_ntoa(httptest->tcptest->addr.sin_addr),
							ntohs(httptest->tcptest->addr.sin_port));
						printf("Request:\n%s\n", httptest->tcptest->sendtxt);
					}
				}
				else {
					add_tcp_test(ip, atoi(port), testspec, NULL, 0, NULL, NULL, NULL, NULL);
				}
			}
			else {
				printf("Invalid testspec '%s'\n", argv[argi]);
			}
		}
	}

	do_tcp_tests(timeout, concurrency);
	show_tcp_test_results();
	return 0;
}
#endif

