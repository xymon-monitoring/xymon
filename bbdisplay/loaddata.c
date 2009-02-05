/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* This file contains code to load the current Hobbit status data.            */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: loaddata.c,v 1.164 2006-07-20 16:06:41 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

#include "bbgen.h"
#include "util.h"
#include "loadbbhosts.h"
#include "loaddata.h"

int		statuscount = 0;

char		*ignorecolumns = NULL;			/* Columns that will be ignored totally */
char		*dialupskin = NULL;			/* BBSKIN used for dialup tests */
char		*reverseskin = NULL;			/* BBSKIN used for reverse tests */
time_t		recentgif_limit = 86400;		/* Limit for recent-gifs display, in seconds */

bbgen_col_t   	null_column = { "", NULL };		/* Null column */

int		purplecount = 0;
char		*purplelogfn = NULL;
static FILE	*purplelog = NULL;

static time_t oldestentry;


typedef struct logdata_t {
	/* hostname|testname|color|testflags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|1st line of message */
	char *hostname;
	char *testname;
	int  color;
	char *testflags;
	time_t lastchange;
	time_t logtime;
	time_t validtime;
	time_t acktime;
	time_t disabletime;
	char *sender;
	int cookie;
	char *msg;
} logdata_t;

char *parse_testflags(char *l)
{
	char *result = NULL;
	char *flagstart = strstr(l, "[flags:");

	if (flagstart) {
		char *flagend;

		flagstart += 7;
		flagend = strchr(flagstart, ']');

		if (flagend) {
			*flagend = '\0';
			result = strdup(flagstart);
			*flagend = ']';
		}
	}

	return result;
}

int testflag_set(entry_t *e, char flag)
{
	if (e->testflags) 
		return (strchr(e->testflags, flag) != NULL);
	else
		return 0;
}


state_t *init_state(char *filename, logdata_t *log)
{
	FILE 		*fd = NULL;
	char		*p;
	char		*hostname;
	char		*testname;
	char		*testnameidx;
	state_t 	*newstate;
	char		fullfn[PATH_MAX];
	host_t		*host;
	struct stat 	log_st;
	time_t		now = getcurrenttime(NULL);
	time_t		histentry_start;
	int		logexpired = 0;

	statuscount++;
	dbgprintf("init_state(%s, %d, ...)\n", textornull(filename));

	/* Ignore summary files and dot-files (this catches "." and ".." also) */
	if ( (strncmp(filename, "summary.", 8) == 0) || (filename[0] == '.')) {
		return NULL;
	}

	if (reportstart || snapshot) {
		/* Dont do reports for info- and trends-columns */
		p = strrchr(filename, '.');
		if (p == NULL) return NULL;
		p++;

		if (strcmp(p, xgetenv("INFOCOLUMN")) == 0) return NULL;
		if (strcmp(p, xgetenv("TRENDSCOLUMN")) == 0) return NULL;

		/*
		 * When doing reports, we are scanning the BBHIST directory. It may
		 * contain files that are named as a host only (no test-name).
		 * Skip those.
		 */
		if (find_host(filename)) return NULL;
	}

	if (!reportstart && !snapshot) {
		hostname = strdup(log->hostname);
		testname = strdup(log->testname);
		logexpired = (log->validtime < now);
	}
	else {
		sprintf(fullfn, "%s/%s", xgetenv("BBHIST"), filename);

		/* Check that we can access this file */
		if ( (stat(fullfn, &log_st) == -1)       || 
		     (!S_ISREG(log_st.st_mode))            ||
		     ((fd = fopen(fullfn, "r")) == NULL)   ) {
			errprintf("Weird file '%s' skipped\n", fullfn);
			return NULL;
		}

		/* Pick out host- and test-name */
		logexpired = (log_st.st_mtime < now);
		hostname = strdup(filename);
		p = strrchr(hostname, '.');

		/* Skip files that have no '.' in filename */
		if (p) {
			/* Pick out the testname ... */
			*p = '\0'; p++;
			testname = strdup(p);
	
			/* ... and change hostname back into normal form */
			for (p=hostname; (*p); p++) {
				if (*p == ',') *p='.';
			}
		}
		else {
			xfree(hostname);
			fclose(fd);
			return NULL;
		}
	}

	testnameidx = (char *)malloc(strlen(testname) + 3);
	sprintf(testnameidx, ",%s,", testname);
	if (ignorecolumns && strstr(ignorecolumns, testnameidx)) {
		xfree(hostname);
		xfree(testname);
		xfree(testnameidx);
		if (fd) fclose(fd);
		return NULL;	/* Ignore this type of test */
	}
	xfree(testnameidx);

	host = find_host(hostname);

	newstate = (state_t *) calloc(1, sizeof(state_t));
	newstate->entry = (entry_t *) calloc(1, sizeof(entry_t));
	newstate->next = NULL;

	newstate->entry->column = find_or_create_column(testname, 1);
	newstate->entry->color = -1;
	strcpy(newstate->entry->age, "");
	newstate->entry->oldage = 0;
	newstate->entry->propagate = 1;
	newstate->entry->testflags = NULL;
	newstate->entry->skin = NULL;
	newstate->entry->repinfo = NULL;
	newstate->entry->causes = NULL;
	newstate->entry->histlogname = NULL;
	newstate->entry->shorttext = NULL;

	if (host) {
		newstate->entry->alert = checkalert(host->alerts, testname);

		/* If no WAP's specified, default all tests to be on WAP page */
		newstate->entry->onwap = (host->waps ? checkalert(host->waps, testname) : 1);
	}
	else {
		dbgprintf("   hostname %s not found\n", hostname);
		newstate->entry->alert = newstate->entry->onwap = 0;
	}

	newstate->entry->sumurl = NULL;

	if (reportstart) {
		/* Determine "color" for this test from the historical data */
		newstate->entry->repinfo = (reportinfo_t *) calloc(1, sizeof(reportinfo_t));
		newstate->entry->color = parse_historyfile(fd, newstate->entry->repinfo, 
				(dynamicreport ? NULL: hostname), (dynamicreport ? NULL : testname), 
				reportstart, reportend, 0, 
				(host ? host->reportwarnlevel : reportwarnlevel), 
				reportgreenlevel,
				(host ? host->reporttime : NULL));
		newstate->entry->causes = (dynamicreport ? NULL : save_replogs());
	}
	else if (snapshot) {
		time_t fileage = snapshot - histentry_start;

		newstate->entry->color = history_color(fd, snapshot, &histentry_start, &newstate->entry->histlogname);

		newstate->entry->oldage = (fileage >= recentgif_limit);
		newstate->entry->fileage = fileage;
		strcpy(newstate->entry->age, agestring(fileage));
	}
	else {
		time_t fileage = (now - log->lastchange);

		newstate->entry->color = log->color;
		newstate->entry->testflags = strdup(log->testflags);
		if (testflag_set(newstate->entry, 'D')) newstate->entry->skin = dialupskin;
		if (testflag_set(newstate->entry, 'R')) newstate->entry->skin = reverseskin;
		newstate->entry->shorttext = strdup(log->msg);
		newstate->entry->acked = (log->acktime > now);

		newstate->entry->oldage = (fileage >= recentgif_limit);
		newstate->entry->fileage = (log->lastchange ? fileage : -1);
		if (log->lastchange == 0)
			strcpy(newstate->entry->age, "");
		else 
			strcpy(newstate->entry->age, agestring(fileage));
	}

	if (purplelog && (newstate->entry->color == COL_PURPLE)) {
		purplecount++;
		fprintf(purplelog, "%s %s%s\n", 
		       hostname, testname, (host ? " (expired)" : " (unknown host)"));
	}


	newstate->entry->propagate = checkpropagation(host, testname, newstate->entry->color, newstate->entry->acked);

	dbgprintf("init_state: hostname=%s, testname=%s, color=%d, acked=%d, age=%s, oldage=%d, propagate=%d, alert=%d\n",
		textornull(hostname), textornull(testname), 
		newstate->entry->color, newstate->entry->acked,
		textornull(newstate->entry->age), newstate->entry->oldage,
		newstate->entry->propagate, newstate->entry->alert);

	if (host) {
        	hostlist_t *l;

		/* Add this state entry to the host's list of state entries. */
		newstate->entry->next = host->entries;
		host->entries = newstate->entry;

		/* There may be multiple host entries, if a host is
		 * listed in several locations in bb-hosts (for display purposes).
		 * This is handled by updating ALL of the cloned host records.
		 * Bug reported by Bluejay Adametz of Fuji.
		 */

		/* Cannot use "find_host()" here, as we need the hostlink record, not the host record */
		l = find_hostlist(hostname);

		/* Walk through the clone-list and set the "entries" for all hosts */
		for (l=l->clones; (l); l = l->clones) l->hostentry->entries = host->entries;
	}
	else {
		/* No host for this test - must be missing from bb-hosts */
		newstate->entry->next = NULL;
	}

	xfree(hostname);
	xfree(testname);
	if (fd) fclose(fd);

	return newstate;
}

dispsummary_t *init_displaysummary(char *fn, logdata_t *log)
{
	char l[MAX_LINE_LEN];
	dispsummary_t *newsum = NULL;
	time_t now = getcurrenttime(NULL);

	dbgprintf("init_displaysummary(%s)\n", textornull(fn));

	if (log->validtime < now) return NULL;
	strcpy(l, log->msg);

	if (strlen(l)) {
		char *p;
		char *color = (char *) malloc(strlen(l));

		newsum = (dispsummary_t *) calloc(1, sizeof(dispsummary_t));
		newsum->url = (char *) malloc(strlen(l));

		if (sscanf(l, "%s %s", color, newsum->url) == 2) {
			char *rowcol;
			newsum->color = parse_color(color);

			rowcol = (char *) malloc(strlen(fn) + 1);
			strcpy(rowcol, fn+8);
			p = strrchr(rowcol, '.');
			if (p) *p = ' ';

			newsum->column = (char *) malloc(strlen(rowcol)+1);
			newsum->row = (char *) malloc(strlen(rowcol)+1);
			sscanf(rowcol, "%s %s", newsum->row, newsum->column);
			newsum->next = NULL;

			xfree(rowcol);
		}
		else {
			xfree(newsum->url);
			xfree(newsum);
			newsum = NULL;
		}

		xfree(color);
	}

	return newsum;
}

state_t *load_state(dispsummary_t **sumhead)
{
	int hobbitdresult;
	char		fn[PATH_MAX];
	state_t		*newstate, *topstate;
	dispsummary_t	*newsum, *topsum;
	char 		*board = NULL;
	char		*nextline;
	int		done;
	logdata_t	log;
	sendreturn_t	*sres;

	dbgprintf("load_state()\n");

	sres = newsendreturnbuf(1, NULL);

	if (!reportstart && !snapshot) {
		char *dumpfn = getenv("BOARDDUMP");

		if (dumpfn) {
			/* Debugging - read data from a dump file */
			struct stat st;
			FILE *fd;

			hobbitdresult = BB_ETIMEOUT;
			if (stat(dumpfn, &st) == 0) {
				fd = fopen(dumpfn, "r");
				if (fd) {
					board = (char *)malloc(st.st_size + 1);
					fread(board, 1, st.st_size, fd);
					fclose(fd);
					hobbitdresult = BB_OK;
				}
			}
		}
		else {
			hobbitdresult = sendmessage("hobbitdboard fields=hostname,testname,color,flags,lastchange,logtime,validtime,acktime,disabletime,sender,cookie,line1,acklist", NULL, BBTALK_TIMEOUT, sres);
			board = getsendreturnstr(sres, 1);
		}
	}
	else {
		hobbitdresult = sendmessage("hobbitdboard fields=hostname,testname", NULL, BBTALK_TIMEOUT, sres);
		board = getsendreturnstr(sres, 1);
	}

	freesendreturnbuf(sres);

	if ((hobbitdresult != BB_OK) || (board == NULL) || (*board == '\0')) {
		errprintf("hobbitd status-board not available, code %d\n", hobbitdresult);
		return NULL;
	}

	if (reportstart || snapshot) {
		oldestentry = getcurrenttime(NULL);
		purplelog = NULL;
		purplelogfn = NULL;
	}
	else {
		if (purplelogfn) {
			purplelog = fopen(purplelogfn, "w");
			if (purplelog == NULL) errprintf("Cannot open purplelog file %s\n", purplelogfn);
			else fprintf(purplelog, "Stale (purple) logfiles as of %s\n\n", timestamp);
		}
	}

	topstate = NULL;
	topsum = NULL;

	done = 0; nextline = board;
	while (!done) {
		char *bol = nextline;
		char *onelog, *acklist;
		char *p;
		int i;

		nextline = strchr(nextline, '\n');
		if (nextline) { *nextline = '\0'; nextline++; } else done = 1;

		if (strlen(bol) == 0) {
			done = 1;
			continue;
		}

		memset(&log, 0, sizeof(log));
		onelog = strdup(bol);
		acklist = NULL;
		p = gettok(onelog, "|"); i = 0;
		while (p) {
			switch (i) {
			  /* hostname|testname|color|testflags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|1st line of message */
			  case  0: log.hostname = p; break;
			  case  1: log.testname = p; break;
			  case  2: log.color = parse_color(p); break;
			  case  3: log.testflags = p; break;
			  case  4: log.lastchange = atoi(p); break;
			  case  5: log.logtime = atoi(p); break;
			  case  6: log.validtime = atoi(p); break;
			  case  7: log.acktime = atoi(p); break;
			  case  8: log.disabletime = atoi(p); break;
			  case  9: log.sender = p; break;
			  case 10: log.cookie = atoi(p); break;
			  case 11: log.msg = p; break;
			  case 12: acklist = p; break;
			}

			p = gettok(NULL, "|");
			i++;
		}
		if (!log.msg) log.msg = "";
		sprintf(fn, "%s.%s", commafy(log.hostname), log.testname);

		/* Get the data */
		if (strncmp(fn, "summary.", 8) == 0) {
			if (!reportstart && !snapshot) {
				newsum = init_displaysummary(fn, &log);
				if (newsum) {
					newsum->next = topsum;
					topsum = newsum;
				}
			}
		}
		else {
			if (acklist && *acklist) {
				/*
				 * It's been acked. acklist looks like
				 * 1149489234:1149510834:1:henrik:Joe promised to take care of this right after lunch\n
				 * The "\n" is the delimiter between multiple acks.
				 */
				char *tok;

				tok = strtok(acklist, ":");
				if (tok) tok = strtok(NULL, ":");
				if (tok) log.acktime = atol(tok);
			}
			newstate = init_state(fn, &log);
			if (newstate) {
				newstate->next = topstate;
				topstate = newstate;
				if (reportstart && (newstate->entry->repinfo->reportstart < oldestentry)) {
					oldestentry = newstate->entry->repinfo->reportstart;
				}
			}
		}
		xfree(onelog);
	}

	if (reportstart) sethostenv_report(oldestentry, reportend, reportwarnlevel, reportgreenlevel);
	if (purplelog) fclose(purplelog);

	*sumhead = topsum;
	return topstate;
}

