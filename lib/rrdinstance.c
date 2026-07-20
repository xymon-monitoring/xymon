/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Reversible, collision-free encoding of an RRD instance name. See the       */
/* header for the rationale.                                                  */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdlib.h>
#include <string.h>

#include "rrdinstance.h"

/* Bytes kept verbatim; every other byte (including '%' itself) becomes %XX,
 * which is what makes the mapping reversible. '/' is excluded so it always
 * encodes - that is the collision the legacy ','-substitution created. */
static int rrdinstance_safe(unsigned char c)
{
	return ((c >= 'A') && (c <= 'Z')) ||
	       ((c >= 'a') && (c <= 'z')) ||
	       ((c >= '0') && (c <= '9')) ||
	       (c == '.') || (c == ',') || (c == '-') || (c == '_');
}

char *rrdinstance_encode(const char *s)
{
	static const char hex[] = "0123456789ABCDEF";
	const unsigned char *in = (const unsigned char *)s;
	char *out, *o;

	if (s == NULL) return NULL;

	/* Worst case every byte expands to three ("%XX"). */
	o = out = (char *)malloc(strlen(s) * 3 + 1);
	if (out == NULL) return NULL;

	for (; *in; in++) {
		if (rrdinstance_safe(*in)) {
			*o++ = (char)*in;
		}
		else {
			*o++ = '%';
			*o++ = hex[*in >> 4];
			*o++ = hex[*in & 0x0f];
		}
	}
	*o = '\0';
	return out;
}

static int rrdinstance_unhex(char c)
{
	if ((c >= '0') && (c <= '9')) return c - '0';
	if ((c >= 'A') && (c <= 'F')) return c - 'A' + 10;
	if ((c >= 'a') && (c <= 'f')) return c - 'a' + 10;
	return -1;
}

char *rrdinstance_decode(const char *s)
{
	const char *in = s;
	char *out, *o;

	if (s == NULL) return NULL;

	/* Decoding only ever shrinks, so the input length is a safe bound. */
	o = out = (char *)malloc(strlen(s) + 1);
	if (out == NULL) return NULL;

	while (*in) {
		int hi, lo;

		if ((*in == '%') && ((hi = rrdinstance_unhex(in[1])) >= 0) &&
		                    ((lo = rrdinstance_unhex(in[2])) >= 0) &&
		                    (((hi << 4) | lo) != 0)) {
			/* %00 stays literal: an embedded NUL would silently
			 * truncate the result for every strlen-based consumer.
			 * The encoder never emits it, so it cannot be canonical
			 * output - treat it like any other non-escape. */
			*o++ = (char)((hi << 4) | lo);
			in += 3;
		}
		else {
			/* Not a valid %XX escape: copy the byte verbatim so names
			 * that were never encoded (or hand-written) pass through. */
			*o++ = *in++;
		}
	}
	*o = '\0';
	return out;
}

/* Decode s only if it is canonical encoder output (decode(s) re-encodes to
 * exactly s, and s holds at least one escape). Legacy filenames can carry
 * literal %XX byte runs that were never our escapes - URLs in tcp.http
 * captures are the common case - but those names also carry raw bytes
 * outside the safe set (':' and friends), so the round-trip rejects them
 * and the caller keeps the name verbatim.
 * Returns a newly malloc()'d decoded string, or NULL for "not encoded". */
char *rrdinstance_decode_ifencoded(const char *s)
{
	char *dec, *enc;
	int canonical;

	dec = rrdinstance_decode(s);
	if (dec == NULL) return NULL;
	if (strcmp(dec, s) == 0) { free(dec); return NULL; }	/* no escapes */
	enc = rrdinstance_encode(dec);
	canonical = (enc != NULL) && (strcmp(enc, s) == 0);
	if (enc) free(enc);
	if (!canonical) { free(dec); return NULL; }
	return dec;
}
