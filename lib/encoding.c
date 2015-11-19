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

char *base64encode(unsigned char *buf)
{
	unsigned char c0, c1, c2;
	unsigned int n0, n1, n2, n3;
	unsigned char *inp, *outp;
	unsigned char *result;

	result = malloc(4*(strlen(buf)/3 + 1) + 1);
	inp = buf; outp=result;

	while (strlen(inp) >= 3) {
		c0 = *inp; c1 = *(inp+1); c2 = *(inp+2);

		n0 = (c0 >> 2);				/* 6 bits from c0 */
		n1 = ((c0 & 3) << 4) + (c1 >> 4);	/* 2 bits from c0, 4 bits from c1 */
		n2 = ((c1 & 15) << 2) + (c2 >> 6);	/* 4 bits from c1, 2 bits from c2 */
		n3 = (c2 & 63);				/* 6 bits from c2 */

		*outp = b64chars[n0]; outp++;
		*outp = b64chars[n1]; outp++;
		*outp = b64chars[n2]; outp++;
		*outp = b64chars[n3]; outp++;

		inp += 3;
	}

	if (strlen(inp) == 1) {
		c0 = *inp; c1 = 0;
		n0 = (c0 >> 2);				/* 6 bits from c0 */
		n1 = ((c0 & 3) << 4) + (c1 >> 4);	/* 2 bits from c0, 4 bits from c1 */

		*outp = b64chars[n0]; outp++;
		*outp = b64chars[n1]; outp++;
		*outp = '='; outp++;
		*outp = '='; outp++;
	}
	else if (strlen(inp) == 2) {
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

char *base64decode(unsigned char *buf)
{
	static short bval[128] = { 0, };
	static short bvalinit = 0;
	int n0, n1, n2, n3;

	unsigned char *inp, *outp;
	unsigned char *result;
	int bytesleft = strlen(buf);

	if (!bvalinit) {
		int i;

		bvalinit = 1;
		for (i=0; (i < strlen(b64chars)); i++) bval[(int)b64chars[i]] = i;
	}

	result = malloc(3*(bytesleft/4 + 1) + 1);
	inp = buf; outp=result;

	while (bytesleft >= 4) {
		n0 = bval[*(inp+0)];
		n1 = bval[*(inp+1)];
		n2 = bval[*(inp+2)];
		n3 = bval[*(inp+3)];

		*(outp+0) = (n0 << 2) + (n1 >> 4);		/* 6 bits from n0, 2 from n1 */
		*(outp+1) = ((n1 & 0x0F) << 4) + (n2 >> 2);	/* 4 bits from n1, 4 from n2 */
		*(outp+2) = ((n2 & 0x03) << 6) + (n3);		/* 2 bits from n2, 6 from n3 */

		inp += 4;
		bytesleft -= 4;
		outp += 3;
	}
	*outp = '\0';

	return result;
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
	static unsigned char empty = '\0';
	static int bufsz = 0;
	int maxneeded;
	unsigned char *inp, *outp;
	int n;

	if (msg == NULL) return &empty;

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

