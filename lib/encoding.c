/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                      */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for Base64 encoding and decoding.                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "libxymon.h"

static char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
 * Encode a counted buffer. The caller may hold bytes rather than a string --
 * a reply framed by a length carries NULs -- and measuring the input with
 * strlen() would encode only what precedes the first one.
 */
char *base64encode_len(unsigned char *buf, int buflen)
{
	unsigned char c0, c1, c2;
	unsigned int n0, n1, n2, n3;
	unsigned char *inp, *outp;
	unsigned char *result;
	int left;

	if (!buf || (buflen < 0)) buflen = 0;
	left = buflen;

	result = malloc(4*(buflen/3 + 1) + 1);
	inp = buf; outp=result;

	while (left >= 3) {
		c0 = *inp; c1 = *(inp+1); c2 = *(inp+2);

		n0 = (c0 >> 2);				/* 6 bits from c0 */
		n1 = ((c0 & 3) << 4) + (c1 >> 4);	/* 2 bits from c0, 4 bits from c1 */
		n2 = ((c1 & 15) << 2) + (c2 >> 6);	/* 4 bits from c1, 2 bits from c2 */
		n3 = (c2 & 63);				/* 6 bits from c2 */

		*outp = b64chars[n0]; outp++;
		*outp = b64chars[n1]; outp++;
		*outp = b64chars[n2]; outp++;
		*outp = b64chars[n3]; outp++;

		inp += 3; left -= 3;
	}

	if (left == 1) {
		c0 = *inp; c1 = 0;
		n0 = (c0 >> 2);				/* 6 bits from c0 */
		n1 = ((c0 & 3) << 4) + (c1 >> 4);	/* 2 bits from c0, 4 bits from c1 */

		*outp = b64chars[n0]; outp++;
		*outp = b64chars[n1]; outp++;
		*outp = '='; outp++;
		*outp = '='; outp++;
	}
	else if (left == 2) {
		c0 = *inp; c1 = *(inp+1); c2 = 0;

		n0 = (c0 >> 2);				/* 6 bits from c0 */
		n1 = ((c0 & 3) << 4) + (c1 >> 4);	/* 2 bits from c0, 4 bits from c1 */
		n2 = ((c1 & 15) << 2) + (c2 >> 6);	/* 4 bits from c1, 2 bits from c2 */

		*outp = b64chars[n0]; outp++;
		*outp = b64chars[n1]; outp++;
		*outp = b64chars[n2]; outp++;
		*outp = '='; outp++;
	}

	*outp = '\0';

	return result;
}

char *base64encode(unsigned char *buf)
{
	return base64encode_len(buf, (buf ? strlen(buf) : 0));
}

/*
 * Decode a counted buffer, and say how many bytes came out.
 *
 * Two things the strlen()-measured decoder below cannot do. What base64
 * carries is usually binary -- a SASL challenge, a salt, a nonce -- and that
 * has a NUL in it more often than not, so the length has to be returned
 * rather than implied. And '=' has to be honoured: the padding says the last
 * group is worth one or two bytes instead of three, and a decoder that
 * decodes every quad blindly hands back one or two bytes of rubbish on the
 * end -- invisible while the result is read as a string, wrong the moment it
 * is hashed.
 *
 * Characters that are not base64 are skipped, so a challenge wrapped across
 * lines decodes the same as one that is not.
 */
char *base64decode_len(unsigned char *buf, int buflen, int *outlen)
{
	static short b64val[256];
	static int b64valinit = 0;
	unsigned char *result, *outp;
	int i, n = 0, quad[4], nq = 0, pad = 0;

	if (!buf || (buflen < 0)) buflen = 0;

	if (!b64valinit) {
		int c;

		b64valinit = 1;
		for (c = 0; (c < 256); c++) b64val[c] = -1;
		for (c = 0; (c < 64); c++) b64val[(int)(unsigned char)b64chars[c]] = c;
	}

	result = malloc(3*(buflen/4 + 1) + 1);
	outp = result;

	for (i = 0; (i < buflen); i++) {
		int c = buf[i];

		if (c == '=') { pad++; quad[nq++] = 0; }
		else if (b64val[c] >= 0) quad[nq++] = b64val[c];
		else continue;			/* newlines and other padding-out */

		if (nq < 4) continue;

		*outp++ = (quad[0] << 2) + (quad[1] >> 4);
		*outp++ = ((quad[1] & 0x0F) << 4) + (quad[2] >> 2);
		*outp++ = ((quad[2] & 0x03) << 6) + quad[3];
		n += 3;
		nq = 0;
	}

	/* One '=' means the last quad carried two bytes, two means one. */
	n -= ((pad > 2) ? 2 : pad);
	if (n < 0) n = 0;
	result[n] = '\0';
	if (outlen) *outlen = n;

	return (char *)result;
}

char *base64decode(unsigned char *buf)
{
	return base64decode_len(buf, (buf ? strlen(buf) : 0), NULL);
}

void getescapestring(char *msg, unsigned char **buf, int *buflen)
{
	char *inp, *outp;
	int outlen = 0;

	inp = msg;
	if (*inp == '\"') inp++; /* Skip the quote */

	outp = *buf = malloc(strlen(msg)+1);
	while (*inp && (*inp != '\"')) {
		if (*inp == '\\') {
			inp++;
			if (*inp == 'r') {
				*outp = '\r'; outlen++; inp++; outp++;
			}
			else if (*inp == 'n') {
				*outp = '\n'; outlen++; inp++; outp++;
			}
			else if (*inp == 't') {
				*outp = '\t'; outlen++; inp++; outp++;
			}
			else if (*inp == '\\') {
				*outp = '\\'; outlen++; inp++; outp++;
			}
			else if (*inp == 'x') {
				inp++;
				if (isxdigit((int) *inp)) {
					*outp = hexvalue(*inp);
					inp++;

					if (isxdigit((int) *inp)) {
						*outp *= 16;
						*outp += hexvalue(*inp);
						inp++;
					}
				}
				else {
					errprintf("Invalid hex escape in '%s'\n", msg);
				}
				outlen++; outp++;
			}
			else {
				errprintf("Unknown escape sequence \\%c in '%s'\n", *inp, msg);
			}
		}
		else {
			*outp = *inp;
			outlen++;
			inp++; outp++;
		}
	}
	*outp = '\0';
	if (buflen) *buflen = outlen;
}


unsigned char *nlencode(unsigned char *msg)
{
	static unsigned char *buf = NULL;
	static int bufsz = 0;
	int maxneeded;
	unsigned char *inp, *outp;
	int n;

	if (msg == NULL) msg = "";

	maxneeded = 2*strlen(msg)+1;

	if (buf == NULL) {
		bufsz = maxneeded;
		buf = (char *)malloc(bufsz);
	}
	else if (bufsz < maxneeded) {
		bufsz = maxneeded;
		buf = (char *)realloc(buf, bufsz);
	}

	inp = msg;
	outp = buf;

	while (*inp) {
		n = strcspn(inp, "|\n\r\t\\");
		if (n > 0) {
			memcpy(outp, inp, n);
			outp += n;
			inp += n;
		}

		if (*inp) {
			*outp = '\\'; outp++;
			switch (*inp) {
			  case '|' : *outp = 'p'; outp++; break;
			  case '\n': *outp = 'n'; outp++; break;
			  case '\r': *outp = 'r'; outp++; break;
			  case '\t': *outp = 't'; outp++; break;
			  case '\\': *outp = '\\'; outp++; break;
			}
			inp++;
		}
	}
	*outp = '\0';

	return buf;
}

void nldecode(unsigned char *msg)
{
	unsigned char *inp = msg;
	unsigned char *outp = msg;
	int n;

	if ((msg == NULL) || (*msg == '\0')) return;

	while (*inp) {
		n = strcspn(inp, "\\");
		if (n > 0) {
			if (inp != outp) memmove(outp, inp, n);
			inp += n;
			outp += n;
		}

		/* *inp is either a backslash or a \0 */
		if (*inp == '\\') {
			inp++;
			switch (*inp) {
			  case 'p': *outp = '|';  outp++; inp++; break;
			  case 'r': *outp = '\r'; outp++; inp++; break;
			  case 'n': *outp = '\n'; outp++; inp++; break;
			  case 't': *outp = '\t'; outp++; inp++; break;
			  case '\\': *outp = '\\'; outp++; inp++; break;
			}
		}
	}
	*outp = '\0';
}

