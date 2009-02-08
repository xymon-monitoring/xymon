/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __ENCODING_H__
#define __ENCODING_H__

extern char *base64encode(unsigned char *buf);
extern char *base64decode(unsigned char *buf);
extern void getescapestring(char *msg, unsigned char **buf, int *buflen);
extern unsigned char *nlencode(unsigned char *msg);
extern void nldecode(unsigned char *msg);

#endif

