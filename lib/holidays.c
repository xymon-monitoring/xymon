/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for handling holidays.                                */
/*                                                                            */
/* Copyright (C) 2006-2007 Michael Nagel                                      */
/* Modifications for Hobbit (C) 2007 Henrik Storner <henrik@hswn.dk>          */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: holidays.c,v 1.1 2007-02-09 14:07:12 henrik Exp $";

#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "libbbgen.h"


static int holidays_like_weekday = -1;
static holiday_t *holidays = NULL, *holidaytail = NULL;


static time_t mkday(int year, int month, int day)
{
	struct tm t;

	memset(&t, 0, sizeof(t));
	t.tm_year  = year;
	t.tm_mon   = month - 1;
	t.tm_mday  = day;
	t.tm_isdst = -1;

	return mktime(&t);
}

/*
 * Algorithm for calculating the date of Easter Sunday
 * (Meeus/Jones/Butcher Gregorian algorithm)
 * For reference, see http://en.wikipedia.org/wiki/Computus
 */
static time_t getEasterDate(int year) 
{
	time_t day;
	int Y = year+1900;
	int a = Y % 19;
	int b = Y / 100;
	int c = Y % 100;
	int d = b / 4;
	int e = b % 4;
	int f = (b + 8) / 25;
	int g = (b - f + 1) / 3;
	int h = (19 * a + b - d - g + 15) % 30;
	int i = c / 4;
	int k = c % 4;
	int L = (32 + 2 * e + 2 * i - h - k) % 7;
	int m = (a + 11 * h + 22 * L) / 451;

	day = mkday(year, (h + L - 7 * m + 114) / 31, ((h + L - 7 * m + 114) % 31) + 1);

	return day;
}


/* Algorithm to compute the 4th Sunday in Advent (ie the last Sunday before Christmas Day) */
static time_t get4AdventDate(int year)
{
	time_t day;
	struct tm *t;

	day = mkday(year, 12, 24);
	t = localtime(&day);
	day -= t->tm_wday * 86400;

	return day;
}



static void reset_holidays(void)
{
	holiday_t *walk, *zombie;

	holidays_like_weekday = -1;
	walk = holidays;
	while (walk) {
		zombie = walk;
		walk = walk->next;
		xfree(zombie->desc);
		xfree(zombie);
	}

	holidays = holidaytail = NULL;
}


static void add_holiday(int year, holiday_t *newholiday)
{
	int isOK = 0;
	struct tm *t;
	time_t day;

	switch (newholiday->holtype) {
	  case HOL_ABSOLUTE:
		isOK =  ( (newholiday->month >= 1 && newholiday->month <=12) &&
			  (newholiday->day >=1 && newholiday->day <=31) );
		break;

	  case HOL_EASTER:
	  case HOL_ADVENT:
		isOK =  (newholiday->month == 0);
		break;
	}

	if (!isOK) {
		errprintf("Error in holiday definition %s\n", newholiday->desc);
		return;
	}

	if (holidays == NULL) {
		holidays = holidaytail = (holiday_t *)calloc(1, sizeof(holiday_t));
	}
	else {
		holidaytail = (holiday_t *)calloc(1, sizeof(holiday_t));
	}
	memcpy (holidaytail, newholiday, sizeof(holiday_t));
	holidaytail->next = NULL;

	switch (holidaytail->holtype) {
	  case HOL_ABSOLUTE:
		day = mkday(year, holidaytail->month, holidaytail->day);
		t = localtime(&day);
		holidaytail->yday = t->tm_yday;
		break;

	  case HOL_EASTER:
		day = getEasterDate(year);
		t = localtime(&day);
		holidaytail->yday = t->tm_yday + holidaytail->day;
		break;

	  case HOL_ADVENT:
		day = get4AdventDate(year);
		t = localtime(&day);
		holidaytail->yday = t->tm_yday + holidaytail->day;
		break;
	}
}


int load_holidays(void)
{
	static void *configholidays = NULL;
	static int current_year = 0;
	char fn[PATH_MAX];
	FILE *fd;
	time_t tnow;
	struct tm *now;
	strbuffer_t *inbuf;
	holiday_t newholiday;

	MEMDEFINE(fn);

	tnow = getcurrenttime(NULL);
	now = localtime(&tnow);

	sprintf(fn, "%s/etc/hobbit-holidays.cfg", xgetenv("BBHOME"));

	/* First check if there were no modifications at all */
	if (configholidays) {
		/* if the new year begins, the holidays have to be recalculated */
		if (!stackfmodified(configholidays) && (now->tm_year == current_year)){
			dbgprintf("No files modified, skipping reload of %s\n", fn);
			MEMUNDEFINE(fn);
			return 0;
		}
		else {
			stackfclist(&configholidays);
			configholidays = NULL;
		}
	}

	fd = stackfopen(fn, "r", &configholidays);
	if (!fd) {
		errprintf("Cannot open configuration file %s\n", fn);
		MEMUNDEFINE(fn);
		return 0;
	}

	reset_holidays();

	memset(&newholiday,0,sizeof(holiday_t));
	inbuf = newstrbuffer(0);

	while (stackfgets(inbuf, NULL)) {
		char *p, *delim, *arg1, *arg2;

		sanitize_input(inbuf, 1, 0);
		if (STRBUFLEN(inbuf) == 0) continue;

		p = STRBUF(inbuf);
		if (strncasecmp(p, "HOLIDAYLIKEWEEKDAY=", 19) == 0)  {
			p+=19;
			holidays_like_weekday = atoi(p);
			if (holidays_like_weekday < -1 || holidays_like_weekday > 6) {
				holidays_like_weekday = -1;
				errprintf("Invalid HOLIDAYLIKEWEEKDAY in %s\n", fn);
			}

			continue;
		}

		delim = strchr(p, ':');
		if (delim) {
			add_holiday(now->tm_year, &newholiday);
			memset(&newholiday,0,sizeof(holiday_t));
			if (delim == p) {
				newholiday.desc = strdup("untitled");
			}
			else {
				*delim = '\0';
				newholiday.desc = strdup(p);
				p=delim+1;
			}
		}
		arg1 = strtok(p,"=");
		while (arg1) {
			arg2=strtok(NULL," ,;\t\n\r");
			if (!arg2) break;
			if (strncasecmp(arg1, "TYPE", 4) == 0) {
				if (strncasecmp(arg2, "STATIC", 6) == 0) newholiday.holtype = HOL_ABSOLUTE;
				if (strncasecmp(arg2, "EASTER", 6) == 0) newholiday.holtype = HOL_EASTER;
				if (strncasecmp(arg2, "4ADVENT", 7) == 0) newholiday.holtype = HOL_ADVENT;
			}
			if (strncasecmp(arg1, "MONTH", 5) == 0) {
				newholiday.month=atoi(arg2);
			}
			if (strncasecmp(arg1, "DAY", 3) == 0) {
				newholiday.day=atoi(arg2);
			}
			if (strncasecmp(arg1, "OFFSET", 3) == 0) {
				newholiday.day=atoi(arg2);
			}

			arg1 = strtok(NULL,"=");
		}
	}
	add_holiday(now->tm_year, &newholiday);

	stackfclose(fd);
	freestrbuffer(inbuf);

	MEMUNDEFINE(fn);
	current_year = now->tm_year;

	return 0;
}				


int getweekdayorholiday(struct tm *t)
{
	holiday_t *p;

	if (holidays_like_weekday == -1) return t->tm_wday;

	p = holidays;
	while (p) {
		if (t->tm_yday == p->yday) {
			return holidays_like_weekday;
		}

		p = p->next;
	}

	return t->tm_wday;
}


