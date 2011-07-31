/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for timehandling.                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "libxymon.h"

#ifdef time
#undef time
#endif

time_t fakestarttime = 0;

time_t getcurrenttime(time_t *retparm)
{
	static time_t firsttime = 0;

	if (fakestarttime != 0) {
		time_t result;

		if (firsttime == 0) firsttime = time(NULL);
		result = fakestarttime + (time(NULL) - firsttime);
		if (retparm) *retparm = result;
		return result;
	}
	else
		return time(retparm);
}


time_t gettimer(void)
{
	int res;
	struct timespec t;

#if (_POSIX_TIMERS > 0) && defined(_POSIX_MONOTONIC_CLOCK)
	res = clock_gettime(CLOCK_MONOTONIC, &t);
	return (time_t) t.tv_sec;
#else
	return time(NULL);
#endif
}


void getntimer(struct timespec *tp)
{
#if (_POSIX_TIMERS > 0) && defined(_POSIX_MONOTONIC_CLOCK)
	int res;
	res = clock_gettime(CLOCK_MONOTONIC, tp);
#else
	struct timeval t;
	struct timezone tz;

	gettimeofday(&t, &tz);
	tp->tv_sec = t.tv_sec;
	tp->tv_nsec = 1000*t.tv_usec;
#endif
}


char *timestamp = NULL;
void init_timestamp(void)
{
	time_t	now;

	if (timestamp == NULL) timestamp = (char *)malloc(30);

        now = getcurrenttime(NULL);
        strcpy(timestamp, ctime(&now));
        timestamp[strlen(timestamp)-1] = '\0';

}


char *timespec_text(char *spec)
{
	static char *daynames[7] = { NULL, };
	static char *wkdays = NULL;
	static strbuffer_t *result = NULL;
	char *sCopy;
	char *p;

	if (result == NULL) result = newstrbuffer(0);
	clearstrbuffer(result);

	if (!daynames[0]) {
		/* Use strftime to get the locale-specific weekday names */
		time_t now;
		int i;

		now = time(NULL);
		for (i=0; (i<7); i++) {
			char dtext[10];
			struct tm *tm = localtime(&now);
			strftime(dtext, sizeof(dtext), "%a", tm);
			daynames[tm->tm_wday] = strdup(dtext);
			now -= 86400;
		}

		wkdays = (char *)malloc(strlen(daynames[1]) + strlen(daynames[5]) + 2);
		sprintf(wkdays, "%s-%s", daynames[1], daynames[5]);
	}


	p = sCopy = strdup(spec);
	do {
		char *s1, *s2, *s3, *s4, *s5;
		char *days, *starttime, *endtime, *columns, *cause;
		char *oneday, *dtext;
		int daysdone = 0, firstday = 1, ecount, causelen;

		/* Its either DAYS:START:END or SERVICE:DAYS:START:END:CAUSE */

		s1 = p; p += strcspn(p, ":"); if (*p != '\0') { *p = '\0'; p++; }
		s2 = p; p += strcspn(p, ":"); if (*p != '\0') { *p = '\0'; p++; }
		s3 = p; p += strcspn(p, ":;,"); 
		if ((*p == ',') || (*p == ';') || (*p == '\0')) { 
			if (*p != '\0') { *p = '\0'; p++; }
			days = s1; starttime = s2; endtime = s3;
			columns = "*";
			cause = strdup("Planned downtime");
		}
		else if (*p == ':') {
			*p = '\0'; p++; 
			s4 = p; p += strcspn(p, ":"); if (*p != '\0') { *p = '\0'; p++; }
			s5 = p; p += strcspn(p, ",;"); if (*p != '\0') { *p = '\0'; p++; }
			days = s2; starttime = s3; endtime = s4;
			columns = s1;
			getescapestring(s5, &cause, &causelen);
		}

		oneday = days;

		while (!daysdone) {
			switch (*oneday) {
			  case '*': dtext = "All days"; break;
			  case 'W': dtext = wkdays; break;
			  case '0': dtext = daynames[0]; break;
			  case '1': dtext = daynames[1]; break;
			  case '2': dtext = daynames[2]; break;
			  case '3': dtext = daynames[3]; break;
			  case '4': dtext = daynames[4]; break;
			  case '5': dtext = daynames[5]; break;
			  case '6': dtext = daynames[6]; break;
			  default : dtext = oneday; daysdone = firstday = 1; break;
			}

			if (!firstday) addtobuffer(result, "/");

			addtobuffer(result, dtext);
			oneday++;
			firstday = 0;
		}

		addtobuffer(result, ":"); addtobuffer(result, starttime);

		addtobuffer(result, ":"); addtobuffer(result, endtime);

		addtobuffer(result, " (status:"); 
		if (strcmp(columns, "*") == 0)
			addtobuffer(result, "All");
		else
			addtobuffer(result, columns); 
		addtobuffer(result, ")");

		if (cause) { 
			addtobuffer(result, " (cause:"); 
			addtobuffer(result, cause); 
			addtobuffer(result, ")");
			xfree(cause);
		}
	} while (*p);

	xfree(sCopy);

	return STRBUF(result);
}

struct timespec *tvdiff(struct timespec *tstart, struct timespec *tend, struct timespec *result)
{
	static struct timespec resbuf;

	if (result == NULL) result = &resbuf;

	result->tv_sec = tend->tv_sec;
	result->tv_nsec = tend->tv_nsec;
	if (result->tv_nsec < tstart->tv_nsec) {
		result->tv_sec--;
		result->tv_nsec += 1000000000;
	}
	result->tv_sec  -= tstart->tv_sec;
	result->tv_nsec -= tstart->tv_nsec;

	return result;
}


static int minutes(char *p)
{
	/* Converts string HHMM to number indicating minutes since midnight (0-1440) */
	if (isdigit((int)*(p+0)) && isdigit((int)*(p+1)) && isdigit((int)*(p+2)) && isdigit((int)*(p+3))) {
		return (10*(*(p+0)-'0')+(*(p+1)-'0'))*60 + (10*(*(p+2)-'0')+(*(p+3)-'0'));
	}
	else {
		errprintf("Invalid timespec - expected 4 digits, got: '%s'\n", p);
		return 0;
	}
}

int within_sla(char *holidaykey, char *timespec, int defresult)
{
	/*
	 *    timespec is of the form W:HHMM:HHMM[,W:HHMM:HHMM]*
	 *    "W" = weekday : '*' = all, 'W' = Monday-Friday, '0'..'6' = Sunday ..Saturday
	 */

	int found = 0;
	time_t tnow;
	struct tm *now;
	int curtime;
	int newwday;
	char *onesla;

	if (!timespec) return defresult;

	tnow = getcurrenttime(NULL);
	now = localtime(&tnow);
	curtime = now->tm_hour*60+now->tm_min;
	newwday = getweekdayorholiday(holidaykey, now);

	onesla = timespec;
	while (!found && onesla) {

		char *wday;
		int validday, wdaymatch = 0;
		char *endsla, *starttimep, *endtimep;
		int starttime, endtime;

		endsla = strchr(onesla, ','); if (endsla) *endsla = '\0';

		for (wday = onesla, validday=1; (validday && !wdaymatch); wday++) {
			switch (*wday) {
			  case '*':
				wdaymatch = 1;
				break;

			  case 'W':
			  case 'w':
				if ((newwday >= 1) && (newwday <=5)) wdaymatch = 1;
				break;

			  case '0': case '1': case '2': case '3': case '4': case '5': case '6':
				if (*wday == (newwday+'0')) wdaymatch = 1;
				break;

			  case ':':
				/* End of weekday spec. is OK */
				validday = 0;
				break;

			  default:
				errprintf("Bad timespec (missing colon or wrong weekdays): %s\n", onesla);
				validday = 0;
				break;
			}
		}

		if (wdaymatch) {
			/* Weekday matches */
			starttimep = strchr(onesla, ':');
			if (starttimep) {
				starttime = minutes(starttimep+1);
				endtimep = strchr(starttimep+1, ':');
				if (endtimep) {
					endtime = minutes(endtimep+1);
					if (endtime > starttime) {
						/* *:0200:0400 */
						found = ((curtime >= starttime) && (curtime <= endtime));
					}
					else {
						/* The period crosses over midnight: *:2330:0400 */
						found = ((curtime >= starttime) || (curtime <= endtime));
					}
					dbgprintf("\tstart,end,current time = %d, %d, %d - found=%d\n", 
						starttime,endtime,curtime,found);
				}
				else errprintf("Bad timespec (missing colon or no endtime): %s\n", onesla);
			}
			else errprintf("Bad timespec (missing colon or no starttime): %s\n", onesla);
		}
		else {
			dbgprintf("\tWeekday does not match\n");
		}

		/* Go to next SLA spec. */
		if (endsla) *endsla = ',';
		onesla = (endsla ? (endsla + 1) : NULL);
	}

	return found;
}

#ifndef CLIENTONLY
char *check_downtime(char *hostname, char *testname)
{
	void *hinfo = hostinfo(hostname);
	char *dtag;
	char *holkey;

	if (hinfo == NULL) return NULL;

	dtag = xmh_item(hinfo, XMH_DOWNTIME);
	holkey = xmh_item(hinfo, XMH_HOLIDAYS);
	if (dtag && *dtag) {
		static char *downtag = NULL;
		static unsigned char *cause = NULL;
		static int causelen = 0;
		char *s1, *s2, *s3, *s4, *s5, *p;
		char timetxt[30];

		if (downtag) xfree(downtag);
		if (cause) xfree(cause);

		p = downtag = strdup(dtag);
		do {
			/* Its either DAYS:START:END or SERVICE:DAYS:START:END:CAUSE */

			s1 = p; p += strcspn(p, ":"); if (*p != '\0') { *p = '\0'; p++; }
			s2 = p; p += strcspn(p, ":"); if (*p != '\0') { *p = '\0'; p++; }
			s3 = p; p += strcspn(p, ":;,"); 
			if ((*p == ',') || (*p == ';') || (*p == '\0')) { 
				if (*p != '\0') { *p = '\0'; p++; }
				snprintf(timetxt, sizeof(timetxt), "%s:%s:%s", s1, s2, s3);
				cause = strdup("Planned downtime");
				s1 = "*";
			}
			else if (*p == ':') {
				*p = '\0'; p++; 
				s4 = p; p += strcspn(p, ":"); if (*p != '\0') { *p = '\0'; p++; }
				s5 = p; p += strcspn(p, ",;"); if (*p != '\0') { *p = '\0'; p++; }
				snprintf(timetxt, sizeof(timetxt), "%s:%s:%s", s2, s3, s4);
				getescapestring(s5, &cause, &causelen);
			}

			if (within_sla(holkey, timetxt, 0)) {
				char *onesvc, *buf;

				if (strcmp(s1, "*") == 0) return cause;

				onesvc = strtok_r(s1, ",", &buf);
				while (onesvc) {
					if (strcmp(onesvc, testname) == 0) return cause;
					onesvc = strtok_r(NULL, ",", &buf);
				}

				/* If we didn't use the "cause" we just created, it must be freed */
				if (cause) xfree(cause);
			}
		} while (*p);
	}

	return NULL;
}
#endif

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
		xfree(endtime); xfree(starttime); xfree(dayspec); return 1;
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

	tnow = getcurrenttime(NULL);
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
	xfree(endtime); xfree(starttime); xfree(dayspec); 
	return result;
}

char *histlogtime(time_t histtime)
{
	static char *result = NULL;
	char d1[40],d2[3],d3[40];

	if (result == NULL) result = (char *)malloc(30);
	MEMDEFINE(d1); MEMDEFINE(d2); MEMDEFINE(d3);

	/*
	 * Historical logs use a filename like "Fri_Nov_7_16:01:08_2002 
	 * But apparently there is no simple way to generate a day-of-month 
	 * with no leading 0.
	 */

        strftime(d1, sizeof(d1), "%a_%b_", localtime(&histtime));
        strftime(d2, sizeof(d2), "%d", localtime(&histtime));
	if (d2[0] == '0') { d2[0] = d2[1]; d2[1] = '\0'; }
        strftime(d3, sizeof(d3), "_%H:%M:%S_%Y", localtime(&histtime));

	snprintf(result, 29, "%s%s%s", d1, d2, d3);

	MEMUNDEFINE(d1); MEMUNDEFINE(d2); MEMUNDEFINE(d3);

	return result;
}


int durationvalue(char *dur)
{
	/* 
	 * Calculate a duration, taking special modifiers into consideration.
	 * Return the duration as number of minutes.
	 */

	int result = 0;
	char *startofval;
	char *endpos;
	char savedelim;

	/* Make sure we only process the first token, dont go past whitespace or some other delimiter */
	endpos = dur + strspn(dur, "01234567890mhdw");
	savedelim = *endpos;
	*endpos = '\0';

	startofval = dur;

	while (startofval && (isdigit((int)*startofval))) {
		char *p;
		char modifier;
		int oneval = 0;

		p = startofval + strspn(startofval, "0123456789");
		modifier = *p;
		*p = '\0';
		oneval = atoi(startofval);
		*p = modifier;

		switch (modifier) {
		  case '\0': break;			/* No delimiter = minutes */
		  case 'm' : break;			/* minutes */
		  case 'h' : oneval *= 60; break;	/* hours */
		  case 'd' : oneval *= 1440; break;	/* days */
		  case 'w' : oneval *= 10080; break;	/* weeks */
		}

		result += oneval;
		startofval = ((*p) ? p+1 : NULL);
	}

	/* Restore the saved delimiter */
	*endpos = savedelim;

	return result;
}

char *durationstring(time_t secs)
{
#define ONE_WEEK   (7*24*60*60)
#define ONE_DAY    (24*60*60)
#define ONE_HOUR   (60*60)
#define ONE_MINUTE (60)

	static char result[50];
	char *p = result;
	time_t v = secs;
	int n;

	if (secs == 0) return "-";

	*result = '\0';

	if (v >= ONE_WEEK) {
		n = (int) (v / ONE_WEEK);
		p += sprintf(p, "%dw ", n);
		v -= (n * ONE_WEEK);
	}

	if (v >= ONE_DAY) {
		n = (int) (v / ONE_DAY);
		p += sprintf(p, "%dd ", n);
		v -= (n * ONE_DAY);
	}

	if (v >= ONE_HOUR) {
		n = (int) (v / ONE_HOUR);
		p += sprintf(p, "%dh ", n);
		v -= (n * ONE_HOUR);
	}

	if (v >= ONE_MINUTE) {
		n = (int) (v / ONE_MINUTE);
		p += sprintf(p, "%dm ", n);
		v -= (n * ONE_MINUTE);
	}

	if (v > 0) {
		p += sprintf(p, "%ds ", (int)v);
	}

	return result;
}

char *agestring(time_t secs)
{
	static char result[128];
	char *p;
	time_t left = secs;

	*result = '\0';
	p = result;

	if (left > 86400) {
		p += sprintf(p, "%ldd", (left / 86400));
		left = (left % 86400);
	}
	if ((left > 3600) || *result) {
		p += sprintf(p, (*result ? "%02ldh" : "%ldh"), (left / 3600));
		left = (left % 3600);
	}
	if ((left > 60) || *result) {
		p += sprintf(p, (*result ? "%02ldm" : "%ldm"), (left / 60));
		left = (left % 60);
	}
	/* Only show seconds if no other info */
	if (*result == '\0') {
		p += sprintf(p, "%02lds", left);
	}

	*p = '\0';
	return result;
}

time_t timestr2timet(char *s)
{
	/* Convert a string "YYYYMMDDHHMM" to time_t value */
	struct tm tm;

	if (strlen(s) != 12) {
		errprintf("Invalid timestring: '%s'\n", s);
		return -1;
	}

	tm.tm_min = atoi(s+10); *(s+10) = '\0';
	tm.tm_hour = atoi(s+8); *(s+8) = '\0';
	tm.tm_mday = atoi(s+6); *(s+6) = '\0';
	tm.tm_mon = atoi(s+4) - 1; *(s+4) = '\0';
	tm.tm_year = atoi(s) - 1900; *(s+4) = '\0';
	tm.tm_isdst = -1;
	return mktime(&tm);
}


time_t eventreport_time(char *timestamp)
{
	time_t event = 0;
	unsigned int year,month,day,hour,min,sec,count;
	struct tm timeinfo;

	if ((*timestamp) && (*(timestamp + strspn(timestamp, "0123456789")) == '\0'))
		return (time_t) atol(timestamp);

	count = sscanf(timestamp, "%u/%u/%u@%u:%u:%u",
		&year, &month, &day, &hour, &min, &sec);
	if(count != 6) {
		return -1;
	}
	if(year < 1970) {
		return 0;
	}
	else {
		memset(&timeinfo, 0, sizeof(timeinfo));
		timeinfo.tm_year  = year - 1900;
		timeinfo.tm_mon   = month - 1;
		timeinfo.tm_mday  = day;
		timeinfo.tm_hour  = hour;
		timeinfo.tm_min   = min;
		timeinfo.tm_sec   = sec;
		timeinfo.tm_isdst = -1;
		event = mktime(&timeinfo);		
	}

	return event;
}


