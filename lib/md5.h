/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* API for the MD5 digest routines.                                           */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __MD5_H__
#define __MD5_H__

extern int  myMD5_Size(void);
extern void myMD5_Init(void *pms);
extern void myMD5_Update(void *pms, unsigned char *data, int nbytes);
extern void myMD5_Final(unsigned char digest[16], void *pms);

#endif

