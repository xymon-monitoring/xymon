/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* API for the SHA1 digest routines.                                          */
/*                                                                            */
/* Copyright (C) 2006-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SHA1_H__
#define __SHA1_H__

extern int  mySHA1_Size(void);
extern void mySHA1_Init(void *context);
extern void mySHA1_Update(void *context, const unsigned char *data, int len);
extern void mySHA1_Final(unsigned char digest[20], void *context);

#endif

