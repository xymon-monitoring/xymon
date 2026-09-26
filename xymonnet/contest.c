/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* This is used to implement the testing of a TCP service.                    */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include "config.h"

#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_SELECT_H
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

#include "libxymon.h"

#include "xymonnet.h"
#include "contest.h"
#include "httptest.h"
#include "dns.h"

/* BSD uses RLIMIT_OFILE */
#if defined(RLIMIT_OFILE) && !defined(RLIMIT_NOFILE)
#define RLIMIT_NOFILE RLIMIT_OFILE
#endif

#define MAX_TELNET_CYCLES 5		/* Max loops with telnet options before aborting banner */
#define SSLSETUP_PENDING -1		/* Magic value for tcptest_t->sslrunning while handshaking */
#define SLOWLIMSECS	  5		/* How long a socket may be inactive before deemed slow */

/* See http://www.openssl.org/docs/apps/ciphers.html for cipher strings */
char *ciphersmedium = "MEDIUM";	/* Must be formatted for openssl library */
char *ciphershigh = "HIGH";	/* Must be formatted for openssl library */

unsigned int tcp_stats_total    = 0;
unsigned int tcp_stats_http     = 0;
unsigned int tcp_stats_plain    = 0;
unsigned int tcp_stats_connects = 0;
unsigned long tcp_stats_read    = 0;
unsigned long tcp_stats_written = 0;
unsigned int warnbytesread = 0;

static tcptest_t *thead = NULL;

int shuffletests = 0;
int sslincludecipherlist = 1;
int sslshowallciphers = 0;
int snienabled = 0;	/* SNI disabled by default */

static svcinfo_t svcinfo_http  = { "http", NULL, 0, NULL, 0, 0, (TCP_GET_BANNER|TCP_HTTP), 80 };
static svcinfo_t svcinfo_https = { "https", NULL, 0, NULL, 0, 0, (TCP_GET_BANNER|TCP_HTTP|TCP_SSL), 443 };
static ssloptions_t default_sslopt = { NULL, SSLVERSION_DEFAULT, NULL, NULL };

static time_t sslcert_expiretime(char *timestr)
{
	int res;
	time_t t1, t2;
	struct tm *t;
	struct tm exptime;
	time_t gmtofs, result;

	memset(&exptime, 0, sizeof(exptime));

	/* expire date: 2004-01-02 08:04:15 GMT */
	res = sscanf(timestr, "%4d-%2d-%2d %2d:%2d:%2d", 
		     &exptime.tm_year, &exptime.tm_mon, &exptime.tm_mday,
		     &exptime.tm_hour, &exptime.tm_min, &exptime.tm_sec);
	if (res != 6) {
		errprintf("Cannot interpret certificate time %s\n", timestr);
		return 0;
	}

	/* tm_year is 1900 based; tm_mon is 0 based */
	exptime.tm_year -= 1900; exptime.tm_mon -= 1;
	result = mktime(&exptime);

	if (result > 0) {
		/* 
		 * Calculate the difference between localtime and GMT 
		 */
		t = gmtime(&result); t->tm_isdst = 0; t1 = mktime(t);
		t = localtime(&result); t->tm_isdst = 0; t2 = mktime(t);
		gmtofs = (t2-t1);

		result += gmtofs;
	}
	else {
		/*
		 * mktime failed - probably it expires after the
		 * Jan 19,2038 rollover for a 32-bit time_t.
		 */

		result = INT_MAX;
	}

	dbgprintf("Output says it expires: %s", timestr);
	dbgprintf("I think it expires at (localtime) %s\n", asctime(localtime(&result)));

	return result;
}


static int tcp_callback(unsigned char *buf, unsigned int len, void *priv)
{
	/*
	 * The default data callback function for simple TCP tests.
	 */

	tcptest_t *item = (tcptest_t *) priv;

	{
		/*
		 * Through a temporary, and checked: the length comes from a remote
		 * peer and a dialogue calls this for every reply, so a NULL return
		 * would both lose the buffer and be written through below. Dropping
		 * the addition keeps whatever was collected so far.
		 */
		unsigned char *grown = (item->banner == NULL)
			? (unsigned char *)malloc(len+1)
			: (unsigned char *)realloc(item->banner, item->bannerbytes+len+1);

		if (!grown) {
			errprintf("Out of memory collecting the banner for %s\n",
				  (item->svcinfo ? item->svcinfo->svcname : "?"));
			return 1;
		}
		item->banner = grown;
	}

	memcpy(item->banner+item->bannerbytes, buf, len);
	item->bannerbytes += len;
	*(item->banner + item->bannerbytes) = '\0';

	/*
	 * Every read of every dialogue is appended here, so this collects the
	 * whole of what the server said, not the greeting alone -- and it does so
	 * whether or not "options banner" is set. The return value only says the
	 * caller need not wait for more on this callback's account.
	 */
	return 1;
}


/*
 * A state name performs no I/O: it says where the steps after it belong, and
 * gives an edge something to aim at. The driver never stops on one, so every
 * place that moves the cursor steps over any it lands on.
 */
static svcstep_t *dlg_skip_labels(svcstep_t *st)
{
	while (st && (st->type == STEP_LABEL)) st = st->next;
	return st;
}

tcptest_t *add_tcp_test(char *ip, int port, char *service, ssloptions_t *sslopt,
			char *srcip,
			char *tspec, int silent, unsigned char *reqmsg, 
		     void *priv, f_callback_data datacallback, f_callback_final finalcallback)
{
	tcptest_t *newtest;

	dbgprintf("Adding tcp test IP=%s, port=%d, service=%s, silent=%d\n", textornull(ip), port, service, silent);

	if (port == 0) {
		errprintf("Trying to scan port 0 for service %s\n", service);
		errprintf("Give the %s service a 'port' line in protocols.cfg\n", service);
		return NULL;
	}

	tcp_stats_total++;
	newtest = (tcptest_t *) calloc(1, sizeof(tcptest_t));

	newtest->tspec = (tspec ? strdup(tspec) : NULL);
	newtest->fd = -1;
	newtest->lastactive = 0;
	newtest->bytesread = 0;
	newtest->byteswritten = 0;
	newtest->open = 0;
	newtest->connres = -1;
	newtest->errcode = CONTEST_ENOERROR;
	newtest->duration.tv_sec = newtest->duration.tv_nsec = 0;
	newtest->totaltime.tv_sec = newtest->totaltime.tv_nsec = 0;

	memset(&newtest->addr, 0, sizeof(newtest->addr));
	newtest->addr.sin_family = PF_INET;
	newtest->addr.sin_port = htons(port);
	if ((ip == NULL) || (strlen(ip) == 0) || (inet_aton(ip, (struct in_addr *) &newtest->addr.sin_addr.s_addr) == 0)) {
		newtest->errcode = CONTEST_EDNS;
	}

	newtest->srcaddr = (srcip ? strdup(srcip) : NULL);

	if (strcmp(service, "http") == 0) {
		newtest->svcinfo = &svcinfo_http;
		tcp_stats_http++;
	}
	else if (strcmp(service, "https") == 0) {
		newtest->svcinfo = &svcinfo_https;
		tcp_stats_http++;
	}
	else {
		newtest->svcinfo = find_tcp_service(service);
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
	newtest->certissuer = NULL;
	newtest->certexpires = 0;
	/* If ALPN is configured, SSL is also necessarily enabled */
	/*
	 * A refused definition speaks to nobody, TLS included: "options
	 * ssl,bogus" would otherwise send a ClientHello and report the SSL
	 * failure instead of the refusal.
	 */
	newtest->sslrunning = ((((newtest->svcinfo->flags & TCP_SSL) || (newtest->svcinfo->flags & TCP_ALPN)) &&
				!(newtest->svcinfo->flags & TCP_DIALOGUE_BROKEN)) ? SSLSETUP_PENDING : 0);
	newtest->sslagain = 0;
	newtest->sslwantwrite = 0;
	newtest->sendagain = 0;
	/*
	 * ONLY for a dialogue. The parser builds a step list for every service,
	 * including the plain send-and-match ones, so keying off steps alone
	 * gave legacy services a non-NULL cursor -- and the close guards below
	 * treat a live cursor as "the conversation is still going", so those
	 * sockets were never closed and every legacy TLS test timed out.
	 */
	/*
	 * A definition that was refused when the file was read runs nothing. It
	 * still connects -- "is the port open" is answerable and worth knowing --
	 * but performing half of a conversation nobody could parse would report
	 * on something the file never described, and the reason shown would be
	 * whatever that half happened to hit rather than the refusal itself.
	 */
	/*
	 * A silent test drives no steps: ":s" has always been a connect check.
	 * Skipping just the sends would leave the expects, and a quiet peer
	 * would go from green to a timeout.
	 */
	newtest->curstep = ((newtest->svcinfo && (newtest->svcinfo->flags & TCP_DIALOGUE) &&
			     !(newtest->svcinfo->flags & TCP_DIALOGUE_BROKEN) && !silent)
			    ? (void *)dlg_skip_labels(newtest->svcinfo->steps) : NULL);
	/*
	 * A state machine begins where "start" says, not at the first line of the
	 * file: the states may be written in whatever order reads best.
	 */
	if (newtest->curstep && newtest->svcinfo->startlabel) {
		svcstep_t *lb;

		for (lb = newtest->svcinfo->steps; (lb); lb = lb->next)
			if ((lb->type == STEP_LABEL) && lb->label &&
			    (strcmp(lb->label, newtest->svcinfo->startlabel) == 0)) break;
		if (lb) newtest->curstep = (void *)dlg_skip_labels(lb);
	}
	newtest->dialogfail = 0;
	newtest->dialogverdict = 0;
	newtest->failstep = NULL;
	newtest->stepbuf = NULL;
	newtest->stepbuflen = 0;

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


static void get_connectiontime(tcptest_t *item, struct timespec *timestamp)
{
	tvdiff(&item->timestart, timestamp, &item->duration);
}

static void get_totaltime(tcptest_t *item, struct timespec *timestamp)
{
	tvdiff(&item->timestart, timestamp, &item->totaltime);
}

static int do_telnet_options(tcptest_t *item)
{
	/*
	 * Handle telnet options.
	 *
	 * This code was taken from the sources for "netcat" version 1.10
	 * by "Xymon" <hobbit@avian.org>.
	 */

	unsigned char *obuf;
	int remain;
	unsigned char y;
	unsigned char *inp;
	unsigned char *outp;
	int result = 0;

	if (item->telnetbuflen == 0) {
		dbgprintf("Ignoring telnet option with length 0\n");
		return 0;
	}

	obuf = (unsigned char *)malloc(item->telnetbuflen);
	if (!obuf) {
		errprintf("Out of memory negotiating telnet options\n");
		item->telnetbuflen = 0;
		return 0;
	}
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
			/*
			 * Copy BEFORE freeing: telnetbuf aliases banner, so inp points
			 * into the very buffer being replaced -- freeing first leaves
			 * inp dangling and the copy reads freed memory. On failure the
			 * old banner is kept rather than left NULL with a length.
			 */
			{
				unsigned char *newbanner = (unsigned char *)strdup((char *)inp);

				if (!newbanner) {
					errprintf("Out of memory keeping the telnet banner\n");
				}
				else {
					if (item->banner) xfree(item->banner);
					item->banner = newbanner;
					item->bannerbytes = strlen((char *)newbanner);
				}
			}
			item->telnetbuf = NULL;
			item->telnetbuflen = 0;
			xfree(obuf);
			return 0;
		}
	        *outp = 255; outp++;
		inp++; remain--;
		/*
		 * RFC 854: WILL is answered DONT and DO is answered WONT. A WONT or
		 * DONT is the end of that option's negotiation and gets no answer --
		 * replying to one starts an exchange that never stops. This came
		 * from netcat, which answered all four.
		 */
		if (*inp == 251)			/* WILL */
			y = 254;			/* -> DON'T */
		if (*inp == 253)			/* DO */
			y = 252;			/* -> WON'T */
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
	xfree(obuf);
	return result;
}

#if TCP_SSL <= 0

char *ssl_library_version = NULL;

/*
 * Define stub routines for plain socket operations without SSL
 */
static void setup_ssl(tcptest_t *item)
{
	errprintf("SSL test, but xymonnet was built without SSL support\n");
	item->sslrunning = 0;
	item->errcode = CONTEST_ESSL;
}

static int socket_write(tcptest_t *item, unsigned char *outbuf, int outlen)
{
	int n = write(item->fd, outbuf, outlen);

	item->byteswritten += n;
	return n;
}

static int socket_read(tcptest_t *item, unsigned char *inbuf, int inbufsize)
{
	int n = read(item->fd, inbuf, inbufsize);
	item->bytesread += n;
	return n;
}

static void socket_shutdown(tcptest_t *item)
{
	shutdown(item->fd, SHUT_RDWR);

	if (warnbytesread && (item->bytesread > warnbytesread)) {
		if (item->tspec)
			errprintf("Huge response %u bytes from %s\n", item->bytesread, item->tspec);
		else
			errprintf("Huge response %u bytes for %s:%s\n",
				  item->bytesread, inet_ntoa(item->addr.sin_addr), item->svcinfo->svcname);
	}
}

#else

char *ssl_library_version = OPENSSL_VERSION_TEXT;

static int cert_password_cb(char *buf, int size, int rwflag, void *userdata)
{
	FILE *passfd;
	char *p;
	char passfn[PATH_MAX];
	char passphrase[1024];
	tcptest_t *item = (tcptest_t *)userdata;

	memset(passphrase, 0, sizeof(passphrase));

	/*
	 * Private key passphrases are stored in the file named same as the
	 * certificate itself, but with extension ".pass"
	 */
	sprintf(passfn, "%s/certs/%s", xgetenv("XYMONHOME"), item->ssloptions->clientcert);
	p = strrchr(passfn, '.'); if (p == NULL) p = passfn+strlen(passfn);
	strcpy(p, ".pass");

	passfd = fopen(passfn, "r");
	if (passfd && fgets(passphrase, sizeof(passphrase)-1, passfd)) {
		p = strchr(passphrase, '\n'); if (p) *p = '\0';
	}
	else
		*passphrase = '\0';
	if (passfd) fclose(passfd);

	strncpy(buf, passphrase, size);
	buf[size - 1] = '\0';

	/* Clear this buffer for security! Don't want passphrases in core dumps... */
	memset(passphrase, 0, sizeof(passphrase));

	return strlen(buf);
}

static char *xymon_ASN1_UTCTIME(ASN1_UTCTIME *tm)
{
	static char result[256];
	char *asn1_string;
	int gmt=0;
	int len, i;
	int century=0,year=0,month=0,day=0,hour=0,minute=0,second=0;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
	len=ASN1_STRING_length(tm);
	asn1_string=(char *)ASN1_STRING_get0_data(tm);
#else
	len=tm->length;
	asn1_string=(char *)tm->data;
#endif

	result[0] = '\0';
	if (len < 10) return result;
	if (asn1_string[len-1] == 'Z') gmt=1;
	for (i=0; i<len-1; i++) {
		if ((asn1_string[i] > '9') || (asn1_string[i] < '0')) return result;
	}

	if (len >= 15) { /* 20541024111745Z format */
		century = 100 * ((asn1_string[0]-'0')*10+(asn1_string[1]-'0'));
		asn1_string += 2;
	}

	year=(asn1_string[0]-'0')*10+(asn1_string[1]-'0');
	if (century == 0 && year < 50) year+=100;

	month=(asn1_string[2]-'0')*10+(asn1_string[3]-'0');
	if ((month > 12) || (month < 1)) return result;

	day=(asn1_string[4]-'0')*10+(asn1_string[5]-'0');
	hour=(asn1_string[6]-'0')*10+(asn1_string[7]-'0');
	minute=(asn1_string[8]-'0')*10+(asn1_string[9]-'0');
	if ( (asn1_string[10] >= '0') && (asn1_string[10] <= '9') &&
	     (asn1_string[11] >= '0') && (asn1_string[11] <= '9')) {
		second= (asn1_string[10]-'0')*10+(asn1_string[11]-'0');
	}

	sprintf(result, "%04d-%02d-%02d %02d:%02d:%02d %s",
		year+(century?century:1900), month, day, hour, minute, second, (gmt?"GMT":""));

	return result;
}


/*
 * Drop the SSL state of a handshake that never completed, clearing the
 * pointers with it: setup_ssl() reads a non-NULL ssldata as a live session
 * and reuses it, so freed-but-set is a use-after-free on the next call.
 *
 * No SSL_shutdown() -- nothing was established. socket_shutdown() handles
 * the completed case.
 */
static void discard_ssl_state(tcptest_t *item)
{
	if (item->ssldata) {
		SSL_free(item->ssldata);
		item->ssldata = NULL;
	}
	if (item->sslctx) {
		SSL_CTX_free(item->sslctx);
		item->sslctx = NULL;
	}
}

static void setup_ssl(tcptest_t *item)
{
	static int ssl_init_complete = 0;
	char portinfo[100];
	X509 *peercert;
	char *certcn, *certstart, *certend, *certissuer; const char *certsigalg;
	int err, keysz = 0;
	strbuffer_t *sslinfo;
	char msglin[2048];

	item->sslrunning = 1;

	if (!ssl_init_complete) {
		/* Setup entropy */
		if (RAND_status() != 1) {
			char path[PATH_MAX];	/* Path for the random file */

			/* load entropy from files */
			RAND_load_file(RAND_file_name(path, sizeof (path)), -1);

			/* shuffle $RANDFILE (or ~/.rnd if unset) */
			RAND_write_file(RAND_file_name(path, sizeof (path)));
			if (RAND_status() != 1) {
				errprintf("Failed to find enough entropy on your system");
				item->errcode = CONTEST_ESSL;
				/*
				 * sslrunning was set on the way in, and every caller reads
				 * it as "the session is up": start tls would advance past
				 * the upgrade and the I/O helpers would use a NULL ssldata.
				 */
				item->sslrunning = 0;
				return;
			}
		}

		SSL_load_error_strings();
		SSL_library_init();
		ssl_init_complete = 1;
	}

	if (item->sslctx == NULL) {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
		item->sslctx = SSL_CTX_new(TLS_client_method());
#else
		item->sslctx = SSL_CTX_new(SSLv23_client_method());
#endif

		switch (item->ssloptions->sslversion) {
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
		  case SSLVERSION_TLS13:
			SSL_CTX_set_min_proto_version(item->sslctx, TLS1_3_VERSION);
			SSL_CTX_set_max_proto_version(item->sslctx, TLS1_3_VERSION);
			break;
#endif
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
		  case SSLVERSION_TLS12:
			SSL_CTX_set_min_proto_version(item->sslctx, TLS1_2_VERSION);
			SSL_CTX_set_max_proto_version(item->sslctx, TLS1_2_VERSION);
			break;
		  case SSLVERSION_TLS11:
			SSL_CTX_set_min_proto_version(item->sslctx, TLS1_1_VERSION);
			SSL_CTX_set_max_proto_version(item->sslctx, TLS1_1_VERSION);
			break;
		  case SSLVERSION_TLS10:
			SSL_CTX_set_min_proto_version(item->sslctx, TLS1_VERSION);
			SSL_CTX_set_max_proto_version(item->sslctx, TLS1_VERSION);
			break;
#elif OPENSSL_VERSION_NUMBER >= 0x10001000L
		  case SSLVERSION_TLS12:
			SSL_CTX_set_options(item->sslctx, (SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3|SSL_OP_NO_TLSv1|SSL_OP_NO_TLSv1_1));
			break;
		  case SSLVERSION_TLS11:
			SSL_CTX_set_options(item->sslctx, (SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3|SSL_OP_NO_TLSv1|SSL_OP_NO_TLSv1_2));
			break;
		  case SSLVERSION_TLS10:
			SSL_CTX_set_options(item->sslctx, (SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3|SSL_OP_NO_TLSv1_1|SSL_OP_NO_TLSv1_2));
			break;
#ifdef HAVE_SSLV2_SUPPORT
		  case SSLVERSION_V2:
			SSL_CTX_set_options(item->sslctx, (SSL_OP_NO_SSLv3|SSL_OP_NO_TLSv1|SSL_OP_NO_TLSv1_1|SSL_OP_NO_TLSv1_2));
			break;
#endif
#ifdef HAVE_SSLV3_SUPPORT
		  case SSLVERSION_V3:
			SSL_CTX_set_options(item->sslctx, (SSL_OP_NO_SSLv2|SSL_OP_NO_TLSv1|SSL_OP_NO_TLSv1_1|SSL_OP_NO_TLSv1_2));
			break;
#endif
#else
		  case SSLVERSION_TLS10:
			SSL_CTX_set_options(item->sslctx, (SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3));
			break;
#ifdef HAVE_SSLV2_SUPPORT
		  case SSLVERSION_V2:
			SSL_CTX_set_options(item->sslctx, (SSL_OP_NO_SSLv3|SSL_OP_NO_TLSv1));
			break;
#endif
#ifdef HAVE_SSLV3_SUPPORT
		  case SSLVERSION_V3:
			SSL_CTX_set_options(item->sslctx, (SSL_OP_NO_SSLv2|SSL_OP_NO_TLSv1));
			break;
#endif
#endif
		  default:
			break;
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
		if (item->ssloptions->cipherlist && !SSL_CTX_set_cipher_list(item->sslctx, item->ssloptions->cipherlist)) {
			char sslerrmsg[256];

			ERR_error_string(ERR_get_error(), sslerrmsg);
			errprintf("Cannot set cipher list '%s' - IP %s, service %s: %s\n",
				   item->ssloptions->cipherlist, inet_ntoa(item->addr.sin_addr), item->svcinfo->svcname, sslerrmsg);
			item->sslrunning = 0;
			discard_ssl_state(item);
			item->errcode = CONTEST_ESSL;
			return;
		}

		/* Set ALPN protocols if specified */
		/* First check service definition for ALPN, then fallback to ssloptions */
		const char *alpn_protocols = NULL;
		if (item->svcinfo && item->svcinfo->alpns) {
			alpn_protocols = item->svcinfo->alpns;
		} else if (item->ssloptions && item->ssloptions->alpns) {
			alpn_protocols = item->ssloptions->alpns;
		}
		
		if (alpn_protocols) {
			char *alpn_copy = strdup(alpn_protocols);
			char *ptr, *token, *saveptr;
			unsigned char alpn_buffer[256];
			int offset = 0;
			
			/* Parse comma-separated protocol list */
			ptr = alpn_copy;
			while ((token = strtok_r(ptr, ",", &saveptr)) != NULL) {
				ptr = NULL;
				
				/* Remove leading/trailing whitespace */
				while (*token == ' ' || *token == '\t') token++;
				int len = strlen(token);
				while (len > 0 && (token[len-1] == ' ' || token[len-1] == '\t')) {
					token[len-1] = '\0';
					len--;
				}
				
				if (len > 0 && (offset + len + 1) < sizeof(alpn_buffer)) {
					alpn_buffer[offset] = len;
					memcpy(&alpn_buffer[offset + 1], token, len);
					offset += len + 1;
				}
			}
			
			if (offset > 0) {
				SSL_CTX_set_alpn_protos(item->sslctx, alpn_buffer, offset);
			}
			
			free(alpn_copy);
		}

		if (item->ssloptions->clientcert) {
			int status;
			char certfn[PATH_MAX];

			SSL_CTX_set_default_passwd_cb(item->sslctx, cert_password_cb);
			SSL_CTX_set_default_passwd_cb_userdata(item->sslctx, item);

			sprintf(certfn, "%s/certs/%s", xgetenv("XYMONHOME"), item->ssloptions->clientcert);
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
				discard_ssl_state(item);
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
			discard_ssl_state(item);
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
				discard_ssl_state(item);
				item->errcode = CONTEST_ESSL;
				return;
			}
		}


#if (SSLEAY_VERSION_NUMBER >= 0x00908070)
		if (item->sni) SSL_set_tlsext_host_name(item->ssldata, item->sni);
#endif

		/* SSL setup is done. Now attach the socket FD to the SSL protocol handler */
		if (SSL_set_fd(item->ssldata, item->fd) != 1) {
			char sslerrmsg[256];

			ERR_error_string(ERR_get_error(), sslerrmsg);
			errprintf("Could not initiate SSL on connection - IP %s, service %s: %s\n", 
				   inet_ntoa(item->addr.sin_addr), item->svcinfo->svcname, sslerrmsg);
			item->sslrunning = 0;
			discard_ssl_state(item);
			item->errcode = CONTEST_ESSL;
			return;
		}
	}

	/*
	 * Named from what we are testing, not from the host's /etc/services: the
	 * service name is what the operator wrote in protocols.cfg, and looking
	 * up the port there could disagree with it.
	 */
	snprintf(portinfo, sizeof(portinfo), "%s (%d/tcp)",
		 (item->svcinfo && item->svcinfo->svcname ? item->svcinfo->svcname : "?"),
		 item->addr.sin_port);
	if ((err = SSL_connect(item->ssldata)) != 1) {
		char sslerrmsg[256];
		int sslerr = SSL_get_error(item->ssldata, err);

		switch (sslerr) {
		  case SSL_ERROR_WANT_READ:
		  case SSL_ERROR_WANT_WRITE:
			/*
			 * Remember which way the handshake is blocked, so the
			 * select() below can wait for that instead of spinning:
			 * a connected socket is almost always writable, so a
			 * WANT_READ registered for writability returns from
			 * select() immediately, every time, until the peer
			 * finally answers.
			 */
			item->sslwantwrite = (sslerr == SSL_ERROR_WANT_WRITE);
			item->sslrunning = SSLSETUP_PENDING;
			break;
		  case SSL_ERROR_SYSCALL:
			ERR_error_string(ERR_get_error(), sslerrmsg);
			/* Filter out the bogus SSL error */
			if (strstr(sslerrmsg, "error:00000000:") == NULL) {
				errprintf("IO error in SSL_connect to %s on host %s: %s\n",
					  portinfo, inet_ntoa(item->addr.sin_addr), sslerrmsg);
			}
			item->errcode = CONTEST_ESSL;
			item->sslrunning = 0; discard_ssl_state(item);
			break;
		  case SSL_ERROR_SSL:
			ERR_error_string(ERR_get_error(), sslerrmsg);
			errprintf("Unspecified SSL error in SSL_connect to %s on host %s: %s\n",
				  portinfo, inet_ntoa(item->addr.sin_addr), sslerrmsg);
			item->errcode = CONTEST_ESSL;
			item->sslrunning = 0; discard_ssl_state(item);
			break;
		  default:
			ERR_error_string(ERR_get_error(), sslerrmsg);
			errprintf("Unknown error %d in SSL_connect to %s on host %s: %s\n",
				  err, portinfo, inet_ntoa(item->addr.sin_addr), sslerrmsg);
			item->errcode = CONTEST_ESSL;
			item->sslrunning = 0; discard_ssl_state(item);
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
		item->sslrunning = 0; discard_ssl_state(item);
		return;
	}

	sslinfo = newstrbuffer(0);

	certcn = X509_NAME_oneline(X509_get_subject_name(peercert), NULL, 0);
	certissuer = X509_NAME_oneline(X509_get_issuer_name(peercert), NULL, 0);
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	certsigalg = OBJ_nid2ln(OBJ_obj2nid(peercert->sig_alg->algorithm));
#else
	certsigalg = OBJ_nid2ln(X509_get_signature_nid(peercert));
#endif
	certstart = strdup(xymon_ASN1_UTCTIME(X509_get_notBefore(peercert)));
	certend = strdup(xymon_ASN1_UTCTIME(X509_get_notAfter(peercert)));
	{
		BIO *o = BIO_new(BIO_s_mem());
		long slen;
		char *sdata, *keyline;

#if OPENSSL_VERSION_NUMBER < 0x0090700fL
		X509_NAME_print_ex(o, X509_get_subject_name(peercert), 8, XN_FLAG_COMPAT);
#else
		X509_print_ex(o, peercert, XN_FLAG_COMPAT, X509_FLAG_COMPAT);
#endif

		slen = BIO_get_mem_data(o, &sdata);
		if (slen > 0) {
			keyline = strstr(sdata, " Public-Key:");
			if (!keyline) keyline = strstr(sdata, " Public Key:");
			if (keyline) {
				keyline = strchr(keyline, '(');
				if (keyline) keysz = atoi(keyline+1);
			}
		}

		BIO_set_close(o, BIO_CLOSE);
		BIO_free(o);
	}

	snprintf(msglin, sizeof(msglin),
		"Server certificate:\n\tsubject:%s\n\tstart date: %s\n\texpire date:%s\n\tkey size:%d\n\tissuer:%s\n\tsignature algorithm: %s\n", 
		certcn, certstart, certend, keysz, certissuer, certsigalg);
	addtobuffer(sslinfo, msglin);
	item->certsubject = strdup(certcn);
	item->certissuer = strdup(certissuer);
	item->certexpires = sslcert_expiretime(certend);
	item->certkeysz = keysz;
	xfree(certcn); xfree(certstart); xfree(certend); xfree(certissuer);
	X509_free(peercert);

	/* We list the available ciphers in the SSL cert data */
	if (sslincludecipherlist) {
		int b1, b2;
		b1 = SSL_get_cipher_bits(item->ssldata, &b2);
		certsigalg = OBJ_nid2ln(X509_get_signature_type(peercert));
		snprintf(msglin, sizeof(msglin), "\nCipher used: %s (%d bits)\n", SSL_get_cipher_name(item->ssldata), b1);
		addtobuffer(sslinfo, msglin);
		item->mincipherbits = b1; 

		if (sslshowallciphers) {
			int i;
			STACK_OF(SSL_CIPHER) *sk;

			addtobuffer(sslinfo, "\nAvailable ciphers:\n");
			sk = SSL_get_ciphers(item->ssldata);
			for (i=0; i<sk_SSL_CIPHER_num(sk); i++) {
				int b1, b2;

				b1 = SSL_CIPHER_get_bits(sk_SSL_CIPHER_value(sk,i), &b2);
				snprintf(msglin, sizeof(msglin), "Cipher %d: %s (%d bits)\n", i, SSL_CIPHER_get_name(sk_SSL_CIPHER_value(sk,i)), b1);
				addtobuffer(sslinfo, msglin);

				if ((item->mincipherbits == 0) || (b1 < item->mincipherbits)) item->mincipherbits = b1;
			}
		}		
	}

	item->certinfo = grabstrbuffer(sslinfo);
}

static int socket_write(tcptest_t *item, char *outbuf, int outlen)
{
	int res = 0;

	item->sendagain = 0;
	item->sslwantread = 0;
	if (item->sslrunning) {
		res = SSL_write(item->ssldata, outbuf, outlen);
		if (res < 0) {
			switch (SSL_get_error (item->ssldata, res)) {
			  case SSL_ERROR_WANT_WRITE:
				  /*
				   * Nothing was sent -- and nothing reached the
				   * kernel, so this is not something TCP will
				   * retransmit: the bytes were never handed to it.
				   * Only calling SSL_write() again with the SAME
				   * buffer sends them. Returning 0 alone looks like
				   * "there was nothing to send", so flag it.
				   * socket_read() uses sslagain the same way.
				   *
				   * Only WANT_WRITE is retried. Clearing
				   * readpending puts the socket back in writefds,
				   * which is exactly what WANT_WRITE waits for.
				   */
				  item->sendagain = 1;
				  res = 0;
				  break;
			  case SSL_ERROR_WANT_READ:
				  /*
				   * The mirror of WANT_WRITE: nothing was sent, and the
				   * retry needs the socket READABLE. Flagged so select()
				   * waits for that and the dispatch still runs the write.
				   */
				  item->sendagain = 1;
				  item->sslwantread = 1;
				  res = 0;
				  break;
			}
		}
	}
	else {
		res = write(item->fd, outbuf, outlen);
	}

	item->byteswritten += res;
	return res;
}

static int socket_read(tcptest_t *item, char *inbuf, int inbufsize)
{
	int res = 0;
	char errtxt[1024];

	if ((item->svcinfo->flags & TCP_SSL) || item->sslrunning) {
		if (item->sslrunning) {
			item->sslagain = 0;
			res = SSL_read(item->ssldata, inbuf, inbufsize);
			if (res < 0) {
				switch (SSL_get_error (item->ssldata, res)) {
				  case SSL_ERROR_WANT_READ:
					  item->sslagain = 1;
					  item->sslwantwrite = 0;
					  break;
				  case SSL_ERROR_WANT_WRITE:
					  /*
					   * A read can need the socket WRITABLE -- renegotiation
					   * does. Waiting for readability instead stalls until
					   * the timeout, the same fault the handshake had.
					   */
					  item->sslagain = 1;
					  item->sslwantwrite = 1;
					  break;
				  default:
					  ERR_error_string(ERR_get_error(), errtxt);
					  dbgprintf("SSL read error %s\n", errtxt);
					  break;
				}
			}
		}
		else {
			/* SSL setup failed - flag 0 bytes read. */
			res = 0;
		}
	}
	else {
		res = read(item->fd, inbuf, inbufsize);
		if (res < 0) {
			dbgprintf("Read error %s\n", strerror(errno));
		}
	}

	if (res > 0) item->bytesread += res;
	return res;
}

static void socket_shutdown(tcptest_t *item)
{
	/*
	 * The inner NULL checks are needed because SSLSETUP_PENDING is -1, so
	 * sslrunning is already true from add_tcp_test() while ssldata is still
	 * NULL -- a connection that times out before setup_ssl() runs would
	 * otherwise reach SSL_shutdown(NULL). Clearing them makes a second call
	 * harmless either way.
	 *
	 * sslrunning stays the outer test because a handshake that never
	 * completed has nothing to shut down -- not to avoid a double free:
	 * setup_ssl()'s failure paths clear what they free.
	 */
	if (item->sslrunning) {
		if (item->ssldata) {
			SSL_shutdown(item->ssldata);
			SSL_free(item->ssldata);
			item->ssldata = NULL;
		}
		if (item->sslctx) {
			SSL_CTX_free(item->sslctx);
			item->sslctx = NULL;
		}
	}
	shutdown(item->fd, SHUT_RDWR);

	if (warnbytesread && (item->bytesread > warnbytesread)) {
		if (item->tspec)
			errprintf("Huge response %u bytes from %s\n", item->bytesread, item->tspec);
		else
			errprintf("Huge response %u bytes for %s:%s\n",
				  item->bytesread, inet_ntoa(item->addr.sin_addr), item->svcinfo->svcname);
	}
}
#endif


static int tcptest_compare(void **a, void **b)
{
	tcptest_t **tcpa = (tcptest_t **)a;
	tcptest_t **tcpb = (tcptest_t **)b;

	if ((*tcpa)->randomizer < (*tcpb)->randomizer) return -1;
	else if ((*tcpa)->randomizer > (*tcpb)->randomizer) return 1;
	else return 0;
}
static void * tcptest_getnext(void *a)
{
	return ((tcptest_t *)a)->next;
}
static void tcptest_setnext(void *a, void *newval)
{
	((tcptest_t *)a)->next = (tcptest_t *)newval;
}


/*
 * Does this line end the reply?
 *
 * "until" is a literal, written "250 " for SMTP: the space separates the last
 * line from a "250-" continuation. But RFC 5321 4.2 puts that space before
 * the TEXT, and a server with nothing to add sends "250" alone -- so a
 * terminator ending in a space also matches at end of line. "250-" still
 * does not, which is the distinction the space was carrying.
 */
static int until_matches(unsigned char *line, int linelen, unsigned char *until, int untillen)
{
	if ((linelen >= untillen) && (memcmp(line, until, untillen) == 0)) return 1;

	if ((untillen > 1) && (until[untillen-1] == ' ') && (linelen >= (untillen-1)) &&
	    (memcmp(line, until, untillen-1) == 0)) {
		unsigned char next = (linelen > (untillen-1)) ? line[untillen-1] : '\n';

		return ((next == '\r') || (next == '\n'));
	}

	return 0;
}

void do_tcp_tests(int timeout, int concurrency)
{
	int		selres;
	fd_set		readfds, writefds;
	struct timespec	timestamp;
	int 		absmaxconcurrency;

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

	struct rlimit lim;

	/* If timeout or concurrency are 0, set them to reasonable defaults */
	if (timeout == 0) timeout = 10;	/* seconds */

	/* 
	 * Decide how many tests to run in parallel.
	 * If no --concurrency set by user, default to (FD_SETSIZE / 4) - typically 256.
	 * But never go above the ressource limit that is set, or above FD_SETSIZE.
	 * And we save 10 fd's for stdio, libs etc.
	 */
	absmaxconcurrency = (FD_SETSIZE - 10);
	getrlimit(RLIMIT_NOFILE, &lim); 
	if ((lim.rlim_cur > 10) && ((lim.rlim_cur - 10) < absmaxconcurrency)) absmaxconcurrency = (lim.rlim_cur - 10);

	if (concurrency == 0) concurrency = (FD_SETSIZE / 4);
	if (concurrency > absmaxconcurrency) concurrency = absmaxconcurrency;

	dbgprintf("Concurrency evaluation: rlim_cur=%lu, FD_SETSIZE=%d, absmax=%d, initial=%d\n", 
		  lim.rlim_cur, FD_SETSIZE, absmaxconcurrency, concurrency);

	if (shuffletests) {
		struct timeval tv;
		struct timezone tz;
		gettimeofday(&tv, &tz);
		srandom(tv.tv_usec);
	}

	/* How many tests to do ? */
	for (item = thead; (item); item = item->next) {
		if (shuffletests) item->randomizer = random();
		pending++; 
	}
	if (shuffletests) thead = msort(thead, tcptest_compare, tcptest_getnext, tcptest_setnext);

	firstactive = nextinqueue = thead;
	dbgprintf("About to do %d TCP tests running %d in parallel, abs.max %d\n", 
		  pending, concurrency, absmaxconcurrency);

	while (pending > 0) {
		int slowrunning, cclimit;
		time_t slowtimestamp = gettimer() - SLOWLIMSECS;

		/*
		 * First, see if we need to allocate new sockets and initiate connections.
		 */

		/*
		 * We start by counting the number of tests where the latest activity
		 * happened more than SLOWLIMSECS seconds ago. These are ignored when counting
		 * how many more tests we can start concurrenly. But never exceed the absolute 
		 * max. number of concurrently open sockets possible.
		 */
		for (item=firstactive, slowrunning = 0; (item != nextinqueue); item=item->next) {
			if ((item->fd > -1) && (item->lastactive < slowtimestamp)) slowrunning++;
		}
		cclimit = concurrency + slowrunning; 
		if (cclimit > absmaxconcurrency) cclimit = absmaxconcurrency;

		sockok = 1;
		while (sockok && nextinqueue && (activesockets < cclimit)) {
			/*
			 * We need to allocate a new socket that has O_NONBLOCK set.
			 */
			nextinqueue->fd = socket(PF_INET, SOCK_STREAM, 0);
			sockok = (nextinqueue->fd != -1);
			if (sockok) {
				/* Set the source address */
				if (nextinqueue->srcaddr) {
					struct sockaddr_in src;
					int isip;

					memset(&src, 0, sizeof(src));
					src.sin_family = PF_INET;
					src.sin_port = 0;
					isip = (inet_aton(nextinqueue->srcaddr, (struct in_addr *) &src.sin_addr.s_addr) != 0);

					if (!isip) {
						char *envaddr = getenv(nextinqueue->srcaddr);
						isip = (envaddr && (inet_aton(envaddr, (struct in_addr *) &src.sin_addr.s_addr) != 0));
					}

					if (isip) {
						res = bind(nextinqueue->fd, (struct sockaddr *)&src, sizeof(src));
						if (res != 0) errprintf("WARNING: Could not bind to source IP %s for test %s: %s\n",
								nextinqueue->srcaddr, nextinqueue->tspec, strerror(errno));
					}
					else {
						errprintf("WARNING: Invalid source IP %s for test %s, using default\n",
								nextinqueue->srcaddr, nextinqueue->tspec);
					}
				}

				res = fcntl(nextinqueue->fd, F_SETFL, O_NONBLOCK);

				if (res == 0) {
					/*
					 * Initiate the connection attempt ... 
					 */
					getntimer(&nextinqueue->timestart);
					nextinqueue->lastactive = nextinqueue->timestart.tv_sec;
					nextinqueue->cutoff = nextinqueue->timestart.tv_sec + timeout + 1;
					res = connect(nextinqueue->fd, (struct sockaddr *)&nextinqueue->addr, sizeof(nextinqueue->addr));

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
						   default           : errprintf("connect returned %d for test %s, errno=%d, %s\n", res, nextinqueue->tspec, errno, strerror(errno));
						}
					}
					else {
						/* Should NEVER happen. connect returns 0 or -1 */
						errprintf("Strange result from connect: %d, for test %s, errno=%d, %s\n", res, nextinqueue->tspec, errno, strerror(errno));
					}
				}
				else {
					/* Could net set to non-blocking mode! Hmmm ... */
					sockok = 0;
					errprintf("Cannot set O_NONBLOCK\n");
				}

				nextinqueue=nextinqueue->next;
			}
			else {
				int newconcurrency = ((activesockets > 5) ? (activesockets-1) : 5);

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

				if (newconcurrency != concurrency) {
					errprintf("Reducing --concurrency setting from %d to %d\n", 
							concurrency, newconcurrency);
					concurrency = newconcurrency;
				}
			}
		}

		/* Ready to go - we have a bunch of connections being established */
		dbgprintf("%d tests pending - %d active tests, %d slow tests\n", 
			  pending, activesockets, slowrunning);

restartselect:
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
				if (item->open && (item->sslrunning == 1) && item->sslagain && item->sslwantwrite) {
					/* A read that returned WANT_WRITE waits for writability. */
					FD_SET(item->fd, &writefds);
				}
				else if (item->open && (item->sslrunning == 1) && item->sendagain && item->sslwantread) {
					/* And a write that returned WANT_READ waits for readability. */
					FD_SET(item->fd, &readfds);
				}
				else if (item->open && (item->sslrunning == SSLSETUP_PENDING)) {
					/*
					 * Mid-handshake: readpending is still 0 (do_talk
					 * is false until the handshake completes), so the
					 * plain rule below would ask for writability while
					 * SSL_connect() is waiting to read. Wait for what
					 * it asked for instead. Gated on item->open: before
					 * the connection completes, writability is still
					 * how completion is detected.
					 */
					FD_SET(item->fd, (item->sslwantwrite ? &writefds : &readfds));
				}
				else if (item->readpending)
					FD_SET(item->fd, &readfds);
				else 
					FD_SET(item->fd, &writefds);

				if (item->fd > maxfd) maxfd = item->fd;
			}
		}

		if (maxfd == -1) {
			/* No active connections */
			if (activesockets == 0) {
				/* This can happen, if we get an immediate CONNREFUSED on all connections. */
				continue;
			}
			else {
				errprintf("contest logic error: No FD's, active=%d, pending=%d\n",
					  activesockets, pending);
				continue;
			}
		}
				
		/*
		 * Wait for something to happen: connect, timeout, banner arrives ...
		 */
		if (maxfd < 0) {
			errprintf("select - no active fd's found, but pending is %d\n", pending);
			selres = 0;
		}
		else {
			struct timeval tmo = { 1, 0 };
			dbgprintf("Doing select with maxfd=%d\n", maxfd);
			selres = select((maxfd+1), &readfds, &writefds, NULL, &tmo);
			dbgprintf("select returned %d\n", selres);
		}

		if (selres == -1) {
			int selerr = errno;

			/*
			 * select() failed - this is BAD!
			 */
			switch (selerr) {
			   case EINTR : errprintf("select failed - EINTR\n"); goto restartselect;
			   case EBADF : errprintf("select failed - EBADF\n"); break;
			   case EINVAL: errprintf("select failed - EINVAL\n"); break;
			   case ENOMEM: errprintf("select failed - ENOMEM\n"); break;
			   default    : errprintf("Unknown select() error %d\n", selerr); break;
			}

			/* Leave this mess ... */
			errprintf("Aborting TCP tests with %d tests pending\n", pending);
			return;
		}

		/* selres == 0 (timeout) isn't special - just go through the list of active tests */

		/* Fetch the timestamp so we can tell how long the connect took */
		getntimer(&timestamp);

		/* Now find out which connections had something happen to them */
		for (item=firstactive; (item != nextinqueue); item=item->next) {
			if (item->fd > -1) {		/* Only active sockets have this */
				if (timestamp.tv_sec > item->cutoff) {
					/* 
					 * Request timed out.
					 */
					if (!item->readpending && !item->sendagain) {
						/* Connection timeout */
						item->open = 0;
					}
					/*
					 * Either way, hand the socket to socket_shutdown(): it frees a session
					 * that handshaked (setup_ssl() disposes of the ones that failed) and
					 * no-ops on one that never ran. The !readpending arm used to skip it,
					 * leaving the SSL objects allocated on a timed-out handshake.
					 */
					socket_shutdown(item);
					item->errcode = CONTEST_ETIMEOUT;
					get_totaltime(item, &timestamp);
					close(item->fd);
					item->fd = -1;
					activesockets--;
					pending--;
					if (item == firstactive) firstactive = item->next;
				}
				else {
					/*
					 * An SSL read that returned WANT_WRITE is registered for
					 * writability, but it is still a READ that has to be
					 * retried -- sending it to the write arm leaves the
					 * expect current and the socket spinning until timeout.
					 * The same the other way for a write that wants a read.
					 */
					if ((FD_ISSET(item->fd, &writefds) &&
					     !(item->sslagain && item->sslwantwrite)) ||
					    (FD_ISSET(item->fd, &readfds) &&
					     item->sendagain && item->sslwantread)) {
						int do_talk = 1;
						unsigned char *outbuf = NULL;
						unsigned int outlen = 0;

						item->lastactive = timestamp.tv_sec;

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
								else if (item->sslrunning == 0) {
									/*
									 * Handshake failed. curstep left set holds
									 * the close guard below open, so the dead
									 * connection would be reported as a timeout
									 * instead of as the failure setup_ssl() saw.
									 */
									item->dialogfail = 1;
									if (!item->failstep) item->failstep = item->curstep;
									item->curstep = NULL;
									item->readpending = 0;
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
						 * NB: We want the banner EITHER if the GET_BANNER flag is set,
						 *     OR if we need it to match the expect string in the servicedef.
						 */

						item->readpending = (do_talk && !item->silenttest && 
							( (item->svcinfo->flags & TCP_GET_BANNER) || item->svcinfo->exptext ));
						/* Refusals owed go out before any step: the peer is
						 * waiting for them, and the next step for the peer. */
						if (do_talk && (item->svcinfo->flags & TCP_DIALOGUE) &&
						    (item->iacreplylen > 0)) {
							res = socket_write(item, (char *)item->iacreply,
									   item->iacreplylen);
							if (res > 0) {
								tcp_stats_written += res;
								if (res >= item->iacreplylen) item->iacreplylen = 0;
								else {
									memmove(item->iacreply, item->iacreply + res,
										item->iacreplylen - res);
									item->iacreplylen -= res;
								}
							}
							else if (res == -1) {
								dbgprintf("telnet refusal write failed\n");
								item->errcode = CONTEST_EIO;
								item->curstep = NULL;
								item->iacreplylen = 0;
							}

							/*
							 * Back to whatever the steps want next. Forcing a
							 * read here deadlocks an entry whose expect has
							 * already matched: its next send would wait for a
							 * reply the peer only makes after receiving it.
							 */
							if (item->iacreplylen == 0) {
								svcstep_t *nst = (svcstep_t *)item->curstep;

								item->readpending = (nst &&
										     ((nst->type == STEP_EXPECT) ||
										      (nst->type == STEP_STARTIAC)));
							}
						}
						else if (do_talk && (item->svcinfo->flags & TCP_DIALOGUE)) {
							/*
							 * A dialogue drives itself from where it stands:
							 * a SEND step is written here, an EXPECT step is
							 * waited for by leaving readpending set. That one
							 * rule is the whole state machine -- the current
							 * step decides which fd set the socket joins.
							 */
							svcstep_t *st = (svcstep_t *)item->curstep;

							while (st && (st->type == STEP_SEND) && item->silenttest) {
								/*
								 * A silent test says nothing on the wire. Step
								 * over the sends rather than sitting on one:
								 * leaving it current would strand the dialogue
								 * with a step it will never perform.
								 */
								st = dlg_skip_labels(st->next);
								item->curstep = (void *)st;
							}
							if (st && (st->type == STEP_STARTTLS)) {
								/*
								 * Upgrade in place, through the same setup_ssl() that "options ssl"
								 * uses at connect, so the certificate reaches the sslcert column. For
								 * SMTP on 25 and submission on 587 this is the only way to reach one.
								 *
								 * The handshake takes several passes: the step stays current until it
								 * settles, and select() waits for whichever direction it asked for.
								 */
								if (item->sslrunning == 0) {
									/*
									 * Discard what was read in the clear, or a
									 * line injected before the handshake is
									 * matched by a step that runs after it --
									 * the STARTTLS injection of CVE-2011-0411.
									 * RFC 3207 4.2 requires it here, and a real
									 * server has nothing pending anyway.
									 */
									item->stepbuflen = 0;
									if (item->stepbuf) item->stepbuf[0] = '\0';

									item->sslrunning = SSLSETUP_PENDING;
									setup_ssl(item);
								}
								else if (item->sslrunning == SSLSETUP_PENDING) {
									/*
									 * Resume it: a WANT_WRITE handshake comes
									 * back to this arm, and the other caller of
									 * setup_ssl() here is gated on TCP_SSL,
									 * which an upgraded service is not.
									 */
									setup_ssl(item);
								}
								if (item->sslrunning == 1) {
									st = dlg_skip_labels(st->next);
									item->curstep = (void *)st;
								}
								else if (item->sslrunning != SSLSETUP_PENDING) {
									/* setup_ssl() has set errcode */
									item->curstep = NULL;
									st = NULL;
								}
							}

							if (st && (st->type == STEP_SEND) && (item->stepbuflen > 0)) {
								/*
								 * Unread bytes are here BEFORE this command goes out, so they
								 * cannot be its reply -- a peer can preload what our next step
								 * is looking for. Checked before the write, not after: the next
								 * step of a custom entry may be an AUTH or anything else that
								 * changes state, and it must not reach a peer already shown to
								 * be misbehaving.
								 *
								 * Failing rather than discarding: unread bytes also mean the
								 * previous reply was not fully consumed, which is what a missing
								 * "until" looks like. Dropping them quietly would hide the config
								 * error the terminator exists for.
								 */
								item->dialogfail = 1;
								item->stalereply = 1;
								if (!item->failstep) item->failstep = (void *)st;
								item->curstep = NULL;
								item->readpending = 0;
								st = NULL;
							}
							else if (st && (st->type == STEP_SEND)) {
								/*
								 * Write what is LEFT of this step. A short
								 * write is ordinary -- SSL_write() returns 0
								 * with sendagain on WANT_WRITE, and write()
								 * can be cut short when the socket buffer
								 * fills. The unsent tail never reached the
								 * kernel, so TCP will not carry it: only
								 * offering it again does. Advancing on any
								 * non-error sent a truncated command. The
								 * offset lives here because the buffer
								 * belongs to the config.
								 */
								res = socket_write(item, (char *)st->text + item->stepsent,
										   st->len - item->stepsent);
								if (res > 0) tcp_stats_written += res;
								if (res == -1) {
									dbgprintf("write failed\n");
									item->errcode = CONTEST_EIO;
									item->curstep = NULL;
									item->stepsent = 0;
									st = NULL;
								}
								else {
									item->stepsent += res;
									if (item->stepsent >= st->len) {
										item->stepsent = 0;
										/*
										 * "fin": the whole send is out, so
										 * retire the write direction. The peer's
										 * read returns end of file, which is the
										 * only way some protocols learn that a
										 * request is finished -- xymond answers a
										 * query on nothing else. Reading continues,
										 * so the expect after this still runs.
										 *
										 * Only HERE, never on a partial write: a
										 * FIN sent mid-message would truncate the
										 * request and the peer would act on half
										 * of it.
										 */
										if (st->fin)
											shutdown(item->fd, SHUT_WR);
										st = dlg_skip_labels(st->next);
										item->curstep = (void *)st;
									}
									/* else: stay here; readpending is false for a
									 * SEND, so writefds brings the rest. */
								}
							}
							item->readpending = (st && ((st->type == STEP_EXPECT) ||
										    (st->type == STEP_STARTIAC) ||
										    ((st->type == STEP_STARTTLS) &&
										     (item->sslrunning == SSLSETUP_PENDING))));

							/*
							 * No step to wait on, but "options banner" asked
							 * for a read. Taking the flag from the step alone
							 * leaves that socket out of readfds and closes it
							 * unread -- the service still reports up, so only
							 * the empty banner shows it.
							 */
							if (!st && !item->silenttest && !item->banner &&
							    (item->svcinfo->flags & TCP_GET_BANNER))
								item->readpending = 1;
						}
						else if (do_talk) {
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
									dbgprintf("write failed\n");
									item->readpending = 0;
									item->errcode = CONTEST_EIO;
								}
								else if (item->sendagain) {
									/*
									 * The SSL layer accepted nothing and wants
									 * another pass. Leave sendtxt/sendlen alone
									 * so the retry sends the same bytes, and do
									 * not wait for a reply to a command that was
									 * never transmitted -- clearing readpending
									 * is also what puts this socket back in the
									 * write set. The shutdown below is taught to
									 * leave it open.
									 */
									item->readpending = 0;
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
							/*
							 * Three independent reasons to keep it open: a
							 * reply still expected, a write waiting to be
							 * retried, or a dialogue step still to run.
							 */
							if (!item->open || (!item->readpending && !item->sendagain && !item->curstep)) {
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
					else if ((FD_ISSET(item->fd, &readfds) &&
						  !(item->sendagain && item->sslwantread)) ||
						 (FD_ISSET(item->fd, &writefds) &&
						  item->sslagain && item->sslwantwrite)) {
						/*
						 * Data ready to read on this socket. Grab the
						 * banner - we only do one read (need the socket
						 * for other tests), so if the banner takes more
						 * than one cycle to arrive, too bad!
						 */
						int wantmoredata = 0;
						int datadone = 0;
						/*
						 * This read was telnet options and nothing else. Not
						 * the same as a closed socket, though both leave res
						 * at 0 once the options are taken off the front.
						 */
						int iaconly = 0;

						item->lastactive = timestamp.tv_sec;

						/*
						 * We may be in the process of setting up an SSL connection
						 */
						if (item->sslrunning == SSLSETUP_PENDING) {
							setup_ssl(item);
							if (item->sslrunning == 0) {
								/*
								 * Handshake failed; setup_ssl() has set errcode. Falling through left
								 * curstep on the STARTTLS step, so the write arm tried again on every
								 * pass until the test timed out -- and curstep also holds the close
								 * guard below open.
								 */
								item->dialogfail = 1;
								if (!item->failstep) item->failstep = item->curstep;
								item->curstep = NULL;
								item->readpending = 0;
							}
						}
						if (item->sslrunning == SSLSETUP_PENDING) {
							/*
							 * Still handshaking: nothing to read yet. continue, not break --
							 * break abandons every remaining socket in the pass. Measured, the
							 * cost is iterations, not results: select() returns again at once.
							 */
							continue;
						}
						if (item->curstep &&
						    (((svcstep_t *)item->curstep)->type == STEP_STARTTLS)) {
							/*
							 * A "starttls" step whose handshake has settled.
							 * The write arm owns the step list, so hand the
							 * socket back to it rather than advancing here.
							 */
							item->readpending = 0;
							continue;
						}

						if (!item->readpending) {
							/*
							 * The handshake finished here in the read arm, so the write arm has
							 * not run for this socket: nothing has sent sendtxt or decided whether
							 * a banner is wanted. Reading now would skip the send and collect a
							 * banner for a silenttest. readpending is 0, so select() puts the
							 * socket back in writefds and the write arm takes it next pass.
							 */
							continue;
						}

						/*
						 * Connection is ready - plain or SSL. Read data.
						 */
						res = socket_read(item, msgbuf, sizeof(msgbuf)-1);
						tcp_stats_read += res;
						dbgprintf("read %d bytes from socket\n", res);

						/*
						 * "start iac": strip the option commands off the front
						 * of this read before the matcher or the banner sees
						 * it. Options share a read with the text behind them,
						 * so stripping later leaves the matcher looking at
						 * 0xff and the greeting arriving too late to match.
						 *
						 * Three-byte commands only, as do_telnet_options().
						 */
						if ((res > 0) && item->curstep &&
						    (((svcstep_t *)item->curstep)->type == STEP_STARTIAC)) {
							int skip = 0;

							while (((skip + 2) < res) &&
							       (((unsigned char *)msgbuf)[skip] == 255)) {
								unsigned char cmd = ((unsigned char *)msgbuf)[skip+1];
								unsigned char opt = ((unsigned char *)msgbuf)[skip+2];
								unsigned char answer = 0;

								/*
								 * Refuse what is offered: an IBM i host sends
								 * DO TERMINAL-TYPE and says nothing until it
								 * is answered. WONT and DONT get no answer --
								 * RFC 854 makes them the end of that option,
								 * and replying loops until the timeout.
								 */
								if (cmd == 251) answer = 254;		/* WILL -> DONT */
								else if (cmd == 253) answer = 252;	/* DO   -> WONT */

								if (answer &&
								    ((item->iacreplylen + 3) <= (int)sizeof(item->iacreply))) {
									item->iacreply[item->iacreplylen++] = 255;
									item->iacreply[item->iacreplylen++] = answer;
									item->iacreply[item->iacreplylen++] = opt;
								}
								skip += 3;
							}

							if (skip > 0) {
								res -= skip;
								if (res > 0) memmove(msgbuf, msgbuf + skip, res);
								msgbuf[res] = '\0';
							}

							if (res > 0) {
								/*
								 * Text behind the options: the negotiation is
								 * over, and what is left belongs to the next
								 * step. Clearing telnetnegotiate keeps the
								 * older handler below from taking it apart a
								 * second time.
								 */
								svcstep_t *iacst = (svcstep_t *)item->curstep;

								item->telnetnegotiate = 0;
								item->curstep = (void *)iacst->next;
							}
							else if (skip > 0) {
								/*
								 * Options and nothing else: not EOF. Hand the
								 * socket to the write side if refusals are
								 * owed.
								 */
								iaconly = 1;
								if (item->iacreplylen > 0) item->readpending = 0;
							}
						}

						if (item->svcinfo->flags & TCP_DIALOGUE) {
							/*
							 * An EXPECT step is matched against what THIS read
							 * returned, not against the accumulated banner: in
							 * a conversation the same socket carries several
							 * replies, and only the one for this step counts.
							 * Compare the start, which is what a status line
							 * is -- "250-" and "250 " both satisfy "250".
							 */
							svcstep_t *st = (svcstep_t *)item->curstep;

							/*
								 * res <= 0 is EOF on a plain socket, but over
								 * TLS it is also how "no data yet" arrives:
								 * socket_read() sets sslagain for WANT_READ.
								 * Without that guard the first wait for a
								 * greeting was read as the peer hanging up,
								 * and every TLS dialogue failed instantly.
								 */
								if ((res <= 0) && !item->sslagain && !iaconly && st) {
								/*
								 * The peer went away with steps still to run.
								 * Fail here rather than leaving the socket to
								 * time out: "hung up mid-conversation" is a
								 * different fault from "never answered", and
								 * waiting out the timeout reports the wrong one.
								 */
								item->dialogfail = 1;
								if (!item->failstep) item->failstep = (void *)st;
								item->curstep = NULL;
								st = NULL;
							}
							else if ((res > 0) && st && (st->type == STEP_EXPECT)) {
								/*
								 * Accumulate: one read is not one reply. TLS
								 * hands back a record at a time and SMTP's
								 * multi-line replies span several, so a step
								 * has three outcomes -- matched, mismatched,
								 * or not enough yet -- and only the third is
								 * new here.
								 */
								/*
								 * Bound it. An expect that waits for a
								 * terminator will otherwise accumulate
								 * whatever a server cares to send, for as
								 * long as it cares to send it -- and the
								 * poll loop runs hundreds of these at once.
								 * Monit caps the same buffer at 255 bytes;
								 * this is roomier because a real EHLO reply
								 * is several hundred, but it is a cap.
								 */
								if ((item->stepbuflen + res) > MAX_DIALOGUE_BYTES) {
									errprintf("%s: reply exceeded %d bytes with no match - giving up\n",
										  item->svcinfo->svcname, MAX_DIALOGUE_BYTES);
									item->dialogfail = 1;
									if (!item->failstep) item->failstep = (void *)st;
									item->curstep = NULL;
									item->readpending = 0;
									st = NULL;
									/*
									 * Fall through, not continue: "continue" skipped the close
									 * below, so the cap stopped the reading and the timeout ended
									 * the test. Not "break" either -- that abandons every
									 * remaining socket in the pass.
									 */
								}

								{
									/*
									 * Through a temporary: assigning realloc()
									 * straight back loses the old pointer when
									 * it returns NULL, so the buffer leaks and
									 * the memcpy() below dereferences NULL.
									 */
									unsigned char *grown = (unsigned char *)realloc(item->stepbuf,
													item->stepbuflen + res + 1);

									if (!grown) {
										errprintf("%s: out of memory growing the reply buffer\n",
											  item->svcinfo->svcname);
										item->errcode = CONTEST_EIO;
										item->dialogfail = 1;
										if (!item->failstep) item->failstep = (void *)st;
										item->curstep = NULL;
										item->readpending = 0;
										st = NULL;
										break;
									}
									item->stepbuf = grown;
								}
								memcpy(item->stepbuf + item->stepbuflen, msgbuf, res);
								item->stepbuflen += res;
								item->stepbuf[item->stepbuflen] = '\0';

								/*
								 * Loop: two replies can arrive in one read, so the tail kept
								 * for the next step may already be its whole answer. Waiting
								 * for another read would time out. IMAP's "* BYE" and its
								 * tagged result are exactly this.
								 */
								while (st && (st->type == STEP_EXPECT) &&
								       (item->stepbuflen >= (((st->ofs > 0) ? st->ofs : 0) + st->len))) {
									int ofs = (st->ofs > 0) ? st->ofs : 0;

									if (memcmp(item->stepbuf + ofs, st->text, st->len) == 0) {
										int cut = ofs + st->len, ready = 1;

										if (st->until) {
											/*
											 * Multi-line: consume complete lines
											 * until one starts with the terminator.
											 * If it has not arrived, consume NOTHING
											 * and wait -- taking the lines we have
											 * would strand the rest of the reply for
											 * the next expect to trip over.
											 */
											int pos = 0;

											ready = 0;
											while (pos < item->stepbuflen) {
												int eol = pos;

												while ((eol < item->stepbuflen) &&
												       (item->stepbuf[eol] != '\n')) eol++;
												if (eol >= item->stepbuflen) break;
												if (until_matches(item->stepbuf + pos,
														  item->stepbuflen - pos,
														  st->until, st->untillen)) {
													cut = eol + 1;
													ready = 1;
													break;
												}
												pos = eol + 1;
											}
										}
										else if (st->ofs >= 0) {
											/*
											 * Positional: "at N" says this reply is bytes,
											 * not a line, so there is no newline to consume
											 * through and none to wait for. Exactly the
											 * matched region goes, and whatever the peer
											 * sent after it stays for the next step.
											 *
											 * Without this a binary reply would be held
											 * until the clock ran out whenever another
											 * alternative followed -- the line rule below
											 * refuses to finish a step that has a next.
											 */
											cut = ofs + st->len;
										}
										else {
											/*
											 * Single line. Consume through its end and KEEP the rest:
											 * discarding it would throw away a reply the peer had already
											 * sent.
											 *
											 * A pattern matching is not the reply having arrived -- "220"
											 * matches after three bytes -- so consume nothing until the
											 * line ends, or the next expect starts mid-line.
											 */
											while ((cut < item->stepbuflen) &&
											       (item->stepbuf[cut] != '\n')) cut++;
											if (cut < item->stepbuflen) cut++;
											else if (st->next) ready = 0;
											/*
											 * Last step: nothing to protect, and the reply may
											 * legitimately end without a newline. A lone expect is driven
											 * here too, so this would otherwise reach entries that never
											 * asked for it.
											 */
										}

										if (ready) {
											memmove(item->stepbuf, item->stepbuf + cut,
												item->stepbuflen - cut);
											item->stepbuflen -= cut;
											/*
											 * What matched decides where the dialogue goes.
											 * Without an edge that is the next line, which is
											 * every entry written the old way; with one it is
											 * the state named, or an answer that ends the test.
											 */
											switch (st->action) {
											  case ACT_SUCCESS:
												item->dialogverdict = 1;
												st = NULL; item->curstep = NULL;
												break;
											  case ACT_WARNING:
											  case ACT_FAIL:
												item->dialogfail = 1;
												item->dialogverdict = (st->action == ACT_WARNING) ? 2 : 3;
												if (!item->failstep) item->failstep = (void *)st;
												st = NULL; item->curstep = NULL;
												break;
											  case ACT_GOTO:
												st = dlg_skip_labels(st->targetstep);
												item->curstep = (void *)st;
												break;
											  default:
												st = dlg_skip_labels(st->next);
												item->curstep = (void *)st;
												break;
											}
										}
										else break;	/* waiting on "until" -- nothing was consumed */
									}
									else if ((item->svcinfo->flags & TCP_STATEMACHINE) &&
										 st->next && (st->next->type == STEP_EXPECT)) {
										/*
										 * In a state machine the expects of one state are
										 * ALTERNATIVES: the reply is whichever of them it
										 * matches, and only if none does has the state
										 * failed. That is what lets one state say "250 is
										 * success, 4xx is a warning, 5xx is a failure".
										 *
										 * Sequence is spelled with another state instead,
										 * so this cannot change an entry written the old
										 * way -- there, two expects in a row still mean one
										 * reply after the other.
										 */
										st = st->next;
										item->curstep = (void *)st;
									}
									else {
										item->dialogfail = 1;
										if (!item->failstep) item->failstep = (void *)st;
										item->curstep = NULL;
										st = NULL;
									}
								}
								/* else: short of the pattern -- wait for more */

								/*
								 * The step we land on decides which fd set the socket
								 * joins next. Without this the flag keeps whatever the
								 * write arm last set, so a dialogue that advances to a
								 * SEND while reading stays parked in readfds and the
								 * send is never written -- which over TLS meant the peer
								 * saw the greeting answered by nothing at all.
								 */
								item->readpending = (item->curstep &&
										      (((svcstep_t *)item->curstep)->type == STEP_EXPECT));
							}
							/*
							 * Hand the socket back to the write side when the
							 * next step is a SEND: clearing readpending is what
							 * puts it in writefds, and the close below is
							 * already taught to spare a socket with steps left.
							 */
							st = (svcstep_t *)item->curstep;
							/* Owed refusals outrank waiting: the peer may be
							 * holding its greeting until they arrive. */
							item->readpending = (st && (item->iacreplylen == 0) &&
									     ((st->type == STEP_EXPECT) ||
									      (st->type == STEP_STARTIAC)));
							if (st) wantmoredata = 1;
						}

						if ((res > 0) && item->datacallback) {
							datadone = item->datacallback(msgbuf, res, item->priv);
						}

						if ((res > 0) && item->telnetnegotiate && item->banner) {
							/*
							 * telnet data has telnet options first.
							 * We must negotiate the session before we
							 * get the banner.
							 */
							item->telnetbuf = item->banner;
							item->telnetbuflen = res;

							/*
							 * Safety measure: Don't loop forever doing
							 * telnet options.
							 * This puts a maximum on how many times
							 * we go here.
							 */
							item->telnetnegotiate--;
							if (!item->telnetnegotiate) {
								dbgprintf("Max. telnet negotiation (%d) reached for host %s\n", 
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

						if (((item->svcinfo->flags & TCP_HTTP) && res > 0) || item->sslagain) {
							/*
							 * Grab the entire HTTP response or wait for
							 * TLS handshake to complete.
							 */
							wantmoredata = !datadone;
						}

						if (!wantmoredata && !item->curstep) {
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

	dbgprintf("TCP tests completed normally\n");
}


void show_tcp_test_results(void)
{
	tcptest_t *item;

	for (item = thead; (item); item = item->next) {
		printf("Address=%s:%d, open=%d, res=%d, err=%d, connecttime=%u.%06u, totaltime=%u.%06u, ",
				inet_ntoa(item->addr.sin_addr), 
				ntohs(item->addr.sin_port),
				item->open, item->connres, item->errcode,
				(unsigned int)item->duration.tv_sec, (unsigned int)(item->duration.tv_nsec/1000),
				(unsigned int)item->totaltime.tv_sec, (unsigned int)(item->totaltime.tv_nsec/1000));

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
				((item->certexpires > getcurrenttime(NULL)) ? "valid" : "expired"));
		}
		printf("\n");

		if ((item->svcinfo == &svcinfo_http) || (item->svcinfo == &svcinfo_https)) {
			http_data_t *httptest = (http_data_t *) item->priv;

			printf("httpstatus = %d, open=%d, errcode=%d, parsestatus=%d\n",
				httptest->httpstatus, httptest->tcptest->open, httptest->tcptest->errcode, httptest->parsestatus);
			printf("Response:\n");
			if (httptest->headers) printf("%s\n", httptest->headers); else printf("(no headers)\n");
			if (httptest->contentcheck == CONTENTCHECK_DIGEST) printf("Content digest: %s\n", httptest->digest);
			if (httptest->output) printf("%s", httptest->output);
		}
	}
}

/*
 * Which step gave up. A dialogue can fail at any of them, and
 * "Unexpected service response" on its own leaves the reader guessing
 * whether it was the greeting, a reply mid-conversation, or a peer that
 * went quiet -- so name it.
 */
char *tcp_dialogue_failure(tcptest_t *test)
{
	static char buf[160];
	svcstep_t *st, *bad;
	int idx = 0, n = 0;

	if (!test || !test->svcinfo) return NULL;

	/*
	 * Before the TCP_DIALOGUE gate: a refused entry keeps whatever shape it
	 * was written in, and when the file could not be read at all the entries
	 * are the compiled-in ones, which are not dialogues. Gating first left
	 * those reporting "Unexpected service response" with no reason at all.
	 */
	if (test->svcinfo->flags & TCP_DIALOGUE_BROKEN)
		return "protocols.cfg refused this definition (or could not be read) - see the xymonnet log";

	/*
	 * No test at all: the service never got a port, because the file that
	 * would have given it one could not be read.
	 */
	if (!test || !test->svcinfo)
		return "protocols.cfg could not be read - no service can be checked";

	if (!(test->svcinfo->flags & TCP_DIALOGUE)) return NULL;

	if (test->stalereply) {
		svcstep_t *w; int n2 = 0, idx2 = 0;

		for (w = test->svcinfo->steps; (w); w = w->next) {
			n2++;
			if (w == (svcstep_t *)test->failstep) idx2 = n2;
		}
		snprintf(buf, sizeof(buf),
			 "unread reply still buffered at step %d - the step before it did "
			 "not consume its answer (missing \"until\"?)", idx2);
		return buf;
	}

	bad = (svcstep_t *)test->failstep;
	if (!bad) {
		/* No step blamed itself: the conversation simply stopped short. */
		st = (svcstep_t *)test->curstep;
		if (!st) return NULL;
		bad = st;
		for (st = test->svcinfo->steps; (st); st = st->next) { n++; if (st == bad) idx = n; }
		snprintf(buf, sizeof(buf), "no reply to step %d (expecting \"%.40s\")",
			 idx, (bad->text ? (char *)bad->text : ""));
		return buf;
	}

	for (st = test->svcinfo->steps; (st); st = st->next) { n++; if (st == bad) idx = n; }
	if (bad->type == STEP_EXPECT)
		snprintf(buf, sizeof(buf), "step %d expected \"%.40s\"", idx,
			 (bad->text ? (char *)bad->text : ""));
	else
		snprintf(buf, sizeof(buf), "step %d failed", idx);

	return buf;
}

/*
 * Was this entry refused when protocols.cfg was read?
 *
 * Separate from tcp_got_expected() because it is not gated on
 * --checkresponse: an expect mismatch is a judgement the operator opts
 * into, a rejected definition means no check ran at all.
 */
int tcp_dialogue_refused(tcptest_t *test)
{
	/*
	 * With no protocols.cfg there is no test to carry the flag: a service
	 * with no port never reached add_tcp_test(), so privdata is NULL.
	 * Checked first, or the report falls through to "Service unavailable".
	 */
	if (tcp_services_unreadable()) return 1;

	return (test && test->svcinfo && (test->svcinfo->flags & TCP_DIALOGUE_BROKEN));
}

int tcp_got_expected(tcptest_t *test)
{
	if (test == NULL) return 1;

	/* A definition that could not be read is not a service that is up. */
	if (test->svcinfo && (test->svcinfo->flags & TCP_DIALOGUE_BROKEN)) return 0;

	/*
	 * A dialogue has already judged itself, step by step, as the replies
	 * arrived. Re-matching exptext against the banner here would compare the
	 * first step's pattern against whatever the last read happened to leave.
	 */
	if (test->svcinfo && (test->svcinfo->flags & TCP_DIALOGUE))
		return (test->dialogfail == 0) && (test->curstep == NULL);

	if (test->svcinfo && test->svcinfo->exptext) {
		int compbytes; /* Number of bytes to compare */


		/* Did we get enough data? */
		if (test->banner == NULL) {
			dbgprintf("tcp_got_expected: No data in banner\n");
			return 0;
		}

		compbytes = (test->svcinfo->explen ? test->svcinfo->explen : strlen(test->svcinfo->exptext));
		if ((test->svcinfo->expofs + compbytes) > test->bannerbytes) {
			dbgprintf("tcp_got_expected: Not enough data\n");
			return 0;
		}

		return (memcmp(test->svcinfo->exptext+test->svcinfo->expofs, test->banner, compbytes) == 0);
	}
	else
		return 1;
}
/*
 * What the entry said to report, rather than one colour for every way of
 * failing. 0 when it said nothing, and then the caller keeps its own default:
 * a positional entry cannot express this, so nothing about it changes.
 */
int tcp_dialogue_verdict(tcptest_t *test)
{
	return (test ? test->dialogverdict : 0);
}


#ifdef STANDALONE

int main(int argc, char *argv[])
{
	int argi;
	char *argp, *p;
	int timeout = 0;
	int concurrency = 0;

	if (xgetenv("XYMONNETSVCS") == NULL) putenv("XYMONNETSVCS=");
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
		else if (strcmp(argv[argi], "--help") == 0) {
			printf("Run with\n~xymon/server/bin/xymoncmd ./contest --debug 172.16.10.2/25/smtp\n");
			printf("I.e. IP/PORTNUMBER/TESTSPEC\n");
			return 0;
		}
		else {
			char *ip;
			char *port;
			char *srcip;
			char *testspec;

			argp = argv[argi]; ip = port = srcip = testspec = NULL;

			ip = argp;
			p = strchr(argp, '/');
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
				srcip = strchr(testspec, '@');
				if (srcip) {
					*srcip = '\0';
					srcip++;
				}
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
					(strncmp(argp, "httpstatus;", 11) == 0) ||
					(strncmp(argp, "httpstatus=", 11) == 0) ||
					(strncmp(argp, "type;", 5) == 0)   ||
					(strncmp(argp, "type=", 5) == 0) ) {

					testitem_t *testitem = calloc(1, sizeof(testitem_t));
					testedhost_t *hostitem = calloc(1, sizeof(testedhost_t));
					http_data_t *httptest;

					hostitem->hostname = strdup("localhost");
					testitem->host = hostitem;
					testitem->testspec = testspec;
					strcpy(hostitem->ip, ip);
					add_url_to_dns_queue(testspec);
					add_http_test(testitem);

					testitem->next = NULL;

					httptest = (http_data_t *)testitem->privdata;
					if (httptest && httptest->tcptest) {
						printf("TCP connection goes to %s:%d\n",
							inet_ntoa(httptest->tcptest->addr.sin_addr),
							ntohs(httptest->tcptest->addr.sin_port));
						printf("Request:\n%s\n", httptest->tcptest->sendtxt);
					}
				}
				else if (strncmp(argp, "dns=", 4) == 0) {
					strbuffer_t *banner = newstrbuffer(0);
					int result;

					result = dns_test_server(ip, argp+4, banner);
					printf("DNS test result=%d\nBanner:%s\n", result, STRBUF(banner));
				}
				else {
					add_tcp_test(ip, atoi(port), testspec, NULL, srcip, NULL, 0, NULL, NULL, NULL, NULL);
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

