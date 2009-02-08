/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This file contains code to calculate availability percentages and do       */
/* SLA calculations.                                                          */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: availability.c,v 1.43 2006-08-01 09:19:43 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "libbbgen.h"

typedef struct {
	int dow;
	time_t start, end;
} reptime_t;
static reptime_t reptimes[10];
static int reptimecnt = 0;

replog_t *reploghead = NULL;

char *durationstr(time_t duration)
{
	static char dur[100];
	char dhelp[100];

	if (duration <= 0) {
		strcpy(dur, "none");
	}
	else {
		dur[0] = '\0';
		if (duration > 86400) {
			sprintf(dhelp, "%u days ", (unsigned int)(duration / 86400));
			duration %= 86400;
			strcpy(dur, dhelp);
		}
		sprintf(dhelp, "%u:%02u:%02u", (unsigned int)(duration / 3600), 
			(unsigned int)((duration % 3600) / 60), (unsigned int)(duration % 60));
		strcat(dur, dhelp);
	}

	return dur;
}

static time_t secs(int hour, int minute, int sec)
{
	return (hour*3600 + minute*60 + sec);
}

static void build_reportspecs(char *reporttime)
{
	/* Timespec:  W:HHMM:HHMM */

	char *spec, *timespec;
	int dow, start, end;
	int found;

	reptimecnt = 0;
	spec = strchr(reporttime, '=');
	timespec = strdup(spec ? (spec+1) : reporttime); 
	
	spec = strtok(timespec, ",");
	while (spec) {
		if (*spec == '*') {
			dow = -1;
			found = sscanf(spec+1, ":%d:%d", &start, &end);
		}
		else if ((*spec == 'W') || (*spec == 'w')) {
			dow = -2;
			found = sscanf(spec+1, ":%d:%d", &start, &end);
		}
		else {
			found = sscanf(spec, "%d:%d:%d", &dow, &start, &end);
		}

		if (found < 2) {
			errprintf("build_reportspecs: Found too few items in %s\n", spec);
		}

		reptimes[reptimecnt].dow = dow;
		reptimes[reptimecnt].start = secs((start / 100), (start % 100), 0);
		reptimes[reptimecnt].end = secs((end / 100), (end % 100), 0);
		reptimecnt++;
		spec = strtok(NULL, ",");
	}

	xfree(timespec);
}

static unsigned long reportduration_oneday(int eventdow, time_t eventstart, time_t eventend)
{
	int i;
	unsigned long result = 0;

	for (i=0; (i<reptimecnt); i++) {
		if ((reptimes[i].dow == eventdow) || (reptimes[i].dow == -1) || ((reptimes[i].dow == -2) && (eventdow >= 1) && (eventdow <= 5)) ) {
			if ((reptimes[i].start > eventend) || (reptimes[i].end < eventstart)) {
				/* Outside our window */
			}
			else {
				time_t winstart, winend;

				winstart = ((eventstart < reptimes[i].start) ? reptimes[i].start : eventstart);
				winend = ((eventend > reptimes[i].end) ? reptimes[i].end : eventend);
				result += (winend - winstart);
			}
		}
	}

	return result;
}

static unsigned long reportingduration(time_t eventstart, time_t eventduration)
{
	struct tm start, end;
	time_t eventend;
	unsigned long result;

	memcpy(&start, localtime(&eventstart), sizeof(start));
	eventend = eventstart + eventduration;
	memcpy(&end, localtime(&eventend), sizeof(end));

	if ((start.tm_mday == end.tm_mday) && (start.tm_mon == end.tm_mon) && (start.tm_year == end.tm_year))
		result = reportduration_oneday(start.tm_wday, secs(start.tm_hour, start.tm_min, start.tm_sec), secs(end.tm_hour, end.tm_min, end.tm_sec));
	else {
		int fulldays = (eventduration - (86400-secs(start.tm_hour, start.tm_min, start.tm_sec))) / 86400;
		int curdow = (start.tm_wday  == 6) ? 0 : (start.tm_wday+1);

		result = reportduration_oneday(start.tm_wday, secs(start.tm_hour, start.tm_min, start.tm_sec), 86400);
		while (fulldays) {
			result += reportduration_oneday(curdow, 0, 86400);
			curdow = (curdow  == 6) ? 0 : (curdow+1);
			fulldays--;
		}
		result += reportduration_oneday(curdow, 0, secs(end.tm_hour, end.tm_min, end.tm_sec));
	}

	return result;
}


static char *parse_histlogfile(char *hostname, char *servicename, char *timespec)
{
	char cause[MAX_LINE_LEN];
	char fn[PATH_MAX];
	char *p;
	FILE *fd;
	char l[MAX_LINE_LEN];
	int causefull = 0;

	cause[0] = '\0';

	sprintf(fn, "%s/%s", xgetenv("BBHISTLOGS"), commafy(hostname));
	for (p = strrchr(fn, '/'); (*p); p++) if (*p == ',') *p = '_';
	sprintf(p, "/%s/%s", servicename, timespec);

	dbgprintf("Looking at history logfile %s\n", fn);
	fd = fopen(fn, "r");
	if (fd != NULL) {
		while (!causefull && fgets(l, sizeof(l), fd)) {
			p = strchr(l, '\n'); if (p) *p = '\0';

			if ((l[0] == '&') && (strncmp(l, "&green", 6) != 0)) {
				p = skipwhitespace(skipword(l));
				if ((strlen(cause) + strlen(p) + strlen("<BR>\n") + 1) < sizeof(cause)) {
					strcat(cause, p);
					strcat(cause, "<BR>\n");
				}
				else causefull = 1;
			}
		}

#if 1
		if (strlen(cause) == 0) {
			strcpy(cause, "See detailed log");
		}
#else
		/* What is this code supposed to do ? The sscanf seemingly never succeeds */
		/* storner, 2006-06-02 */
		if (strlen(cause) == 0) {
			int offset;
			rewind(fd);
			if (fgets(l, sizeof(l), fd)) {
				p = strchr(l, '\n'); if (p) *p = '\0';
				if (sscanf(l, "%*s %*s %*s %*s %*s %*s %*s %n", &offset) == 1) {
					strncpy(cause, l+offset, sizeof(cause));
				}
				else {
					errprintf("Scan of file %s failed, l='%s'\n", fn, l);
				}
				cause[sizeof(cause)-1] = '\0';
			}
		}
#endif

		if (causefull) {
			cause[sizeof(cause) - strlen(" [Truncated]") - 1] = '\0';
			strcat(cause, " [Truncated]");
		}

		fclose(fd);
	}
	else {
		strcpy(cause, "No historical status available");
	}

	return strdup(cause);
}

static char *get_historyline(char *buf, int bufsize, FILE *fd, int *err,
			     char *colstr, unsigned int *start, unsigned int *duration, int *scanres)
{
	int ok;

	do {
		ok = 1;

		if (fgets(buf, bufsize, fd) == NULL) {
			return NULL;
		}

		if (strlen(buf) < 25) {
			ok = 0;
			*err += 1;
			dbgprintf("Bad history line (short): %s\n", buf);
			continue;
		}

		*scanres = sscanf(buf+25, "%s %u %u", colstr, start, duration);
		if (*scanres < 2) {
			ok = 0;
			*err += 1;
			dbgprintf("Bad history line (missing items): %s\n", buf);
			continue;
		}

		if (parse_color(colstr) == -1) {
			ok = 0;
			*err += 1;
			dbgprintf("Bad history line (bad color string): %s\n", buf);
			continue;
		}
	} while (!ok);

	return buf;
}

static int scan_historyfile(FILE *fd, time_t fromtime, time_t totime,
		char *buf, size_t bufsize, 
		time_t *starttime, time_t *duration, char *colstr)
{
	time_t start, dur;
	unsigned int uistart, uidur;
	int scanres;
	int err = 0;

	/*
	 * Format of history entries:
	 *    asctime-stamp newcolor starttime [duration]
	 */

	/* Is start of history after our report-end time ? */
	rewind(fd);
	if (!get_historyline(buf, bufsize, fd, &err, colstr, &uistart, &uidur, &scanres)) {
		*starttime = getcurrenttime(NULL);
		*duration = 0;
		strcpy(colstr, "clear");
		return err;
	}

	if (scanres == 2) uidur = getcurrenttime(NULL)-uistart;
	start = uistart; dur = uidur;

	if (start > totime) {
		*starttime = start;
		*duration = dur;
		strcpy(colstr, "clear");
		return 0;
	}

	/* First, do a quick scan through the file to find the approximate position where we should start */
	while ((start+dur) < fromtime) {
		if (get_historyline(buf, bufsize, fd, &err, colstr, &uistart, &uidur, &scanres)) {
			start = uistart; dur = uidur;
			if (scanres == 2) dur = getcurrenttime(NULL) - start;

			if (scanres >= 2) {
				dbgprintf("Skipped to entry starting %lu\n", start);

				if ((start + dur) < fromtime) {
					fseeko(fd, 2048, SEEK_CUR);
					fgets(buf, bufsize, fd); /* Skip partial line */
				}
			}
		}
		else {
			start = getcurrenttime(NULL);
			dur = 0;
		}
	};

	/* We know the start position of the logfile is between current pos and (current-~2048 bytes) */
	if (ftello(fd) < 2300)
		rewind(fd);
	else {
		fseeko(fd, -2300, SEEK_CUR); 
		fgets(buf, bufsize, fd); /* Skip partial line */
	}

	/* Read one line at a time until we hit start of our report period */
	do {
		if (get_historyline(buf, bufsize, fd, &err, colstr, &uistart, &uidur, &scanres)) {
			start = uistart; dur = uidur;
			if (scanres == 2) dur = getcurrenttime(NULL) - start;

			dbgprintf("Got entry starting %lu lasting %lu\n", start, dur);
		}
		else {
			start = getcurrenttime(NULL);
			dur = 0;
		}
	} while ((start+dur) < fromtime);

	dbgprintf("Reporting starts with this entry: %s\n", buf);

	*starttime = start;
	*duration = dur;
	return err;
}


static char *timename(char *timestring)
{
	static char timespec[26];
	char *timecopy;
	char *tokens[5];
	int i;

	/* Compute the timespec string used as the name of the historical logfile */
	*timespec = '\0';
	timecopy = strdup(timestring);
	tokens[0] = tokens[1] = tokens[2] = tokens[3] = tokens[4] = NULL;

	tokens[0] = strtok(timecopy, " "); i = 0;
	while (tokens[i] && (i < 4)) { i++; tokens[i] = strtok(NULL, " "); }

	if (tokens[4]) {
		/* Got all 5 elements */
		snprintf(timespec, sizeof(timespec), "%s_%s_%s_%s_%s",
			 tokens[0], tokens[1], tokens[2], tokens[3], tokens[4]);
	}
	else {
		errprintf("Bad timespec in history file: %s\n", timestring);
	}
	xfree(timecopy);

	return timespec;
}


int parse_historyfile(FILE *fd, reportinfo_t *repinfo, char *hostname, char *servicename, 
			time_t fromtime, time_t totime, int for_history, 
			double warnlevel, double greenlevel, int warnstops, char *reporttime)
{
	char l[MAX_LINE_LEN];
	time_t starttime, duration;
	unsigned int uistart, uidur;
	char colstr[MAX_LINE_LEN];
	int color, done, i, scanres;
	int fileerrors = 0;

	repinfo->fstate = "OK";
	repinfo->withreport = 0;
	repinfo->reportstart = getcurrenttime(NULL);
	for (i=0; (i<COL_COUNT); i++) {
		repinfo->count[i] = 0;
		repinfo->fullduration[i] = 0;
		repinfo->fullpct[i] = 0.0;
		repinfo->reportduration[i] = 0;
		repinfo->reportpct[i] = 0.0;
	}
	repinfo->fullavailability = 0.0;
	repinfo->reportavailability = 0.0;
	repinfo->fullstops = 0;
	repinfo->reportstops = 0;

	if (reporttime) build_reportspecs(reporttime);

	/* Sanity check */
	if (totime > getcurrenttime(NULL)) totime = getcurrenttime(NULL);

	/* If for_history and fromtime is 0, dont do any seeking */
	if (!for_history || (fromtime > 0)) {
		fileerrors = scan_historyfile(fd, fromtime, totime, 
				      l, sizeof(l), &starttime, &duration, colstr);
	}
	else {
		/* Already positioned (probably in a pipe) */
		if (get_historyline(l, sizeof(l), fd, &fileerrors, colstr, &uistart, &uidur, &scanres)) {
			starttime = uistart; duration = uidur;
			if (scanres == 2) duration = getcurrenttime(NULL) - starttime;
		}
		else {
			starttime = getcurrenttime(NULL); duration = 0;
			strcpy(colstr, "clear");
			fileerrors = 1;
		}
	}

	if (starttime > totime) {
		repinfo->fullavailability = repinfo->reportavailability = 100.0;
		repinfo->fullpct[COL_CLEAR] = repinfo->reportpct[COL_CLEAR] = 100.0;
		repinfo->count[COL_CLEAR] = 1;
		return COL_CLEAR;
	}

	/* If event starts before our fromtime, adjust starttime and duration */
	if (starttime < fromtime) {
		duration -= (fromtime - starttime);
		starttime = fromtime;
	}
	repinfo->reportstart = starttime;

	done = 0;
	do {
		/* If event ends after our reportend, adjust duration */
		if ((starttime + duration) > totime) duration = (totime - starttime);
		strcat(colstr, " "); color = parse_color(colstr);

		if (color != -1) {
			unsigned long sladuration = 0;

			dbgprintf("In-range entry starting %lu lasting %lu color %d: %s", starttime, duration, color, l);
			repinfo->count[color]++;
			repinfo->fullduration[color] += duration;
			if (color > COL_YELLOW) repinfo->fullstops++;
			if (reporttime) {
				sladuration = reportingduration(starttime, duration);
				repinfo->reportduration[color] += sladuration;
				if ((color > COL_YELLOW) && (sladuration > 0)) repinfo->reportstops++;
			}

			if (for_history || ((hostname != NULL) && (servicename != NULL))) {
				replog_t *newentry;
				char *timespec = timename(l);

				newentry = (replog_t *) malloc(sizeof(replog_t));
				newentry->starttime = starttime;
				newentry->duration = duration;
				newentry->color = color;
				newentry->affectssla = (reporttime && (sladuration > 0));

				if (!for_history && timespec && (color != COL_GREEN)) {
					newentry->cause = parse_histlogfile(hostname, servicename, timespec);
				}
				else newentry->cause = "";

				newentry->timespec = (timespec ? strdup(timespec): NULL);
				newentry->next = reploghead;
				reploghead = newentry;
			}
		}

		if ((starttime + duration) < totime) {
			if (get_historyline(l, sizeof(l), fd, &fileerrors, colstr, &uistart, &uidur, &scanres)) {
				starttime = uistart; duration = uidur;
				if (scanres == 2) duration = getcurrenttime(NULL) - starttime;
			}
			else done = 1;
		}
		else done = 1;
	} while (!done);

	for (i=0; (i<COL_COUNT); i++) {
		dbgprintf("Duration for color %d: %lu\n", i, repinfo->fullduration[i]);
		repinfo->fullpct[i] = (100.0*repinfo->fullduration[i] / (totime - repinfo->reportstart));
	}
	repinfo->fullavailability = 100.0 - repinfo->fullpct[COL_RED];

	if (reporttime) {
		repinfo->withreport = 1;
		duration = repinfo->reportduration[COL_GREEN] + 
			   repinfo->reportduration[COL_YELLOW] + 
			   repinfo->reportduration[COL_RED] + 
			   repinfo->reportduration[COL_CLEAR];

		if (duration > 0) {
			repinfo->reportpct[COL_GREEN] = (100.0*repinfo->reportduration[COL_GREEN] / duration);
			repinfo->reportpct[COL_YELLOW] = (100.0*repinfo->reportduration[COL_YELLOW] / duration);
			repinfo->reportpct[COL_RED] = (100.0*repinfo->reportduration[COL_RED] / duration);
			repinfo->reportpct[COL_CLEAR] = (100.0*repinfo->reportduration[COL_CLEAR] / duration);
			repinfo->reportavailability = 100.0 - repinfo->reportpct[COL_RED] - repinfo->reportpct[COL_CLEAR];

			if (repinfo->reportavailability > greenlevel) color = COL_GREEN;
			else if (repinfo->reportavailability >= warnlevel) color = COL_YELLOW;
			else color = COL_RED;

			if ((warnstops >= 0) && (repinfo->reportstops > warnstops)) color = COL_RED;
		}
		else {
			/* Reporting period has no match with REPORTTIME setting */
			repinfo->reportpct[COL_CLEAR] = 100.0;
			repinfo->reportavailability = 100.0;
			color = COL_GREEN;
		}
	}
	else {
		if (repinfo->fullavailability > greenlevel) color = COL_GREEN;
		else if (repinfo->fullavailability >= warnlevel) color = COL_YELLOW;
		else color = COL_RED;

		if ((warnstops >= 0) && (repinfo->fullstops > warnstops)) color = COL_RED;

		/* Copy the full percentages/durations to the SLA ones */
		repinfo->reportavailability = repinfo->fullavailability;
		repinfo->reportstops = repinfo->fullstops;
		for (i=0; (i<COL_COUNT); i++) {
			repinfo->reportduration[i] = repinfo->fullduration[i];
			repinfo->reportpct[i] = repinfo->fullpct[i];
		}
	}

	if (fileerrors) repinfo->fstate = "NOTOK";
	return color;
}


replog_t *save_replogs(void)
{
	replog_t *tmp = reploghead;

	reploghead = NULL;
	return tmp;
}

void restore_replogs(replog_t *head)
{
	reploghead = head;
}


int history_color(FILE *fd, time_t snapshot, time_t *starttime, char **histlogname)
{
	int fileerrors;
	char l[MAX_LINE_LEN];
	time_t duration;
	char colstr[MAX_LINE_LEN];
	int color;
	char *p;

	*histlogname = NULL;
	fileerrors = scan_historyfile(fd, snapshot, snapshot, 
				      l, sizeof(l), starttime, &duration, colstr);
	
	strcat(colstr, " ");
	color = parse_color(colstr);
	if ((color == COL_PURPLE) || (color == -1)) {
		color = -2;
	}

	p = timename(l);
	if (p) *histlogname = strdup(p);

	return color;
}


#ifdef STANDALONE

time_t reportstart, reportend;
double reportgreenlevel = 99.995;
double reportwarnlevel = 98.0;
int    warnstops = -1;

int main(int argc, char *argv[])
{
	FILE *fd;
	reportinfo_t repinfo;
	int i, color;
	char *p, *hostsvc, *host, *svc;
	replog_t *rwalk;

	debug=1;

	if (argc != 4) {
		fprintf(stderr, "Usage: %s HISTFILE STARTTIME ENDTIME\n", argv[0]);
		fprintf(stderr, "Start- and end-times are in Unix epoch format - date +%%s\n");
		return 1;
	}

	fd = fopen(argv[1], "r");
	if (fd == NULL) { printf("Cannot open %s\n", argv[1]); exit(1); }

	reportstart = atol(argv[2]);
	reportend = atol(argv[3]);

	hostsvc = strdup(argv[1]);
	p = strrchr(hostsvc, '.');
	*p = '\0'; svc = p+1;
	p = strrchr(hostsvc, '/'); host = p+1;
	while ((p = strchr(host, ','))) *p = '.';

	color = parse_historyfile(fd, &repinfo, host, svc, reportstart, reportend, 0, reportwarnlevel, reportgreenlevel, warnstops, NULL);

	for (i=0; (i<COL_COUNT); i++) {
		dbgprintf("Color %d: Count=%d, pct=%.2f\n", i, repinfo.count[i], repinfo.fullpct[i]);
	}
	dbgprintf("Availability: %.2f, color =%d\n", repinfo.fullavailability, color);
	dbgprintf("History file status: %s\n", repinfo.fstate);

	fclose(fd);

	for (rwalk = reploghead; (rwalk); rwalk = rwalk->next) {
		char start[30];
		char end[30];
		char dur[30], dhelp[30];
		time_t endtime;
		time_t duration;

		strftime(start, sizeof(start), "%a %b %d %H:%M:%S %Y", localtime(&rwalk->starttime));
		endtime = rwalk->starttime + rwalk->duration;
		strftime(end, sizeof(end), "%a %b %d %H:%M:%S %Y", localtime(&endtime));

		duration = rwalk->duration;
		dur[0] = '\0';
		if (duration > 86400) {
			sprintf(dhelp, "%lu days ", (duration / 86400));
			duration %= 86400;
			strcpy(dur, dhelp);
		}
		sprintf(dhelp, "%lu:%02lu:%02lu", duration / 3600, ((duration % 3600) / 60), (duration % 60));
		strcat(dur, dhelp);

		dbgprintf("Start: %s, End: %s, Color: %s, Duration: %s, Cause: %s\n",
			start, end, colorname(rwalk->color), dur, rwalk->cause);
	}

	return 0;
}
#endif

