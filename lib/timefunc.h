/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __TIMEFUNC_H__
#define __TIMEFUNC_H__

extern char timestamp[];

extern void init_timestamp(void);
extern char *weekday_text(char *dayspec);
extern char *time_text(char *timespec);
extern struct timeval *tvdiff(struct timeval *tstart, struct timeval *tend, struct timeval *result);
extern int within_sla(char *l, char *tag, int defresult);
extern int periodcoversnow(char *tag);
extern char *histlogtime(time_t histtime);
extern int durationvalue(char *dur);

#endif

