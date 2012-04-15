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
		snprintf(msgline, sizeof(msgline), "PLAINlog: %d\n", STRBUFLEN(rec->textlog));
		addtobuffer(txt, msgline);
		addtostrbuffer(txt, rec->textlog);
		addtobuffer(txt, "\n");
		clearstrbuffer(rec->textlog);
	}
}

static void result_ntp(myconn_t *rec,  strbuffer_t *txt)
{
	char msgline[4096];

	snprintf(msgline, sizeof(msgline), "NTPstratum: %d\n", rec->ntpstratum);
	addtobuffer(txt, msgline);
	snprintf(msgline, sizeof(msgline), "NTPoffset: %12.6f\n", rec->ntpoffset);
	addtobuffer(txt, msgline);
}

static void result_http(myconn_t *rec,  strbuffer_t *txt)
{
	char msgline[4096];

	snprintf(msgline, sizeof(msgline), "HTTPstatus: %d\n", rec->httpstatus);
	addtobuffer(txt, msgline);
	if (rec->textlog) {
		char *authtoken;
		snprintf(msgline, sizeof(msgline), "HTTPrequest: %d\n", STRBUFLEN(rec->textlog));
		addtobuffer(txt, msgline);
		/*
		 * If there is an authentication header, it is best to obscure it here.
		 * Otherwise, anyone who can view the "client data" will be able to see
		 * the login.
		 */
		authtoken = strstr(STRBUF(rec->textlog), "\nAuthorization:");
		if (authtoken) {
			authtoken += 15;
			while (*authtoken != '\r') {
				*authtoken = '*'; authtoken++;
			}
		}
		addtostrbuffer(txt, rec->textlog);
		addtobuffer(txt, "\n");
		clearstrbuffer(rec->textlog);
	}
	if (rec->httpheaders) {
		snprintf(msgline, sizeof(msgline), "HTTPheaders: %d\n", STRBUFLEN(rec->httpheaders));
		addtobuffer(txt, msgline);
		addtostrbuffer(txt, rec->httpheaders);
		addtobuffer(txt, "\n");
		clearstrbuffer(rec->httpheaders);
	}
	if (rec->httpbody) {
		snprintf(msgline, sizeof(msgline), "HTTPbody: %d\n", STRBUFLEN(rec->httpbody));
		addtobuffer(txt, msgline);
		addtostrbuffer(txt, rec->httpbody);
		addtobuffer(txt, "\n");
		clearstrbuffer(rec->httpbody);
	}
}

static void result_dns(myconn_t *rec,  strbuffer_t *txt)
{
	result_plain(rec, txt);
}

static void result_ping(myconn_t *rec,  strbuffer_t *txt)
{
	char msgline[4096];

	if (rec->textlog) {
		snprintf(msgline, sizeof(msgline), "PINGlog: %d\n", STRBUFLEN(rec->textlog));
		addtobuffer(txt, msgline);
		addtostrbuffer(txt, rec->textlog);
		addtobuffer(txt, "\n");
		clearstrbuffer(rec->textlog);
	}
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

			snprintf(msgline, sizeof(msgline), "client/%s %s.netcollect netcollect\n", 
				collector, xmh_item(rec->hostinfo, XMH_HOSTNAME));
			addtobuffer(hres->txt, msgline);
		}
		else {
			hres = xtreeData(hostresults, handle);
		}

		snprintf(msgline, sizeof(msgline), "\n[%s]\n", rec->testspec);
		addtobuffer(hres->txt, msgline);

		if (rec->netparams.lookupstring) {
			snprintf(msgline, sizeof(msgline), "TargetHostname: %s\n", rec->netparams.lookupstring);
			addtobuffer(hres->txt, msgline);
		}
		snprintf(msgline, sizeof(msgline), "TargetIP: %s\n", rec->netparams.destinationip);
		addtobuffer(hres->txt, msgline);
		snprintf(msgline, sizeof(msgline), "TargetPort: %d\n", rec->netparams.destinationport);
		addtobuffer(hres->txt, msgline);
		if (rec->netparams.sourceip) {
			snprintf(msgline, sizeof(msgline), "SourceIP: %s\n", rec->netparams.sourceip);
			addtobuffer(hres->txt, msgline);
		}

		switch (rec->netparams.socktype) {
		  case CONN_SOCKTYPE_STREAM: s = "TCP"; break;
		  case CONN_SOCKTYPE_DGRAM: s = "UDP"; break;
		  default: s = "UNKNOWN"; break;
		}
		snprintf(msgline, sizeof(msgline), "Protocol: %s\n", s);
		addtobuffer(hres->txt, msgline);

		switch (rec->netparams.sslhandling) {
		  case CONN_SSL_NO: s = "NO"; break;
		  case CONN_SSL_YES: s = "YES"; break;
		  case CONN_SSL_STARTTLS_CLIENT: s = "STARTTLS_CLIENT"; break;
		  case CONN_SSL_STARTTLS_SERVER: s = "STARTTLS_SERVER"; break;
		  default: s = "UNKNOWN"; break;
		}
		snprintf(msgline, sizeof(msgline), "SSL: %s\n", s);
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
		snprintf(msgline, sizeof(msgline), "Status: %s\n", s);
		addtobuffer(hres->txt, msgline);

		snprintf(msgline, sizeof(msgline), "ElapsedMS: %d\nDNSMS:%d\ntimeoutMS:%d\n",
			rec->elapsedms, rec->dnselapsedms, rec->timeout*1000);
		addtobuffer(hres->txt, msgline);

		snprintf(msgline, sizeof(msgline), "BytesRead: %u\nBytesWritten: %u\n", rec->bytesread, rec->byteswritten);
		addtobuffer(hres->txt, msgline);

		if (rec->peercertificate) {
			char exps[50];

			strftime(exps, sizeof(exps), "%Y-%m-%d %H:%M:%S UTC", gmtime(&rec->peercertificateexpiry));
			addtobuffer(hres->txt, "PeerCertificateSubject: ");
			addtobuffer(hres->txt, rec->peercertificate);
			addtobuffer(hres->txt, "\n");
			addtobuffer(hres->txt, "PeerCertificateIssuer: ");
			addtobuffer(hres->txt, rec->peercertificateissuer);
			addtobuffer(hres->txt, "\n");
			snprintf(msgline, sizeof(msgline), "PeerCertificateExpiry: %d %s\n", (int)rec->peercertificateexpiry, exps);
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

		sendmessage(STRBUF(hres->txt), NULL, XYMON_TIMEOUT, NULL);
		freestrbuffer(hres->txt);
		xtreeDelete(hostresults, xmh_item(hres->hinfo, XMH_HOSTNAME));
		xfree(hres);
	}

	xtreeDestroy(hostresults);
}

