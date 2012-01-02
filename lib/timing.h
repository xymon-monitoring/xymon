/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __TIMING_H__
#define __TIMING_H__

extern int timing;

extern void add_timestamp(const char *msg);
extern void show_timestamps(char **buffer);
extern long total_runtime(void);

extern time_t gettimer(void);
extern void getntimer(struct timespec *tp);
extern int ntimerms(struct timespec *start, struct timespec *now);

#endif

