/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for timing program execution.                         */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <unistd.h>	// For POSIX timer definitions
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

#include "libxymon.h"

int timing = 0;

typedef struct timestamp_t {
	char		*eventtext;
	struct timespec	eventtime;
	struct timestamp_t *prev;
	struct timestamp_t *next;
} timestamp_t;

static timestamp_t *stamphead = NULL;
static timestamp_t *stamptail = NULL;


time_t gettimer(void)
{
	int res;
	struct timespec t;

#if (_POSIX_TIMERS > 0) && defined(_POSIX_MONOTONIC_CLOCK) && defined(CLOCK_MONOTONIC)
	res = clock_gettime(CLOCK_MONOTONIC, &t);
	if(-1 == res)
	{
		res = clock_gettime(CLOCK_REALTIME, &t);
		if(-1 == res)
		{
			return time(NULL);
		}
	}
	return (time_t) t.tv_sec;
#else
	return time(NULL);
#endif
}

void getntimer(struct timespec *tp)
{
	struct timeval t;
	struct timezone tz;
	int res;

#if (_POSIX_TIMERS > 0) && defined(_POSIX_MONOTONIC_CLOCK) && defined(CLOCK_MONOTONIC)
	res = clock_gettime(CLOCK_MONOTONIC, tp);
	if(-1 == res)
	{
		res = clock_gettime(CLOCK_REALTIME, tp);
		if(-1 != res) return;
		/* Fall through to use gettimeofday() */
	}
#endif

	gettimeofday(&t, &tz);
	tp->tv_sec = t.tv_sec;
	tp->tv_nsec = 1000*t.tv_usec;
}


void add_timestamp(const char *msg)
{
	if (timing) {
		timestamp_t *newstamp = (timestamp_t *) malloc(sizeof(timestamp_t));

		getntimer(&newstamp->eventtime);
		newstamp->eventtext = strdup(msg);

		if (stamphead == NULL) {
			newstamp->next = newstamp->prev = NULL;
			stamphead = newstamp;
		}
		else {
			newstamp->prev = stamptail;
			newstamp->next = NULL;
			stamptail->next = newstamp;
		}
		stamptail = newstamp;
	}
}

int ntimerus(struct timespec *start, struct timespec *now)
{
	struct timespec tdiff;

	/* See how long the query took */
	if (now) {
		memcpy(&tdiff, now, sizeof(struct timespec));
	}
	else {
		getntimer(&tdiff);
	}

	if (tdiff.tv_nsec < start->tv_nsec) {
		tdiff.tv_sec--;
		tdiff.tv_nsec += 1000000000;
	}
	tdiff.tv_sec  -= start->tv_sec;
	tdiff.tv_nsec -= start->tv_nsec;
	return (tdiff.tv_sec*1000000 + tdiff.tv_nsec/1000);
}

void show_timestamps(char **buffer)
{
	timestamp_t *s;
	struct timespec dif;
	SBUF_DEFINE(outbuf);
	char buf1[80];

	if (!timing || (stamphead == NULL)) return;

	SBUF_MALLOC(outbuf, 4096);

	strncpy(outbuf, "\n\nTIME SPENT\n", outbuf_buflen);
	strncat(outbuf, "Event                                   ", (outbuf_buflen - strlen(outbuf)));
	strncat(outbuf, "        Start time", (outbuf_buflen - strlen(outbuf)));
	strncat(outbuf, "          Duration\n", (outbuf_buflen - strlen(outbuf)));

	for (s=stamphead; (s); s=s->next) {
		snprintf(buf1, sizeof(buf1), "%-40s ", s->eventtext);
		strncat(outbuf, buf1, (outbuf_buflen - strlen(outbuf)));
		snprintf(buf1, sizeof(buf1), "%10u.%06u ", (unsigned int)s->eventtime.tv_sec, (unsigned int)s->eventtime.tv_nsec / 1000);
		strncat(outbuf, buf1, (outbuf_buflen - strlen(outbuf)));
		if (s->prev) {
			tvdiff(&((timestamp_t *)s->prev)->eventtime, &s->eventtime, &dif);
			snprintf(buf1, sizeof(buf1), "%10u.%06u ", (unsigned int)dif.tv_sec, (unsigned int)dif.tv_nsec / 1000);
			strncat(outbuf, buf1, (outbuf_buflen - strlen(outbuf)));
		}
		else strncat(outbuf, "                -", (outbuf_buflen - strlen(outbuf)));
		strncat(outbuf, "\n", (outbuf_buflen - strlen(outbuf)));

		if ((outbuf_buflen - strlen(outbuf)) < 200) {
			SBUF_REALLOC(outbuf, outbuf_buflen + 4096);
		}
	}

	tvdiff(&stamphead->eventtime, &stamptail->eventtime, &dif);
	snprintf(buf1, sizeof(buf1), "%-40s ", "TIME TOTAL"); strncat(outbuf, buf1, (outbuf_buflen - strlen(outbuf)));
	snprintf(buf1, sizeof(buf1), "%-18s", ""); strncat(outbuf, buf1, (outbuf_buflen - strlen(outbuf)));
	snprintf(buf1, sizeof(buf1), "%10u.%06u ", (unsigned int)dif.tv_sec, (unsigned int)dif.tv_nsec / 1000); strncat(outbuf, buf1, (outbuf_buflen - strlen(outbuf)));
	strncat(outbuf, "\n", (outbuf_buflen - strlen(outbuf)));

	if (buffer == NULL) {
		printf("%s", outbuf);
		xfree(outbuf);
	}
	else *buffer = outbuf;
}


long total_runtime(void)
{
	if (timing)
		return (stamptail->eventtime.tv_sec - stamphead->eventtime.tv_sec);
	else
		return 0;
}


