/*----------------------------------------------------------------------------*/
/* Xymon mail-acknowledgment filter.                                          */
/*                                                                            */
/* This program runs from the Xymon users' .procmailrc file, and processes    */
/* incoming e-mails that are responses to alert mails that Xymon has sent     */
/* out.                                                                       */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "../lib/pcre2_api_compat.h"

#include "libxymon.h"

/*
 * Replace control bytes (CR, LF, NUL, TAB and the like) in a decoded header
 * with spaces, so a byte a decoder turned into a newline cannot inject an extra
 * line into the xymondack message, and cap the length -- not splitting a
 * trailing UTF-8 character at the cap -- so the result stays under the ack
 * log's line length.
 */
static void cleantext(char *s, int maxlen)
{
	int i;

	if (!s) return;
	for (i = 0; s[i]; i++)
		if (((unsigned char)s[i] < 0x20) || ((unsigned char)s[i] == 0x7f)) s[i] = ' ';
	if (i > maxlen) {
		int floor = (maxlen > 3) ? (maxlen - 3) : 0;
		while ((maxlen > floor) && (((unsigned char)s[maxlen] & 0xc0) == 0x80)) maxlen--;
		s[maxlen] = '\0';
	}
}

/* --- Minimal MIME body decoding -------------------------------------------
 * Find the decoded text of a reply's first text/plain part, to use as the
 * acknowledgement cause. Bounded against a hostile message: at most a few
 * levels of multipart nesting, a limited part count, and a capped decode.
 */
#define MIME_MAXDEPTH 4
#define MIME_MAXPARTS 100
#define MIME_MAXDECODE (64*1024)

/* The value of a Content-Type parameter (e.g. "boundary"), unquoted and
 * malloc'd, or NULL. Only a parameter at the start of a ';'-delimited field
 * matches, so "boundary" is not found inside "xboundary". */
static char *ct_param(const char *ct, const char *name)
{
	const char *p;
	size_t nl = strlen(name);

	if (!ct) return NULL;
	for (p = ct; (p = strchr(p, ';')) != NULL; ) {
		const char *v, *e;
		p++;
		while ((*p == ' ') || (*p == '\t')) p++;
		if (strncasecmp(p, name, nl) != 0) continue;
		v = p + nl;
		while ((*v == ' ') || (*v == '\t')) v++;
		if (*v != '=') continue;
		v++;
		while ((*v == ' ') || (*v == '\t')) v++;
		if (*v == '"') {
			v++;
			e = strchr(v, '"');
			if (!e) e = v + strlen(v);
		}
		else {
			e = v;
			while (*e && (*e != ';') && (*e != ' ') && (*e != '\t')) e++;
		}
		{ char *r = (char *)malloc((size_t)(e - v) + 1); memcpy(r, v, (size_t)(e - v)); r[e - v] = '\0'; return r; }
	}
	return NULL;
}

static int ct_is_multipart(const char *ct) { return ct && (strncasecmp(ct, "multipart/", 10) == 0); }
static int ct_is_textplain(const char *ct) { return (!ct) || (strncasecmp(ct, "text/plain", 10) == 0); }

/* Decode an isolated entity body (len bytes) by its transfer-encoding, into a
 * malloc'd NUL-terminated string the caller xfree()s. Capped at MIME_MAXDECODE. */
static char *decode_cte(const char *body, size_t len, const char *cte)
{
	char *out;

	if (len > MIME_MAXDECODE) len = MIME_MAXDECODE;
	if (cte && (strncasecmp(cte, "base64", 6) == 0)) {
		char *packed = (char *)malloc(len + 1);
		size_t i, j = 0;
		for (i = 0; i < len; i++) if (!isspace((unsigned char)body[i])) packed[j++] = body[i];
		packed[j] = '\0';
		out = base64decode((unsigned char *)packed);
		xfree(packed);
		return out;
	}
	if (cte && (strncasecmp(cte, "quoted-printable", 16) == 0)) {
		size_t n;
		out = (char *)malloc(len + 1);
		n = qpdecode(body, len, out);
		out[n] = '\0';
		return out;
	}
	out = (char *)malloc(len + 1); memcpy(out, body, len); out[len] = '\0';
	return out;	/* 7bit / 8bit / binary / unknown: carried as-is */
}

/* One header value from an entity's header block [h, h+len), malloc'd, or NULL. */
static char *part_header(const char *h, size_t len, const char *name)
{
	size_t nl = strlen(name);
	const char *p = h, *end = h + len;

	while (p < end) {
		const char *eol = memchr(p, '\n', (size_t)(end - p));
		const char *next = eol ? eol + 1 : end;
		if (((size_t)(end - p) >= nl) && (strncasecmp(p, name, nl) == 0)) {
			/* Unfold the value: a header may be split over several lines,
			 * each continuation marked by leading whitespace (RFC 5322
			 * 2.2.3), so a part's "boundary=" can sit on the second line. */
			strbuffer_t *val = newstrbuffer(0);
			const char *v = p + nl, *ve = eol ? eol : end;
			while ((v < ve) && ((*v == ' ') || (*v == '\t'))) v++;
			for (;;) {
				if ((ve > v) && (ve[-1] == '\r')) ve--;
				addtobufferraw(val, (char *)v, (int)(ve - v));
				if (!eol || (eol + 1 >= end)) break;
				if ((eol[1] != ' ') && (eol[1] != '\t')) break;
				v = eol + 1;
				while ((v < end) && ((*v == ' ') || (*v == '\t'))) v++;
				eol = memchr(v, '\n', (size_t)(end - v));
				ve = eol ? eol : end;
				addtobuffer(val, " ");
			}
			return grabstrbuffer(val);
		}
		p = next;
	}
	return NULL;
}

static char *first_plain(const char *body, size_t len, const char *ct, const char *cte, int depth, int *parts);

/* Process one multipart part [part, part+len): split its headers from its body,
 * read the part's Content-Type/-Transfer-Encoding, and recurse. */
static char *process_part(const char *part, size_t len, int depth, int *parts)
{
	const char *hend = part, *end = part + len, *pbody;
	char *pct, *pcte, *got;

	/* the part's headers end at the first blank line */
	while (hend < end) {
		const char *eol = memchr(hend, '\n', (size_t)(end - hend));
		size_t ll = eol ? (size_t)(eol - hend) : (size_t)(end - hend);
		if ((ll == 0) || ((ll == 1) && (hend[0] == '\r'))) { hend = eol ? eol + 1 : end; break; }
		hend = eol ? eol + 1 : end;
	}
	pbody = hend;
	pct  = part_header(part, (size_t)(pbody - part), "content-type:");
	pcte = part_header(part, (size_t)(pbody - part), "content-transfer-encoding:");
	got = first_plain(pbody, (size_t)(end - pbody), pct, pcte, depth + 1, parts);
	if (pct) xfree(pct); if (pcte) xfree(pcte);
	return got;
}

static char *first_plain(const char *body, size_t len, const char *ct, const char *cte, int depth, int *parts)
{
	char *boundary, *dash, *got = NULL;
	const char *p, *bodyend = body + len, *part_start = NULL;
	size_t bl;

	if (depth > MIME_MAXDEPTH) return NULL;
	if (!ct_is_multipart(ct)) {
		if (ct_is_textplain(ct)) return decode_cte(body, len, cte);
		return NULL;	/* text/html, application/..., ... : no plain text here */
	}

	boundary = ct_param(ct, "boundary");
	if (!boundary || !*boundary) { if (boundary) xfree(boundary); return NULL; }
	bl = strlen(boundary);
	dash = (char *)malloc(bl + 3); sprintf(dash, "--%s", boundary);

	for (p = body; p < bodyend; ) {
		const char *line = p, *eol = memchr(p, '\n', (size_t)(bodyend - p));
		const char *next = eol ? eol + 1 : bodyend;
		size_t ll = eol ? (size_t)(eol - line) : (size_t)(bodyend - line);
		int is_boundary, is_close;

		if (ll && (line[ll - 1] == '\r')) ll--;
		is_boundary = (ll >= bl + 2) && (memcmp(line, dash, bl + 2) == 0) &&
			      ((ll == bl + 2) || (line[bl+2] == ' ') || (line[bl+2] == '\t') ||
			       ((ll >= bl + 4) && (line[bl+2] == '-') && (line[bl+3] == '-')));
		is_close = is_boundary && (ll >= bl + 4) && (memcmp(line + bl + 2, "--", 2) == 0);

		if (is_boundary) {
			if (part_start) {
				got = process_part(part_start, (size_t)(line - part_start), depth, parts);
				if (got) break;
			}
			if (is_close) break;
			if (++(*parts) > MIME_MAXPARTS) break;
			part_start = next;
		}
		p = next;
	}
	/* a final part with no closing boundary runs to the end of the message */
	if (!got && part_start) got = process_part(part_start, (size_t)(bodyend - part_start), depth, parts);
	xfree(dash); xfree(boundary);
	return got;
}

int main(int argc, char *argv[])
{
	strbuffer_t *inbuf;
	char *ackbuf;
	char *subjectline = NULL;
	char *returnpathline = NULL;
	char *fromline = NULL;
	char *firsttxtline = NULL;
	char **lastheader = NULL;
	strbuffer_t *bodybuf, *rawline;
	char *ctype = NULL;
	char *cte = NULL;
	int inheaders = 1;
	char *p;
	pcre2_code *subjexp;
	int err, result;
	PCRE2_SIZE errofs;
	pcre2_match_data *ovector;
	char cookie[10];
	PCRE2_SIZE l = sizeof(cookie);
	int duration = 0;
	int argi;
	char *envarea = NULL;

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
	}

	initfgets(stdin);
	inbuf = newstrbuffer(0);
	bodybuf = newstrbuffer(0);
	rawline = newstrbuffer(0);
	while (unlimfgets(inbuf, stdin)) {
		int cont = (inheaders && (STRBUFLEN(inbuf) > 0) &&
			    ((*STRBUF(inbuf) == ' ') || (*STRBUF(inbuf) == '\t')));

		if (!inheaders) {
			/* Keep the body line as received: a folded MIME part header
			 * is marked by the leading whitespace that sanitize strips,
			 * and the part parser needs it to unfold "boundary=". */
			clearstrbuffer(rawline);
			addtostrbuffer(rawline, inbuf);
		}

		sanitize_input(inbuf, 0, 0);

		if (!inheaders) {
			/* We're in the message body. Look for a "delay=N" line here. */
			if ((strncasecmp(STRBUF(inbuf), "delay=", 6) == 0) || (strncasecmp(STRBUF(inbuf), "delay ", 6) == 0)) {
				duration = durationvalue(STRBUF(inbuf)+6);
				continue;
			}
			else if ((strncasecmp(STRBUF(inbuf), "ack=", 4) == 0) || (strncasecmp(STRBUF(inbuf), "ack ", 4) == 0)) {
				/* Some systems cannot generate a subject. Allow them to ack
				 * via text in the message body. */
				subjectline = (char *)malloc(STRBUFLEN(inbuf) + 1024);
				sprintf(subjectline, "Subject: Xymon [%s]", STRBUF(inbuf)+4);
			}
			else if (STRBUFLEN(bodybuf) < 256*1024) {
				/* Buffer the body to MIME-decode its first text/plain part into
				 * the ack cause below; cap a single over-long line too, so one
				 * unbounded line cannot push the buffer past the limit. */
				size_t room = (size_t)(256*1024) - STRBUFLEN(bodybuf);
				size_t blen = STRBUFLEN(rawline);
				/* buffer the line as received, without its own newline */
				while ((blen > 0) && ((STRBUF(rawline)[blen-1] == '\n') || (STRBUF(rawline)[blen-1] == '\r'))) blen--;
				if (blen > room) blen = room;
				addtobufferraw(bodybuf, STRBUF(rawline), (int)blen);
				addtobuffer(bodybuf, "\n");
			}

			continue;	/* We don't care about the rest of the message body */
		}

		if (cont && lastheader && *lastheader) {
			/* RFC 5322 folded header: append this line to the one being built,
			 * but stop once it reaches the ack line length -- a hostile message
			 * can fold a header into very many lines, and merging each would be
			 * O(n^2). */
			if (strlen(*lastheader) < MAX_LINE_LEN) {
				char *merged = (char *)malloc(strlen(*lastheader) + STRBUFLEN(inbuf) + 2);
				sprintf(merged, "%s %s", *lastheader, STRBUF(inbuf));
				xfree(*lastheader);
				*lastheader = merged;
			}
			continue;
		}

		/* See if we're at the end of the mail headers */
		if (inheaders && (STRBUFLEN(inbuf) == 0)) { inheaders = 0; continue; }

		/* Is it one of those we want to keep ? */
		if (strncasecmp(STRBUF(inbuf), "return-path:", 12) == 0) { returnpathline = strdup(skipwhitespace(STRBUF(inbuf)+12)); lastheader = &returnpathline; }
		else if (strncasecmp(STRBUF(inbuf), "from:", 5) == 0)    { fromline = strdup(skipwhitespace(STRBUF(inbuf)+5)); lastheader = &fromline; }
		else if (strncasecmp(STRBUF(inbuf), "subject:", 8) == 0) { subjectline = strdup(skipwhitespace(STRBUF(inbuf)+8)); lastheader = &subjectline; }
		else if (strncasecmp(STRBUF(inbuf), "content-type:", 13) == 0) { ctype = strdup(skipwhitespace(STRBUF(inbuf)+13)); lastheader = &ctype; }
		else if (strncasecmp(STRBUF(inbuf), "content-transfer-encoding:", 26) == 0) { cte = strdup(skipwhitespace(STRBUF(inbuf)+26)); lastheader = &cte; }
		else lastheader = NULL;
	}
	freestrbuffer(inbuf);
	freestrbuffer(rawline);

	/* No subject ? No deal */
	if (subjectline == NULL) {
		dbgprintf("Subject-line not found\n");
		return 1;
	}

	/* Decode RFC 2047 encoded-words in the Subject before the cookie, DELAY
	 * and MSG regexes read it: a fully-encoded subject keeps no literal space
	 * for the cookie pattern and the ack would otherwise be dropped. */
	subjectline = rfc2047_decode(subjectline);

	/* Get the alert cookie */
	subjexp = pcre2_compile(PCRE2STR(".*(Xymon|Hobbit|BB)[ -]* \\[*(-*[0-9]+)[\\]!]*"), PCRE2_ZERO_TERMINATED, PCRE2_CASELESS, &err, &errofs, NULL);
	if (subjexp == NULL) {
		dbgprintf("pcre compile failed - 1\n");
		return 2;
	}
	ovector = pcre2_match_data_create_from_pattern(subjexp, NULL);
	if (ovector == NULL) {
		pcre2_code_free(subjexp);
		dbgprintf("pcre match-data allocation failed - 1\n");
		return 2;
	}
	result = pcre2_match(subjexp, PCRE2STR(subjectline), strlen(subjectline), 0, 0, ovector, NULL);
	if (result < 0) {
		pcre2_code_free(subjexp);
		pcre2_match_data_free(ovector);
		dbgprintf("Subject line did not match pattern\n");
		return 3; /* Subject did not match what we expected */
	}
	if (pcre2_substring_copy_bynumber(ovector, 2, PCRE2BUF(cookie), &l) != 0) {
		pcre2_code_free(subjexp);
		pcre2_match_data_free(ovector);
		dbgprintf("Could not find cookie value\n");
		return 4; /* No cookie */
	}
	pcre2_match_data_free(ovector);
	pcre2_code_free(subjexp);

	/* See if there's a "DELAY=" delay-value also */
	subjexp = pcre2_compile(PCRE2STR(".*DELAY[ =]+([0-9]+[mhdw]*)"), PCRE2_ZERO_TERMINATED, PCRE2_CASELESS, &err, &errofs, NULL);
	if (subjexp == NULL) {
		dbgprintf("pcre compile failed - 2\n");
		return 2;
	}
	ovector = pcre2_match_data_create_from_pattern(subjexp, NULL);
	if (ovector == NULL) {
		pcre2_code_free(subjexp);
		dbgprintf("pcre match-data allocation failed - 2\n");
		return 2;
	}
	result = pcre2_match(subjexp, PCRE2STR(subjectline), strlen(subjectline), 0, 0, ovector, NULL);
	if (result >= 0) {
		char delaytxt[4096];
		l = sizeof(delaytxt);
		if (pcre2_substring_copy_bynumber(ovector, 1, PCRE2BUF(delaytxt), &l) == 0) {
			duration = durationvalue(delaytxt);
		}
	}
	pcre2_match_data_free(ovector);
	pcre2_code_free(subjexp);

	/* See if there's a "msg" text also */
	subjexp = pcre2_compile(PCRE2STR(".*MSG[ =]+(.*)"), PCRE2_ZERO_TERMINATED, PCRE2_CASELESS, &err, &errofs, NULL);
	if (subjexp == NULL) {
		dbgprintf("pcre compile failed - 3\n");
		return 2;
	}
	ovector = pcre2_match_data_create_from_pattern(subjexp, NULL);
	if (ovector == NULL) {
		pcre2_code_free(subjexp);
		dbgprintf("pcre match-data allocation failed - 3\n");
		return 2;
	}
	result = pcre2_match(subjexp, PCRE2STR(subjectline), strlen(subjectline), 0, 0, ovector, NULL);
	if (result >= 0) {
		char msgtxt[4096];
		l = sizeof(msgtxt);
		if (pcre2_substring_copy_bynumber(ovector, 1, PCRE2BUF(msgtxt), &l) == 0) {
			firsttxtline = strdup(msgtxt);
		}
	}
	pcre2_match_data_free(ovector);
	pcre2_code_free(subjexp);

	/* No cause from the subject's MSG=? Take the first line of the body's
	 * first text/plain part, MIME-decoded. */
	if (firsttxtline == NULL) {
		int parts = 0;
		char *bodytxt = first_plain(STRBUF(bodybuf), STRBUFLEN(bodybuf), ctype, cte, 0, &parts);
		if (bodytxt) {
			char *ln = bodytxt;
			while (*ln) {
				char *eol = strchr(ln, '\n');
				char *t;
				if (eol) *eol = '\0';
				/* trim the line: sanitize_input() used to do this for the
				 * body, but it is now buffered as received (for folding) */
				ln += strspn(ln, " \t\r");
				t = ln + strlen(ln);
				while ((t > ln) && isspace((unsigned char)t[-1])) *(--t) = '\0';
				if (*ln) { firsttxtline = strdup(ln); break; }
				if (!eol) break;
				ln = eol + 1;
			}
			xfree(bodytxt);
		}
	}

	/* Use the "return-path:" header if we didn't see a From: line */
	if ((fromline == NULL) && returnpathline) fromline = returnpathline;
	if (fromline) fromline = rfc2047_decode(fromline);
	if (fromline) {
		/* Remove '<' and '>' from the fromline - they mess up HTML */
		while ((p = strchr(fromline, '<')) != NULL) *p = ' ';
		while ((p = strchr(fromline, '>')) != NULL) *p = ' ';
	}

	/* Scrub control bytes a decode may have produced, before they reach the
	 * xymondack message (a decoded newline would forge an "Acked by:" line). */
	if (fromline) cleantext(fromline, 4000);
	if (firsttxtline) cleantext(firsttxtline, 4000);

	/* Setup the acknowledge message */
	if (duration == 0) duration = 60;	/* Default: Ack for 60 minutes */
	if (firsttxtline == NULL) firsttxtline = "<No cause specified>";
	ackbuf = (char *)malloc(4096 + strlen(firsttxtline) + (fromline ? strlen(fromline) : 0));
	p = ackbuf;
	p += sprintf(p, "xymondack %s %d %s", cookie, duration, firsttxtline);
	if (fromline) {
		p += sprintf(p, "\nAcked by: %s", fromline);
	}

	if (debug) {
		printf("%s\n", ackbuf);
		return 0;
	}

	sendmessage(ackbuf, NULL, XYMON_TIMEOUT, NULL);
	return 0;
}

