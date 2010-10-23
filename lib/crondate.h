/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2010 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __CRONDATE_H__
#define __CRONDATE_H__

extern void * parse_cron_time(char * ch);
extern void crondatefree(void *vcdate);
extern void crongettime(void);
extern int cronmatch(void *vcdate);
#endif

