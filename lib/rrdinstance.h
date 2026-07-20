/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Reversible, collision-free encoding of an RRD instance name (the variable  */
/* part of a "<base>.<instance>.rrd" filename). Only '/' and NUL are illegal  */
/* in a Unix filename, but the legacy '/'->',' scheme is lossy and ambiguous  */
/* ("/a/b" and "/a,b" both collapse to the same file). This encodes any byte  */
/* outside a small filename-safe set as %XX, so decode(encode(s)) == s for    */
/* every s and distinct instances never share a file.                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __RRDINSTANCE_H__
#define __RRDINSTANCE_H__

/* All return a newly malloc()'d string the caller must free. */
extern char *rrdinstance_encode(const char *s);
/* Invalid escapes pass through verbatim; so does "%00" (the encoder never
 * emits it, and a decoded NUL would truncate the result mid-string). */
extern char *rrdinstance_decode(const char *s);
/* NULL when s is not canonical encoder output (see the .c rationale) */
extern char *rrdinstance_decode_ifencoded(const char *s);

#endif
