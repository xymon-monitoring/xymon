/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __ERRORMSG_H__
#define __ERRORMSG_H__

extern char *errbuf;
extern int save_errbuf;
extern int debug;

extern void errprintf(const char *fmt, ...);
extern void dprintf(const char *fmt, ...);
extern void flush_errbuf(void);

#endif

