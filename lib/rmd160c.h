/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* API for the RMD160 digest routines.                                        */
/*                                                                            */
/* Copyright (C) 2006-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __RMD160C_H__
#define __RMD160C_H__

extern int  myRIPEMD160_Size(void);
extern void myRIPEMD160_Init(void *c);
extern void myRIPEMD160_Update(void *c, const void *data, size_t len);
extern void myRIPEMD160_Final(unsigned char *md, void *c);

#endif

