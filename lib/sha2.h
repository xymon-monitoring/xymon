/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* API for the SHA1 digest routines.                                          */
/*                                                                            */
/* Copyright (C) 2006-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SHA2_H__
#define __SHA2_H__

extern int  mySHA512_Size(void);
extern void mySHA512_Init(void *context);
extern void mySHA512_Update(void *context, const unsigned char *data, int len);
extern void mySHA512_Final(unsigned char digest[20], void *context);

extern int  mySHA256_Size(void);
extern void mySHA256_Init(void *context);
extern void mySHA256_Update(void *context, const unsigned char *data, int len);
extern void mySHA256_Final(unsigned char digest[20], void *context);

extern int  mySHA384_Size(void);
extern void mySHA384_Init(void *context);
extern void mySHA384_Update(void *context, const unsigned char *data, int len);
extern void mySHA384_Final(unsigned char digest[20], void *context);

extern int  mySHA224_Size(void);
extern void mySHA224_Init(void *context);
extern void mySHA224_Update(void *context, const unsigned char *data, int len);
extern void mySHA224_Final(unsigned char digest[20], void *context);

#endif

