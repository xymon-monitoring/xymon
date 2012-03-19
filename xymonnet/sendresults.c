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
#include <stdlib.h>
#include <stdio.h>

#include "libxymon.h"
#include "tcptalk.h"
#include "sendresults.h"

typedef struct hostresult_t {
	void *hinfo;
	strbuffer_t *txt;
} hostresult_t;

static void result_plain(myconn_t *rec,  strbuffer_t *txt)
{
	char msgline[4096];

	if (rec->textlog) {
		sprintf(msgline, "PLAINlog: %d\n", STRBUFLEN(rec->textlog));
		addtobuffer(txt, msgline);
		addtostrbuffer(txt, rec->textlog);
		addtobuffer(txt, "\n");
	}
}

static void result_ntp(myconn_t *rec,  strbuffer_t *txt)
{
	char msgline[4096];

	sprintf(msgline, "NTPstratum: %d\n", rec->ntpstratum);
	addtobuffer(txt, msgline);
	sprintf(msgline, "NTPoffset: %12.6f\n", rec->ntpoffset);
	addtobuffer(txt, msgline);
}

static void result_http(myconn_t *rec,  strbuffer_t *txt)
{
	char msgline[4096];

	sprintf(msgline, "HTTPstatus: %d\n", rec->httpstatus);
	addtobuffer(txt, msgline);
	if (rec->httpheaders) {
		sprintf(msgline, "HTTPheaders: %d\n", STRBUFLEN(rec->httpheaders));
		addtobuffer(txt, msgline);
		addtostrbuffer(txt, rec->httpheaders);
		addtobuffer(txt, "\n");
	}
}

static void result_dns(myconn_t *rec,  strbuffer_t *txt)
{
	result_plain(rec, txt);
}

static void result_ping(myconn_t *rec,  strbuffer_t *txt)
{
}

void send_test_results(listhead_t *head, char *collector, int pingtest)
{
	char msgline[4096];
	listitem_t *walk;
	xtreePos_t handle;
	void *hostresults = xtreeNew(strcasecmp);

	for (walk = head->head; (walk); walk = walk->next) {
		hostresult_t *hres;
		myconn_t *rec = (myconn_t *)walk->data;
		char *s;

		if ((pingtest) && (rec->talkprotocol != TALK_PROTO_PING)) continue;
		if ((!pingtest) && (rec->talkprotocol == TALK_PROTO_PING)) continue;

		handle = xtreeFind(hostresults, xmh_item(rec->hostinfo, XMH_HOSTNAME));
		if (handle == xtreeEnd(hostresults)) {
			hres = (hostresult_t *)calloc(1, sizeof(hostresult_t));
			hres->hinfo = rec->hostinfo;
			hres->txt = newstrbuffer(0);
			xtreeAdd(hostresults, xmh_item(rec->hostinfo, XMH_HOSTNAME), hres);

			sprintf(msgline, "client/%s %s.xymonnet xymonnet\n", 
				collector, xmh_item(rec->hostinfo, XMH_HOSTNAME));
			addtobuffer(hres->txt, msgline);
		}
		else {
			hres = xtreeData(hostresults, handle);
		}

		sprintf(msgline, "\n[%s]\n", rec->testspec);
		addtobuffer(hres->txt, msgline);

		if (rec->netparams.lookupstring) {
			sprintf(msgline, "TargetHostname: %s\n", rec->netparams.lookupstring);
			addtobuffer(hres->txt, msgline);
		}
		sprintf(msgline, "TargetIP: %s\n", rec->netparams.destinationip);
		addtobuffer(hres->txt, msgline);
		sprintf(msgline, "TargetPort: %d\n", rec->netparams.destinationport);
		addtobuffer(hres->txt, msgline);
		if (rec->netparams.sourceip) {
			sprintf(msgline, "SourceIP: %s\n", rec->netparams.sourceip);
			addtobuffer(hres->txt, msgline);
		}

		switch (rec->netparams.socktype) {
		  case CONN_SOCKTYPE_STREAM: s = "TCP"; break;
		  case CONN_SOCKTYPE_DGRAM: s = "UDP"; break;
		  default: s = "UNKNOWN"; break;
		}
		sprintf(msgline, "Protocol: %s\n", s);
		addtobuffer(hres->txt, msgline);

		switch (rec->netparams.sslhandling) {
		  case CONN_SSL_NO: s = "NO"; break;
		  case CONN_SSL_YES: s = "YES"; break;
		  case CONN_SSL_STARTTLS_CLIENT: s = "STARTTLS_CLIENT"; break;
		  case CONN_SSL_STARTTLS_SERVER: s = "STARTTLS_SERVER"; break;
		  default: s = "UNKNOWN"; break;
		}
		sprintf(msgline, "SSL: %s\n", s);
		addtobuffer(hres->txt, msgline);

		switch (rec->talkresult) {
		  case TALK_CONN_FAILED: s = "CONN_FAILED"; break;
		  case TALK_CONN_TIMEOUT: s = "CONN_TIMEOUT"; break;
		  case TALK_OK: s = "OK"; break;
		  case TALK_BADDATA: s = "BADDATA"; break;
		  case TALK_BADSSLHANDSHAKE: s = "BADSSLHANDSHAKE"; break;
		  case TALK_INTERRUPTED: s = "INTERRUPTED"; break;
		  case TALK_CANNOT_RESOLVE: s = "CANNOT_RESOLVE"; break;
		  default: s = "UNKNOWN"; break;
		}
		sprintf(msgline, "Status: %s\n", s);
		addtobuffer(hres->txt, msgline);

		sprintf(msgline, "ElapsedMS: %d\nDNSMS:%d\ntimeoutMS:%d\n",
			rec->elapsedms, rec->dnselapsedms, rec->timeout*1000);
		addtobuffer(hres->txt, msgline);

		sprintf(msgline, "BytesRead: %u\nBytesWritten: %u\n", rec->bytesread, rec->byteswritten);
		addtobuffer(hres->txt, msgline);

		if (rec->peercertificate) {
			addtobuffer(hres->txt, "PeerCertificateSubject: ");
			addtobuffer(hres->txt, rec->peercertificate);
			addtobuffer(hres->txt, "\n");
			sprintf(msgline, "PeerCertificateExpiry: %d\n", (int)rec->peercertificateexpiry);
			addtobuffer(hres->txt, msgline);
		}

		switch (rec->talkprotocol) {
		  case TALK_PROTO_PLAIN: result_plain(rec, hres->txt); break;
		  case TALK_PROTO_NTP: result_ntp(rec, hres->txt); break;
		  case TALK_PROTO_HTTP: result_http(rec, hres->txt); break;
		  case TALK_PROTO_DNSQUERY: result_dns(rec, hres->txt); break;
		  case TALK_PROTO_PING: result_ping(rec, hres->txt); break;
		  default: break;
		}
	}

	for (handle = xtreeFirst(hostresults); handle != xtreeEnd(hostresults); handle = xtreeNext(hostresults, handle)) {
		hostresult_t *hres = xtreeData(hostresults, handle);

		fprintf(stdout, "======== %s ========\n", xmh_item(hres->hinfo, XMH_HOSTNAME));
		fprintf(stdout, "%s\n", STRBUF(hres->txt));
	}
}

