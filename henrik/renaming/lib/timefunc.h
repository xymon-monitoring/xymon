/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2010 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __TIMEFUNC_H__
#define __TIMEFUNC_H__

extern time_t fakestarttime;
extern char *timestamp;

extern time_t getcurrenttime(time_t *retparm);
#define time(X) getcurrenttime(X)
extern time_t gettimer(void);
extern void getntimer(struct timespec *tp);

extern void init_timestamp(void);
extern char *timespec_text(char *spec);
extern struct timespec *tvdiff(struct timespec *tstart, struct timespec *tend, struct timespec *result);
extern int within_sla(char *holidaykey, char *timespec, int defresult);
extern char *check_downtime(char *hostname, char *testname);
extern int periodcoversnow(char *tag);
extern char *histlogtime(time_t histtime);
extern int durationvalue(char *dur);
extern char *durationstring(time_t secs);
extern char *agestring(time_t secs);
extern time_t timestr2timet(char *s);
extern time_t eventreport_time(char *timestamp);

#endif

