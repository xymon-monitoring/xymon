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


extern int standardoption(char *id, char *opt);

#endif

