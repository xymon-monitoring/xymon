/*----------------------------------------------------------------------------*/
/* Hobbit monitor network test tool.                                          */
/*                                                                            */
/* This is used to implement the testing of a TCP service.                    */
/*                                                                            */
/* Copyright (C) 2003-2008 Henrik Storner <henrik@hswn.dk>                    */
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

#include "libbbgen.h"

#include "bbtest-net.h"
#include "contest.h"
#include "httptest.h"
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
unsigned int warnbytesread = 0;

static tcptest_t *thead = NULL;

static svcinfo_t svcinfo_http  = { "http", NULL, 0, NULL, 0, 0, (TCP_GET_BANNER|TCP_HTTP), 80 };
static svcinfo_t svcinfo_https = { "https", NULL, 0, NULL, 0, 0, (TCP_GET_BANNER|TCP_HTTP|TCP_SSL), 443 };
static ssloptions_t default_sslopt = { NULL, SSLVERSION_DEFAULT };

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
	newtest = (tcptest_t *) malloc(sizeof(tcptest_t));

	newtest->tspec = (tspec ? strdup(tspec) : NULL);
	newtest->fd = -1;
	newtest->bytesread = 0;
	newtest->byteswritten = 0;
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
	errprintf("SSL test, but bbtest-net was built without SSL support\n");
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
	sprintf(passfn, "%s/certs/%s", xgetenv("BBHOME"), item->ssloptions->clientcert);
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
	strbuffer_t *sslinfo;
	char msglin[2048];

	item->sslrunning = 1;

	if (!ssl_init_complete) {
		/* Setup entropy */
		if (RAND_status() != 1) {
			char path[PATH_MAX];	/* Path for the random file */

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
			char certfn[PATH_MAX];

			SSL_CTX_set_default_passwd_cb(item->sslctx, cert_password_cb);
			SSL_CTX_set_default_passwd_cb_userdata(item->sslctx, item);

			sprintf(certfn, "%s/certs/%s", xgetenv("BBHOME"), item->ssloptions->clientcert);
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
	certstart = strdup(bbgen_ASN1_UTCTIME(X509_get_notBefore(peercert)));
	certend = strdup(bbgen_ASN1_UTCTIME(X509_get_notAfter(peercert)));

	snprintf(msglin, sizeof(msglin),
		"Server certificate:\n\tsubject:%s\n\tstart date: %s\n\texpire date:%s\n", 
		certcn, certstart, certend);
	addtobuffer(sslinfo, msglin);
	item->certexpires = sslcert_expiretime(certend);
	xfree(certcn); xfree(certstart); xfree(certend);
	X509_free(peercert);

	/* We list the available ciphers in the SSL cert data */
	{
		int i;
		STACK_OF(SSL_CIPHER) *sk;

		addtobuffer(sslinfo, "\nAvailable ciphers:\n");
		sk = SSL_get_ciphers(item->ssldata);
		for (i=0; i<sk_SSL_CIPHER_num(sk); i++) {
			int b1, b2;
			char *cph;

			b1 = SSL_CIPHER_get_bits(sk_SSL_CIPHER_value(sk,i), &b2);
			cph = SSL_CIPHER_get_name(sk_SSL_CIPHER_value(sk,i));
			snprintf(msglin, sizeof(msglin), "Cipher %d: %s (%d bits)\n", i, cph, b1);
			addtobuffer(sslinfo, msglin);

			if ((item->mincipherbits == 0) || (b1 < item->mincipherbits)) item->mincipherbits = b1;
		}
	}

	item->certinfo = grabstrbuffer(sslinfo);
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

	item->byteswritten += res;
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

	item->bytesread += res;
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

	if (warnbytesread && (item->bytesread > warnbytesread)) {
		if (item->tspec)
			errprintf("Huge response %u bytes from %s\n", item->bytesread, item->tspec);
		else
			errprintf("Huge response %u bytes for %s:%s\n",
				  item->bytesread, inet_ntoa(item->addr.sin_addr), item->svcinfo->svcname);
	}
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

	struct rlimit lim;
	struct timezone tz;

	/* If timeout or concurrency are 0, set them to reasonable defaults */
	if (timeout == 0) timeout = 10;	/* seconds */

	/* 
	 * Decide how many tests to run in parallel.
	 * If no --concurrency set by user, default to (FD_SETSIZE / 4) - typically 256.
	 * But never go above the ressource limit that is set, or above FD_SETSIZE.
	 */
	if (concurrency == 0) concurrency = (FD_SETSIZE / 4);
	getrlimit(RLIMIT_NOFILE, &lim); if (lim.rlim_cur < concurrency) concurrency = lim.rlim_cur;
	if (concurrency > FD_SETSIZE) concurrency = FD_SETSIZE;
	if (concurrency > 10) concurrency -= 10; /* Save 10 descriptors for stuff like stdin/stdout/stderr and shared libs */

	/* How many tests to do ? */
	for (item = thead; (item); item = item->next) pending++; 
	firstactive = nextinqueue = thead;
	dbgprintf("About to do %d TCP tests running %d in parallel\n", pending, concurrency);

	while (pending > 0) {
		/*
		 * First, see if we need to allocate new sockets and initiate connections.
		 */
		sockok = 1;
		while (sockok && nextinqueue && (activesockets < concurrency)) {

			/*
			 * We need to allocate a new socket that has O_NONBLOCK set.
			 */
			nextinqueue->fd = socket(PF_INET, SOCK_STREAM, 0);
			sockok = (nextinqueue->fd != -1);
			if (sockok) {
				res = fcntl(nextinqueue->fd, F_SETFL, O_NONBLOCK);

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
						if (res != 0) errprintf("Could not bind to source IP %s for test %s: %s\n", 
									nextinqueue->srcaddr, nextinqueue->tspec, strerror(errno));
					}
					else {
						errprintf("Invalid source IP %s for test %s, using default\n", 
							  nextinqueue->srcaddr, nextinqueue->tspec);
					}

					res = 0;
				}

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
		dbgprintf("%d tests pending - %d active tests\n", pending, activesockets);

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
				if (item->readpending)
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
		gettimeofday(&timestamp, &tz);
		tvdiff(&timestamp, &cutoff, &tmo);
		if ((tmo.tv_sec < 0) || (tmo.tv_usec < 0)) {
			/*
			 * This is actually OK, and it does happen occasionally.
			 * It just means that we passed the cutoff-threshold.
			 * So set selres=0 (timeout) without doing the select,
			 * and we will act as correctly.
			 */
			dbgprintf("select timeout is < 0: %d.%06d (cutoff=%d.%06d, timestamp=%d.%06d)\n", 
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
			dbgprintf("Doing select\n");
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
						 * NB: We want the banner EITHER if the GET_BANNER flag is set,
						 *     OR if we need it to match the expect string in the servicedef.
						 */

						item->readpending = (do_talk && !item->silenttest && 
							( (item->svcinfo->flags & TCP_GET_BANNER) || item->svcinfo->exptext ));
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
									dbgprintf("write failed\n");
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
						dbgprintf("read %d bytes from socket\n", res);

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
				((item->certexpires > getcurrenttime(NULL)) ? "valid" : "expired"));
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
	testitem_t *thead = NULL;
	int timeout = 0;
	int concurrency = 0;

	if (xgetenv("BBNETSVCS") == NULL) putenv("BBNETSVCS=");
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
			printf("Run with\n~hobbit/server/bin/bbcmd ./contest --debug 172.16.10.2/25/smtp\n");
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
					thead = testitem;

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

