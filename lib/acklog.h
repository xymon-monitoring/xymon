/*----------------------------------------------------------------------------*/
/* Hobbit webpage generator tool.                                             */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __ACKLOG_H_
#define __ACKLOG_H_

extern int havedoneacklog;
extern void do_acklog(FILE *output, int maxcount, int maxminutes);

#endif
