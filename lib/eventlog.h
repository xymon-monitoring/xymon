/*----------------------------------------------------------------------------*/
/* Hobbit eventlog generator tool.                                            */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __EVENTLOG_H_
#define __EVENTLOG_H_

extern char *eventignorecolumns;
extern int havedoneeventlog;

extern void do_eventlog(FILE *output, int maxcount, int maxminutes);

#endif
