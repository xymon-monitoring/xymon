/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: dns2.c 6743 2011-09-03 15:44:52Z storner $";

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#include "libxymon.h"
#include "tcptalk.h"
#include "tcphttp.h"
#include "ntptalk.h"
#include "dnstalk.h"

static char *silent_dialog[] = {
	"READ", "CLOSE", NULL
};

static char *telnet_dialog[] = {
	"READ", "CLOSE", NULL
};

static char *smtp_dialog[] = {
	"EXPECT:220",
	"SEND:HELO hswn.dk\r\n",
	"EXPECT:250",
	"SEND:MAIL FROM:<xymon>\r\n",
	"EXPECT:250",
	"SEND:RSET\r\n",
	"EXPECT:250",
	"SEND:QUIT\r\n",
	"EXPECT:221",
	"CLOSE",
	NULL
};

static char *xymonping_dialog[] = {
	"SEND:size:4\nping",
	"EXPECT:xymon",
	"CLOSE",
	NULL
};

static char *pop_dialog[] = {
	"EXPECT:+OK",
	"CLOSE",
	NULL
};

static char *http_dialog[] = { NULL };
static char *ntp_dialog[] = { NULL };
static char *dns_dialog[] = { NULL };

typedef struct connlist_t {
	char *listname;
	myconn_t *head, *tail;
	int len;
} connlist_t;

connlist_t pendingtests = { "pending", NULL, NULL, 0 };
connlist_t activetests = { "active", NULL, NULL, 0 };
connlist_t donetests = { "done", NULL, NULL, 0 };
connlist_t failedtests = { "failed", NULL, NULL, 0 };
static int totaltests = 0;		/* Total number of tests */


static void list_item_move(connlist_t *tolist, connlist_t *fromlist, myconn_t *rec)
{
	dbgprintf("Moving test %s from %s to %s\n", rec->testspec, (fromlist ? fromlist->listname : "<null>"), tolist->listname);

	if (fromlist) {
		if (rec == fromlist->head) {
			/* Removing the head of the list */
			fromlist->head = rec->next;
			if (rec == fromlist->tail) fromlist->tail = NULL;
		}
		else if (rec == fromlist->tail) {
			/* Removing the tail of the list */
			fromlist->tail = rec->previous;
			fromlist->tail->next = NULL;
		}
		else {
			/* We are removing an item inside the list */
			rec->previous->next = rec->next;
		}

		fromlist->len -= 1;
	}

	if (tolist->tail) {
		tolist->tail->next = rec;
		tolist->tail = rec;
		rec->previous = tolist->tail;
		rec->next = NULL;
	}
	else {
		tolist->head = tolist->tail = rec;
		rec->previous = rec->next = NULL;
	}

	tolist->len += 1;
	rec->next = NULL;
}

static int last_write_step(myconn_t *rec)
{
	int i;

	for (i = rec->step; (rec->dialog[i]); i++)
		if (strncmp(rec->dialog[i], "SEND:", 5) == 0) return 0;

	return 1;
}

static int telnet_datahandler(myconn_t *rec, int iobytes)
{
	unsigned char *inp, *outp;
	int inlen = iobytes;
	int optionid, optionresponse;

	inp = (unsigned char *)rec->readbuf;
	outp = (unsigned char *)rec->writebuf;

	while (inlen) {
		if ((inlen < 3) || (*inp != 255)) {
			/* No more options, save rest of the buffer as the start of the banner */
			if (inp != (unsigned char *)rec->readbuf) memmove((unsigned char *)rec->readbuf, inp, inlen);
			*(rec->readbuf + inlen) = '\0';
			rec->readp = rec->readbuf;
			rec->writep = rec->writebuf;
			*rec->writep = '\0';	/* There may be some telnet options left here, just discard them */
			rec->istelnet = 0;
			return inlen;
		}

		/* We know *inp == 255, i.e. telnet option. Send an option response back */
		inp++; inlen--;
		*outp = 255; outp++;

		/* See what the question is */
		switch (*inp) {
		  case 251: case 252: /* WILL or WONT */
			optionresponse = 254;	/* -> WONT */
			break;
		  case 253: case 254: /* Do or DONT */
			optionresponse = 252;	/* -> DONT */
			break;
		  default:
			optionresponse = 0;
			break;
		}
		inp++; inlen--;

		/* Get the ID for this option */
		optionid = *inp;
		inp++; inlen--;

		if (optionresponse) {
			*outp = optionresponse; outp++;
			*outp = optionid; outp++;
		}
	}

	if ((unsigned char *)rec->writebuf != outp) {
		rec->writep = rec->writebuf;
		rec->istelnet = -(outp - (unsigned char *)rec->writebuf);
	}

	return 0;
}

static int http_datahandler(myconn_t *rec, int iobytes, int startoffset, int *advancestep)
{
	char *endofhdrs;
	int http1subver;
	char *xferencoding;
	int len = iobytes;
	char *bol, *buf;
	int hdrbytes, bodybytes, n;

	*advancestep = 0;

	switch (rec->httpdatastate) {
	  case HTTPDATA_HEADERS:
		addtobufferraw(rec->httpheaders, rec->readbuf+startoffset, (iobytes - startoffset));

check_for_endofheaders:
		/* 
		 * Now see if we have the end-of-headers delimiter.
		 * This SHOULD be <cr><lf><cr><lf>, but RFC 2616 says
		 * you SHOULD recognize just plain <lf><lf>.
		 * So try the second form, if the first one is not there.
		 */
		endofhdrs = strstr(STRBUF(rec->httpheaders), "\r\n\r\n");
		if (endofhdrs) {
			endofhdrs += 4;
			/* Chop the non-header section of data from the headers */
			strbufferchop(rec->httpheaders, strlen(endofhdrs));
		}
		else {
			endofhdrs = strstr(STRBUF(rec->httpheaders), "\n\n");
			if (endofhdrs) endofhdrs += 2;
		}

		if (!endofhdrs)
			/* No more to do for now, but pass the databyte-count back to the caller for further processing. */
			return iobytes;


		/* We have an end-of-header delimiter, but it could be just a "100 Continue" response */
		sscanf(STRBUF(rec->httpheaders), "HTTP/1.%d %d", &http1subver, &rec->httpstatus);
		if (rec->httpstatus == 100) {
			/* 
			 * It's a "100"  continue-status.
			 * Just drop this set of headers, and re-do the end-of-headers check.
			 */
			strbuffer_t *newhdrbuf = newstrbuffer(0);
			addtobuffer(newhdrbuf, endofhdrs);
			freestrbuffer(rec->httpheaders);
			rec->httpheaders = newhdrbuf;
			goto check_for_endofheaders;
		}

		/* Have all the http headers now */
		rec->httpdatastate = HTTPDATA_BODY;


		/* 
		 * Find the "Transfer-encoding: " header (if there is one) to see if the transfer uses chunks,
		 * and grab "Content-Length:" to get the length of the body.
		 */
		xferencoding = NULL;
		bol = STRBUF(rec->httpheaders);
		while (bol && !xferencoding && !rec->httpcontentleft) {
			if (strncasecmp(bol, "Transfer-encoding:", 18) == 0) {
				bol += 18; bol += strspn(bol, " ");
				xferencoding = bol;
			}
			else if (strncasecmp(bol, "Content-Length:", 15) == 0) {
				bol += 15; bol += strspn(bol, " ");
				rec->httpcontentleft = atoi(bol);
			}
			else {
				bol = strchr(bol, '\n'); if (bol) bol++;
			}
		}

		if (xferencoding && (strncasecmp(xferencoding, "chunked", 7) == 0)) 
			rec->httpchunkstate = HTTP_CHUNK_INIT;
		else {
			rec->httpchunkstate = (rec->httpcontentleft > 0) ? HTTP_CHUNK_NOTCHUNKED : HTTP_CHUNK_NOTCHUNKED_NOCLEN;
		}

		/* Done with all the http header processing. Call ourselves to handle any remaining data we got after the headers */
		hdrbytes = (endofhdrs - STRBUF(rec->httpheaders));
		bodybytes = iobytes - hdrbytes;
		http_datahandler(rec, bodybytes, hdrbytes, advancestep); 
		break;


	  case HTTPDATA_BODY:
		buf = rec->readbuf+startoffset;
		while (len > 0) {
			bodybytes = 0;

			switch (rec->httpchunkstate) {
			  case HTTP_CHUNK_NOTCHUNKED:
			  case HTTP_CHUNK_NOTCHUNKED_NOCLEN:
				bodybytes = len;
				break;

			  case HTTP_CHUNK_INIT:
				/* We're about to pick up a chunk length */
				rec->httpleftinchunk = 0;
				rec->httpchunkstate = HTTP_CHUNK_GETLEN;
				break;

			  case HTTP_CHUNK_GETLEN:
				/* We are collecting the length of the chunk */
				n = hexvalue(*buf);
				if (n == -1) {
					rec->httpchunkstate = HTTP_CHUNK_SKIPLENCR;
				}
				else {
					rec->httpleftinchunk = rec->httpleftinchunk*16 + n;
					buf++; len--;
				}
				break;
				
			  case HTTP_CHUNK_SKIPLENCR:
				/* We've got the length, now skip to the next LF */
				if (*buf == '\n') {
					buf++; len--; 
					rec->httpchunkstate = ((rec->httpleftinchunk > 0) ? HTTP_CHUNK_DATA : HTTP_CHUNK_NOMORE);
				}
				else if ((*buf == '\r') || (*buf == ' ')) {
					buf++; len--;
				}
				else {
					errprintf("Yikes - strange data following chunk len. Saw a '%c'\n", *buf);
					buf++; len--;
				}
				break;

			  case HTTP_CHUNK_DATA:
				/* Passing off the data */
				bodybytes = (len > rec->httpleftinchunk) ? rec->httpleftinchunk : len;
				rec->httpleftinchunk -= bodybytes;
				if (rec->httpleftinchunk == 0) rec->httpchunkstate = HTTP_CHUNK_SKIPENDCR;
				break;

			  case HTTP_CHUNK_SKIPENDCR:
				/* Skip CR/LF after a chunk */
				if (*buf == '\n') {
					buf++; len--; rec->httpchunkstate = HTTP_CHUNK_DONE;
				}
				else if (*buf == '\r') {
					buf++; len--;
				}
				else {
					errprintf("Yikes - strange data following chunk data. Saw a '%c'\n", *buf);
					buf++; len--;
				}
				break;

			  case HTTP_CHUNK_DONE:
				/* One chunk is done, continue with the next */
				rec->httpchunkstate = HTTP_CHUNK_GETLEN;
				break;

			  case HTTP_CHUNK_NOMORE:
				/* All chunks done. Skip the rest (trailers) */
				len = 0;
				break;
			}

			/* bodybytes holds the number of bytes data from buf that should go to userspace */
			if (bodybytes > 0) {
				addtobufferraw(rec->httpbody, buf, bodybytes);
				buf += bodybytes;
				len -= bodybytes;
				if ((rec->httpcontentleft > 0) && (rec->httpcontentleft >= bodybytes)) rec->httpcontentleft -= bodybytes;
			}
		}

		/* Done processing body content. Now see if we have all of it - if we do, then proceed to next step. */
		switch (rec->httpchunkstate) {
		  case HTTP_CHUNK_NOTCHUNKED:
			if (rec->httpcontentleft <= 0) *advancestep = 1;
			break;

		  case HTTP_CHUNK_NOTCHUNKED_NOCLEN:
			/* We have no content-length: header, so keep going until we do two null-reads */
			if ((rec->httplastbodyread == 0) && (bodybytes == 0))
				*advancestep = 1;
			else
				rec->httplastbodyread = bodybytes;
			break;

		  case HTTP_CHUNK_NOMORE:
			*advancestep = 1;
			break;

		  default:
			break;
		}

		break;
	}


	return iobytes;
}



#define USERBUFSZ 4096

int tcp_standard_callback(tcpconn_t *connection, enum conn_callback_t id, void *userdata)
{
	int res = 0, n, advancestep;
	size_t used;
	time_t start, expire;
	char *certsubject;
	myconn_t *rec = (myconn_t *)userdata;

	switch (id) {
	  case CONN_CB_CONNECT_START:          /* Client mode: New outbound connection start */
		break;

	  case CONN_CB_CONNECT_FAILED:         /* Client mode: New outbound connection failed */
		rec->talkresult = TALK_CONN_FAILED;
		break;

	  case CONN_CB_CONNECT_COMPLETE:       /* Client mode: New outbound connection succeded */
		rec->textlog = newstrbuffer(0);
		rec->talkresult = TALK_OK;		/* Will change below if we fail later */
		rec->readbufsz = USERBUFSZ;
		rec->readbuf = rec->readp = malloc(rec->readbufsz);
		*(rec->readbuf) = '\0';
		rec->writebuf = rec->writep = malloc(USERBUFSZ);
		*(rec->writebuf) = '\0';
		break;

	  case CONN_CB_SSLHANDSHAKE_OK:        /* Client/server mode: SSL handshake completed OK (peer certificate ready) */
		certsubject = conn_peer_certificate(connection, &start, &expire);
		if (certsubject) {
			rec->peercertificate = strdup(certsubject);
			rec->peercertificateexpiry = expire;
		}
		break;

	  case CONN_CB_SSLHANDSHAKE_FAILED:    /* Client/server mode: SSL handshake failed (connection will close) */
		rec->talkresult = TALK_BADSSLHANDSHAKE;
		break;

	  case CONN_CB_READCHECK:              /* Client/server mode: Check if application wants to read data */
		if (!rec->dialog[rec->step])
			res = 0;
		else if (rec->istelnet > 0)
			res = 1;
		else
			res = ((strncmp(rec->dialog[rec->step], "EXPECT:", 7) == 0) || (strncmp(rec->dialog[rec->step], "READ", 4) == 0));
		break;

	  case CONN_CB_READ:                   /* Client/server mode: Ready for application to read data w/ conn_read() */
		/* Make sure we have some buffer space */
		used = (rec->readp - rec->readbuf);
		if ((rec->readbufsz - used) < USERBUFSZ) {
			rec->readbufsz += USERBUFSZ;
			rec->readbuf = (char *)realloc(rec->readbuf, rec->readbufsz);
			rec->readp = rec->readbuf + used;
		}
		/* Read the data */
		n = conn_read(connection, rec->readp, (rec->readbufsz - used - 1));
		if (n > 0) rec->bytesread += n;

		/* Process data for some protocols */
		if (rec->istelnet) n = telnet_datahandler(rec, n);
		if (rec->talkprotocol == TALK_PROTO_HTTP) n = http_datahandler(rec, n, 0, &advancestep);

		/* Save the data */
		if (n > 0) {
			*(rec->readp + n) = '\0';
			if (rec->talkprotocol == TALK_PROTO_PLAIN) addtobuffer(rec->textlog, rec->readp);
			rec->readp += n;
		}

		/* See how the dialog is progressing */
		if (strncmp(rec->dialog[rec->step], "EXPECT:", 7) == 0) {
			int explen = strlen(rec->dialog[rec->step]+7);

			if ((n < explen) && (strncmp(rec->readbuf, rec->dialog[rec->step]+7, n) == 0)) {
				/* 
				 * Got the right data so far, but not the complete amount.
				 * Do nothing, we'll just keep reading until we have all of the data
				 */
			}
			else if (strncmp(rec->readbuf, rec->dialog[rec->step]+7, explen) == 0) {
				/* Got the expected data, go to next step */
				rec->step++;
				rec->readp = rec->readbuf;
				*rec->readp = '\0';
			}
			else {
				/* Got some unexpected data, give up */
				rec->talkresult = TALK_BADDATA;
				conn_close_connection(connection, NULL);
			}
		}
		else if (strcmp(rec->dialog[rec->step], "READALL") == 0) {
			/* No need to save the data twice (we store it in rec->textlog), so reset the readp to start of our readbuffer */
			rec->readp = rec->readbuf;
			*(rec->readp) = '\0';
			if (advancestep) rec->step++;
		}
		else if (strcmp(rec->dialog[rec->step], "READ") == 0) {
			rec->step++;
		}

		res = n;
		break;

	  case CONN_CB_WRITECHECK:             /* Client/server mode: Check if application wants to write data */
		if (!rec->dialog[rec->step])
			res = 0;
		else if (rec->istelnet != 0)
			res = (rec->istelnet < 0);
		else {
			if ((*rec->writep == '\0') && (strncmp(rec->dialog[rec->step], "SEND:", 5) == 0)) {
				strcpy(rec->writebuf, rec->dialog[rec->step]+5);
				rec->writep = rec->writebuf;
			}
			res = (*rec->writep != '\0');
		}
		break;

	  case CONN_CB_WRITE:                  /* Client/server mode: Ready for application to write data w/ conn_write() */
		if (rec->istelnet < 0) {
			n = conn_write(connection, rec->writep, -(rec->istelnet));
			rec->writep += n;
			rec->istelnet += n; if (rec->istelnet == 0) rec->istelnet = 1;
		}
		else {
			n = conn_write(connection, rec->writep, strlen(rec->writep));
			if (n > 0) {
				rec->byteswritten += n;
				if (rec->talkprotocol == TALK_PROTO_PLAIN) addtobufferraw(rec->textlog, rec->writep, n);
				rec->writep += n;
				if (*rec->writep == '\0') {
					rec->step++;	/* Next step */
					if (last_write_step(rec)) {
						conn_close_connection(connection, "w");
					}
				}
			}
		}
		res = n;
		break;

	  case CONN_CB_TIMEOUT:
		rec->talkresult = TALK_CONN_TIMEOUT;
		conn_close_connection(connection, NULL);
		break;

	  case CONN_CB_CLOSED:                 /* Client/server mode: Connection has been closed */
		/* See if we need to report an error from closing the connection unexpectedly */
		if ((rec->talkresult == TALK_OK) && rec->dialog[rec->step]) {
			/*
			 * We should only close if
			 * - we hit a CLOSE command
			 * - we hit the end of the command list (NULL dialog step)
			 * - peer disconnects during a READ step
			 * So if the current step is NOT a CLOSE or a READALL step, then 
			 * the close was unexpected - so flag it as an error.
			 */
			if ((strcmp(rec->dialog[rec->step], "CLOSE") != 0) && (strcmp(rec->dialog[rec->step], "READALL") != 0))
				rec->talkresult = TALK_INTERRUPTED;
		}
		rec->elapsedms = connection->elapsedms;
		return 0;

	  case CONN_CB_CLEANUP:                /* Client/server mode: Connection cleanup */
		if (rec->readbuf) xfree(rec->readbuf);
		if (rec->writebuf) xfree(rec->writebuf);
		connection->userdata = NULL;
		test_is_done(rec);
		return 0;

	  default:
		break;
	}

	/* Note: conn_read/write may have zap'ed the connection entry and we have recursively been called to cleanup the connection entry */
	if ((connection->connstate != CONN_DEAD) && rec && rec->dialog[rec->step] && (strcmp(rec->dialog[rec->step], "CLOSE") == 0)) {
		conn_close_connection(connection, NULL);
	}

	return res;
}


void *add_tcp_test(char *destinationip, int destinationport, char *sourceip, char *testspec, char **dialog)
{
	myconn_t *newtest;

	totaltests++;
	newtest = (myconn_t *)calloc(1, sizeof(myconn_t));
	newtest->netparams.destinationip = strdup(destinationip);
	newtest->netparams.destinationport = destinationport;
	newtest->netparams.socktype = CONN_SOCKTYPE_STREAM;
	newtest->netparams.callback = tcp_standard_callback;
	newtest->netparams.sourceip = (sourceip ? strdup(sourceip) : NULL);
	newtest->testspec = strdup(testspec);

	if (dialog == http_dialog) {
		newtest->talkprotocol = TALK_PROTO_HTTP;
		newtest->dialog = build_http_dialog(testspec);
		newtest->httpheaders = newstrbuffer(0);
		newtest->httpbody = newstrbuffer(0);
	}
	else if (dialog == ntp_dialog) {
		newtest->talkprotocol = TALK_PROTO_NTP;
		newtest->netparams.socktype = CONN_SOCKTYPE_DGRAM;
		newtest->netparams.callback = ntp_callback;
	}
	else if (dialog == dns_dialog) {
		newtest->talkprotocol = TALK_PROTO_DNSQUERY;
		newtest->dnsstatus = DNS_NOTDONE;
		dns_init_channel(newtest);
		/* The DNS-specific routines handle the rest */
	}
	else {
		newtest->talkprotocol = TALK_PROTO_PLAIN;
		newtest->dialog = dialog;
	}

	list_item_move(&pendingtests, NULL, newtest);

	return newtest;
}


#define CONCURRENCY 20
#define TIMEOUT 10

void run_tcp_tests(void)
{
	int maxfd;

	/* Loop to process data */
	do {
		fd_set fdread, fdwrite;
		int n, dodns;
		struct timeval tmo;

		/* Start some more tests */
		while (pendingtests.len && (activetests.len < CONCURRENCY)) {
			switch (pendingtests.head->talkprotocol) {
			  case TALK_PROTO_PLAIN:
			  case TALK_PROTO_HTTP:
			  case TALK_PROTO_NTP:
				if (conn_prepare_connection(pendingtests.head->netparams.destinationip, 
							    pendingtests.head->netparams.destinationport, 
							    pendingtests.head->netparams.socktype,
							    pendingtests.head->netparams.sourceip, 
							    (pendingtests.head->netparams.sslver != SSLVERSION_NOSSL), NULL, NULL, 
							    TIMEOUT*1000,
							    pendingtests.head->netparams.callback, pendingtests.head)) {
					list_item_move(&activetests, &pendingtests, pendingtests.head);
				}
				else {
					list_item_move(&failedtests, &pendingtests, pendingtests.head);
				}
				break;

			  case TALK_PROTO_DNSQUERY:
				if (dns_start_query(pendingtests.head, pendingtests.head->netparams.destinationip)) {
					list_item_move(&activetests, &pendingtests, pendingtests.head);
				}
				else {
					list_item_move(&failedtests, &pendingtests, pendingtests.head);
				}
				break;
			}
		}

		maxfd = conn_fdset(&fdread, &fdwrite);
		dodns = dns_add_active_fds(&maxfd, &fdread, &fdwrite);

		if (maxfd > 0) {
			tmo.tv_sec = 1; tmo.tv_usec = 0;
			n = select(maxfd+1, &fdread, &fdwrite, NULL, &tmo);
			if (n < 0) {
				if (errno != EINTR) {
					fprintf(stderr, "select returned error (%s)\n", strerror(errno));
					return;
				}
			}

			conn_process_active(&fdread, &fdwrite);
			if (dodns) dns_process_active(&fdread, &fdwrite);
		}

		conn_trimactive();
		dns_trimactive();
	}
	while ((activetests.len || pendingtests.len));
}

void test_is_done(myconn_t *rec)
{
	list_item_move(&donetests, &activetests, rec);
}

void showtext(char *s)
{
	char *bol, *eoln;

	if (!s) return;

	bol = s;
	while (bol) {
		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
		printf("\t%s\n", bol);
		bol = (eoln ? eoln+1 : NULL);
	}

}

int main(int argc, char **argv)
{
	myconn_t *walk;

	debug = 1;
	// conn_register_infohandler(NULL, 7);
	conn_init_client();

	add_tcp_test("172.16.10.3", 25, NULL, "smtp", smtp_dialog);
	add_tcp_test("2a00:1450:4001:c01::6a", 80, NULL, "http://ipv6.google.com/", http_dialog);
	add_tcp_test("173.194.69.105", 80, NULL, "http://www.google.com/", http_dialog);
	add_tcp_test("172.16.10.3", 123, NULL, "ntp", ntp_dialog);
	add_tcp_test("172.16.10.3", 53, NULL, "www.xymon.com", dns_dialog);
	add_tcp_test("89.150.129.22", 53, NULL, "www.sslug.dk", dns_dialog);
	add_tcp_test("89.150.129.22", 53, NULL, "www.csc.dk", dns_dialog);
	// add_tcp_test("172.16.10.7", 53, NULL, "www.sslug.dk", dns_dialog);

	run_tcp_tests();

	for (walk = donetests.head; (walk); walk = walk->next) {
		printf("Test %s\n", walk->testspec);
		printf("\tStatus   : ");
		switch (walk->talkresult) {
		  case TALK_CONN_FAILED: printf("Connection failed\n"); break;
		  case TALK_CONN_TIMEOUT: printf("Connection timeout\n"); break;
		  case TALK_OK: printf("OK\n"); break;
		  case TALK_BADDATA: printf("Bad dialog\n"); break;
		  case TALK_BADSSLHANDSHAKE: printf("SSL handshake failure\n"); break;
		  case TALK_INTERRUPTED: printf("Peer disconnect\n"); break;
		}
		printf("\tTime     : %d.%03d ms\n", (walk->elapsedms / 1000), (walk->elapsedms % 1000));
		printf("\tRead     : %d\n", walk->bytesread);
		printf("\tWritten  : %d\n", walk->byteswritten);
		printf("\t------------------------\n");
		switch (walk->talkprotocol) {
		  case TALK_PROTO_PLAIN:
			showtext(STRBUF(walk->textlog));
			break;

		  case TALK_PROTO_HTTP:
			showtext(walk->dialog[0] + 5);
			showtext(STRBUF(walk->httpheaders));
			showtext(STRBUF(walk->httpbody));
			break;

		  case TALK_PROTO_NTP:
			printf("\tNTP server is stratum %d, offset %9.6f secs\n", walk->ntpstratum, walk->ntpoffset);
			break;

		  case TALK_PROTO_DNSQUERY:
			printf("\tDNS query:\n");
			showtext(STRBUF(walk->textlog));
			break;

		  default:
			break;
		}
	}

	conn_deinit();

	return 0;
}

