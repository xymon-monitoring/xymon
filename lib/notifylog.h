/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __NOTIFYLOG_H_
#define __NOTIFYLOG_H_

extern void do_notifylog(FILE *output, int maxcount, int maxminutes, char *fromtime, char *totime, 
			 char *pagematch, char *expagematch, 
			 char *hostmatch, char *exhostmatch, 
			 char *testmatch, char *extestmatch,
			 char *rcptmatch, char *exrcptmatch);

#endif

