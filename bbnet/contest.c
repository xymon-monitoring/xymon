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

static char rcsid[] = "$Id: contest.c,v 1.43 2004-08-07 11:11:38 henrik Exp $";

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
#include "bbgen.h"
#include "debug.h"
#include "util.h"

/* BSD uses RLIMIT_OFILE */
#if defined(RLIMIT_OFILE) && !defined(RLIMIT_NOFILE)
#define RLIMIT_NOFILE RLIMIT_OFILE
#endif

#define MAX_BANNER 1024
#define MAX_TELNET_CYCLES 5		/* Max loops with telnet options before aborting banner */

#define SSLSETUP_PENDING -1		/* Magic value for test_t->sslrunning while handshaking */

static test_t *thead = NULL;

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

static void getescapestring(char *msg, unsigned char **buf, int *buflen, int *offset)
{
	char *inp, *outp;
	int outlen = 0;

	inp = msg;
	if (*inp != '\"') {
		errprintf("services.txt error - strings must be in quotes\n");
		return;
	}
	inp++; /* Skip the quote */

	outp = *buf = malloc(strlen(msg)+1);
	while (*inp && (*inp != '\"')) {
		if (*inp == '\\') {
			inp++;
			if (*inp == 'r') {
				*outp = '\r'; outlen++; inp++; outp++;
			}
			else if (*inp == 'n') {
				*outp = '\n'; outlen++; inp++; outp++;
			}
			else if (*inp == 't') {
				*outp = '\t'; outlen++; inp++; outp++;
			}
			else if (*inp == 'x') {
				inp++;
				if (isxdigit((int) *inp)) {
					*outp = hexvalue(*inp);
					inp++;

					if (isxdigit((int) *inp)) {
						*outp *= 16;
						*outp += hexvalue(*inp);
						inp++;
					}
				}
				else {
					errprintf("Invalid hex escape in service.txt\n");
				}
				outlen++; outp++;
			}
			else {
				errprintf("Unknown escape sequence in service.txt at: %s\n", inp);
			}
		}
		else {
			*outp = *inp;
			outlen++;
			inp++; outp++;
		}
	}
	*outp = '\0';
	*buflen = outlen;
}

char *init_tcp_services(void)
{
	char filename[MAX_PATH];
	FILE *fd;
	char buf[MAX_LINE_LEN];
	svclist_t *head = NULL;
	svclist_t *item = NULL;
	svclist_t *first, *walk;
	char *bbnetsvcs = NULL;
	char *searchstring;
	int svcnamebytes = 0;
	int svccount = 1;
	int i;

	sprintf(filename, "%s/etc/services.txt", getenv("BBHOME"));
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
				getescapestring(skipwhitespace(l+4), &item->rec->sendtxt, &item->rec->sendlen, NULL);
				for (walk = item; (walk != first); walk = walk->next) {
					walk->next->rec->sendtxt = item->rec->sendtxt;
					walk->next->rec->sendlen = item->rec->sendlen;
				}
			}
		}
		else if (strncmp(l, "expect ", 7) == 0) {
			if (item) {
				getescapestring(skipwhitespace(l+7), &item->rec->exptext, &item->rec->explen, &item->rec->expofs);
				for (walk = item; (walk != first); walk = walk->next) {
					walk->next->rec->exptext = item->rec->exptext;
					walk->next->rec->explen = item->rec->explen;
					walk->next->rec->expofs = item->rec->expofs;
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

test_t *add_tcp_test(char *ip, int port, char *service, int silent)
{
	test_t *newtest;
	int i;

	dprintf("Adding tcp test IP=%s, port=%d, service=%s, silent=%d\n", ip, port, service, silent);

	if (port == 0) {
		errprintf("Trying to scan port 0 for service %s\n", service);
		errprintf("You probably need to define the %s service in /etc/services\n", service);
		return NULL;
	}

	newtest = (test_t *) malloc(sizeof(test_t));

	memset(&newtest->addr, 0, sizeof(newtest->addr));
	newtest->addr.sin_family = PF_INET;
	newtest->addr.sin_port = htons(port);
	inet_aton(ip, (struct in_addr *) &newtest->addr.sin_addr.s_addr);

	newtest->fd = -1;
	newtest->open = 0;
	newtest->connres = -1;
	newtest->duration.tv_sec = newtest->duration.tv_usec = 0;

	for (i=0; (svcinfo[i].svcname && (strcmp(service, svcinfo[i].svcname) != 0)); i++) ;
	newtest->svcinfo = &svcinfo[i];

	newtest->silenttest = silent;
	newtest->readpending = 0;
	newtest->telnetnegotiate = (((svcinfo[i].flags & TCP_TELNET) && !silent) ? MAX_TELNET_CYCLES : 0);
	newtest->telnetbuf = NULL;
	newtest->telnetbuflen = 0;

	newtest->sslctx = NULL;
	newtest->ssldata = NULL;
	newtest->certinfo = NULL;
	newtest->certexpires = 0;
	newtest->sslrunning = ((svcinfo[i].flags & TCP_SSL) ? SSLSETUP_PENDING : 0);

	newtest->banner = NULL;
	newtest->bannerbytes = 0;
	newtest->next = thead;

	thead = newtest;
	return newtest;
}


static void get_connectiontime(test_t *item, struct timeval *timestamp)
{
	item->duration.tv_sec = timestamp->tv_sec - item->timestart.tv_sec;
	item->duration.tv_usec = timestamp->tv_usec - item->timestart.tv_usec;
	if (item->duration.tv_usec < 0) {
		item->duration.tv_sec--;
		item->duration.tv_usec += 1000000;
	}
}

static int do_telnet_options(test_t *item)
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
/*
 * Define stub routines for plain socket operations without SSL
 */
static void setup_ssl(test_t *item)
{
	dprintf("SSL service checked as simple TCP test - bbtest-net compiled without SSL\n");
	item->sslrunning = 0;
}

int socket_write(test_t *item, unsigned char *outbuf, int outlen)
{
	return write(item->fd, outbuf, outlen);
}

int socket_read(test_t *item, unsigned char *inbuf, int inbufsize)
{
	return read(item->fd, inbuf, inbufsize);
}

void socket_shutdown(test_t *item)
{
	shutdown(item->fd, SHUT_RDWR);
}

#else

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


static void setup_ssl(test_t *item)
{
	static int ssl_init_complete = 0;
	X509 *peercert;
	struct servent *sp;
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
				return;
			}
		}

		SSL_load_error_strings();
		SSL_library_init();
		ssl_init_complete = 1;
	}

	sp = getservbyport(item->addr.sin_port, "tcp");
	if (item->sslctx == NULL) {
		item->sslctx = SSL_CTX_new(SSLv23_client_method());
		if (!item->sslctx) {
			errprintf("Cannot create SSL context\n");
			item->sslrunning = 0;
			return;
		}

		/* Workaround SSL bugs */
		SSL_CTX_set_options(item->sslctx, SSL_OP_ALL);
		SSL_CTX_set_quiet_shutdown(item->sslctx, 1);
	}

	if (item->ssldata == NULL) {
		item->ssldata = SSL_new(item->sslctx);
		if (!item->ssldata) {
			errprintf("SSL_new failed\n");
			item->sslrunning = 0;
			SSL_CTX_free(item->sslctx);
			return;
		}

		if (SSL_set_fd(item->ssldata, item->fd) != 1) {
			errprintf("Could not initiate SSL on connection\n");
			item->sslrunning = 0;
			SSL_free(item->ssldata); SSL_CTX_free(item->sslctx);
			return;
		}
	}

	if ((err = SSL_connect(item->ssldata)) != 1) {
		switch (SSL_get_error (item->ssldata, err)) {
		  case SSL_ERROR_WANT_READ:
		  case SSL_ERROR_WANT_WRITE:
			item->sslrunning = SSLSETUP_PENDING;
			break;
		  case SSL_ERROR_SYSCALL:
			errprintf("IO error in SSL_connect to %s on host %s\n",
				  sp->s_name, inet_ntoa(item->addr.sin_addr));
			item->sslrunning = 0; SSL_free(item->ssldata); SSL_CTX_free(item->sslctx);
			break;
		  case SSL_ERROR_SSL:
			errprintf("Unspecified SSL error in SSL_connect to %s on host %s\n",
				  sp->s_name, inet_ntoa(item->addr.sin_addr));
			item->sslrunning = 0; SSL_free(item->ssldata); SSL_CTX_free(item->sslctx);
			break;
		  default:
			errprintf("Unknown error %d in SSL_connect to %s on host %s\n",
				  err, sp->s_name, inet_ntoa(item->addr.sin_addr));
			item->sslrunning = 0; SSL_free(item->ssldata); SSL_CTX_free(item->sslctx);
			break;
		}

		return;
	}

	/* If we get this far, the SSL handshake has completed. So grab the certificate */
	peercert = SSL_get_peer_certificate(item->ssldata);
	if (!peercert) {
		errprintf("Cannot get peer certificate for %s on host %s\n",
			  sp->s_name, inet_ntoa(item->addr.sin_addr));
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

int socket_write(test_t *item, char *outbuf, int outlen)
{
	int res = 0;

	if (item->sslrunning) {
		res = SSL_write(item->ssldata, outbuf, outlen);
	}
	else {
		res = write(item->fd, outbuf, outlen);
	}

	return res;
}

int socket_read(test_t *item, char *inbuf, int inbufsize)
{
	int res = 0;

	if (item->svcinfo->flags & TCP_SSL) {
		if (item->sslrunning) {
			res = SSL_read(item->ssldata, inbuf, inbufsize);
		}
		else {
			/* SSL setup failed - flag 0 bytes read. */
			res = 0;
		}
	}
	else res = read(item->fd, inbuf, inbufsize);

	return res;
}

void socket_shutdown(test_t *item)
{
	if (item->sslrunning) {
		SSL_shutdown(item->ssldata);
		SSL_free(item->ssldata);
		SSL_CTX_free(item->sslctx);
	}
	shutdown(item->fd, SHUT_RDWR);
}
#endif


void do_tcp_tests(int conntimeout, int concurrency)
{
	int		selres;
	fd_set		readfds, writefds;
	struct timeval	tmo, timestamp;

	int		activesockets = 0; /* Number of allocated sockets */
	int		pending = 0;	   /* Total number of tests */
	test_t		*nextinqueue;      /* Points to the next item to start testing */
	test_t		*firstactive;      /* Points to the first item currently being tested */
					   /* Thus, active connections are between firstactive..nextinqueue */
	test_t		*item;
	int		sockok;
	int		maxfd;
	int		res;
	socklen_t	connressize;
	char		msgbuf[MAX_BANNER];

	struct timezone tz;

	/* If conntimeout or concurrency are 0, set them to reasonable defaults */
	if (conntimeout == 0) conntimeout = DEF_TIMEOUT;
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
	dprintf("About to do  %d TCP tests\n", pending);

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

					/*
					 * Did it work ?
					 */
					if ((res == 0) || ((res == -1) && (errno == EINPROGRESS))) {
						/* This is OK - EINPROGRES and res=0 pick up status in select() */
						activesockets++;
					}
					else if (res == -1) {
						/* connect() failed. Flag the item as "not open" */
						nextinqueue->connres = errno;
						nextinqueue->open = 0;
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
		FD_ZERO(&readfds); FD_ZERO(&writefds); maxfd = 0;
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
		dprintf("Doing select\n");
		tmo.tv_sec = conntimeout; tmo.tv_usec = 0;
		selres = select((maxfd+1), &readfds, &writefds, NULL, &tmo);
		dprintf("select returned %d\n", selres);
		if (selres == -1) {
			int selerr = errno;

			/*
			 * select() failed - this is BAD!
			 */
			switch (selerr) {
			   case EBADF : errprintf("select failed - EBADF\n"); break;
			   case EINTR : errprintf("select failed - EINTR\n"); break;
			   case EINVAL: errprintf("select failed - EINVAL\n"); break;
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
				if (selres == 0) {
					/* 
					 * Timeout on all active connection attempts.
					 * Close all sockets.
					 */
					if (item->readpending) {
						/* Final read timeout - just shut this socket */
						socket_shutdown(item);
					}
					else {
						/* Connection timeout */
						item->open = 0;
						item->connres = ETIMEDOUT;
					}
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
							else if (item->svcinfo->sendtxt && !item->silenttest) {
								outbuf = item->svcinfo->sendtxt;
								outlen = (item->svcinfo->sendlen ? item->svcinfo->sendlen : strlen(outbuf));
							}

							if (outbuf && outlen) {
								/*
								 * It may be that we cannot write all of the
								 * data we want to. Tough ... 
								 */
								res = socket_write(item, outbuf, outlen);
								if (res == -1) {
									/* Write failed - this socket is done. */
									dprintf("write failed\n");
									item->readpending = 0;
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

						/*
						 * We may be in the process of setting up an SSL connection
						 */
						if (item->sslrunning == SSLSETUP_PENDING) setup_ssl(item);
						if (item->sslrunning == SSLSETUP_PENDING) break;  /* Loop again waiting for more data */

						/*
						 * Connection is ready - plain or SSL. Read data.
						 */
						res = socket_read(item, msgbuf, sizeof(msgbuf)-1);
						dprintf("read %d bytes from socket\n", res);

						if (res) {
							msgbuf[res] = '\0';
							if (item->banner == NULL) {
								item->banner = (unsigned char *)malloc(res+1);
								memcpy(item->banner, msgbuf, res+1);
								item->bannerbytes += res;
							}
							else {
								item->banner = (unsigned char *)realloc(item->banner, item->bannerbytes+res+1);
								memcpy(item->banner+item->bannerbytes, msgbuf, res+1);
								item->bannerbytes += res;
							}
						}

						if (res && item->telnetnegotiate) {
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

						if (!wantmoredata) {
							if (item->open) {
								socket_shutdown(item);
							}
							item->readpending = 0;
							close(item->fd);
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
	test_t *item;

	for (item = thead; (item); item = item->next) {
		printf("Address=%s:%d, open=%d, res=%d, time=%ld.%06ld, ",
				inet_ntoa(item->addr.sin_addr), 
				ntohs(item->addr.sin_port),
				item->open, item->connres, 
				item->duration.tv_sec, item->duration.tv_usec);

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
	}
}

int tcp_got_expected(test_t *test)
{
	if (test == NULL) return 1;

	if (test->banner && test->svcinfo && test->svcinfo->exptext) {
		int compbytes; /* Number of bytes to compare */

		compbytes = (test->svcinfo->explen ? test->svcinfo->explen : strlen(test->svcinfo->exptext));

		/* Did we get enough data? */
		if ((test->svcinfo->expofs + compbytes) > test->bannerbytes) {
			dprintf("tcp_got_expected: Not enough data");
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
	if ((argc > 1) && (strcmp(argv[1], "--debug") == 0)) debug = 1;

	add_tcp_test("172.16.10.100", 23, "telnet", 0);
	add_tcp_test("172.16.10.100", 22, "ssh", 0);
	add_tcp_test("172.16.10.1", 993, "imaps", 0);
	add_tcp_test("172.16.10.1", 995, "pop3s", 0);

	do_tcp_tests(0, 0);
	show_tcp_test_results();
	return 0;
}
#endif

