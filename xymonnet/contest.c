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
			char *srcip,
			char *tspec, int silent, unsigned char *reqmsg, 
		     void *priv, f_callback_data datacallback, f_callback_final finalcallback)
{
	tcptest_t *newtest;

	dbgprintf("Adding tcp test IP=%s, port=%d, service=%s, silent=%d\n", textornull(ip), port, service, silent);

	if (port == 0) {
		errprintf("Trying to scan port 0 for service %s\n", service);
		errprintf("You probably need to define the %s service in /etc/services\n", service);
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
	newtest->sslrunning = (((newtest->svcinfo->flags & TCP_SSL) || (newtest->svcinfo->flags & TCP_ALPN)) ? SSLSETUP_PENDING : 0);
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
	newtest->curstep = ((newtest->svcinfo && (newtest->svcinfo->flags & TCP_DIALOGUE))
			    ? (void *)newtest->svcinfo->steps : NULL);
	newtest->dialogfail = 0;
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
			item->banner = strdup(inp);
			item->bannerbytes = strlen(inp);
			item->telnetbuflen = 0;
			xfree(obuf);
			return 0;
		}
	        *outp = 255; outp++;
		inp++; remain--;
		if ((*inp == 251) || (*inp == 252))     /* WILL or WON'T */
			y = 254;                          /* -> DON'T */
		if ((*inp == 253) || (*inp == 254))     /* DO or DON'T */
			y = 252;                          /* -> WON'T */
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


static void setup_ssl(tcptest_t *item)
{
	static int ssl_init_complete = 0;
	struct servent *sp;
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
			SSL_CTX_free(item->sslctx);
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
				SSL_CTX_free(item->sslctx);
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
				SSL_free(item->ssldata);
				SSL_CTX_free(item->sslctx);
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
	if (item->sslrunning) {
		res = SSL_write(item->ssldata, outbuf, outlen);
		if (res < 0) {
			switch (SSL_get_error (item->ssldata, res)) {
			  case SSL_ERROR_WANT_WRITE:
				  /*
				   * Nothing was sent. Returning 0 alone is
				   * indistinguishable from "there was nothing to
				   * send", so flag it: the caller must come back
				   * and retry with the SAME buffer. socket_read()
				   * uses sslagain for the same purpose.
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
				   * WANT_READ would need the fd in readfds, and the
				   * read branch there would consume application data
				   * rather than retry this write. Left as it behaves
				   * today -- the payload is not sent and the test
				   * waits out its timeout -- rather than retried on a
				   * fd set that is always ready, which would busy-wait.
				   * Reachable only via renegotiation; TLS 1.3 session
				   * tickets and KeyUpdate do not produce it.
				   */
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
	 * Both guards are load-bearing. sslrunning must stay the outer test:
	 * setup_ssl()'s failure paths free ssldata/sslctx without clearing them
	 * and set sslrunning to 0, so a pointer-only guard would free dangling
	 * pointers here. And the inner NULL checks are needed because
	 * SSLSETUP_PENDING is -1, so sslrunning is already true from
	 * add_tcp_test() while ssldata is still NULL -- a connection that times
	 * out before setup_ssl() runs would otherwise reach SSL_shutdown(NULL).
	 * Clearing them makes a second call harmless either way.
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
 * ---- dialogue support: variables, expansion, and the instant steps ----
 */

/*
 * Overwrite before releasing. ${password} lives in this list, and a plain
 * memset on memory that is about to be freed is exactly what a compiler is
 * entitled to delete, so the write goes through a volatile pointer.
 */
static void dlg_wipe(char *s)
{
	volatile char *p = (volatile char *)s;

	while (p && *p) *p++ = '\0';
}


static void dlg_free(tcptest_t *item)
{
	dlgvar_t *v = (dlgvar_t *)item->dlgvars;

	while (v) {
		dlgvar_t *next = v->next;

		if (v->value) { dlg_wipe(v->value); xfree(v->value); }
		if (v->name) xfree(v->name);
		xfree(v);
		v = next;
	}
	item->dlgvars = NULL;

	if (item->stepbuf) { xfree(item->stepbuf); item->stepbuf = NULL; }
	item->stepbuflen = 0;
	if (item->lastreply) { xfree(item->lastreply); item->lastreply = NULL; }
}


static char *dlg_get(tcptest_t *item, const char *name)
{
	dlgvar_t *v;

	for (v = (dlgvar_t *)item->dlgvars; (v); v = v->next)
		if (strcmp(v->name, name) == 0) return v->value;

	return NULL;
}

static void dlg_set(tcptest_t *item, const char *name, const char *value)
{
	dlgvar_t *v;

	for (v = (dlgvar_t *)item->dlgvars; (v); v = v->next) {
		if (strcmp(v->name, name) != 0) continue;
		if (v->value) xfree(v->value);
		v->value = strdup(value ? value : "");
		return;
	}

	v = (dlgvar_t *)calloc(1, sizeof(dlgvar_t));
	v->name  = strdup(name);
	v->value = strdup(value ? value : "");
	v->next  = (dlgvar_t *)item->dlgvars;
	item->dlgvars = (void *)v;
}

/*
 * Expand ${...} in a send string. Nesting is why this recurses rather than
 * scanning once: APOP is ${md5:${challenge}${password}}, and the digest has
 * to be taken of the *expanded* argument.
 *
 * The result is NUL-terminated but the length is returned separately --
 * a send may legitimately carry NULs.
 */
static char *dlg_expand(tcptest_t *item, const char *text, int len, int *outlen)
{
	char *out;
	int outsz, n = 0, i;

	outsz = len + 128;
	out = (char *)malloc(outsz + 1);

	for (i = 0; (i < len); i++) {
		int depth, j, blen;
		char *body, *inner, *val = NULL, *freeval = NULL;
		int innerlen;

		if (!((text[i] == '$') && ((i+1) < len) && (text[i+1] == '{'))) {
			if (n >= outsz) { outsz *= 2; out = (char *)realloc(out, outsz + 1); }
			out[n++] = text[i];
			continue;
		}

		/* find the matching brace, counting nested ${ } */
		depth = 1;
		for (j = i + 2; (j < len) && depth; j++) {
			if ((text[j] == '$') && ((j+1) < len) && (text[j+1] == '{')) { depth++; j++; }
			else if (text[j] == '}') depth--;
		}
		if (depth) {			/* unterminated -- emit literally */
			if (n >= outsz) { outsz *= 2; out = (char *)realloc(out, outsz + 1); }
			out[n++] = text[i];
			continue;
		}

		blen = (j - 1) - (i + 2);
		body = (char *)malloc(blen + 1);
		memcpy(body, text + i + 2, blen);
		body[blen] = '\0';

		if (strncmp(body, "md5:", 4) == 0) {
			inner = dlg_expand(item, body + 4, blen - 4, &innerlen);
			/* md5hash() hands back a static buffer -- copy, never free. */
			val = md5hash(inner);
			xfree(inner);
		}
		else if (strncmp(body, "base64:", 7) == 0) {
			inner = dlg_expand(item, body + 7, blen - 7, &innerlen);
			val = freeval = base64encode((unsigned char *)inner);
			xfree(inner);
		}
		else {
			/* Mistyped functions are reported when the file is read. */
			val = dlg_get(item, body);
			if (val == NULL) {
				dbgprintf("dialogue: ${%s} is not set, expanding empty\n", body);
				val = "";
			}
		}

		{
			int vlen = strlen(val);

			while ((n + vlen) >= outsz) { outsz *= 2; out = (char *)realloc(out, outsz + 1); }
			memcpy(out + n, val, vlen);
			n += vlen;
		}

		if (freeval) xfree(freeval);
		xfree(body);
		i = j - 1;
	}

	out[n] = '\0';
	if (outlen) *outlen = n;
	return out;
}

/* Group 1 of the pattern, against the reply the last expect accepted. */
static void dlg_capture(tcptest_t *item, svcstep_t *st)
{
	char *subject = (item->lastreply ? item->lastreply : "");
	pcre2_match_data *md;
	int res, n;
	char names[256], *name, *rest;

	if (st->re == NULL) {			/* plain "capture as NAME" */
		dlg_set(item, st->varname, subject);
		return;
	}

	md = pcre2_match_data_create(32, NULL);
	res = pcre2_match((pcre2_code *)st->re, (PCRE2_SPTR)subject,
			  strlen(subject), 0, 0, md, NULL);

	/*
	 * One name per capture group, in order: "as server;challenge" binds
	 * group 1 to server and group 2 to challenge. The parser has already
	 * refused a list whose length does not match the pattern, so a group
	 * with no name cannot reach here.
	 */
	strncpy(names, st->varname, sizeof(names) - 1);
	names[sizeof(names) - 1] = '\0';
	rest = names;
	n = 1;

	while ((name = strtok(rest, ";")) != NULL) {
		rest = NULL;

		if (res > n) {
			PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
			int b = ov[2*n], e = ov[2*n + 1];
			char *v = (char *)malloc(e - b + 1);

			memcpy(v, subject + b, e - b);
			v[e-b] = '\0';
			dlg_set(item, name, v);
			xfree(v);
		}
		else {
			dbgprintf("dialogue: capture-regex did not match, ${%s} left empty\n", name);
			dlg_set(item, name, "");
		}
		n++;
	}

	pcre2_match_data_free(md);
}


/*
 * Run every step that needs no socket, and return the next one that does
 * (or NULL at the end of the dialogue). Bounded: a "when" that jumps
 * backwards is a legal retry loop, but a loop that performs no I/O would
 * spin here forever, so cap the run.
 */
static svcstep_t *dlg_run_instant(tcptest_t *item, svcstep_t *st)
{
	int guard = 0;

	while (st && STEP_IS_INSTANT(st->type)) {
		if (++guard > 1000) {
			errprintf("Service %s: dialogue loops without doing any I/O\n",
				  (item->svcinfo ? item->svcinfo->svcname : "?"));
			item->dialogfail = 1;
			if (!item->failstep) item->failstep = (void *)st;
			return NULL;
		}

		switch (st->type) {
		  case STEP_LABEL:
			st = st->next;
			break;

		  case STEP_CAPTURE:
			dlg_capture(item, st);
			st = st->next;
			break;

		  case STEP_CREDS:
			dlg_set(item, "username", st->user);
			dlg_set(item, "password", st->pass);
			st = st->next;
			break;

		  case STEP_WHEN: {
			char *v = dlg_get(item, st->varname);

			/*
			 * An edge: taken when the test matches, fallen through when
			 * it does not, so the next line -- another '~' or an 'else'
			 * -- gets its turn. The regex tests a value already bound,
			 * never the socket, so nothing here races a partial read.
			 */
			if (!(v && st->re && matchregex(v, (pcre2_code *)st->re))) {
				st = st->next;
				break;
			}
		  }
		  /* FALLTHROUGH: a matched '~' takes its edge exactly as a jump does */
		  case STEP_JUMP:
			if (st->action == ACT_FAIL) {
				item->dialogfail = 1;
				if (!item->failstep) item->failstep = (void *)st;
				return NULL;
			}
			st = st->targetstep;
			break;

		  default:
			st = st->next;
			break;
		}
	}

	return st;
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
				if (item->open && (item->sslrunning == SSLSETUP_PENDING)) {
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
					 * Either way, hand the socket to socket_shutdown():
					 * it is the only place SSL_free()/SSL_CTX_free()
					 * happen, and it no-ops on a session that was never
					 * running. The !readpending arm used to skip it, so
					 * an SSL session that timed out with nothing pending
					 * to read -- a handshake that never finished, and now
					 * also a write still waiting to be retried -- had its
					 * fd closed with the SSL objects still allocated.
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
					if (FD_ISSET(item->fd, &writefds)) {
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
						if (do_talk && (item->svcinfo->flags & TCP_DIALOGUE)) {
							/*
							 * A dialogue drives itself from where it stands:
							 * a SEND step is written here, an EXPECT step is
							 * waited for by leaving readpending set. That one
							 * rule is the whole state machine -- the current
							 * step decides which fd set the socket joins.
							 */
							svcstep_t *st;

							/* Settle onto a step that actually needs the socket. */
							st = dlg_run_instant(item, (svcstep_t *)item->curstep);
							item->curstep = (void *)st;


							while (st && (st->type == STEP_SEND) && item->silenttest) {
								/*
								 * A silent test says nothing on the wire. Step
								 * over the sends rather than sitting on one:
								 * leaving it current would strand the dialogue
								 * with a step it will never perform.
								 */
								st = dlg_run_instant(item, st->next);
								item->curstep = (void *)st;
							}
							if (st && (st->type == STEP_SEND)) {
								int slen = 0;
								char *sbuf = dlg_expand(item, (char *)st->text, st->len, &slen);

								res = socket_write(item, sbuf, slen);
								xfree(sbuf);
								tcp_stats_written += res;
								if (res == -1) {
									dbgprintf("write failed\n");
									item->errcode = CONTEST_EIO;
									item->curstep = NULL;
									st = NULL;
								}
								else {
									st = dlg_run_instant(item, st->next);
									item->curstep = (void *)st;
								}
							}
							item->readpending = (st && (st->type == STEP_EXPECT));
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
					else if (FD_ISSET(item->fd, &readfds)) {
						/*
						 * Data ready to read on this socket. Grab the
						 * banner - we only do one read (need the socket
						 * for other tests), so if the banner takes more
						 * than one cycle to arrive, too bad!
						 */
						int wantmoredata = 0;
						int datadone = 0;

						item->lastactive = timestamp.tv_sec;

						/*
						 * We may be in the process of setting up an SSL connection
						 */
						if (item->sslrunning == SSLSETUP_PENDING) setup_ssl(item);
						if (item->sslrunning == SSLSETUP_PENDING) {
							/*
							 * Still handshaking: nothing to read yet.
							 * continue, not break -- break leaves the whole
							 * loop over items, abandoning the scan at the
							 * first socket that is readable but not yet
							 * handshaken. Measured, that costs iterations
							 * rather than results: select() returns again
							 * immediately and the remaining sockets are
							 * serviced on the next pass. It was unreachable
							 * during a handshake before (the fd was always in
							 * writefds); registering by direction above makes
							 * it the normal path, so leave the scan intact.
							 */
							continue;
						}
						if (!item->readpending) {
							/*
							 * The handshake just finished, here, in the read
							 * arm -- which only became possible once pending
							 * handshakes were registered for readability. The
							 * write arm has not run for this socket yet, so
							 * nothing has sent sendtxt or decided whether a
							 * banner is even wanted. Reading now would skip the
							 * send outright, and would collect a banner for a
							 * silenttest. Clearing readpending is 0 here means
							 * select() puts the socket back in writefds, so the
							 * write arm picks it up on the next pass exactly as
							 * it did when the handshake completed there.
							 */
							continue;
						}

						/*
						 * Connection is ready - plain or SSL. Read data.
						 */
						res = socket_read(item, msgbuf, sizeof(msgbuf)-1);
						tcp_stats_read += res;
						dbgprintf("read %d bytes from socket\n", res);

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
								if ((res <= 0) && !item->sslagain && st) {
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
								int progress = 1;

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
									st = NULL;
									break;
								}

								item->stepbuf = (unsigned char *)realloc(item->stepbuf, item->stepbuflen + res + 1);
								memcpy(item->stepbuf + item->stepbuflen, msgbuf, res);
								item->stepbuflen += res;
								item->stepbuf[item->stepbuflen] = '\0';

								/*
								 * Loop, because one read can satisfy more than one
								 * expect when the peer coalesces its replies -- and
								 * we are only woken again by readable data.
								 */
								while (progress) {
									svcstep_t *alt, *hit = NULL;
									int undecided = 0;

									progress = 0;
									st = (svcstep_t *)item->curstep;
									if (!st || (st->type != STEP_EXPECT)) break;

									/* Consecutive expects are alternatives of ONE state. */
									for (alt = st; (alt && (alt->type == STEP_EXPECT)); alt = alt->next) {
										if (item->stepbuflen < alt->len) { undecided++; continue; }
										if (memcmp(item->stepbuf, alt->text, alt->len) == 0) { hit = alt; break; }
									}

									/*
									 * An ambiguous group is one where a longer
									 * alternative could still overtake the one
									 * that just matched. Wait until every
									 * pattern in the group is decidable, so the
									 * winner is the config's order rather than
									 * the server's packet boundaries. Bounded:
									 * maxaltlen is known at load time, and the
									 * buffer cap and test timeout end the wait.
									 */
									if (hit && hit->ambiguous &&
									    (item->stepbuflen < hit->maxaltlen)) break;

									if (hit) {
										int cut = hit->len;

										if (hit->until) {
											/*
											 * A multi-line reply: consume complete
											 * lines until one starts with the
											 * terminator. If it has not arrived yet
											 * consume NOTHING and wait -- taking the
											 * lines we have would strand the rest of
											 * the reply for the next expect to trip
											 * over.
											 */
											int pos = 0, done = 0;

											while (pos < item->stepbuflen) {
												int eol = pos;

												while ((eol < item->stepbuflen) &&
												       (item->stepbuf[eol] != '\n')) eol++;
												if (eol >= item->stepbuflen) break;   /* partial line */
												if (((item->stepbuflen - pos) >= hit->untillen) &&
												    (memcmp(item->stepbuf + pos, hit->until, hit->untillen) == 0)) {
													cut = eol + 1;
													done = 1;
													break;
												}
												pos = eol + 1;
											}
											if (!done) break;	/* wait for the rest */
										}
										else {
											/*
											 * Single line. Consume through its end and
											 * KEEP the rest: discarding it would throw
											 * away a reply the peer had already sent,
											 * and the next expect would wait for data
											 * that already arrived.
											 */
											while ((cut < item->stepbuflen) && (item->stepbuf[cut] != '\n')) cut++;
											if (cut < item->stepbuflen) cut++;
										}

										if (item->lastreply) xfree(item->lastreply);
										item->lastreply = (char *)malloc(cut + 1);
										memcpy(item->lastreply, item->stepbuf, cut);
										item->lastreply[cut] = '\0';

										memmove(item->stepbuf, item->stepbuf + cut, item->stepbuflen - cut);
										item->stepbuflen -= cut;

										if (hit->action == ACT_FAIL) {
											item->dialogfail = 1;
											if (!item->failstep) item->failstep = (void *)st;
											item->curstep = NULL;
											st = NULL;
										}
										else {
											if (hit->action == ACT_GOTO) alt = hit->targetstep;
											else for (alt = st; (alt && (alt->type == STEP_EXPECT)); alt = alt->next) ;

											st = dlg_run_instant(item, alt);
											item->curstep = (void *)st;
											progress = 1;
										}
									}
									else if (!undecided) {
										/* every alternative has been ruled out */
										item->dialogfail = 1;
										if (!item->failstep) item->failstep = (void *)st;
										item->curstep = NULL;
										st = NULL;
									}
									/* else: short of every pattern -- wait for more */
								}

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
							item->readpending = (st && (st->type == STEP_EXPECT));
							if (st) wantmoredata = 1;
						}

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

	/*
	 * Release the dialogue state. Not in socket_shutdown(): a connection
	 * that is refused, or that times out before it opens, never reaches
	 * that. Here every test in the list is covered however it ended.
	 */
	for (item = thead; (item); item = item->next) dlg_free(item);

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
	char *name = NULL;
	int idx = 0, n = 0;

	if (!test || !test->svcinfo || !(test->svcinfo->flags & TCP_DIALOGUE)) return NULL;

	bad = (svcstep_t *)test->failstep;
	if (!bad) {
		/* No step blamed itself: the conversation simply stopped short. */
		st = (svcstep_t *)test->curstep;
		if (!st) return NULL;
		bad = st;
		for (st = test->svcinfo->steps; (st); st = st->next) {
			n++;
			if (st->type == STEP_LABEL) name = st->label;
			if (st == bad) { idx = n; break; }
		}
		if (name)
			snprintf(buf, sizeof(buf), "no reply in state %s (expecting \"%.40s\")",
				 name, (bad->text ? (char *)bad->text : ""));
		else
			snprintf(buf, sizeof(buf), "no reply to step %d (expecting \"%.40s\")",
				 idx, (bad->text ? (char *)bad->text : ""));
		return buf;
	}

	/*
	 * Prefer the state's name. "step 7" is accurate and tells an operator
	 * nothing; "state want-password" is what they need to read. Entries
	 * that name no states still get the number.
	 */
	for (st = test->svcinfo->steps; (st); st = st->next) {
		n++;
		if (st->type == STEP_LABEL) name = st->label;
		if (st == bad) { idx = n; break; }
	}

	if (bad->type == STEP_EXPECT) {
		if (name)
			snprintf(buf, sizeof(buf), "state %s expected \"%.40s\"", name,
				 (bad->text ? (char *)bad->text : ""));
		else
			snprintf(buf, sizeof(buf), "step %d expected \"%.40s\"", idx,
				 (bad->text ? (char *)bad->text : ""));
	}
	else if (name) snprintf(buf, sizeof(buf), "state %s failed", name);
	else           snprintf(buf, sizeof(buf), "step %d failed", idx);

	return buf;
}

int tcp_got_expected(tcptest_t *test)
{
	if (test == NULL) return 1;

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

