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

/*
 * Most of the code for parsing DNS responses and formatting these into
 * text were taken from the "adig.c" source-file included with the
 * C-ARES 1.7.5 library. This file carries the following copyright
 * notice, reproduced in full:
 *
 * --------------------------------------------------------------------
 * Copyright 1998 by the Massachusetts Institute of Technology.
 *
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting
 * documentation, and that the name of M.I.T. not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 * --------------------------------------------------------------------
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>

#include "ares.h"
#include "ares_dns.h"
#include "nameser.h"

#include "libxymon.h"
#include "tcptalk.h"
#include "dnstalk.h"
#include "dnsbits.h"


#ifndef T_SRV
#  define T_SRV     33 /* Server selection */
#endif
#ifndef T_NAPTR
#  define T_NAPTR   35 /* Naming authority pointer */
#endif
#ifndef T_DS
#  define T_DS      43 /* Delegation Signer (RFC4034) */
#endif
#ifndef T_SSHFP
#  define T_SSHFP   44 /* SSH Key Fingerprint (RFC4255) */
#endif
#ifndef T_RRSIG
#  define T_RRSIG   46 /* Resource Record Signature (RFC4034) */
#endif
#ifndef T_NSEC
#  define T_NSEC    47 /* Next Secure (RFC4034) */
#endif
#ifndef T_DNSKEY
#  define T_DNSKEY  48 /* DNS Public Key (RFC4034) */
#endif

struct nv {
  const char *name;
  int value;
};

static const struct nv flags[] = {
  { "usevc",            ARES_FLAG_USEVC },
  { "primary",          ARES_FLAG_PRIMARY },
  { "igntc",            ARES_FLAG_IGNTC },
  { "norecurse",        ARES_FLAG_NORECURSE },
  { "stayopen",         ARES_FLAG_STAYOPEN },
  { "noaliases",        ARES_FLAG_NOALIASES }
};
static const int nflags = sizeof(flags) / sizeof(flags[0]);

static const struct nv classes[] = {
  { "IN",       C_IN },
  { "CHAOS",    C_CHAOS },
  { "HS",       C_HS },
  { "ANY",      C_ANY }
};
static const int nclasses = sizeof(classes) / sizeof(classes[0]);

static const struct nv types[] = {
  { "A",        T_A },
  { "NS",       T_NS },
  { "MD",       T_MD },
  { "MF",       T_MF },
  { "CNAME",    T_CNAME },
  { "SOA",      T_SOA },
  { "MB",       T_MB },
  { "MG",       T_MG },
  { "MR",       T_MR },
  { "NULL",     T_NULL },
  { "WKS",      T_WKS },
  { "PTR",      T_PTR },
  { "HINFO",    T_HINFO },
  { "MINFO",    T_MINFO },
  { "MX",       T_MX },
  { "TXT",      T_TXT },
  { "RP",       T_RP },
  { "AFSDB",    T_AFSDB },
  { "X25",      T_X25 },
  { "ISDN",     T_ISDN },
  { "RT",       T_RT },
  { "NSAP",     T_NSAP },
  { "NSAP_PTR", T_NSAP_PTR },
  { "SIG",      T_SIG },
  { "KEY",      T_KEY },
  { "PX",       T_PX },
  { "GPOS",     T_GPOS },
  { "AAAA",     T_AAAA },
  { "LOC",      T_LOC },
  { "SRV",      T_SRV },
  { "AXFR",     T_AXFR },
  { "MAILB",    T_MAILB },
  { "MAILA",    T_MAILA },
  { "NAPTR",    T_NAPTR },
  { "DS",       T_DS },
  { "SSHFP",    T_SSHFP },
  { "RRSIG",    T_RRSIG },
  { "NSEC",     T_NSEC },
  { "DNSKEY",   T_DNSKEY },
  { "ANY",      T_ANY }
};
static const int ntypes = sizeof(types) / sizeof(types[0]);

static const char *opcodes[] = {
  "QUERY", "IQUERY", "STATUS", "(reserved)", "NOTIFY",
  "(unknown)", "(unknown)", "(unknown)", "(unknown)",
  "UPDATEA", "UPDATED", "UPDATEDA", "UPDATEM", "UPDATEMA",
  "ZONEINIT", "ZONEREF"
};

static const char *rcodes[] = {
  "NOERROR", "FORMERR", "SERVFAIL", "NXDOMAIN", "NOTIMP", "REFUSED",
  "(unknown)", "(unknown)", "(unknown)", "(unknown)", "(unknown)",
  "(unknown)", "(unknown)", "(unknown)", "(unknown)", "NOCHANGE"
};

static const unsigned char *display_question(const unsigned char *aptr,
                                             const unsigned char *abuf,
                                             int alen,
					     strbuffer_t *log);
static const unsigned char *display_rr(const unsigned char *aptr,
                                       const unsigned char *abuf, int alen,
				       strbuffer_t *log);
static const char *type_name(int type);
static const char *class_name(int dnsclass);

void dns_query_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen)
{
	myconn_t *rec = (myconn_t *)arg;

	dbgprintf("Got result for %s\n", rec->testspec);

	rec->elapsedus = ntimerus(&rec->dnsstarttime, NULL);
	rec->dnsstatus = DNS_QUERY_COMPLETED;
	rec->talkresult = TALK_BADDATA; /* We'll set an explicit OK or timeout status below */

	/* Display an error message if there was an error, but only stop if
	 * we actually didn't get an answer buffer.
	 */
	switch (status) {
		case ARES_SUCCESS	: rec->talkresult = TALK_OK; break;
		case ARES_ENODATA	: addtobuffer(rec->textlog, "No data returned from server\n"); break;
		case ARES_EFORMERR	: addtobuffer(rec->textlog, "Server could not understand query\n"); break;
		case ARES_ESERVFAIL	: addtobuffer(rec->textlog, "Server failed\n"); break;
		case ARES_ENOTFOUND	: addtobuffer(rec->textlog, "Name not found\n"); break;
		case ARES_ENOTIMP	: addtobuffer(rec->textlog, "Not implemented\n"); break;
		case ARES_EREFUSED	: addtobuffer(rec->textlog, "Server refused query\n"); break;
		case ARES_EBADNAME	: addtobuffer(rec->textlog, "Invalid name in query\n"); break;
		case ARES_ETIMEOUT	: rec->talkresult = TALK_CONN_TIMEOUT; addtobuffer(rec->textlog, "Timeout\n"); break;
		case ARES_ECONNREFUSED	: rec->talkresult = TALK_CONN_FAILED; addtobuffer(rec->textlog, "Server unavailable\n"); break;
		case ARES_ENOMEM	: addtobuffer(rec->textlog, "Out of memory\n"); break;
		case ARES_EDESTRUCTION	: rec->talkresult = TALK_CONN_TIMEOUT; addtobuffer(rec->textlog, "Timeout (channel destroyed)\n"); break;
		default			: addtobuffer(rec->textlog, "Undocumented ARES return code\n"); break;
	}

	dns_print_response(abuf, alen, rec->textlog);
	test_is_done(rec);
}


void dns_print_response(unsigned char *abuf, int alen, strbuffer_t *log)
{
	int id, qr, opcode, aa, tc, rd, ra, rcode;
	unsigned int qdcount, ancount, nscount, arcount, i;
	const unsigned char *aptr;
	char outbuf[4096];

	if (!abuf || (alen < HFIXEDSZ)) return;

	/* Parse the answer header. */
	id = DNS_HEADER_QID(abuf);
	qr = DNS_HEADER_QR(abuf);
	opcode = DNS_HEADER_OPCODE(abuf);
	aa = DNS_HEADER_AA(abuf);
	tc = DNS_HEADER_TC(abuf);
	rd = DNS_HEADER_RD(abuf);
	ra = DNS_HEADER_RA(abuf);
	rcode = DNS_HEADER_RCODE(abuf);
	qdcount = DNS_HEADER_QDCOUNT(abuf);
	ancount = DNS_HEADER_ANCOUNT(abuf);
	nscount = DNS_HEADER_NSCOUNT(abuf);
	arcount = DNS_HEADER_ARCOUNT(abuf);

	/* Display the answer header. */
	sprintf(outbuf, "id: %d\nflags: %s%s%s%s%s\nopcode: %s\nrcode: %s\n", 
		id,
		qr ? "qr " : "",
		aa ? "aa " : "",
		tc ? "tc " : "",
		rd ? "rd " : "",
		ra ? "ra " : "",
		opcodes[opcode],
		rcodes[rcode]);
	addtobuffer(log, outbuf);

	/* Display the questions. */
	if (qdcount) addtobuffer(log, "Questions:\n");
	aptr = abuf + HFIXEDSZ;
	for (i = 0; i < qdcount; i++)
	{
		aptr = display_question(aptr, abuf, alen, log);
		if (!aptr) return;
	}

	/* Display the answers. */
	if (ancount) addtobuffer(log, "Answers:\n");
	for (i = 0; i < ancount; i++)
	{
		aptr = display_rr(aptr, abuf, alen, log);
		if (!aptr) return;
	}

	/* Display the NS records. */
	if (nscount) addtobuffer(log, "NS records:\n");
	for (i = 0; i < nscount; i++)
	{
		aptr = display_rr(aptr, abuf, alen, log);
		if (!aptr) return;
	}

	/* Display the additional records. */
	if (arcount) addtobuffer(log, "Additional records:\n");
	for (i = 0; i < arcount; i++)
	{
		aptr = display_rr(aptr, abuf, alen, log);
		if (!aptr) return;
	}
}

static const unsigned char *display_question(const unsigned char *aptr,
		const unsigned char *abuf, int alen, strbuffer_t *log)
{
	char *name;
	int type, dnsclass, status;
	long len;
	char *outp, outbuf[4096];

	/* Parse the question name. */
	status = ares_expand_name(aptr, abuf, alen, &name, &len);
	if (status != ARES_SUCCESS)
		return NULL;
	aptr += len;

	/* Make sure there's enough data after the name for the fixed part
	 * of the question.
	 */
	if (aptr + QFIXEDSZ > abuf + alen)
	{
		ares_free_string(name);
		return NULL;
	}

	/* Parse the question type and class. */
	type = DNS_QUESTION_TYPE(aptr);
	dnsclass = DNS_QUESTION_CLASS(aptr);
	aptr += QFIXEDSZ;

	outp = outbuf;

	/* Display the question, in a format sort of similar to how we will
	 * display RRs.
	 */
	outp += sprintf(outp, "\t%-15s.\t", name);
	if (dnsclass != C_IN)
		outp += sprintf(outp, "\t%s", class_name(dnsclass));
	outp += sprintf(outp, "\t%s\n", type_name(type));
	ares_free_string(name);

	addtobuffer(log, outbuf);
	return aptr;
}

static const unsigned char *display_rr(const unsigned char *aptr,
		const unsigned char *abuf, int alen, strbuffer_t *log)
{
	const unsigned char *p;
	int type, dnsclass, ttl, dlen, status;
	long len;
	char addr[46];
	union {
		unsigned char * as_uchar;
		char * as_char;
	} name;
	char *outp, outbuf[4096];

	/* Parse the RR name. */
	status = ares_expand_name(aptr, abuf, alen, &name.as_char, &len);
	if (status != ARES_SUCCESS)
		return NULL;
	aptr += len;

	/* Make sure there is enough data after the RR name for the fixed
	 * part of the RR.
	 */
	if (aptr + RRFIXEDSZ > abuf + alen)
	{
		ares_free_string(name.as_char);
		return NULL;
	}

	/* Parse the fixed part of the RR, and advance to the RR data
	 * field. */
	type = DNS_RR_TYPE(aptr);
	dnsclass = DNS_RR_CLASS(aptr);
	ttl = DNS_RR_TTL(aptr);
	dlen = DNS_RR_LEN(aptr);
	aptr += RRFIXEDSZ;
	if (aptr + dlen > abuf + alen)
	{
		ares_free_string(name.as_char);
		return NULL;
	}

	outp = outbuf;

	/* Display the RR name, class, and type. */
	outp += sprintf(outp, "\t%-15s.\t%d", name.as_char, ttl);
	if (dnsclass != C_IN)
		outp += sprintf(outp, "\t%s", class_name(dnsclass));
	outp += sprintf(outp, "\t%s", type_name(type));
	ares_free_string(name.as_char);

	/* Display the RR data.  Don't touch aptr. */
	switch (type)
	{
		case T_CNAME:
		case T_MB:
		case T_MD:
		case T_MF:
		case T_MG:
		case T_MR:
		case T_NS:
		case T_PTR:
			/* For these types, the RR data is just a domain name. */
			status = ares_expand_name(aptr, abuf, alen, &name.as_char, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t%s.", name.as_char);
			ares_free_string(name.as_char);
			break;

		case T_HINFO:
			/* The RR data is two length-counted character strings. */
			p = aptr;
			len = *p;
			if (p + len + 1 > aptr + dlen)
				return NULL;
			status = ares_expand_string(p, abuf, alen, &name.as_uchar, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t%s", name.as_char);
			ares_free_string(name.as_char);
			p += len;
			len = *p;
			if (p + len + 1 > aptr + dlen)
				return NULL;
			status = ares_expand_string(p, abuf, alen, &name.as_uchar, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t%s", name.as_char);
			ares_free_string(name.as_char);
			break;

		case T_MINFO:
			/* The RR data is two domain names. */
			p = aptr;
			status = ares_expand_name(p, abuf, alen, &name.as_char, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t%s.", name.as_char);
			ares_free_string(name.as_char);
			p += len;
			status = ares_expand_name(p, abuf, alen, &name.as_char, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t%s.", name.as_char);
			ares_free_string(name.as_char);
			break;

		case T_MX:
			/* The RR data is two bytes giving a preference ordering, and
			 * then a domain name.
			 */
			if (dlen < 2)
				return NULL;
			outp += sprintf(outp, "\t%d", DNS__16BIT(aptr));
			status = ares_expand_name(aptr + 2, abuf, alen, &name.as_char, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t%s.", name.as_char);
			ares_free_string(name.as_char);
			break;

		case T_SOA:
			/* The RR data is two domain names and then five four-byte
			 * numbers giving the serial number and some timeouts.
			 */
			p = aptr;
			status = ares_expand_name(p, abuf, alen, &name.as_char, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t%s.\n", name.as_char);
			ares_free_string(name.as_char);
			p += len;
			status = ares_expand_name(p, abuf, alen, &name.as_char, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t\t\t\t\t\t%s.\n", name.as_char);
			ares_free_string(name.as_char);
			p += len;
			if (p + 20 > aptr + dlen)
				return NULL;
			outp += sprintf(outp, "\t\t\t\t\t\t( %lu %lu %lu %lu %lu )",
					(unsigned long)DNS__32BIT(p), (unsigned long)DNS__32BIT(p+4),
					(unsigned long)DNS__32BIT(p+8), (unsigned long)DNS__32BIT(p+12),
					(unsigned long)DNS__32BIT(p+16));
			break;

		case T_TXT:
			/* The RR data is one or more length-counted character
			 * strings. */
			p = aptr;
			while (p < aptr + dlen)
			{
				len = *p;
				if (p + len + 1 > aptr + dlen)
					return NULL;
				status = ares_expand_string(p, abuf, alen, &name.as_uchar, &len);
				if (status != ARES_SUCCESS)
					return NULL;
				outp += sprintf(outp, "\t%s", name.as_char);
				ares_free_string(name.as_char);
				p += len;
			}
			break;

#ifdef IPV4_SUPPORT
		case T_A:
			/* The RR data is a four-byte Internet address. */
			if (dlen != 4)
				return NULL;
			outp += sprintf(outp, "\t%s", inet_ntop(AF_INET,aptr,addr,sizeof(addr)));
			break;
#endif

#ifdef IPV6_SUPPORT
		case T_AAAA:
			/* The RR data is a 16-byte IPv6 address. */
			if (dlen != 16)
				return NULL;
			outp += sprintf(outp, "\t%s", inet_ntop(AF_INET6,aptr,addr,sizeof(addr)));
			break;
#endif

		case T_WKS:
			/* Not implemented yet */
			break;

		case T_SRV:
			/* The RR data is three two-byte numbers representing the
			 * priority, weight, and port, followed by a domain name.
			 */

			outp += sprintf(outp, "\t%d", DNS__16BIT(aptr));
			outp += sprintf(outp, " %d", DNS__16BIT(aptr + 2));
			outp += sprintf(outp, " %d", DNS__16BIT(aptr + 4));

			status = ares_expand_name(aptr + 6, abuf, alen, &name.as_char, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t%s.", name.as_char);
			ares_free_string(name.as_char);
			break;

		case T_NAPTR:

			outp += sprintf(outp, "\t%d", DNS__16BIT(aptr)); /* order */
			outp += sprintf(outp, " %d\n", DNS__16BIT(aptr + 2)); /* preference */

			p = aptr + 4;
			status = ares_expand_string(p, abuf, alen, &name.as_uchar, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t\t\t\t\t\t%s\n", name.as_char);
			ares_free_string(name.as_char);
			p += len;

			status = ares_expand_string(p, abuf, alen, &name.as_uchar, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t\t\t\t\t\t%s\n", name.as_char);
			ares_free_string(name.as_char);
			p += len;

			status = ares_expand_string(p, abuf, alen, &name.as_uchar, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t\t\t\t\t\t%s\n", name.as_char);
			ares_free_string(name.as_char);
			p += len;

			status = ares_expand_name(p, abuf, alen, &name.as_char, &len);
			if (status != ARES_SUCCESS)
				return NULL;
			outp += sprintf(outp, "\t\t\t\t\t\t%s", name.as_char);
			ares_free_string(name.as_char);
			break;

		case T_DS:
		case T_SSHFP:
		case T_RRSIG:
		case T_NSEC:
		case T_DNSKEY:
			outp += sprintf(outp, "\t[RR type parsing unavailable]");
			break;

		default:
			outp += sprintf(outp, "\t[Unknown RR; cannot parse]");
			break;
	}
	outp += sprintf(outp, "\n");
	addtobuffer(log, outbuf);

	return aptr + dlen;
}

static const char *type_name(int type)
{
	int i;

	for (i = 0; i < ntypes; i++)
	{
		if (types[i].value == type)
			return types[i].name;
	}
	return "(unknown)";
}

static const char *class_name(int dnsclass)
{
	int i;

	for (i = 0; i < nclasses; i++)
	{
		if (classes[i].value == dnsclass)
			return classes[i].name;
	}
	return "(unknown)";
}

int dns_name_type(char *name)
{
	int i;

	for (i = 0; i < ntypes; i++) {
		if (strcasecmp(types[i].name, name) == 0) return types[i].value;
	}
	return T_A;
}

int dns_name_class(char *name)
{
	int i;

	for (i = 0; i < nclasses; i++) {
		if (strcasecmp(classes[i].name, name) == 0) return classes[i].value;
	}
	return C_IN;
}


void dns_lookup_callback(void *arg, int status, int timeouts, struct hostent *host)
{
	/*
	 * This callback is used for the ares_gethostbyname() calls that we do to 
	 * lookup the IP's of the hosts we are about to test.
	 */
	myconn_t *rec = (myconn_t *)arg;

	rec->dnselapsedus = ntimerus(&rec->netparams.lookupstart, NULL);

	if ((status == ARES_SUCCESS) && (host->h_addr_list[0] != NULL)) {
		/* Got a DNS result */
		char addr_buf[46];

		dbgprintf("Got lookup result for %s\n", rec->netparams.lookupstring);
		inet_ntop(host->h_addrtype, *(host->h_addr_list), addr_buf, sizeof(addr_buf));

		dns_addtocache(rec, addr_buf);
	}
	else if ( (status == ARES_ENOTFOUND) || 
		  ((status == ARES_SUCCESS) && (host->h_addr_list[0] == NULL)) ) {
		/* No IP for this hostname/address family combination */
		dbgprintf("Got negative lookup result for %s\n", rec->netparams.lookupstring);
		dns_addtocache(rec, "-");
	}
	else {
		/* Uh-oh ... */
		errprintf("ARES library failed during name resolution: %s\n",
			  ares_strerror(status));
		dns_addtocache(rec, "-");
	}
}

