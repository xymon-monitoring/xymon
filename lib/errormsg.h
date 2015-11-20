/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __ERRORMSG_H__
#define __ERRORMSG_H__

#include <stdio.h>

extern char *errbuf;
extern int save_errbuf;
extern int debug;

extern void errprintf(const char *fmt, ...);
extern void real_dbgprintf(const char *fmt, ...);
#define dbgprintf(...) { if (debug) real_dbgprintf(__VA_ARGS__); }
extern void logprintf(const char *fmt, ...);
extern void flush_errbuf(void);
extern void set_debugfile(char *fn, int appendtofile);

extern void starttrace(const char *fn);
extern void stoptrace(void);
extern void traceprintf(const char *fmt, ...);

extern void redirect_cgilog(char *cginame);
extern void reopen_file(char *fn, char *mode, FILE *fd);

#endif

