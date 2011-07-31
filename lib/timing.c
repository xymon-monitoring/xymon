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

void show_timestamps(char **buffer)
{
	timestamp_t *s;
	struct timespec dif;
	char *outbuf = (char *) malloc(4096);
	int outbuflen = 4096;
	char buf1[80];

	if (!timing || (stamphead == NULL)) return;

	MEMDEFINE(buf1);

	strcpy(outbuf, "\n\nTIME SPENT\n");
	strcat(outbuf, "Event                                   ");
	strcat(outbuf, "        Start time");
	strcat(outbuf, "          Duration\n");

	for (s=stamphead; (s); s=s->next) {
		sprintf(buf1, "%-40s ", s->eventtext);
		strcat(outbuf, buf1);
		sprintf(buf1, "%10u.%06u ", (unsigned int)s->eventtime.tv_sec, (unsigned int)s->eventtime.tv_nsec / 1000);
		strcat(outbuf, buf1);
		if (s->prev) {
			tvdiff(&((timestamp_t *)s->prev)->eventtime, &s->eventtime, &dif);
			sprintf(buf1, "%10u.%06u ", (unsigned int)dif.tv_sec, (unsigned int)dif.tv_nsec / 1000);
			strcat(outbuf, buf1);
		}
		else strcat(outbuf, "                -");
		strcat(outbuf, "\n");

		if ((outbuflen - strlen(outbuf)) < 200) {
			outbuflen += 4096;
			outbuf = (char *) realloc(outbuf, outbuflen);
		}
	}

	tvdiff(&stamphead->eventtime, &stamptail->eventtime, &dif);
	sprintf(buf1, "%-40s ", "TIME TOTAL"); strcat(outbuf, buf1);
	sprintf(buf1, "%-18s", ""); strcat(outbuf, buf1);
	sprintf(buf1, "%10u.%06u ", (unsigned int)dif.tv_sec, (unsigned int)dif.tv_nsec / 1000); strcat(outbuf, buf1);
	strcat(outbuf, "\n");

	if (buffer == NULL) {
		printf("%s", outbuf);
		xfree(outbuf);
	}
	else *buffer = outbuf;

	MEMUNDEFINE(buf1);
}


long total_runtime(void)
{
	if (timing)
		return (stamptail->eventtime.tv_sec - stamphead->eventtime.tv_sec);
	else
		return 0;
}


