/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for communicating with the Xymon daemon               */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include "config.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdio.h>

#include <limits.h>
#include <sys/resource.h>
#include <time.h>

#include <sys/ipc.h>
#include <sys/msg.h>

#include "libxymon.h"

#define SENDRETRIES 2

/* These commands go to all Xymon servers */
static char *multircptcmds[] = { "status", "combo", "extcombo", "compress", "data", "notify", "enable", "disable", "drop", "rename", "client", "clientsubmit", "dummy", NULL };
static char errordetails[1024];

static strbuffer_t *msgbuf = NULL;      /* message buffer for one status message */
static int msgcolor;                    /* color of status message in msgbuf */
static int combo_is_local = 0;
static strbuffer_t *xymonmsg = NULL;    /* Complete combo message buffer */

static int xymonmsgqueued;		/* Anything in the buffer ? */

static int maxmsgspercombo = 100;       /* 0 = no limit. 100 is a reasonable default. */
static int max_combosz = 256*1024;
static int sleepbetweenmsgs = 0;

static int backfeedqueue = -1;
static int max_backfeedsz = 16384;
#define ASSUMELARGEMEM 0		/* Reserve and persist larger buffer sizes instead of growing slowly */

static char *comboofsstr = NULL;
static size_t comboofssz = 0;
static unsigned int *combooffsets = NULL;

#define USERBUFSZ 4096

typedef struct myconn_t {
	char tlsbuf[10]; char *tlsptr;		/* This holds the "starttls" command we send when talking to a starttls-enabled xymon server */
	char *prebuf; char *preptr;		/* This holds the "size: N" line, or http request headers */
	char *readbuf, *writebuf;		/* This holds what we read and write during the communication */
	char *readp, *writep;
	size_t readbufsz, lefttowrite;
	strbuffer_t *httpheaderbuf;		/* Holds our data until we have the full http headers */
	char *peer;				/* IP of our peer */
	int port, readmore, starttlspending, waitforhttpheaders;
	enum sslhandling_t usessl;
	sendresult_t result;
	sendreturn_t *response;
	struct myconn_t *next;
} myconn_t;

typedef struct mytarget_t {
	char *targetip;
	int defaultport;
	enum sslhandling_t usessl;
	char *httprequest;
} mytarget_t;

static myconn_t *myhead = NULL, *mytail = NULL;


char *strxymonsendresult(sendresult_t res)
{
	switch (res) {
	  case XYMONSEND_OK               : return "OK";
	  case XYMONSEND_EBADIP           : return "Bad IP address";
	  case XYMONSEND_EIPUNKNOWN       : return "Cannot resolve hostname";
	  case XYMONSEND_ENOSOCKET        : return "Cannot get a socket";
	  case XYMONSEND_ECANNOTDONONBLOCK: return "Non-blocking I/O failed";
	  case XYMONSEND_ECONNFAILED      : return "Connection failed";
	  case XYMONSEND_ESELFAILED       : return "select(2) failed";
	  case XYMONSEND_ETIMEOUT         : return "timeout";
	  case XYMONSEND_EWRITEERROR      : return "write error";
	  case XYMONSEND_EREADERROR       : return "read error";
	  case XYMONSEND_EBADURL          : return "Bad URL";
	  case XYMONSEND_EBADMSG          : return "Bad message";
	  case XYMONSEND_EEMPTY           : return "Empty message";
	  case XYMONSEND_EOTHER           : return "Unspecified xymon error";
	  default			  : return "Unknown error";
	};
}


static enum conn_cbresult_t client_callback(tcpconn_t *connection, enum conn_callback_t id, void *userdata)
{
	enum conn_cbresult_t res = CONN_CBRESULT_OK;
	int n, sslhandshakeinprogress = 0;
	size_t used;
	time_t start, expire;
	myconn_t *rec = (myconn_t *)userdata;

	// dbgprintf("CB: %s\n", conn_callback_names[id]);

	switch (id) {
	  case CONN_CB_CONNECT_COMPLETE:       /* Client mode: New outbound connection succeded */
		rec->writep = rec->writebuf;
		rec->readbufsz = USERBUFSZ;
		rec->readbuf = rec->readp = malloc(rec->readbufsz);
		*(rec->readbuf) = '\0';
		rec->readmore = 1;
		break;

	  case CONN_CB_SSLHANDSHAKE_OK:        /* Client/server mode: SSL handshake completed OK (peer certificate ready) */
		rec->starttlspending = 0;
		rec->readp = rec->readbuf;
		*(rec->readbuf) = '\0';
		break;

	  case CONN_CB_READCHECK:              /* Client/server mode: Check if application wants to read data */
		res = (rec->readmore || rec->starttlspending) ? CONN_CBRESULT_OK : CONN_CBRESULT_FAILED;
		break;

	  case CONN_CB_READ:                   /* Client/server mode: Ready for application to read data w/ conn_read() */
		used = (rec->readp - rec->readbuf);
		if ((rec->readbufsz - used) < USERBUFSZ) {
			rec->readbufsz += USERBUFSZ;
			rec->readbuf = (char *)realloc(rec->readbuf, rec->readbufsz);
			rec->readp = rec->readbuf + used;
		}

		/*
		 * When doing an SSL handshake, CB_READ is invoked. Eventually there
		 * is enough data arriving so that *during* a CB READ, the callback
		 * is recursively invoked with CB_SSL_HANDSHAKE_OK. After that has
		 * completed, we return here from conn_read, and the status has
		 * now been changed to CONN_SSL_READY. We should NOT clear the readmore
		 * flag in this case, since it will happen before we have read
		 * any data. So we use a local flag variable to remember if we
		 * were doing a conn_read as part of the handshake.
		 */
		switch (connection->connstate) {
		  case CONN_SSL_ACCEPT_READ:
		  case CONN_SSL_ACCEPT_WRITE:
		  case CONN_SSL_CONNECT_READ:
		  case CONN_SSL_CONNECT_WRITE:
		  case CONN_SSL_STARTTLS_READ:
		  case CONN_SSL_STARTTLS_WRITE:
		  case CONN_SSL_READ:
		  case CONN_SSL_WRITE:
			sslhandshakeinprogress = 1; break;
		  default:
			sslhandshakeinprogress = 0; break;
		}

		n = conn_read(connection, rec->readp, (rec->readbufsz - used - 1));

		if ((n > 0) && rec->starttlspending) {
			*(rec->readp + n) = '\0';
			// dbgprintf("Read %d bytes while waiting for starttls OK: %s\n", n, rec->readp);
			if ((strncmp(rec->readp, "OK", 2) == 0) && strchr(rec->readp, '\n')) {
				rec->starttlspending = 0;
				return CONN_CBRESULT_STARTTLS;
			}
			else if ((strncmp(rec->readp, "ERR", 3) == 0) && strchr(rec->readp, '\n')) {
				errprintf("Server does not accept STARTTLS\n");
				conn_close_connection(connection, NULL);
				rec->result = XYMONSEND_ECONNFAILED;
				return CONN_CBRESULT_OK;
			}
			else {
				rec->readp += n;
			}
		}
		else if (n > 0) {
			// dbgprintf("Read %d bytes data\n", n);
			char *outstart = rec->readp;
			int outlen = n;

			if (rec->response && rec->waitforhttpheaders) {
				*(rec->readp + n) = '\0';
				addtobuffer(rec->httpheaderbuf, rec->readp);

				outstart = strstr(STRBUF(rec->httpheaderbuf), "\r\n\r\n");
				if (outstart)
					outstart += 4;
				else {
					outstart = strstr(STRBUF(rec->httpheaderbuf), "\n\n");
					if (outstart) outstart += 2;
				}

				if (outstart) {
					rec->waitforhttpheaders = 0;
					outlen -= (rec->readp - outstart);
				}
			}

			if (rec->response && outstart) {
				if (rec->response->respfd) {
					fwrite(outstart, outlen, 1, rec->response->respfd);
				}
				else {
					*(outstart + outlen) = '\0';
					addtobuffer(rec->response->respstr, outstart);
					if (!rec->response->fullresponse && strchr(outstart, '\n'))
						conn_close_connection(connection, NULL);
				}
			}
		}
		else if ((n == 0) && !sslhandshakeinprogress) {
			// dbgprintf("No more data\n");
			rec->readmore = 0;
			conn_close_connection(connection, "r");
		}
		break;

	  case CONN_CB_WRITECHECK:             /* Client/server mode: Check if application wants to write data */
		if (rec->starttlspending)
			res = CONN_CBRESULT_FAILED;
		else
			res = (rec->lefttowrite != 0) ? CONN_CBRESULT_OK : CONN_CBRESULT_FAILED;
		break;

	  case CONN_CB_WRITE:                  /* Client/server mode: Ready for application to write data w/ conn_write() */
		if (rec->tlsptr) {
			n = conn_write(connection, rec->tlsptr, strlen(rec->tlsptr));
			if (n > 0) {
				rec->tlsptr += n;
				if (*rec->tlsptr == '\0') {
					rec->tlsptr = NULL;
					rec->starttlspending = 1;
					// dbgprintf("Switching to STARTTLS pending\n");
				}
				n = 0;	/* Didn't send any message data yet */
			}
		}
		else if (rec->preptr) {
			n = conn_write(connection, rec->preptr, strlen(rec->preptr));
			// dbgprintf("Sent %d bytes of size command\n", n);
			if (n > 0) {
				rec->preptr += n;
				if (*rec->preptr == '\0') rec->preptr = NULL;
				n = 0;	/* Didn't send any message data yet */
			}
		}
		else {
			n = conn_write(connection, rec->writep, rec->lefttowrite);
			// dbgprintf("Sent %d bytes of data\n", n);
			if (n > 0) {
				rec->writep += n;
				rec->lefttowrite -= n;
				if (rec->lefttowrite == 0) {
					conn_close_connection(connection, "w");
				}
			}
		}
		break;

	  case CONN_CB_TIMEOUT:
		conn_close_connection(connection, NULL);
		rec->result = XYMONSEND_ETIMEOUT;
		break;

	  case CONN_CB_CLOSED:                 /* Client/server mode: Connection has been closed */
		break;

	  case CONN_CB_CLEANUP:                /* Client/server mode: Connection cleanup */
		connection->userdata = NULL;
		break;

	  case CONN_CB_SSLHANDSHAKE_FAILED:
	  case CONN_CB_CONNECT_FAILED:
		rec->result = XYMONSEND_ECONNFAILED;
		break;

	  default:
		break;
	}

	return res;
}


static int sendtoall(char *msg, size_t msglen, int timeout, mytarget_t **targets, sendreturn_t *responsebuffer)
{
	myconn_t *myconn;
	int i;
	int maxfd;
	strbuffer_t *cbuf = NULL;
	static int first_run = 1;
	static int v5server = 0;
	static int default_timeout = 0;

	if (first_run) {
	   /* Enable to remove backwards compatibility to v4 servers */
	   v5server = (strcmp(xgetenv("XYMONV5SERVER"), "TRUE") == 0);
	   /* XYMON_TIMEOUT = legacy */
	   default_timeout = (sendtimeout >= 0) ? sendtimeout : 
		atoi(getenv("XYMON_TIMEOUT"))   ? atoi(xgetenv("XYMON_TIMEOUT")) : 
		atoi(xgetenv("XYMONTIMEOUT"));
	   setup_compression_opts();
	   first_run = 0;
	}

	if (timeout == TIMEOUT_USEENV) timeout = default_timeout;

	conn_register_infohandler(NULL, (debug ? INFO_DEBUG : INFO_WARN));
	conn_init_client();

	/*
	 * XXX: This is being somewhat jammed in from the original patches
	 * based on the 4.3 code base. Open questions remain on whether
	 * multi-rcpt commands should or should not be compressed by
	 * default. There's also no point to re-compressing messages again
	 * or trying to compress an encrypted payload. It's possible for 
	 * an extcombo message to be assembled from compressed messages;
	 * it would be nice to have an easy way of skipping these.
	 */
	if (docompress && (strcmp(msg, "compress:") != 0)) {
		cbuf = compress_message_to_strbuffer(comptype, msg, msglen, cbuf, NULL);
		if (cbuf) {
			msglen = STRBUFLEN(cbuf);
			msg = STRBUF(cbuf);	/* original message freed by caller */
			dbgprintf(" - compressed message OK; %zu bytes\n", msglen);
		}
		else errprintf("Error attempting to compress message! Leaving unchanged...\n");
	}

	for (i = 0; (targets[i]); i++) {
		char *ip;
		int portnum;
		enum conn_ipproto_t wantedproto = CONN_IPPROTO_ANY;

		switch (ipprotocol) {
		  case XYMON_IPPROTO_ANY: wantedproto = CONN_IPPROTO_ANY; break;
		  case XYMON_IPPROTO_4: wantedproto = CONN_IPPROTO_V4; break;
		  case XYMON_IPPROTO_6: wantedproto = CONN_IPPROTO_V6; break;
		}

		ip = conn_lookup_ip(targets[i]->targetip, &portnum, wantedproto);

		myconn = (myconn_t *)calloc(1, sizeof(myconn_t));
		strcpy(myconn->tlsbuf, "starttls\n");
		myconn->tlsptr = ((targets[i]->usessl == CONN_SSL_STARTTLS_CLIENT) ? myconn->tlsbuf : NULL);
		myconn->usessl = targets[i]->usessl;
		myconn->lefttowrite = msglen;
		if (targets[i]->httprequest) {
			/* Must strdup() here, since it is freed once per conn_t (connection) - and prebuf will always be freed when connection terminates */
			myconn->prebuf = myconn->preptr = strdup(targets[i]->httprequest);
			if (!debug) {
				/* We dont want to see the HTTP headers except when in debug mode */
				myconn->waitforhttpheaders = 1;
				myconn->httpheaderbuf = newstrbuffer(1024);
			}
		}
		else if (v5server) {
			myconn->prebuf = myconn->preptr = (char *)malloc(20);
			sprintf(myconn->prebuf, "size:%d\n", (int)myconn->lefttowrite);
		}
		myconn->writebuf = msg;
		myconn->peer = strdup(ip ? ip : "");
		myconn->port = (portnum ? portnum : targets[i]->defaultport);
		if (mytail) {
			mytail->next = myconn;
			mytail = myconn;
		}
		else {
			myhead = mytail = myconn;
		}

		if (ip) {
			conn_prepare_connection(myconn->peer, myconn->port, CONN_SOCKTYPE_STREAM, NULL, 
						myconn->usessl, NULL, getenv("XYMONCLIENTCERT"), getenv("XYMONCLIENTKEY"), 
						1000000*timeout, client_callback, myconn);
		}
		else {
			myconn->result = XYMONSEND_EBADIP;
		}
	}

	myhead->response = responsebuffer;	/* Only want the response from the first server returned to the user */

	/* Loop to process data */
	do {
		fd_set fdread, fdwrite;
		int n;
		struct timeval tmo;

		maxfd = conn_fdset(&fdread, &fdwrite);

		if (maxfd > 0) {
			if (timeout) { tmo.tv_sec = 1; tmo.tv_usec = 0; }

			n = select(maxfd+1, &fdread, &fdwrite, NULL, (timeout ? &tmo : NULL));
			if (n < 0) {
				if (errno != EINTR) {
					if (cbuf) freestrbuffer(cbuf);
					return 1;
				}
			}

			conn_process_active(&fdread, &fdwrite);
		}

		conn_trimactive();
	} while (conn_active() && (maxfd > 0));

	if (cbuf) freestrbuffer(cbuf);

	return 0;
}

static mytarget_t **build_targetlist(char *recips, int defaultport, size_t msglength)
{
	/*
	 * Target format: [tls:][IP|hostname]:[portnumber|servicename]
	 */
	mytarget_t **targets;
	char *multilist, *r, *p, *tokr;
	int tcount = 0;

	multilist = strdup(recips);

	targets = (mytarget_t **)malloc(sizeof(mytarget_t *));
	r = strtok_r(multilist, " ,", &tokr);
	while (r) {
		urlelem_t url;

		memset(&url, 0, sizeof(url));

		targets = (mytarget_t **)realloc(targets, (tcount+2)*sizeof(mytarget_t *));
		targets[tcount] = (mytarget_t *)calloc(1, sizeof(mytarget_t));
		if (strncmp(r, "tls:", 4) == 0) {
			r += 4;
			targets[tcount]->usessl = CONN_SSL_STARTTLS_CLIENT;
		}
		else if (strncmp(r, "https:", 6) == 0) {
			targets[tcount]->usessl = CONN_SSL_YES;
		}
		else {
			targets[tcount]->usessl = CONN_SSL_NO;
		}

		/* If an http target, generate the http request headers - and point r to the URL hostname */
		if (strncmp(r, "http", 4) == 0) {
			char lengthstr[10];
			strbuffer_t *httpreq = newstrbuffer(0);

			parse_url(r, &url);
			if (url.parseerror) {
				errprintf("Skipping invalid URL target %s\n", r);
				goto target_done;
			}

			snprintf(lengthstr, sizeof(lengthstr), "%zu", msglength);
			addtobuffer_many(httpreq, 
					"POST ", url.relurl, " HTTP/1.0\r\n",
					"Host: ", url.host, "\r\n",
					"MIME-Version: 1.0\r\n",
					"Content-Type: application/octet-stream\r\n",
					"Content-Length: ", lengthstr, "\r\n",
					"\r\n", NULL);
			targets[tcount]->httprequest = grabstrbuffer(httpreq);
			dbgprintf("Sending HTTP request:\n%s", targets[tcount]->httprequest);

			/* We use the url.ip field for the destination address+port - it is only used for net-tests with http-requests using http://website=10.1.2.3/ notation */
			url.ip = (char *)malloc(strlen(url.host) + 10);
			sprintf(url.ip, "%s:%d", url.host, url.port);
			r = url.ip;
		}

		if (*r == '[') {
			/* IPv6 address in brackets [1234:5678:abcd::9]:1984 */
			r++;
			p = strchr(r, ']');
			if (p) {
				*p = '\0'; p++;
				targets[tcount]->defaultport = ((*p == ':') ? atoi(p+1) : defaultport);
			}
			else {
				/* Not really valid format, but let it pass */
				targets[tcount]->defaultport = defaultport;
			}
		}
		else {
			p = strrchr(r, ':');
			if (p) {
				*p = '\0';
				targets[tcount]->defaultport = atoi(p+1);
			}
			else {
				targets[tcount]->defaultport = defaultport;
			}
		}

		targets[tcount]->targetip = strdup(r);
		tcount++;
target_done:
		free_urlelem_data(&url);
		r = strtok_r(NULL, " ,", &tokr);
	}
	targets[tcount] = NULL;
	xfree(multilist);

	return targets;
}

int sendmessage_init_local(void)
{
        backfeedqueue = setup_feedback_queue(CHAN_CLIENT);
	if (backfeedqueue == -1) return -1;

	max_backfeedsz = 1024*shbufsz(C_FEEDBACK_QUEUE)-1;
	dbgprintf("Max backfeed size set to: %ju\n", max_backfeedsz);
	return max_backfeedsz;
}

void sendmessage_finish_local(void)
{
        close_feedback_queue(backfeedqueue, CHAN_CLIENT);
}

sendresult_t sendmessage_local(char *msg, size_t msglen)
{
	int n, done = 0, tries = 0;
	strbuffer_t *cbuf = NULL;
	static int first_run = 1;

	if (first_run) {
	   setup_compression_opts();
	   first_run = 0;
	}

	if ((!msg) || (!msglen)) return XYMONSEND_EREADERROR;

	if (backfeedqueue == -1) {
		errprintf("sendmessage_local: no backfeed queue present; falling back to normal send\n");
		return sendmessage_safe(msg, msglen, NULL, XYMON_TIMEOUT, NULL);
	}

	/* see sendtoall() above */
	if (docompress && (strcmp(msg, "compress:") != 0)) {
		cbuf = compress_message_to_strbuffer(comptype, msg, msglen, cbuf, NULL);
		if (cbuf) {
			msglen = STRBUFLEN(cbuf);
			msg = STRBUF(cbuf);	/* original message freed by caller */
			dbgprintf(" - compressed message OK; %zu bytes\n", msglen);
		}
		else errprintf("Error attempting to compress message! Leaving unchanged...\n");
	}


	/* Make sure we dont overflow the message buffer. */
	/* Perhaps we should save the original msglen and truncate the uncompressed version? */
	if (msglen > max_backfeedsz) {
		errprintf("Truncating backfeed channel message from %d to %d\n", msglen, max_backfeedsz);
		*(msg+max_backfeedsz) = '\0';
		msglen = max_backfeedsz;
	}

	/* Retry a few times if not immediately available, otherwise return error */
	do {
#if defined(__OpenBSD__) || defined(__dietlibc__)
		n = msgsnd(backfeedqueue, msg, (unsigned long)msglen, IPC_NOWAIT);
#else
		n = msgsnd(backfeedqueue, msg, (msglen_t)msglen, IPC_NOWAIT);
#endif
		if ((n == 0) || ((n == -1) && (errno != EINTR) && (errno != EAGAIN))) done = 1;
	} while (!done && (tries++ < SENDRETRIES));

	if (n == -1) {
		errprintf("Sending %d bytes via backfeed channel failed: %s\n", msglen, strerror(errno));
		if (errno == EIDRM) {
			int newqueue = setup_feedback_queue(CHAN_CLIENT);
			if (newqueue != -1) {
				backfeedqueue = newqueue;
				/* Try one more time */
#if defined(__OpenBSD__) || defined(__dietlibc__)
				n = msgsnd(backfeedqueue, msg, (unsigned long)msglen, IPC_NOWAIT);
#else
				n = msgsnd(backfeedqueue, msg, (msglen_t)msglen, IPC_NOWAIT);
#endif
				if (cbuf) freestrbuffer(cbuf);
				if (n != -1) return XYMONSEND_OK;
			}
			else errprintf("BFQ re-scan failed; message lost\n");
		}
		if (cbuf) freestrbuffer(cbuf);
		return XYMONSEND_ECONNFAILED;
	}
	// dbgprintf("Sending %d bytes via backfeed channel succeeded (%d retries)\n", msglen, tries);

	if (cbuf) freestrbuffer(cbuf);
	return XYMONSEND_OK;
}

sendresult_t sendmessage_local_buffer(strbuffer_t *buf)
{
	if (buf) return sendmessage_local(STRBUF(buf), STRBUFLEN(buf));
	errprintf("BUG: sendmessage_local_buffer given empty message\n");
	return XYMONSEND_EEMPTY;
}

/* TODO: http targets, http proxy */
sendresult_t sendmessage_safe(char *msg, size_t msglen, char *recipient, int timeout, sendreturn_t *response)
{
	static mytarget_t **defaulttargets = NULL;
	static int defaultport = 0;
	mytarget_t **targets;
	int i;
	myconn_t *walk;
	sendresult_t res;

	if (dontsendmessages) {
		fprintf(stdout, "%s\n", msg);
		fflush(stdout);
		return XYMONSEND_OK;
	}

	if (defaultport == 0) {
		if (xgetenv("XYMONDPORT")) defaultport = atoi(xgetenv("XYMONDPORT"));
		if (defaultport == 0) defaultport = conn_lookup_portnumber("bb", 1984);
	}

	if (defaulttargets == NULL) {
		char *recips = xgetenv("XYMSRV");

		if ((strcmp(recips, "0.0.0.0") == 0) || (strcmp(recips, "0") == 0) || (strcmp(recips, "::") == 0)) 
			recips = xgetenv("XYMSERVERS");

		defaulttargets = build_targetlist(recips, defaultport, 0);
	}

	if ((recipient == NULL) || 
		((strcmp(recipient, "0.0.0.0") == 0) || (strcmp(recipient, "0") == 0) || (strcmp(recipient, "::") == 0)) ) {
		targets = defaulttargets;
		dbgprintf("Using default target list\n");
	}
	else targets = build_targetlist(recipient, defaultport, msglen);

	sendtoall(msg, msglen, timeout, targets, response);

	res = myhead->result;	/* Always return the result from the first server */

	for (walk = myhead; (walk); walk = walk->next) {
		if (walk->result != XYMONSEND_OK) {
			char *eoln;

			eoln = strchr(msg, '\n'); if (eoln) *eoln = '\0';
			errprintf("Whoops ! Failed to send message (%s)\n", strxymonsendresult(res));
			errprintf("->  %s\n", errordetails);
			errprintf("->  Recipient '%s', timeout %d\n", walk->peer, timeout);
			errprintf("->  1st line: '%s'\n", msg);
			if (eoln) *eoln = '\n';
		}
	}

	while (myhead) {
		walk = myhead; myhead = myhead->next;

		if (walk->peer) xfree(walk->peer);
		if (walk->readbuf) xfree(walk->readbuf);
		if (walk->prebuf) xfree(walk->prebuf);
		if (walk->httpheaderbuf) xfree(walk->httpheaderbuf);
		xfree(walk);
	}
	mytail = NULL;

	if (targets != defaulttargets) {
		int i;

		for (i = 0; (targets[i]); i++) {
			xfree(targets[i]->targetip);
			if (targets[i]->httprequest) xfree(targets[i]->httprequest);
			xfree(targets[i]);
		}

		xfree(targets);
	}

	/* Give it a break */
	if (sleepbetweenmsgs) usleep(sleepbetweenmsgs);

	return res;
}


void setproxy(char *proxy)
{
}


sendreturn_t *newsendreturnbuf(int fullresponse, FILE *respfd)
{
	sendreturn_t *result;

	result = (sendreturn_t *)calloc(1, sizeof(sendreturn_t));
	result->fullresponse = fullresponse;
	result->respfd = respfd;
	if (!respfd) {
		/* No response file, so return it in a strbuf */
		result->respstr = newstrbuffer(0);
	}

	return result;
}

void freesendreturnbuf(sendreturn_t *s)
{
	if (!s) return;
	if (s->respstr) freestrbuffer(s->respstr);
	xfree(s);
}

char *getsendreturnstr(sendreturn_t *s, int takeover)
{
	char *result = NULL;

	if (!s) return NULL;
	if (!s->respstr) return NULL;
	if (takeover) {
		/*
		 * We cannot leave respstr as NULL, because later calls 
		 * to sendmessage() might re-use this sendreturn_t struct
		 * and expect to get the data back. So allocate a new
		 * responsebuffer for future use - if it isn't used, it
		 * will be freed by freesendreturnbuf().
		 */
		result = grabstrbuffer(s->respstr);
		s->respstr = newstrbuffer(0);
	}
	else result = STRBUF(s->respstr);

	return result;
}


/* Like sendmessage, but given a strbuffer -- safer if we're passing around binary/compressed data */
sendresult_t sendmessage_buffer(strbuffer_t *msgbuf, char *recipient, int timeout, sendreturn_t *response)
{
	if (msgbuf) return sendmessage_safe(STRBUF(msgbuf), STRBUFLEN(msgbuf), recipient, timeout, response);
	errprintf("BUG: sendmessage_buffer given empty message\n");
	return XYMONSEND_EEMPTY;
}


/* Routines for handling combo message transmission */
static void combo_params(void)
{
	static int issetup = 0;

	if (issetup) return;

	issetup = 1;

	if (xgetenv("MAXMSGSPERCOMBO")) maxmsgspercombo = atoi(xgetenv("MAXMSGSPERCOMBO"));
	if (maxmsgspercombo == 0) {
		/* Force it to 100 */
		errprintf("MAXMSGSPERCOMBO is 0, setting it to 100\n");
		maxmsgspercombo = 100;
	}

	if (xgetenv("SLEEPBETWEENMSGS")) sleepbetweenmsgs = atoi(xgetenv("SLEEPBETWEENMSGS"));

	/* 'extcombo' + (space + unsigned int), repeat for maxmsgspercombo */
	comboofssz = (1 + 10)*maxmsgspercombo;
	comboofsstr = (char *)malloc(comboofssz+1);
	combooffsets = (unsigned int *)malloc((maxmsgspercombo+1)*sizeof(unsigned int));
}

void combo_start(void)
{
	combo_params();

	memset(comboofsstr, ' ', comboofssz);
	memcpy(comboofsstr, "extcombo", 8);
	*(comboofsstr + comboofssz) = '\0';

	memset(combooffsets, 0, (maxmsgspercombo+1)*sizeof(unsigned int));
	combooffsets[0] = comboofssz;

	if (xymonmsg == NULL) xymonmsg = newstrbuffer(ASSUMELARGEMEM ? 1024*shbufsz(C_LAST) : 0 );
	clearstrbuffer(xymonmsg);
	addtobufferraw(xymonmsg, comboofsstr, comboofssz);
	xymonmsgqueued = 0;
	combo_is_local = 0;
}

void combo_start_local(void)
{
	combo_start();
	combo_is_local = 1;
}

static void combo_flush(void)
{
	int i;
	char *outp;

	/* Flushing may be attempted even if there's nothing waiting to go out. That's fine. */
	if (!xymonmsgqueued) return;
	dbgprintf("Flushing combo message\n");

	outp = strchr(STRBUF(xymonmsg), ' ');
	for (i = 0; (i <= xymonmsgqueued); i++) {
		outp += sprintf(outp, " %d", combooffsets[i]);
	}
	*outp = '\n';
	
	if (debug) {
		char *p1, *p2;

		p1 = p2 = STRBUF(xymonmsg);

		do {
			p2++;
			p1 = strstr(p2, "\nstatus ");
			if (p1) {
				p1++; /* Skip the newline */
				p2 = strchr(p1, '\n');
				if (p2) *p2='\0';
				printf("      %s\n", p1);
				if (p2) *p2='\n';
			}
		} while (p1 && p2);
	}

	if (combo_is_local) {
		sendmessage_local_buffer(xymonmsg);
		combo_start_local();
	}
	else {
		sendmessage_buffer(xymonmsg, NULL, XYMON_TIMEOUT, NULL);
		combo_start();
	}
}

static int combo_hasroom(size_t len)
{
	if (combo_is_local) {
		/* Check that message fits into the backfeed message buffer AND that we haven't exceeded maxmsgspercombo */
		dbgprintf(" combo_hasroom -> current state (bfq): xymonmsg sz: %zd, buffer sz: %zd, max_backfeedsz: %zd; maxmsgspercombo: %d, messages queued so far: %d\n", STRBUFLEN(xymonmsg), len, max_backfeedsz, maxmsgspercombo, xymonmsgqueued);
		if ( ((STRBUFLEN(xymonmsg) + len) >= max_backfeedsz) || (maxmsgspercombo && (xymonmsgqueued >= maxmsgspercombo)) ) return 0;
	}
	else {
		/* Check that we haven't exceeded msgmsgspercombo */
		/* TODO: We don't yet have a paradigm for max combo size over the network */
		dbgprintf(" combo_hasroom -> current state (tcp): xymonmsg sz: %zd, buffer sz: %zd; maxmsgspercombo: %d, messages queued so far: %d\n", STRBUFLEN(xymonmsg), len, maxmsgspercombo, xymonmsgqueued);
		if (maxmsgspercombo && (xymonmsgqueued >= maxmsgspercombo)) return 0;
	}
	/* OK to proceed */
	return 1;
}

void combo_addcharbytes(char *p, size_t len)
{
	if (!combo_hasroom(len)) combo_flush();

	strbuf_addtobuffer(xymonmsg, p, len);
	combooffsets[++xymonmsgqueued] = STRBUFLEN(xymonmsg);
}

void combo_addchar(char *p)
{
	if (!p) { errprintf("BUG: combo_addchar received a null pointer\n"); return; }
	return combo_addcharbytes(p, strlen(p));
}

void combo_add(strbuffer_t *buf)
{
	if (!combo_hasroom(STRBUFLEN(buf))) combo_flush();

	addtostrbuffer(xymonmsg, buf);
	combooffsets[++xymonmsgqueued] = STRBUFLEN(xymonmsg);
}

void combo_end(void)
{
	combo_flush();
	combo_is_local = 0;
}

void init_status(int color)
{
	if (msgbuf == NULL) msgbuf = newstrbuffer(0);
	clearstrbuffer(msgbuf);
	msgcolor = color;
}

void addtostatus(char *p)
{
	addtobuffer(msgbuf, p);
}

void addtostrstatus(strbuffer_t *p)
{
	addtostrbuffer(msgbuf, p);
}

void finish_status(void)
{
	if (debug) {
		char *p = strchr(STRBUF(msgbuf), '\n');

		if (p) *p = '\0';
		dbgprintf("Adding to combo msg: %s\n", STRBUF(msgbuf));
		if (p) *p = '\n';
	}

	combo_add(msgbuf);
}

