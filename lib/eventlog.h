/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __EVENTLOG_H_
#define __EVENTLOG_H_

/* Format of records in the $BBHIST/allevents file */
typedef struct event_t {
	void    *host;
	struct htnames_t *service;
	time_t	eventtime;
	time_t	changetime;
	time_t	duration;
	int	newcolor;	/* stored as "re", "ye", "gr" etc. */
	int	oldcolor;
	int	state;		/* 2=escalated, 1=recovered, 0=no change */
	struct event_t *next;
} event_t;

typedef struct eventcount_t {
	struct htnames_t *service;
	unsigned long count;
	struct eventcount_t *next;
} eventcount_t;
typedef struct countlist_t {
	void *src; /* May be a pointer to a host or a service */
	unsigned long total;
	struct countlist_t *next;
} countlist_t;

typedef enum { S_NONE, S_HOST_BREAKDOWN, S_SERVICE_BREAKDOWN } eventsummary_t;
typedef enum { COUNT_NONE, COUNT_EVENTS, COUNT_DURATION } countsummary_t;

typedef int (*f_hostcheck)(char *hostname);

extern char *eventignorecolumns;
extern int havedoneeventlog;

extern void do_eventlog(FILE *output, int maxcount, int maxminutes, char *fromtime, char *totime, 
			char *pagematch, char *expagematch, 
			char *hostmatch, char *exhostmatch, 
			char *testmatch, char *extestmatch,
			char *colormatch, int ignoredialups,
			f_hostcheck hostcheck,
			event_t **eventlist, countlist_t **hostcounts, countlist_t **servicecounts,
			countsummary_t counttype, eventsummary_t sumtype, char *periodstring);

#endif
