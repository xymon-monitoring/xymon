/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>

#include "libxymon.h"
#include "tcptalk.h"
#include "sendresults.h"
#include "netsql.h"

static void add_to_sub_queue(myconn_t *rec, char *moduleid, char *location, ...)
{
	va_list extraparams;
	char *s;
	strbuffer_t *extrastr = newstrbuffer(0);

	if (!rec || !moduleid) return;

	va_start(extraparams, location);
	while ((s = va_arg(extraparams, char *)) != NULL) {
		if (STRBUFLEN(extrastr) > 0) addtobuffer(extrastr, "\t");
		addtobuffer(extrastr, s);
	}
	va_end(extraparams);

	xymon_sqldb_netmodule_additem(moduleid, location, xmh_item(rec->hostinfo, XMH_HOSTNAME), rec->netparams.destinationip, rec->testspec, STRBUF(extrastr));
	freestrbuffer(extrastr);
}

typedef struct hostresult_t {
	void *hinfo;
	strbuffer_t *txt;
} hostresult_t;

static void result_plain(myconn_t *rec,  strbuffer_t *txt)
{
	char msgline[4096];

	if (dontsendmessages >= 1) {
		if (rec->textlog) clearstrbuffer(rec->textlog);
		return;
	}

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

	if (dontsendmessages >= 2) {
		if (rec->textlog) clearstrbuffer(rec->textlog);
		if (rec->httpheaders) clearstrbuffer(rec->httpheaders);
		if (rec->httpbody) clearstrbuffer(rec->httpbody);
		return;
	}

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

static void result_subqueue(char *id, myconn_t *rec,  strbuffer_t *txt)
{
	char msgline[4096];

	if (rec->textlog) {
		snprintf(msgline, sizeof(msgline), "%slog: %d\n", id, STRBUFLEN(rec->textlog));
		addtobuffer(txt, msgline);
		addtostrbuffer(txt, rec->textlog);
		addtobuffer(txt, "\n");
		clearstrbuffer(rec->textlog);
	}
}

void send_test_results(listhead_t *head, char *collector, int issubmodule, char *location)
{
	char msgline[4096];
	listitem_t *walk;
	xtreePos_t handle;
	void *hostresults = xtreeNew(strcasecmp);

	for (walk = head->head; (walk); walk = walk->next) {
		hostresult_t *hres;
		myconn_t *rec = (myconn_t *)walk->data;
		char *s;

		switch (rec->talkprotocol) {
		  case TALK_PROTO_PING:
			if (!issubmodule && (rec->talkresult == TALK_OK)) {
				add_to_sub_queue(rec, "ping", location, NULL);
				continue;
			}
			break;

#ifdef HAVE_LDAP
		  case TALK_PROTO_LDAP:
			if (!issubmodule && (rec->talkresult == TALK_OK)) {
				char *creds = xmh_item(rec->hostinfo, XMH_LDAPLOGIN);

				add_to_sub_queue(rec, "ldap", location, creds, NULL);
				continue;
			}
			break;
#endif

		  case TALK_PROTO_EXTERNAL:
			if (!issubmodule && (rec->talkresult == TALK_OK)) {
				if (strncmp(rec->testspec, "rpc=", 4) == 0)
					add_to_sub_queue(rec, "rpc", location, NULL);
				else
					add_to_sub_queue(rec, rec->testspec, location, NULL);
				continue;
			}
			break;

		  default:
			break;
		}

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

		snprintf(msgline, sizeof(msgline), "StartTime: %d\nEndTime: %d\n", (int)rec->teststarttime, (int)rec->testendtime);
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

		if (location) {
			snprintf(msgline, sizeof(msgline), "Location: %s\n", location);
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
		  case TALK_MODULE_FAILED: s = "MODULE_FAILED"; break;
		  default: s = "UNKNOWN"; break;
		}
		snprintf(msgline, sizeof(msgline), "Status: %s\n", s);
		addtobuffer(hres->txt, msgline);

		snprintf(msgline, sizeof(msgline), "ElapsedMS: %d.%02d\nDNSMS: %d.%02d\nTimeoutMS: %d\nIntervalMS: %d\n",
			(rec->elapsedus / 1000), (rec->elapsedus % 1000), 
			(rec->dnselapsedus / 1000), (rec->dnselapsedus % 1000), 
			rec->timeout*1000, rec->interval*1000);
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
			snprintf(msgline, sizeof(msgline), "PeerCertificateDetails: %d\n", (int)strlen(rec->peercertificatedetails)+1);
			addtobuffer(hres->txt, msgline);
			addtobuffer(hres->txt, rec->peercertificatedetails);
			addtobuffer(hres->txt, "\n");
		}

		switch (rec->talkprotocol) {
		  case TALK_PROTO_PLAIN: result_plain(rec, hres->txt); break;
		  case TALK_PROTO_NTP: result_ntp(rec, hres->txt); break;
		  case TALK_PROTO_HTTP: result_http(rec, hres->txt); break;
		  case TALK_PROTO_DNSQUERY: result_dns(rec, hres->txt); break;
		  case TALK_PROTO_PING: result_subqueue("PING", rec, hres->txt); break;
#ifdef HAVE_LDAP
		  case TALK_PROTO_LDAP: result_subqueue("LDAP", rec, hres->txt); break;
#endif
		  case TALK_PROTO_EXTERNAL: result_subqueue(rec->testspec, rec, hres->txt); break;
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

void cleanup_myconn_list(listhead_t *head)
{
	listitem_t *walk, *nextlistitem;
	myconn_t *testrec;

	walk = head->head;
	while (walk) {
		nextlistitem = walk->next;
		testrec = (myconn_t *)walk->data;

		if (testrec->netparams.lookupstring) xfree(testrec->netparams.lookupstring);
		if (testrec->netparams.destinationip) xfree(testrec->netparams.destinationip);
		if (testrec->netparams.sourceip) xfree(testrec->netparams.sourceip);
		if (testrec->testspec) xfree(testrec->testspec);
		if (testrec->textlog) freestrbuffer(testrec->textlog);
		if (testrec->httpheaders) freestrbuffer(testrec->httpheaders);
		if (testrec->httpbody) freestrbuffer(testrec->httpbody);
		if (testrec->dnschannel) xfree(testrec->dnschannel);
		if (testrec->peercertificate) xfree(testrec->peercertificate);
		if (testrec->peercertificateissuer) xfree(testrec->peercertificateissuer);
		if (testrec->peercertificatedetails) xfree(testrec->peercertificatedetails);
		xfree(testrec);

		list_item_delete(walk, "");
		walk = nextlistitem;
	}
}

