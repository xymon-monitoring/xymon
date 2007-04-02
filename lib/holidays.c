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

static char rcsid[] = "$Id: holidays.c,v 1.2 2007-04-02 09:01:24 henrik Exp $";

#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "libbbgen.h"


static int holidays_like_weekday = -1;

typedef struct holidayset_t {
	char *key;
	holiday_t *head;
	holiday_t *tail;
} holidayset_t;

static RbtHandle holidays;


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
	static int firsttime = 1;
	RbtIterator handle;
	holidayset_t *hset;
	holiday_t *walk, *zombie;

	if (!firsttime) {
		for (handle = rbtBegin(holidays); (handle != rbtEnd(holidays)); handle = rbtNext(holidays, handle)) {
			hset = (holidayset_t *)gettreeitem(holidays, handle);
			xfree(hset->key);

			walk = hset->head;
			while (walk) {
				zombie = walk;
				walk = walk->next;
				xfree(zombie->desc);
				xfree(zombie);
			}
		}

		rbtDelete(holidays);
	}

	holidays_like_weekday = -1;
	firsttime = 0;
	holidays = rbtNew(name_compare);
}


static void add_holiday(char *key, int year, holiday_t *newhol)
{
	int isOK = 0;
	struct tm *t;
	time_t day;
	holiday_t *newitem;
	RbtIterator handle;
	holidayset_t *hset;

	switch (newhol->holtype) {
	  case HOL_ABSOLUTE:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <=31) );
		if (isOK) {
			day = mkday(year, newhol->month, newhol->day);
			t = localtime(&day);
			newhol->yday = t->tm_yday;
		}
		break;

	  case HOL_EASTER:
		isOK = (newhol->month == 0);
		if (isOK) {
			day = getEasterDate(year);
			t = localtime(&day);
			newhol->yday = t->tm_yday + newhol->day;
		}
		break;

	  case HOL_ADVENT:
		isOK = (newhol->month == 0);
		if (isOK) {
			day = get4AdventDate(year);
			t = localtime(&day);
			newhol->yday = t->tm_yday + newhol->day;
		}
		break;
	}

	if (!isOK) {
		errprintf("Error in holiday definition %s\n", newhol->desc);
		return;
	}

	newitem = (holiday_t *)calloc(1, sizeof(holiday_t));
	newitem->holtype = newhol->holtype;
	newitem->day = newhol->day;
	newitem->month = newhol->month;
	newitem->desc = strdup(newhol->desc);
	newitem->yday = newhol->yday;

	handle = rbtFind(holidays, key);
	if (handle == rbtEnd(holidays)) {
		hset = (holidayset_t *)calloc(1, sizeof(holidayset_t));
		hset->key = strdup(key);
		rbtInsert(holidays, hset->key, hset);
	}
	else {
		hset = (holidayset_t *)gettreeitem(holidays, handle);
	}

	if (hset->head == NULL) {
		hset->head = hset->tail = newitem;
	}
	else {
		hset->tail->next = newitem; hset->tail = hset->tail->next;
	}
}

static int record_compare(void *a, void *b)
{
	return (((holiday_t *)a)->yday < ((holiday_t *)b)->yday);
}

static void * record_getnext(void *a)
{
	return ((holiday_t *)a)->next;
}

static void record_setnext(void *a, void *newval)
{
	((holiday_t *)a)->next = (holiday_t *)newval;
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
	RbtIterator handle, commonhandle;
	char *setname = NULL;
	holidayset_t *commonhols;

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

		if (*p == '[') {
			/* New set of holidays */
			if (setname) xfree(setname);
			delim = strchr(p, ']'); if (delim) *delim = '\0';
			setname = strdup(p+1);
			continue;
		}

		delim = strchr(p, ':');
		if (delim) {
			memset(&newholiday,0,sizeof(holiday_t));
			if (delim == p) {
				newholiday.desc = "untitled";
			}
			else {
				*delim = '\0';
				newholiday.desc = p;
				p=delim+1;
			}
		}

		arg1 = strtok(p, "=");
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

		add_holiday((setname ? setname : ""), now->tm_year, &newholiday);
	}

	stackfclose(fd);
	freestrbuffer(inbuf);

	commonhandle = rbtFind(holidays, "");
	commonhols = (commonhandle != rbtEnd(holidays)) ? (holidayset_t *)gettreeitem(holidays, commonhandle) : NULL;

	for (handle = rbtBegin(holidays); (handle != rbtEnd(holidays)); handle = rbtNext(holidays, handle)) {
		holidayset_t *oneset = (holidayset_t *)gettreeitem(holidays, handle);
		if (commonhols && (oneset != commonhols)) {
			/* Add the common holidays to this set */
			holiday_t *walk;

			for (walk = commonhols->head; (walk); walk = walk->next) add_holiday(oneset->key, now->tm_year, walk);
		}

		oneset->head = msort(oneset->head, record_compare, record_getnext, record_setnext);
	}

	MEMUNDEFINE(fn);
	current_year = now->tm_year;

	return 0;
}


static holiday_t *findholiday(char *key, int dayinyear, int year)
{
	RbtIterator handle;
	holidayset_t *hset;
	holiday_t *p;

	if (key && *key) {
		handle = rbtFind(holidays, key);
		if (handle == rbtEnd(holidays)) {
			key = NULL;
			handle = rbtFind(holidays, "");
		}
	}
	else {
		key = NULL;
		handle = rbtFind(holidays, "");
	}

	if (handle != rbtEnd(holidays)) 
		hset = (holidayset_t *)gettreeitem(holidays, handle);
	else
		return NULL;

	p = hset->head;
	while (p) {
		if (dayinyear == p->yday) {
			return p;
		}

		p = p->next;
	}

	return NULL;
}

int getweekdayorholiday(char *key, struct tm *t)
{
	holiday_t *rec;

	if (holidays_like_weekday == -1) return t->tm_wday;

	rec = findholiday(key, t->tm_yday, t->tm_year);
	if (rec) return holidays_like_weekday;

	return t->tm_wday;
}

char *isholiday(char *key, int dayinyear, int year)
{
	holiday_t *rec;

	rec = findholiday(key, dayinyear, year);
	if (rec) return rec->desc;

	return NULL;
}


void printholidays(char *key, int year, strbuffer_t *buf)
{
	int day;

	char oneh[1024];

	if (year == 0) {
		struct tm tm;
		time_t t;

		t = time(NULL);
		memcpy(&tm, localtime(&t), sizeof(struct tm));
		year = tm.tm_year;
	}

	for (day = 0; (day < 366); day++) {
		char *desc = isholiday(key, day, year);

		if (desc) {
			struct tm tm;
			time_t t;

			/*
			 * mktime() ignores the tm_yday parameter, so to get the 
			 * actual date for the "yday'th" day of the year we just set
			 * tm_mday to the yday value, and month to January. mktime() 
			 * will figure out that the 56th of January is really Feb 25.
			 *
			 * Note: tm_yday is zero-based, but tm_mday is 1-based!
			 */
			tm.tm_mon = 0; tm.tm_mday = day+1; tm.tm_year = year;
			tm.tm_hour = 12; tm.tm_min = 0; tm.tm_sec = 0;

			t = mktime(&tm);
			memcpy(&tm, localtime(&t), sizeof(struct tm));
			sprintf(oneh, "<tr><td>%s</td><td>%02d/%02d</td>\n", desc, tm.tm_mday, tm.tm_mon+1);
			addtobuffer(buf, oneh);
		}
	}
}

#ifdef STANDALONE
int main(int argc, char *argv[])
{
	char l[1024];
	char *hset = NULL;
	char *p;
	strbuffer_t *sbuf = newstrbuffer(0);

	load_holidays();
	do {
		printf("Select set: "); fflush(stdout);
		if (!fgets(l, sizeof(l), stdin)) return 0;
		p = strchr(l, '\n'); if (p) *p = '\0';
		if (hset) xfree(hset);
		hset = strdup(l);

		printholidays(hset, 0, sbuf);
		printf("Holidays in set: %s\n", STRBUF(sbuf));
		clearstrbuffer(sbuf);
	} while (1);

	return 0;
}

#endif

