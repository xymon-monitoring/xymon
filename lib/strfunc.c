/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains string handling routines.                                      */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include "config.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include "libxymon.h"
#include "version.h"

#define BUFSZINCREMENT 4096

strbuffer_t *newstrbuffer(int initialsize)
{
	strbuffer_t *newbuf;
	
	newbuf = calloc(1, sizeof(strbuffer_t));

	if (!initialsize) initialsize = 4096;

	newbuf->s = (char *)malloc(initialsize);
	if (newbuf->s == NULL) {
		errprintf("newstrbuffer: Attempt to allocate failed (initialsize=%d): %s\n", initialsize, strerror(errno));
		xfree(newbuf);
		return NULL;
	}
	*(newbuf->s) = '\0';
	newbuf->sz = initialsize;

	return newbuf;
}

strbuffer_t *convertstrbuffer(char *buffer, int bufsz)
{
	strbuffer_t *newbuf;
	
	newbuf = calloc(1, sizeof(strbuffer_t));
	newbuf->s = buffer;
	newbuf->used = strlen(buffer);
	newbuf->sz = (bufsz ? bufsz : newbuf->used+1);

	return newbuf;
}

void freestrbuffer(strbuffer_t *buf)
{
	if (buf == NULL) return;

	if (buf->s) xfree(buf->s);
	xfree(buf);
}

void clearstrbuffer(strbuffer_t *buf)
{
	if (buf == NULL) return;

	if (buf->s) {
		*(buf->s) = '\0';
		buf->used = 0;
	}
}

char *grabstrbuffer(strbuffer_t *buf)
{
	char *result;

	if (buf == NULL) return NULL;

	result = buf->s;
	xfree(buf);

	return result;
}

strbuffer_t *dupstrbuffer(char *src)
{
	strbuffer_t *newbuf;
	int len = 0;
	
	newbuf = newstrbuffer(0);
	if (src) {
		newbuf->s = strdup(src);
		len = strlen(src);
		newbuf->used = newbuf->sz = len;
	}

	return newbuf;
}

static void strbuf_addtobuffer(strbuffer_t *buf, char *newtext, int newlen)
{
	if (buf->s == NULL) {
		buf->used = 0;
		buf->sz = newlen + BUFSZINCREMENT;
		buf->s = (char *) malloc(buf->sz);
		*(buf->s) = '\0';
	}
	else if ((buf->used + newlen + 1) > buf->sz) {
		buf->sz += (newlen + BUFSZINCREMENT);
		buf->s = (char *) realloc(buf->s, buf->sz);
	}

	if (newtext) {
		memcpy(buf->s+buf->used, newtext, newlen);
		buf->used += newlen;
		/* Make sure result buffer is NUL-terminated; newtext might not be. */
		*(buf->s + buf->used) = '\0';
	}
}

void addtobuffer(strbuffer_t *buf, char *newtext)
{
	if (newtext) strbuf_addtobuffer(buf, newtext, strlen(newtext));
}

void addtobuffer_many(strbuffer_t *buf, ...)
{
	va_list ap;
	char *newtext;

	va_start(ap, buf);
	newtext = va_arg(ap, char *);
	while (newtext) {
		strbuf_addtobuffer(buf, newtext, strlen(newtext));
		newtext = va_arg(ap, char *);
	}
	va_end(ap);
}

void addtostrbuffer(strbuffer_t *buf, strbuffer_t *newtext)
{
	strbuf_addtobuffer(buf, STRBUF(newtext), STRBUFLEN(newtext));
}

void addtobufferraw(strbuffer_t *buf, char *newdata, int bytes)
{
	/* Add binary data to the buffer */
	strbuf_addtobuffer(buf, newdata, bytes);
}

void strbufferchop(strbuffer_t *buf, int count)
{
	/* Remove COUNT characters from end of buffer */
	if ((buf == NULL) || (buf->s == NULL)) return;

	if (count >= buf->used) count = buf->used;

	buf->used -= count;
	*(buf->s+buf->used) = '\0';
}

void strbufferrecalc(strbuffer_t *buf)
{
	if (buf == NULL) return;

	if (buf->s == NULL) {
		buf->used = 0;
		return;
	}

	buf->used = strlen(buf->s);
}

void strbuffergrow(strbuffer_t *buf, int bytes)
{
	if (buf == NULL) return;

	buf->sz += bytes;
	buf->s = (char *) realloc(buf->s, buf->sz);
}

void strbufferuse(strbuffer_t *buf, int bytes)
{
	if (buf == NULL) return;

	if ((buf->used + bytes) < buf->sz) {
		buf->used += bytes;
	}
	else {
		errprintf("strbuffer: Attempt to use more than allocated (sz=%d, used=%d, growbytes=%d\n", 
			  buf->sz, buf->used, bytes);
	}
	*(buf->s+buf->used) = '\0';
}

/*
 * The four *quoted() routines below are one scan over different character
 * sets. qsub pairs each character these routines act on with the text that
 * replaces it; quote() runs the scan, copying the input a run at a time and
 * substituting at each listed character. Each public routine keeps its own
 * static result buffer, so a caller mixing two of them in one expression
 * gets two buffers as before; two calls to the same routine still alias, as
 * every single-static-buffer routine in this file does.
 */

typedef struct qsub {
	char c;			/* the character to replace, 0 ends the list */
	char *rep;		/* the text that replaces it */
} qsub_t;

static char *quote(strbuffer_t **resultp, char *s, qsub_t *subs, int nullisnull)
{
	strbuffer_t *result;
	char set[16];
	char *inp, *endp;
	char c;
	int i;

	/*
	 * set holds the table's characters for strcspn(). A table that does not
	 * fit is a mistake in this file, not in the input: stopping short would
	 * leave its last characters unescaped, so stop the program instead --
	 * before the early returns, so that a NULL input does not hide it.
	 */
	for (i = 0; subs[i].c; i++) {
		if (i >= (int)sizeof(set) - 1) {
			errprintf("quote: a substitution table has more than %d entries\n", (int)sizeof(set) - 1);
			abort();
		}
		set[i] = subs[i].c;
	}
	set[i] = '\0';

	if (!s && nullisnull) return NULL;

	if (!*resultp) *resultp = newstrbuffer(4096);
	result = *resultp;
	clearstrbuffer(result);
	if (!s) return STRBUF(result);

	inp = s;
	do {
		endp = inp + strcspn(inp, set);
		c = *endp;
		if (endp > inp) addtobufferraw(result, inp, endp-inp);
		if (c) {
			for (i = 0; subs[i].c; i++) {
				if (subs[i].c == c) { addtobuffer(result, subs[i].rep); break; }
			}
		}
		inp = (c == '\0') ? NULL : endp+1;
	} while (inp);

	return STRBUF(result);
}

char *htmlquoted(char *s)
{
	/*
	 * This routine converts a plain string into an html-quoted string
	 */

	static strbuffer_t *result = NULL;
	static qsub_t subs[] = {
		{'"', "&quot;"}, {'&', "&amp;"}, {'<', "&lt;"}, {'>', "&gt;"},
		{' ', "&nbsp;"}, {0, NULL}
	};

	return quote(&result, s, subs, 1);
}

char *htmlattrquoted(char *s)
{
	/*
	 * HTML-escape a value for an attribute (VALUE="..."), an <option> or
	 * table text: & < > " ' are escaped, and a space stays a space. Not
	 * htmlquoted(), which turns a space into &nbsp;: inside a VALUE
	 * attribute that would come back literally on the next form submit.
	 * A NULL gives "", so the result can go straight to printf("%s").
	 */

	static strbuffer_t *result = NULL;
	static qsub_t subs[] = {
		{'"', "&quot;"}, {'&', "&amp;"}, {'<', "&lt;"}, {'>', "&gt;"},
		{'\'', "&#39;"}, {0, NULL}
	};

	return quote(&result, s, subs, 0);
}

char *jsquoted(char *s)
{
	/*
	 * Escape a value for a double-quoted JavaScript string literal inside a
	 * <script> element: the backslash and the quote that would end the string,
	 * "<" and ">" so a "</script>" in the value cannot close the element, and
	 * the line terminators a string literal cannot span. A NULL gives "".
	 */

	static strbuffer_t *result = NULL;
	static qsub_t subs[] = {
		{'"', "\\\""}, {'\\', "\\\\"}, {'<', "\\x3c"}, {'>', "\\x3e"},
		{'\n', "\\n"}, {'\r', "\\r"}, {0, NULL}
	};

	return quote(&result, s, subs, 0);
}

/*
 * The length of the well-formed UTF-8 character at s, or 0 when the byte at s
 * does not start one: overlong forms, surrogates and code points past U+10FFFF
 * are not well-formed. U+FFFE and U+FFFF are well-formed but not allowed in
 * XML 1.0, and give -3. s is NUL-terminated; no byte past the NUL is read.
 */
int utf8_char(const unsigned char *s)
{
	unsigned char c = s[0];
	int len, i;
	unsigned char lo = 0x80, hi = 0xBF;

	if (c < 0x80) return 1;
	else if ((c >= 0xC2) && (c <= 0xDF)) len = 2;
	else if ((c >= 0xE0) && (c <= 0xEF)) {
		len = 3;
		if (c == 0xE0) lo = 0xA0;	/* overlong */
		if (c == 0xED) hi = 0x9F;	/* surrogates */
	}
	else if ((c >= 0xF0) && (c <= 0xF4)) {
		len = 4;
		if (c == 0xF0) lo = 0x90;	/* overlong */
		if (c == 0xF4) hi = 0x8F;	/* past U+10FFFF */
	}
	else return 0;

	if ((s[1] < lo) || (s[1] > hi)) return 0;
	for (i = 2; (i < len); i++) {
		if ((s[i] < 0x80) || (s[i] > 0xBF)) return 0;
	}
	if ((c == 0xEF) && (s[1] == 0xBF) && ((s[2] == 0xBE) || (s[2] == 0xBF))) return -3;	/* U+FFFE, U+FFFF */

	return len;
}

void addtobuffer_xmlquoted(strbuffer_t *result, char *s, int utf8, int cdata)
{
	/*
	 * Append text as XML character data to result, for a feed declared UTF-8
	 * (utf8 set) or not. With cdata, inside a CDATA section, each "]]>" is split
	 * so the text cannot end the section; without, with & < > as entities.
	 * Either way the control bytes XML forbids (below 0x20 but tab, newline,
	 * carriage return) are left out, and when utf8 is set each byte that is not
	 * part of well-formed UTF-8 becomes U+FFFD, so one stray byte cannot make
	 * the document unreadable. A NULL s appends nothing (an empty CDATA section
	 * with cdata). Writing straight to the caller's buffer, several fields can
	 * be appended in sequence with no shared state -- unlike xmlquoted() below.
	 */
	if (cdata) addtobuffer(result, "<![CDATA[");
	for (; (s && *s); s++) {
		if (!cdata && (*s == '&')) addtobuffer(result, "&amp;");
		else if (!cdata && (*s == '<')) addtobuffer(result, "&lt;");
		else if (!cdata && (*s == '>')) addtobuffer(result, "&gt;");
		else if (cdata && (strncmp(s, "]]>", 3) == 0)) {
			addtobuffer(result, "]]]]><![CDATA[>");
			s += 2;
		}
		else if (((unsigned char)*s < 0x20) && (*s != '\t') && (*s != '\n') && (*s != '\r')) {
			continue;
		}
		else if (utf8 && ((unsigned char)*s >= 0x80)) {
			int len = utf8_char((unsigned char *)s);

			if (len > 0) {
				addtobufferraw(result, s, len);
				s += len - 1;
			}
			else {
				addtobuffer(result, "\xEF\xBF\xBD");
				if (len < 0) s += -len - 1;
			}
		}
		else {
			addtobufferraw(result, s, 1);
		}
	}
	if (cdata) addtobuffer(result, "]]>");
}

char *xmlquoted(char *s, int utf8, int cdata)
{
	/*
	 * addtobuffer_xmlquoted() into a static buffer, for a FILE* caller that
	 * wants a string. The result is good until the next call -- one xmlquoted()
	 * per statement. A caller appending several fields to one buffer uses
	 * addtobuffer_xmlquoted() instead, which keeps no shared state.
	 */
	static strbuffer_t *result = NULL;

	if (!result) result = newstrbuffer(4096);
	clearstrbuffer(result);
	addtobuffer_xmlquoted(result, s, utf8, cdata);

	return STRBUF(result);
}

char *prehtmlquoted(char *s)
{
	/*
	 * This routine converts a string which may contain html to a string
	 * safe to include in a PRE block. It's similar to above, but escapes
	 * only the minmum characters for efficiency. The ">" is not, strictly
	 * speaking, needed, but unbalanced encoding might confuse automated readers.
	 */

	static strbuffer_t *result = NULL;
	static qsub_t subs[] = {
		{'&', "&amp;"}, {'<', "&lt;"}, {'>', "&gt;"}, {0, NULL}
	};

	return quote(&result, s, subs, 1);
}

strbuffer_t *replacetext(char *original, char *oldtext, char *newtext)
{
	strbuffer_t *result = newstrbuffer(0);
	char *pos = original, *start;

	do {
		start = pos; pos = strstr(pos, oldtext);
		if (pos) {
			if (pos > start) strbuf_addtobuffer(result, start, (pos - start));
			addtobuffer(result, newtext);
			pos += strlen(oldtext);
		}
		else
			addtobuffer(result, start);
	} while (pos);

	return result;
}

