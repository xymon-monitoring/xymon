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
#include "ntptalk.h"
#include "dnstalk.h"

static listhead_t *pendingtests = NULL;
static listhead_t *activetests = NULL;
static listhead_t *donetests = NULL;

static int last_write_step(myconn_t *rec)
{
	int i;

	for (i = rec->step; (rec->dialog[i]); i++)
		if (strncasecmp(rec->dialog[i], "SEND:", 5) == 0) return 0;

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
	int hdrbytes, bodybytes = 0, n;

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
			/* We have no content-length: header, so keep going until we do two NULL-reads */
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

enum conn_cbresult_t tcp_standard_callback(tcpconn_t *connection, enum conn_callback_t id, void *userdata)
{
	int res = CONN_CBRESULT_OK;
	int n, advancestep;
	size_t used;
	time_t start, expire;
	char *certsubject;
	myconn_t *rec = (myconn_t *)userdata;

	// dbgprintf("CB: %s\n", conn_callback_names[id]);

	switch (id) {
	  case CONN_CB_CONNECT_START:          /* Client mode: New outbound connection start */
		break;

	  case CONN_CB_CONNECT_FAILED:         /* Client mode: New outbound connection failed */
		rec->talkresult = TALK_CONN_FAILED;
		rec->textlog = newstrbuffer(0);
		addtobuffer(rec->textlog, strerror(connection->errcode));
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
			rec->peercertificate = certsubject;	/* certsubject is malloc'ed by conn_peer_certificate */
			rec->peercertificateexpiry = expire;
		}
		break;

	  case CONN_CB_SSLHANDSHAKE_FAILED:    /* Client/server mode: SSL handshake failed (connection will close) */
		rec->talkresult = TALK_BADSSLHANDSHAKE;
		break;

	  case CONN_CB_READCHECK:              /* Client/server mode: Check if application wants to read data */
		if (!rec->dialog[rec->step])
			res = CONN_CBRESULT_FAILED;
		else if (rec->istelnet > 0)
			res = CONN_CBRESULT_OK;
		else
			res = ((strncasecmp(rec->dialog[rec->step], "EXPECT:", 7) == 0) || (strncasecmp(rec->dialog[rec->step], "READ", 4) == 0)) ? CONN_CBRESULT_OK : CONN_CBRESULT_FAILED;
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
		if (n <= 0) return CONN_CBRESULT_OK;	/* n == 0 happens during SSL handshakes, n < 0 means connection will close */
		rec->bytesread += n;

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
		if (strncasecmp(rec->dialog[rec->step], "EXPECT:", 7) == 0) {
			int explen = strlen(rec->dialog[rec->step]+7);

			if ((n < explen) && (strncasecmp(rec->readbuf, rec->dialog[rec->step]+7, n) == 0)) {
				/* 
				 * Got the right data so far, but not the complete amount.
				 * Do nothing, we'll just keep reading until we have all of the data
				 */
			}
			else if (strncasecmp(rec->readbuf, rec->dialog[rec->step]+7, explen) == 0) {
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
		else if (strcasecmp(rec->dialog[rec->step], "READALL") == 0) {
			/* No need to save the data twice (we store it in rec->textlog), so reset the readp to start of our readbuffer */
			rec->readp = rec->readbuf;
			*(rec->readp) = '\0';
			if (advancestep) rec->step++;
		}
		else if (strcasecmp(rec->dialog[rec->step], "READ") == 0) {
			rec->step++;
		}

		/* See if we have reached a point where we switch to TLS mode */
		if (rec->dialog[rec->step] && (strcasecmp(rec->dialog[rec->step], "STARTTLS") == 0)) {
			res = CONN_CBRESULT_STARTTLS;
			rec->step++;
		}
		break;

	  case CONN_CB_WRITECHECK:             /* Client/server mode: Check if application wants to write data */
		if (!rec->dialog[rec->step])
			res = CONN_CBRESULT_FAILED;
		else if (rec->istelnet != 0)
			res = (rec->istelnet < 0) ? CONN_CBRESULT_OK : CONN_CBRESULT_FAILED;
		else {
			if ((*rec->writep == '\0') && (strncasecmp(rec->dialog[rec->step], "SEND:", 5) == 0)) {
				strcpy(rec->writebuf, rec->dialog[rec->step]+5);
				rec->writep = rec->writebuf;
			}
			res = (*rec->writep != '\0') ? CONN_CBRESULT_OK : CONN_CBRESULT_FAILED;
		}
		break;

	  case CONN_CB_WRITE:                  /* Client/server mode: Ready for application to write data w/ conn_write() */
		if (rec->istelnet < 0) {
			n = conn_write(connection, rec->writep, -(rec->istelnet));
			if (n <= 0) return CONN_CBRESULT_OK;	/* n == 0 happens during SSL handshakes, n < 0 means connection will close */
			rec->writep += n;
			rec->istelnet += n; if (rec->istelnet == 0) rec->istelnet = 1;
		}
		else {
			n = conn_write(connection, rec->writep, strlen(rec->writep));
			if (n <= 0) return CONN_CBRESULT_OK;	/* n == 0 happens during SSL handshakes, n < 0 means connection will close */
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

		/* See if we have reached a point where we switch to TLS mode */
		if (rec->dialog[rec->step] && (strcasecmp(rec->dialog[rec->step], "STARTTLS") == 0)) {
			res = CONN_CBRESULT_STARTTLS;
			rec->step++;
		}
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
			if ((strcasecmp(rec->dialog[rec->step], "CLOSE") != 0) && (strcasecmp(rec->dialog[rec->step], "READALL") != 0))
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
	if ((connection->connstate != CONN_DEAD) && rec && rec->dialog[rec->step] && (strcasecmp(rec->dialog[rec->step], "CLOSE") == 0)) {
		conn_close_connection(connection, NULL);
	}

	return res;
}


void *add_net_test(char *testspec, char **dialog, net_test_options_t *options, myconn_netparams_t *netparams, void *hostinfo)
{
	myconn_t *newtest;

	newtest = (myconn_t *)calloc(1, sizeof(myconn_t));
	newtest->testspec = strdup(testspec ? testspec : "<null>");
	memcpy(&newtest->netparams, netparams, sizeof(newtest->netparams));
	newtest->netparams.callback = tcp_standard_callback;
	newtest->hostinfo = hostinfo;
	newtest->dialog = dialog;
	newtest->timeout = options->timeout;

	switch (options->testtype) {
	  case NET_TEST_HTTP:
		newtest->talkprotocol = TALK_PROTO_HTTP;
		newtest->httpheaders = newstrbuffer(0);
		newtest->httpbody = newstrbuffer(0);
		break;
	  case NET_TEST_NTP:
		newtest->talkprotocol = TALK_PROTO_NTP;
		newtest->netparams.socktype = CONN_SOCKTYPE_DGRAM;
		newtest->netparams.callback = ntp_callback;
		break;
	  case NET_TEST_DNS:
		newtest->talkprotocol = TALK_PROTO_DNSQUERY;
		newtest->dnsstatus = DNS_NOTDONE;
		/* The DNS-specific routines handle the rest */
		break;
	  case NET_TEST_PING:
		newtest->talkprotocol = TALK_PROTO_PING;
		break;
	  case NET_TEST_TELNET:
		newtest->istelnet = 1;
		newtest->talkprotocol = TALK_PROTO_PLAIN;
		break;
	  case NET_TEST_STANDARD:
		newtest->talkprotocol = TALK_PROTO_PLAIN;
		break;
	}

	if (conn_is_ip(newtest->netparams.destinationip) == 0) {
		/* Destination is not an IP, so try doing a hostname lookup */
		newtest->netparams.lookupstring = strdup(newtest->netparams.destinationip);
		newtest->netparams.lookupstatus = LOOKUP_NEEDED;
	}

	newtest->listitem = list_item_create(pendingtests, newtest, newtest->testspec);

	return newtest;
}



listhead_t *run_net_tests(int concurrency)
{
	int maxfd;

	list_shuffle(&pendingtests);

	/* Loop to process data */
	do {
		fd_set fdread, fdwrite;
		int n, dodns;
		struct timeval tmo;
		myconn_t *rec;
		listitem_t *pcur, *pnext;
		int lookupsposted = 0;
		
		/* Start some more tests */
		pcur = pendingtests->head;
		while (pcur && (activetests->len < concurrency) && (lookupsposted < concurrency)) {
			rec = (myconn_t *)pcur->data;

			/* 
			 * Must save the pointer to the next pending test now, 
			 * since we may move the current item from the pending
			 * list to the active list before going to the next
			 * item in the pending-list.
			 */
			pnext = pcur->next;

			if (rec->netparams.lookupstatus == LOOKUP_NEEDED) {
				lookupsposted++;
				dns_lookup(rec);
			}

			if ((rec->netparams.lookupstatus == LOOKUP_ACTIVE) || (rec->netparams.lookupstatus == LOOKUP_NEEDED)) {
				/* DNS lookup in progress, skip this test until lookup completes */
				pcur = pnext;
				continue;
			}
			else if (rec->netparams.lookupstatus == LOOKUP_FAILED) {
				/* DNS lookup determined that this host does not have a valid IP. */
				list_item_move(donetests, pcur, rec->testspec);
				rec->talkresult = TALK_CANNOT_RESOLVE;
				pcur = pnext;
				continue;
			}

			switch (rec->talkprotocol) {
			  case TALK_PROTO_PLAIN:
			  case TALK_PROTO_HTTP:
			  case TALK_PROTO_NTP:
				if (conn_prepare_connection(rec->netparams.destinationip, 
							rec->netparams.destinationport, 
							rec->netparams.socktype,
							rec->netparams.sourceip, 
							rec->netparams.sslhandling, rec->netparams.sslcertfn, rec->netparams.sslkeyfn, 
							rec->timeout*1000,
							rec->netparams.callback, rec)) {
					list_item_move(activetests, pcur, rec->testspec);
				}
				else {
					rec->talkresult = TALK_CONN_FAILED;
					list_item_move(donetests, pcur, rec->testspec);
				}
				break;

			  case TALK_PROTO_DNSQUERY:
				if (dns_start_query(rec, rec->netparams.destinationip)) {
					list_item_move(activetests, pcur, rec->testspec);
				}
				else {
					rec->talkresult = TALK_CONN_FAILED;
					list_item_move(donetests, pcur, rec->testspec);
				}
				break;

			  case TALK_PROTO_PING:
				/* NULL is only for doing DNS lookups, ping tests etc. */
				list_item_move(donetests, pcur, rec->testspec);
				break;

			  default:
				break;
			}

			pcur = pnext;
		}

		maxfd = conn_fdset(&fdread, &fdwrite);
		dns_add_active_fds(activetests, &maxfd, &fdread, &fdwrite);

		if (maxfd > 0) {
			tmo.tv_sec = 1; tmo.tv_usec = 0;
			n = select(maxfd+1, &fdread, &fdwrite, NULL, &tmo);
			if (n < 0) {
				if (errno != EINTR) {
					errprintf("FATAL: select() returned error %s\n", strerror(errno));
					return NULL;
				}
			}

			conn_process_active(&fdread, &fdwrite);
			dns_process_active(activetests, &fdread, &fdwrite);
		}

		conn_trimactive();
		dns_finish_queries(activetests);
		// dbgprintf("Active: %d, pending: %d\n", activetests->len, pendingtests->len);
#if 0
		if ((activetests->len == 0) && (pendingtests->len > 0)) {
			static int bugcount = 0;

			bugcount++;
			if (bugcount == 10) {
				errprintf("BUG: No progress being done!\n");
				dump_net_tests(pendingtests);
				return;
			}
		}
#endif
	}
	while ((activetests->len + pendingtests->len) > 0);

	return donetests;
}

void test_is_done(myconn_t *rec)
{
	list_item_move(donetests, rec->listitem, rec->testspec);
}


void init_tcp_testmodule(void)
{
	conn_init_client();
	dns_library_init();
	dns_lookup_init();

	pendingtests = list_create("pending");
	activetests = list_create("active");
	donetests = list_create("done");
}


#ifdef STANDALONE
static void showtext(char *s)
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

void dump_net_tests(listhead_t *head)
{
	listitem_t *walk;

	if (!head) head = donetests;

	for (walk = head->head; (walk); walk = walk->next) {
		myconn_t *rec = (myconn_t *)walk->data;
		if (rec->talkprotocol == TALK_PROTO_PING) continue;

		printf("Test %s\n", rec->testspec);
		printf("\tTarget   : %s:%d\n", rec->netparams.destinationip, rec->netparams.destinationport);
		printf("\tStatus   : ");
		switch (rec->talkresult) {
		  case TALK_CANNOT_RESOLVE: printf("Cannot resolve hostname"); break;
		  case TALK_CONN_FAILED: printf("Connection failed"); break;
		  case TALK_CONN_TIMEOUT: printf("Connection timeout"); break;
		  case TALK_OK: printf("OK"); break;
		  case TALK_BADDATA: printf("Bad dialog"); break;
		  case TALK_BADSSLHANDSHAKE: printf("SSL handshake failure"); break;
		  case TALK_INTERRUPTED: printf("Peer disconnect"); break;
		}
		printf("\t(sslhandling=%d, af_idx=%d, lookupstatus=%d)\n",
			rec->netparams.sslhandling, rec->netparams.af_index, rec->netparams.lookupstatus);
		if (rec->peercertificate) {
			printf("\tCert.    : %s\n", rec->peercertificate);
		}
		printf("\tLookup   : %d.%03d ms\n", (rec->dnselapsedms / 1000), (rec->dnselapsedms % 1000));
		printf("\tTime     : %d.%03d ms\n", (rec->elapsedms / 1000), (rec->elapsedms % 1000));
		printf("\tRead     : %d\n", rec->bytesread);
		printf("\tWritten  : %d\n", rec->byteswritten);
		printf("\t------------------------\n");
		switch (rec->talkprotocol) {
		  case TALK_PROTO_PLAIN:
			if (rec->textlog) showtext(STRBUF(rec->textlog));
			break;

		  case TALK_PROTO_HTTP:
			showtext(rec->dialog[0] + 5);
			showtext(STRBUF(rec->httpheaders));
			showtext(STRBUF(rec->httpbody));
			break;

		  case TALK_PROTO_NTP:
			printf("\tNTP server is stratum %d, offset %9.6f secs\n", rec->ntpstratum, rec->ntpoffset);
			break;

		  case TALK_PROTO_DNSQUERY:
			if (rec->textlog) {
				printf("\tDNS query:\n");
				showtext(STRBUF(rec->textlog));
			}
			break;

		  default:
			break;
		}
	}
}

static char *silent_dialog[] = {
	"READ", "CLOSE", NULL
};

static char *smtp_dialog[] = {
	"EXPECT:220",
	"SEND:STARTTLS\r\n",
	"EXPECT:220",
	"STARTTLS",
	"SEND:EHLO hswn.dk\r\n",
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
	"SEND:starttls\n",
	"EXPECT:OK",
	"STARTTLS",
	"SEND:size:4\nping\n",
	"EXPECT:xymon",
	"CLOSE",
	NULL
};

static char *bbd_dialog[] = {
	"SEND:size:4\nping\n",
	"EXPECT:xymon",
	"CLOSE",
	NULL
};

static char *pop_dialog[] = {
	"EXPECT:+OK",
	"CLOSE",
	NULL
};

static char *telnet_dialog[] = {
	"READ", "CLOSE", NULL
};
 
static char *http_dialog[] = { NULL };
static char *ntp_dialog[] = { NULL };
static char *dns_dialog[] = { NULL };
static char *null_dialog[] = { NULL };

static void *add_tcp_test(char *destinationip, int destinationport, char *sourceip, char *testspec, char **dialog, 
			  enum sslhandling_t sslhandling, char *sslcertfn, char *sslkeyfn)
{
	myconn_netparams_t netparams;
	net_test_options_t options = { NET_TEST_STANDARD, 10 };

	memset(&netparams, 0, sizeof(netparams));
	netparams.destinationip = strdup(destinationip);
	netparams.destinationport = destinationport;
	netparams.sourceip = (sourceip ? strdup(sourceip) : NULL);
	netparams.sslhandling = sslhandling;
	netparams.sslcertfn = sslcertfn;
	netparams.sslkeyfn = sslkeyfn;

	if      (dialog == http_dialog)		options.testtype = NET_TEST_HTTP;
	else if (dialog == ntp_dialog)		options.testtype = NET_TEST_NTP;
	else if (dialog == dns_dialog)		options.testtype = NET_TEST_DNS;
	else if (dialog == telnet_dialog)	options.testtype = NET_TEST_TELNET;
	else if (dialog == null_dialog)		options.testtype = NET_TEST_PING;

	return add_net_test(testspec, dialog, &options, &netparams, NULL);
}

int main(int argc, char **argv)
{
	listitem_t *walk;

	debug = 1;
	conn_register_infohandler(NULL, 7);

	init_tcp_testmodule();

	add_tcp_test("jorn", 995, NULL, "pop3s", pop_dialog, CONN_SSL_YES, NULL, NULL);
	add_tcp_test("jorn.hswn.dk", 1984, NULL, "bbd", bbd_dialog, CONN_SSL_STARTTLS_CLIENT, NULL, NULL);

#if 0
	add_tcp_test("jorn.hswn.dk", 25, NULL, "smtp", smtp_dialog, CONN_SSL_STARTTLS_CLIENT, NULL, NULL);
	// add_tcp_test("2a00:1450:4001:c01::6a", 80, NULL, "http://ipv6.google.com/", http_dialog, CONN_SSL_NO, NULL, NULL);
	// add_tcp_test("173.194.69.105", 80, NULL, "http://www.google.com/", http_dialog, CONN_SSL_NO, NULL, NULL);
	add_tcp_test("jorn.hswn.dk", 123, NULL, "ntp", ntp_dialog, CONN_SSL_NO, NULL, NULL);
	add_tcp_test("ns.hswn.dk", 53, NULL, "www.xymon.com", dns_dialog, CONN_SSL_NO, NULL, NULL);
	add_tcp_test("ns1.fullrate.dk", 53, NULL, "www.sslug.dk", dns_dialog, CONN_SSL_NO, NULL, NULL);
	add_tcp_test("ns1.fullrate.dk", 53, NULL, "www.csc.dk", dns_dialog, CONN_SSL_NO, NULL, NULL);
	add_tcp_test("ipv6.google.com", 80, NULL, "http://ipv6.google.com/", http_dialog, CONN_SSL_NO, NULL, NULL);
	add_tcp_test("ns1.fullrate.dk", 53, NULL, "www.fullrate.dk", dns_dialog, CONN_SSL_NO, NULL, NULL);
	add_tcp_test("www.dba.dk", 0, NULL, NULL, null_dialog, CONN_SSL_NO, NULL, NULL);

	// add_tcp_test("172.16.10.7", 53, NULL, "www.sslug.dk", dns_dialog, CONN_SSL_NO, NULL, NULL);
#endif

#if 0
	{
		xymonping_dialog[3] = (char *)malloc(30 + strlen(argv[1]));
		sprintf(xymonping_dialog[3], "SEND:size:%d\n%s\n", (int)strlen(argv[1]), argv[1]);

		add_tcp_test("127.0.0.1", 1984, NULL, "xymon", xymonping_dialog, CONN_SSL_STARTTLS_CLIENT, NULL, NULL);
	}
#endif

	run_net_tests(10);
	dump_net_tests(donetests);

	return 0;
}
#endif

