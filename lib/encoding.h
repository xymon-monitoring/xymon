/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __ENCODING_H__
#define __ENCODING_H__

extern char *base64encode(unsigned char *buf);
extern char *base64decode(unsigned char *buf);
extern size_t qpdecode(const char *in, size_t inlen, char *out);
extern char *rfc2047_decode(char *hdr);
extern void getescapestring(char *msg, unsigned char **buf, int *buflen);
extern unsigned char *nlencode(unsigned char *msg);
extern void nldecode(char *msg);

#endif

