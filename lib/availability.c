/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002-2003 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: availability.c,v 1.31 2005-01-15 17:38:55 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "bbgen.h"
#include "reportdata.h"

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

time_t secs(int hour, int minute, int sec)
{
	return (hour*3600 + minute*60 + sec);
}

void build_reportspecs(char *reporttime)
{
	/* Timespec:  W:HHMM:HHMM */

	char *spec, *timespec;
	int dow, start, end;

	reptimecnt = 0;
	spec = strchr(reporttime, '=');
	if (spec == NULL) return; 
	
	timespec = xstrdup(spec+1);
	spec = strtok(timespec, ",");
	while (spec) {
		if (*spec == '*') {
			dow = -1;
			sscanf(spec, "*:%d:%d", &start, &end);
		}
		else if ((*spec == 'W') || (*spec == 'w')) {
			dow = -2;
			sscanf(spec, "*:%d:%d", &start, &end);
		}
		else {
			sscanf(spec, "%d:%d:%d", &dow, &start, &end);
		}

		reptimes[reptimecnt].dow = dow;
		reptimes[reptimecnt].start = secs((start / 100), (start % 100), 0);
		reptimes[reptimecnt].end = secs((end / 100), (end % 100), 0);
		reptimecnt++;
		spec = strtok(NULL, ",");
	}

	xfree(timespec);
}

unsigned long reportduration_oneday(int eventdow, time_t eventstart, time_t eventend)
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

unsigned long reportingduration(time_t eventstart, time_t eventduration)
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


char *parse_histlogfile(char *hostname, char *servicename, char *timespec)
{
	char fn[PATH_MAX];
	char *p;
	FILE *fd;
	char l[MAX_LINE_LEN];
	char cause[MAX_LINE_LEN];
	int causefull = 0;

	cause[0] = '\0';

	sprintf(fn, "%s/%s", getenv("BBHISTLOGS"), commafy(hostname));
	for (p = strrchr(fn, '/'); (*p); p++) if (*p == ',') *p = '_';
	sprintf(p, "/%s/%s", servicename, timespec);

	dprintf("Looking at history logfile %s\n", fn);
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

		if (strlen(cause) == 0) {
			int offset;
			rewind(fd);
			if (fgets(l, sizeof(l), fd)) {
				p = strchr(l, '\n'); if (p) *p = '\0';
				sscanf(l, "%*s %*s %*s %*s %*s %*s %*s %n", &offset);
				strncpy(cause, l+offset, sizeof(cause));
				cause[sizeof(cause)-1] = '\0';
			}
		}

		if (causefull) {
			cause[sizeof(cause) - strlen(" [Truncated]") - 1] = '\0';
			strcat(cause, " [Truncated]");
		}

		fclose(fd);
	}
	else {
		strcpy(cause, "No historical status available");
	}

	return xstrdup(cause);
}

int scan_historyfile(FILE *fd, time_t fromtime, time_t totime,
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
	fgets(buf, bufsize, fd);
	if (sscanf(buf+25, "%s %u %u", colstr, &uistart, &uidur) == 2) 
		uidur = time(NULL)-uistart;
	start = uistart; dur = uidur;

	if (start > totime) {
		*starttime = start;
		*duration = dur;
		strcpy(colstr, "clear");
		return 0;
	}

	/* First, do a quick scan through the file to find the approximate position where we should start */
	while ((start+dur) < fromtime) {
		if (fgets(buf, bufsize, fd)) {
			scanres = sscanf(buf+25, "%s %u %u", colstr, &uistart, &uidur);
			start = uistart; dur = uidur;
			if (scanres == 2) dur = time(NULL) - start;

			if (scanres >= 2) {
				dprintf("Skipped to entry starting %lu\n", start);

				if ((start + dur) < fromtime) {
					fseek(fd, 2048, SEEK_CUR);
					fgets(buf, bufsize, fd); /* Skip partial line */
				}
			}
			else {
				err++;
				dprintf("Bad line in history file '%s'\n", buf);
				start = dur = 0; /* Try next line */
			}
		}
		else {
			start = time(NULL);
			dur = 0;
		}
	};

	/* We know the start position of the logfile is between current pos and (current-~2048 bytes) */
	if (ftell(fd) < 2300)
		rewind(fd);
	else {
		fseek(fd, -2300, SEEK_CUR); 
		fgets(buf, bufsize, fd); /* Skip partial line */
	}

	/* Read one line at a time until we hit start of our report period */
	do {
		if (fgets(buf, bufsize, fd)) {
			scanres = sscanf(buf+25, "%s %u %u", colstr, &uistart, &uidur);
			start = uistart; dur = uidur;
			if (scanres == 2) dur = time(NULL) - start;

			if (scanres < 2) {
				err++;
				dprintf("Bad line in history file '%s'\n", buf);
				start = dur = 0; /* Try next line */
			}
			else {
				dprintf("Got entry starting %lu lasting %lu\n", start, dur);
			}
		}
		else {
			start = time(NULL);
			dur = 0;
		}
	} while ((start+dur) < fromtime);

	dprintf("Reporting starts with this entry: %s\n", buf);

	*starttime = start;
	*duration = dur;
	return err;
}


static char *timename(char *timestring)
{
	static char timespec[26];

	char timecopy[26];
	char *token;
	int i;

	/* Compute the timespec string used as the name of the historical logfile */
	strncpy(timecopy, timestring, 25);
	timecopy[25] = '\0';

	token = strtok(timecopy, " ");
	strcpy(timespec, token);

	for (i=1; i<5; i++) {
		strcat(timespec, "_");
		token = strtok(NULL, " ");
		strcat(timespec, token);
	}

	return timespec;
}


int parse_historyfile(FILE *fd, reportinfo_t *repinfo, char *hostname, char *servicename, 
			time_t fromtime, time_t totime, int for_history, 
			double warnlevel, double greenlevel, char *reporttime)
{
	char l[MAX_LINE_LEN];
	time_t starttime, duration;
	unsigned int uistart, uidur;
	char colstr[MAX_LINE_LEN];
	int color, done, i, scanres;
	int fileerrors;

	repinfo->fstate = "OK";
	repinfo->withreport = 0;
	repinfo->reportstart = time(NULL);
	for (i=0; (i<COL_COUNT); i++) {
		repinfo->count[i] = 0;
		repinfo->fullduration[i] = 0;
		repinfo->fullpct[i] = 0.0;
		repinfo->reportduration[i] = 0;
		repinfo->reportpct[i] = 0.0;
	}
	repinfo->fullavailability = 0.0;
	repinfo->reportavailability = 0.0;

	if (reporttime) build_reportspecs(reporttime);

	/* Sanity check */
	if (totime > time(NULL)) totime = time(NULL);

	/* If for_history and fromtime is 0, dont do any seeking */
	if (!for_history || (fromtime > 0)) {
		fileerrors = scan_historyfile(fd, fromtime, totime, 
				      l, sizeof(l), &starttime, &duration, colstr);
	}
	else {
		/* Already positioned (probably in a pipe) */
		fgets(l, sizeof(l), fd);
		scanres = sscanf(l+25, "%s %u %u", colstr, &uistart, &uidur);
		starttime = uistart; duration = uidur;
		if (scanres == 2) duration = time(NULL) - starttime;
		fileerrors = 0;
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

			dprintf("In-range entry starting %lu lasting %lu color %d: %s", starttime, duration, color, l);
			repinfo->count[color]++;
			repinfo->fullduration[color] += duration;
			if (reporttime) {
				sladuration = reportingduration(starttime, duration);
				repinfo->reportduration[color] += sladuration;
			}

			if (for_history || ((hostname != NULL) && (servicename != NULL))) {
				replog_t *newentry;
				char *timespec = timename(l);

				newentry = (replog_t *) xmalloc(sizeof(replog_t));
				newentry->starttime = starttime;
				newentry->duration = duration;
				newentry->color = color;
				newentry->affectssla = (reporttime && (sladuration > 0));

				if (!for_history && (color != COL_GREEN)) {
					newentry->cause = parse_histlogfile(hostname, servicename, timespec);
				}
				else newentry->cause = "";

				newentry->timespec = xstrdup(timespec);
				newentry->next = reploghead;
				reploghead = newentry;
			}
		}

		if ((starttime + duration) < totime) {
			if (fgets(l, sizeof(l), fd)) {
				scanres = sscanf(l+25, "%s %u %u", colstr, &uistart, &uidur);
				starttime = uistart; duration = uidur;
				if (scanres == 2) duration = time(NULL) - starttime;
			}
			else done = 1;
		}
		else done = 1;
	} while (!done);

	for (i=0; (i<COL_COUNT); i++) {
		dprintf("Duration for color %d: %lu\n", i, repinfo->fullduration[i]);
		repinfo->fullpct[i] = (100.0*repinfo->fullduration[i] / (totime - repinfo->reportstart));
	}
	repinfo->fullavailability = 100.0 - repinfo->fullpct[COL_RED];

	if (reporttime) {
		repinfo->withreport = 1;
		duration = repinfo->reportduration[COL_GREEN] + 
			   repinfo->reportduration[COL_YELLOW] + 
			   repinfo->reportduration[COL_RED] + 
			   repinfo->reportduration[COL_CLEAR];
		repinfo->reportpct[COL_GREEN] = (100.0*repinfo->reportduration[COL_GREEN] / duration);
		repinfo->reportpct[COL_YELLOW] = (100.0*repinfo->reportduration[COL_YELLOW] / duration);
		repinfo->reportpct[COL_RED] = (100.0*repinfo->reportduration[COL_RED] / duration);
		repinfo->reportpct[COL_CLEAR] = (100.0*repinfo->reportduration[COL_CLEAR] / duration);
		repinfo->reportavailability = 100.0 - repinfo->reportpct[COL_RED] - repinfo->reportpct[COL_CLEAR];

		if (repinfo->reportavailability > greenlevel) color = COL_GREEN;
		else if (repinfo->reportavailability >= warnlevel) color = COL_YELLOW;
		else color = COL_RED;
	}
	else {
		repinfo->reportavailability = repinfo->fullavailability;
		if (repinfo->fullavailability > greenlevel) color = COL_GREEN;
		else if (repinfo->fullavailability >= warnlevel) color = COL_YELLOW;
		else color = COL_RED;
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

	fileerrors = scan_historyfile(fd, snapshot, snapshot, 
				      l, sizeof(l), starttime, &duration, colstr);
	
	strcat(colstr, " ");
	color = parse_color(colstr);
	if ((color == COL_PURPLE) || (color == -1)) {
		color = -2;
	}

	*histlogname = xstrdup(timename(l));

	return color;
}


#ifdef STANDALONE

time_t reportstart, reportend;
double reportgreenlevel = 99.995;
double reportwarnlevel = 98.0;

int main(int argc, char *argv[])
{
	FILE *fd;
	reportinfo_t repinfo;
	int i, color;
	char *p, *hostsvc, *host, *svc;
	replog_t *rwalk;

	debug=1;

	fd = fopen(argv[1], "r");
	if (fd == NULL) { printf("Cannot open %s\n", argv[1]); exit(1); }

	reportstart = atol(argv[2]);
	reportend = atol(argv[3]);

	hostsvc = xstrdup(argv[1]);
	p = strrchr(hostsvc, '.');
	*p = '\0'; svc = p+1;
	p = strrchr(hostsvc, '/'); host = p+1;
	while ((p = strchr(host, ','))) *p = '.';

	color = parse_historyfile(fd, &repinfo, host, svc, reportstart, reportend, reportwarnlevel, reportgreenlevel, NULL);

	for (i=0; (i<COL_COUNT); i++) {
		dprintf("Color %d: Count=%d, pct=%.2f\n", i, repinfo.count[i], repinfo.pct[i]);
	}
	dprintf("Availability: %.2f, color =%d\n", repinfo.availability, color);
	dprintf("History file status: %s\n", repinfo.fstate);

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

		dprintf("Start: %s, End: %s, Color: %s, Duration: %s, Cause: %s\n",
			start, end, colorname(rwalk->color), dur, rwalk->cause);
	}

	return 0;
}
#endif

