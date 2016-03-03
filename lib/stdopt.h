/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __STDOPT_H__
#define __STDOPT_H__

extern char *programname;
extern char *pidfn;
extern char *hostsfn;
extern char *logfn;
extern char *envarea;
extern int  showhelp;
extern int  dontsendmessages;
extern int  sendtimeout;
extern int  idletimeout;

extern int enablecompression;
extern char *defaultcompression;

typedef enum { XYMON_IPPROTO_ANY, XYMON_IPPROTO_4, XYMON_IPPROTO_6 } ipprotocol_t;
extern ipprotocol_t ipprotocol;

extern int standardoption(char *opt);
extern void libxymon_init(char *toolname);

#endif

