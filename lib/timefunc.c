/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for timehandling.                                     */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: timefunc.c,v 1.4 2004-11-17 16:23:52 henrik Exp $";

#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#include "errormsg.h"
#include "timefunc.h"

char timestamp[30];
void init_timestamp(void)
{
	time_t	now;

        now = time(NULL);
        strcpy(timestamp, ctime(&now));
        timestamp[strlen(timestamp)-1] = '\0';

}


char *weekday_text(char *dayspec)
{
	static char result[80];
	static char *dayname[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	char *p;

	if (strcmp(dayspec, "*") == 0) {
		strcpy(result, "All days");
		return result;
	}

	result[0] = '\0';
	for (p=dayspec; (*p); p++) {
		switch (*p) {
			case '0': case '1': case '2':
			case '3': case '4': case '5':
			case '6':
				strcat(result, dayname[(*p)-'0']);
				break;
			case '-':
				strcat(result, "-");
				break;
			case ' ':
			case ',':
				strcat(result, ",");
				break;
		}
	}
	return result;
}


char *time_text(char *timespec)
{
	static char result[80];

	if (strcmp(timespec, "*") == 0) {
		strcpy(result, "0000-2359");
	}
	else {
		strcpy(result, timespec);
	}

	return result;
}


struct timeval *tvdiff(struct timeval *tstart, struct timeval *tend, struct timeval *result)
{
	static struct timeval resbuf;

	if (result == NULL) result = &resbuf;

	result->tv_sec = tend->tv_sec;
	result->tv_usec = tend->tv_usec;
	if (result->tv_usec < tstart->tv_usec) {
		result->tv_sec--;
		result->tv_usec += 1000000;
	}
	result->tv_sec  -= tstart->tv_sec;
	result->tv_usec -= tstart->tv_usec;

	return result;
}


static int minutes(char *p)
{
	/* Converts string HHMM to number indicating minutes since midnight (0-1440) */
	return (10*(*(p+0)-'0')+(*(p+1)-'0'))*60 + (10*(*(p+2)-'0')+(*(p+3)-'0'));
}

int within_sla(char *l, char *tag, int defresult)
{
	/*
	 * Usage: slatime hostline
	 *    SLASPEC is of the form SLA=W:HHMM:HHMM[,WXHHMM:HHMM]*
	 *    "W" = weekday : '*' = all, 'W' = Monday-Friday, '0'..'6' = Sunday ..Saturday
	 */

	char *p;
	char *slaspec = NULL;
	char *endofslaspec = NULL;
	char *tagspec;

	time_t tnow;
	struct tm *now;

	int result = 0;
	int found = 0;
	int starttime,endtime,curtime;

	if (strlen(tag)) {
		tagspec = (char *) malloc(strlen(tag)+2);
		sprintf(tagspec, "%s=", tag);
		p = strstr(l, tagspec);
	}
	else {
		tagspec = strdup("");
		p = l;
	}

	if (p) {
		slaspec = p + strlen(tagspec);
		endofslaspec = slaspec + strcspn(slaspec, " \t\r\n");
		tnow = time(NULL);
		now = localtime(&tnow);

		/*
		 * Now find the appropriate SLA definition.
		 * We take advantage of the fixed (11 bytes + delimiter) length of each entry.
		 */
		while ( (!found) && slaspec && (slaspec < endofslaspec) )
		{
			dprintf("Now checking slaspec='%s'\n", slaspec);

			if ( (*slaspec == '*') || 						/* Any day */
                             (*slaspec == now->tm_wday+'0') ||					/* This day */
                             ((toupper((int)*slaspec) == 'W') && (now->tm_wday >= 1) && (now->tm_wday <=5)) )	/* Monday thru Friday */
			{
				/* Weekday matches */
				starttime = minutes(slaspec+2);
				endtime = minutes(slaspec+7);
				curtime = now->tm_hour*60+now->tm_min;
				found = ((curtime >= starttime) && (curtime <= endtime));
				dprintf("\tstart,end,current time = %d, %d, %d - found=%d\n", 
					starttime,endtime,curtime,found);
			}
			else {
				dprintf("\tWeekday does not match\n");
			}

			if (!found) {
				slaspec = strchr(slaspec, ',');
				if (slaspec) slaspec++;
			};
		}
		result = found;
	}
	else {
		/* No SLA -> default to always included */
		result = defresult;
	}
	free(tagspec);

	return result;
}


int periodcoversnow(char *tag)
{
	/*
	 * Tag format: "-DAY-HHMM-HHMM:"
	 * DAY = 0-6 (Sun .. Mon), or W (1..5)
	 */

	time_t tnow;
	struct tm *now;

        int result = 1;
        char *dayspec, *starttime, *endtime;
	unsigned int istart, iend, inow;
	char *p;

        if ((tag == NULL) || (*tag != '-')) return 1;

	dayspec = (char *) malloc(strlen(tag)+1+12); /* Leave room for expanding 'W' and '*' */
	starttime = (char *) malloc(strlen(tag)+1); 
	endtime = (char *) malloc(strlen(tag)+1); 

	strcpy(dayspec, (tag+1));
	for (p=dayspec; ((*p == 'W') || (*p == '*') || ((*p >= '0') && (*p <= '6'))); p++) ;
	if (*p != '-') {
		free(endtime); free(starttime); free(dayspec); return 1;
	}
	*p = '\0';

	p++;
	strcpy(starttime, p); p = starttime;
	if ( (strlen(starttime) < 4) || 
	     !isdigit((int) *p)            || 
	     !isdigit((int) *(p+1))        ||
	     !isdigit((int) *(p+2))        ||
	     !isdigit((int) *(p+3))        ||
	     !(*(p+4) == '-') )          goto out;
	else *(starttime+4) = '\0';

	p+=5;
	strcpy(endtime, p); p = endtime;
	if ( (strlen(endtime) < 4) || 
	     !isdigit((int) *p)          || 
	     !isdigit((int) *(p+1))      ||
	     !isdigit((int) *(p+2))      ||
	     !isdigit((int) *(p+3))      ||
	     !(*(p+4) == ':') )          goto out;
	else *(endtime+4) = '\0';

	tnow = time(NULL);
	now = localtime(&tnow);


	/* We have a timespec. So default to "not included" */
	result = 0;

	/* Check day-spec */
	if (strchr(dayspec, 'W')) strcat(dayspec, "12345");
	if (strchr(dayspec, '*')) strcat(dayspec, "0123456");
	if (strchr(dayspec, ('0' + now->tm_wday)) == NULL) goto out;

	/* Calculate minutes since midnight for start, end and now */
	istart = (600 * (starttime[0]-'0'))   +
		 (60  * (starttime[1]-'0'))   +
		 (10  * (starttime[2]-'0'))   +
		 (1   * (starttime[3]-'0'));
	iend   = (600 * (endtime[0]-'0'))     +
		 (60  * (endtime[1]-'0'))     +
		 (10  * (endtime[2]-'0'))     +
		 (1   * (endtime[3]-'0'));
	inow   = 60*now->tm_hour + now->tm_min;

	if ((inow < istart) || (inow > iend)) goto out;

	result = 1;
out:
	free(endtime); free(starttime); free(dayspec); 
	return result;
}

char *histlogtime(time_t histtime)
{
	static char result[30];
	char d1[40],d2[3],d3[40];

	/*
	 * Historical logs use a filename like "Fri_Nov_7_16:01:08_2002 
	 * But apparently there is no simple way to generate a day-of-month 
	 * with no leading 0.
	 */

        strftime(d1, sizeof(d1), "%a_%b_", localtime(&histtime));
        strftime(d2, sizeof(d2), "%d", localtime(&histtime));
	if (d2[0] == '0') { d2[0] = d2[1]; d2[1] = '\0'; }
        strftime(d3, sizeof(d3), "_%H:%M:%S_%Y", localtime(&histtime));

	snprintf(result, sizeof(result)-1, "%s%s%s", d1, d2, d3);

	return result;
}

