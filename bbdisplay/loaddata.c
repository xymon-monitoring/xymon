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
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: loaddata.c,v 1.139 2005-01-18 22:25:59 henrik Exp $";

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
#include "reportdata.h"

int		statuscount = 0;

char		*ignorecolumns = NULL;			/* Columns that will be ignored totally */
char		*dialupskin = NULL;			/* BBSKIN used for dialup tests */
char		*reverseskin = NULL;			/* BBSKIN used for reverse tests */

bbgen_col_t   	null_column = { "", NULL };		/* Null column */

/* Items controlling handling of purple statuses. */
int		enable_purpleupd = 1;
int		purpledelay = 0;			/* Lifetime of purple status-messages. Default 0 for
							   compatibility with standard bb-display.sh behaviour */
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
			result = xstrdup(flagstart);
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


state_t *init_state(const char *filename, logdata_t *log, int dopurple, int *is_purple)
{
	FILE 		*fd = NULL;
	char		*p;
	char		*hostname;
	char		*testname;
	state_t 	*newstate;
	char		l[MAXMSG];
	char		fullfn[PATH_MAX];
	host_t		*host;
	struct stat 	log_st;
	time_t		now = time(NULL);
	time_t		histentry_start;
	int		logexpired = 0;

	statuscount++;
	dprintf("init_state(%s, %d, ...)\n", textornull(filename), dopurple);

	*is_purple = 0;

	/* Ignore summary files and dot-files (this catches "." and ".." also) */
	if ( (strncmp(filename, "summary.", 8) == 0) || (filename[0] == '.')) {
		return NULL;
	}

	if (reportstart || snapshot) {
		/* Dont do reports for info- and larrd-columns */
		p = strrchr(filename, '.');
		if (p == NULL) return NULL;
		p++;
		if (strcmp(p, infocol) == 0) return NULL;
		if (strcmp(p, larrdcol) == 0) return NULL;

		/* 
		 * We may not be running with --larrd; in that case
		 * larrdcol is the default ("larrd" or "trends").
		 * Avoid stumbling over those.
		 * From Tom Schmidt.
		 */
		if ((strcmp(p, "larrd") == 0) || (strcmp(p, "trends") == 0)) {
			return NULL;
		}

		/*
		 * When doing reports, we are scanning the BBHIST directory. It may
		 * contain files that are named as a host only (no test-name).
		 * Skip those.
		 */
		if (find_host(filename)) return NULL;
	}

	if (usehobbitd && !(reportstart || snapshot)) {
		hostname = xstrdup(log->hostname);
		testname = xstrdup(log->testname);
		logexpired = (log->validtime < now);
	}
	else {
		sprintf(fullfn, "%s/%s", xgetenv(((reportstart || snapshot) ? "BBHIST" : "BBLOGS")), filename);

		/* Check that we can access this file */
		if ( (stat(fullfn, &log_st) == -1)       || 
		     (!S_ISREG(log_st.st_mode))            ||
		     ((fd = fopen(fullfn, "r")) == NULL)   ) {
			errprintf("Weird file '%s' skipped\n", fullfn);
			return NULL;
		}

		/* Pick out host- and test-name */
		logexpired = (log_st.st_mtime < now);
		hostname = xstrdup(filename);
		p = strrchr(hostname, '.');

		/* Skip files that have no '.' in filename */
		if (p) {
			/* Pick out the testname ... */
			*p = '\0'; p++;
			testname = xstrdup(p);
	
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

	sprintf(l, ",%s,", testname);
	if (ignorecolumns && strstr(ignorecolumns, l)) {
		xfree(hostname);
		xfree(testname);
		if (fd) fclose(fd);
		return NULL;	/* Ignore this type of test */
	}

	host = find_host(hostname);

	/* If the host is a modem-bank host, dont mix in normal status messages */
	if (host && (host->banksize > 0)) {
		errprintf("Modembank %s has additional status-logs - ignored\n", hostname);
		return NULL;
	}

	newstate = (state_t *) xmalloc(sizeof(state_t));
	newstate->entry = (entry_t *) xmalloc(sizeof(entry_t));
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
		dprintf("   hostname %s not found\n", hostname);
		newstate->entry->alert = newstate->entry->onwap = 0;
	}

	newstate->entry->sumurl = NULL;

	if (reportstart) {
		/* Determine "color" for this test from the historical data */
		newstate->entry->repinfo = (reportinfo_t *) xcalloc(1, sizeof(reportinfo_t));
		newstate->entry->color = parse_historyfile(fd, newstate->entry->repinfo, 
				(dynamicreport ? NULL: hostname), (dynamicreport ? NULL : testname), 
				reportstart, reportend, 0, 
				(host ? host->reportwarnlevel : reportwarnlevel), 
				reportgreenlevel,
				(host ? host->reporttime : NULL));
		newstate->entry->causes = (dynamicreport ? NULL : save_replogs());
	}
	else if (snapshot) {
		newstate->entry->color = history_color(fd, snapshot, &histentry_start, &newstate->entry->histlogname);
	}
	else if (usehobbitd) {
		newstate->entry->color = log->color;
		newstate->entry->testflags = xstrdup(log->testflags);
		if (testflag_set(newstate->entry, 'D')) newstate->entry->skin = dialupskin;
		if (testflag_set(newstate->entry, 'R')) newstate->entry->skin = reverseskin;
		newstate->entry->shorttext = xstrdup(log->msg);
	}
	else if (fgets(l, sizeof(l), fd)) {
		char *flags = parse_testflags(l);

		newstate->entry->color = parse_color(l);
		if (flags) {
			newstate->entry->testflags = xstrdup(flags);
			if (testflag_set(newstate->entry, 'D')) newstate->entry->skin = dialupskin;
			if (testflag_set(newstate->entry, 'R')) newstate->entry->skin = reverseskin;
		}
		newstate->entry->shorttext = xstrdup(l);
	}
	else if ((strcmp(testname, "larrd") == 0) || (strcmp(testname, "trends") == 0)) {
		/* 
		 * Unreadable LARRD file without us doing larrd -->
		 * it's another script building files while we run.
		 * Don't complain about these, just assume they are green.
		 * Spotted by Tom Schmidt.
		 */
		newstate->entry->color = COL_GREEN;
	}
	else {
		errprintf("Empty or unreadable status file %s/%s\n", ((reportstart || snapshot) ? "BBHIST" : "BBLOGS"), filename);
		newstate->entry->color = COL_CLEAR;
	}

	if ( !reportstart && !snapshot && logexpired && (strcmp(testname, larrdcol) != 0) && (strcmp(testname, infocol) != 0) ) {
		/* Log file too old = go purple */

		if (host && host->dialup) {
			/* Dialup hosts go clear, not purple */
			newstate->entry->color = COL_CLEAR;
		}
		else {
			/* Not in bb-hosts, or logfile too old */
			newstate->entry->color = COL_PURPLE;
			*is_purple = 1;
			purplecount++;
			if (purplelog) fprintf(purplelog, "%s %s%s\n", 
					       hostname, testname, (host ? " (expired)" : " (unknown host)"));
		}
	}

	/* Acked column ? */
	if (usehobbitd && !reportstart && !snapshot) {
		newstate->entry->acked = (log->acktime > now);
	}
	else {
		if (!reportstart && !snapshot && host && (newstate->entry->color != COL_GREEN)) {
			struct stat ack_st;
			char ackfilename[PATH_MAX];

			/*
			 * ACK's are named by the client alias, if that exists.
			 */
			sprintf(ackfilename, "%s/ack.%s.%s", xgetenv("BBACKS"), 
				(host->clientalias ? host->clientalias : host->hostname), testname);
			newstate->entry->acked = (stat(ackfilename, &ack_st) == 0);
		}
		else {
			newstate->entry->acked = 0;
		}
	}

	newstate->entry->propagate = checkpropagation(host, testname, newstate->entry->color, newstate->entry->acked);

	if (reportstart) {
		/* Reports have no purple handling */
	}
	else if (snapshot) {
		time_t fileage = snapshot - histentry_start;

		newstate->entry->oldage = (fileage >= 86400);
		if (fileage >= 86400)
			sprintf(newstate->entry->age, "%.2f days", (fileage / 86400.0));
		else if (fileage > 3600)
			sprintf(newstate->entry->age, "%.2f hours", (fileage / 3600.0));
		else
			sprintf(newstate->entry->age, "%.2f minutes", (fileage / 60.0));
	}
	else if (usehobbitd) {
		time_t fileage = (now - log->lastchange);

		newstate->entry->oldage = (fileage >= 86400);
		if (fileage >= 86400)
			sprintf(newstate->entry->age, "%.2f days", (fileage / 86400.0));
		else if (fileage > 3600)
			sprintf(newstate->entry->age, "%.2f hours", (fileage / 3600.0));
		else
			sprintf(newstate->entry->age, "%.2f minutes", (fileage / 60.0));
	}
	else if (dopurple && *is_purple) {
		/* Send a message to update status to purple */

		char *p;
		char *purplemsg;
		int bufleft = log_st.st_size + 1024;

		init_status(newstate->entry->color);

		for (p = strchr(l, ' '); (p && (*p == ' ')); p++); /* Skip old color */

		purplemsg = (char *) xmalloc(bufleft);
		sprintf(purplemsg, "status+%d %s.%s %s %s", purpledelay,
			commafy(hostname), testname,
                        colorname(newstate->entry->color), (p ? p : ""));
		bufleft -= strlen(purplemsg);

		if (host) {
			while (fgets(l, sizeof(l), fd)) {
				if (strncmp(l, "Status unchanged", 16) == 0) {
					char *p;

					p = strchr(l, '\n'); if (p) *p = '\0';
					strncat(newstate->entry->age, l+20, sizeof(newstate->entry->age)-1);
					newstate->entry->oldage = (strstr(l+20, "days") != NULL);
				}
				else if ( (strncmp(l, "Encrypted status message", 24) != 0)  &&
				          (strncmp(l, "Status message received from", 28) != 0) ) {
					strncat(purplemsg, l, bufleft);
				}
			}
			/* Avoid newlines piling up at end of logfile */
			for (p = purplemsg + strlen(purplemsg) - 1; 
				((p > purplemsg) && ((*p == '\n') || (*p == '\r')) ); p--) ;
			if (p>purplemsg) *(p+1) = '\0';
			strcat(purplemsg, "\n\n");
		}
		else {
			/* No longer in bb-hosts */
			sprintf(l, "%s\n\n", hostname);
			strcat(purplemsg, l);

			sprintf(l, "This entry is no longer listed in %s/etc/bb-hosts.  To remove this\n",
				xgetenv("BBHOME"));
			strcat(purplemsg, l);

			sprintf(l, "purple message, please delete the log files for this host located in\n");
			strcat(purplemsg, l);

			sprintf(l, "%s, %s and %s if this host is no longer monitored.\n",
				xgetenv("BBLOGS"), xgetenv("BBHIST"), xgetenv("BBHISTLOGS"));
			strcat(purplemsg, l);
		}

		addtostatus(purplemsg);
		xfree(purplemsg);
		finish_status();
	}
	else {
		if (*is_purple) {
			/* 
			 * dopurple is false, so we are not updating purple messages.
			 * That means we can use the age of the log file as an indicator
			 * for how old this status message really is.
			 */

			time_t fileage = (now - log_st.st_mtime);

			newstate->entry->oldage = (fileage >= 86400);
			if (fileage >= 86400)
				sprintf(newstate->entry->age, "%.2f days", (fileage / 86400.0));
			else if (fileage > 3600)
				sprintf(newstate->entry->age, "%.2f hours", (fileage / 3600.0));
			else
				sprintf(newstate->entry->age, "%.2f minutes", (fileage / 60.0));
		}
		else {
			if ((strcmp(testname, larrdcol) != 0) && (strcmp(testname, infocol) != 0)) {
				while (fgets(l, sizeof(l), fd) && (strncmp(l, "Status unchanged in ", 20) != 0)) ;

				if (strncmp(l, "Status unchanged in ", 20) == 0) {
					char *p;

					p = strchr(l, '\n'); if (p) *p = '\0';
					strncat(newstate->entry->age, l+20, sizeof(newstate->entry->age)-1);
					newstate->entry->oldage = (strstr(l+20, "days") != NULL);
				}
			}
			else {
				newstate->entry->oldage = 1;
			}
		}
	}

	dprintf("init_state: hostname=%s, testname=%s, color=%d, acked=%d, age=%s, oldage=%d, propagate=%d, alert=%d, *is_purple=%d\n",
		textornull(hostname), textornull(testname), 
		newstate->entry->color, newstate->entry->acked,
		textornull(newstate->entry->age), newstate->entry->oldage,
		newstate->entry->propagate, newstate->entry->alert, *is_purple);

	if (host) {
        	hostlist_t      *l;

		/* Add this state entry to the host's list of state entries. */
		newstate->entry->next = host->entries;
		host->entries = newstate->entry;

		/* There may be multiple host entries, if a host is
		 * listed in several locations in bb-hosts (for display purposes).
		 * This is handled by updating ALL of the cloned host records.
		 * Bug reported by Bluejay Adametz of Fuji.
		 */

		/* Cannot use "find_host()" here, as we need the hostlink record, not the host record */
		for (l=hosthead; (l && (strcmp(l->hostentry->hostname, host->hostname) != 0)); l=l->next);

		/* Walk through the clone-list and set the "entries" for all hosts */
		for (l=l->clones; (l); l = l->next) l->hostentry->entries = host->entries;
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
	time_t now = time(NULL);

	dprintf("init_displaysummary(%s)\n", textornull(fn));

	if (usehobbitd) {
		if (log->validtime < now) return NULL;
		strcpy(l, log->msg);
	}
	else {
		char sumfn[PATH_MAX];
		FILE *fd;
		struct stat st;

		sprintf(sumfn, "%s/%s", xgetenv("BBLOGS"), fn);

		/* Check that we can access this file */
		if ( (stat(sumfn, &st) == -1)          || 
		     (!S_ISREG(st.st_mode))            ||     /* Not a regular file */
		     ((fd = fopen(sumfn, "r")) == NULL)   ) {
			errprintf("Weird summary file BBLOGS/%s skipped\n", fn);
			return NULL;
		}

		if (st.st_mtime < now) {
			/* Stale summary file - ignore and delete */
			errprintf("Stale summary file BBLOGS/%s - deleted\n", fn);
			unlink(sumfn);
			return NULL;
		}

		if (fgets(l, sizeof(l), fd) == NULL) {
			errprintf("Read error reading from file %s\n", sumfn);
			return NULL;
		}

		fclose(fd);
	}

	if (strlen(l)) {
		char *p;
		char *color = (char *) xmalloc(strlen(l));

		newsum = (dispsummary_t *) xmalloc(sizeof(dispsummary_t));
		newsum->url = (char *) xmalloc(strlen(l));

		if (sscanf(l, "%s %s", color, newsum->url) == 2) {
			char *rowcol;
			newsum->color = parse_color(color);

			rowcol = (char *) xmalloc(strlen(fn) + 1);
			strcpy(rowcol, fn+8);
			p = strrchr(rowcol, '.');
			if (p) *p = ' ';

			newsum->column = (char *) xmalloc(strlen(rowcol)+1);
			newsum->row = (char *) xmalloc(strlen(rowcol)+1);
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

void init_modembank_status(char *fn, logdata_t *log)
{
	FILE *fd;
	char statusfn[PATH_MAX];
	struct stat st;
	char l[MAXMSG];
	host_t *targethost;
	time_t now = time(NULL);

	dprintf("init_modembank_status(%s)\n", textornull(fn));

	if (usehobbitd) {
		if (log->validtime < now) return;
		strcpy(l, log->msg);
	}
	else {
		sprintf(statusfn, "%s/%s", xgetenv("BBLOGS"), fn);

		/* Check that we can access this file */
		if ( (stat(statusfn, &st) == -1)          || 
		     (!S_ISREG(st.st_mode))            ||     /* Not a regular file */
		     ((fd = fopen(statusfn, "r")) == NULL)   ) {
			errprintf("Weird modembank/dialup logfile BBLOGS/%s skipped\n", fn);
			return;
		}

		if (st.st_mtime < now) {
			/* Stale summary file - ignore and delete */
			errprintf("Stale modembank summary file BBLOGS/%s - deleted\n", fn);
			fclose(fd);
			unlink(statusfn);
			return;
		}

		if (fgets(l, sizeof(l), fd) == NULL) {
			errprintf("Cannot read modembank logfile %s\n", fn);
			fclose(fd);
			return;
		}

		fclose(fd);
	}

	targethost = find_host(fn+strlen("dialup."));
	if (targethost == NULL) {
		dprintf("Modembank status from unknown host %s - ignored\n", fn+strlen("dialup."));
		return;
	}

	if (strlen(l)) {
		char *startip, *endip, *tag;
		int idx = -1;

		startip = endip = NULL;
		tag = strtok(l, " \n");
		while (tag) {
			if (idx >= 0) {
				/* Next result */
				if (idx < targethost->banksize) targethost->banks[idx] = parse_color(tag);
				idx++;
			}
			else if (strcmp(tag, "DATA") == 0) {
				if (startip && endip) idx = 0;
				else errprintf("Invalid modembank status logfile %s (missing FROM and/or TO)\n", fn);
			}
			else if (strcmp(tag, "FROM") == 0) {
				tag = strtok(NULL, " \n");

				if (tag) {
					startip = tag;
					if (strcmp(startip, targethost->ip) != 0) {
						errprintf("Modembank in bb-hosts begins with %s, but logfile begins with %s\n",
						  	targethost->ip, startip);
					}
				} else errprintf("Invalid modembank status logfile %s (truncated)\n", fn);
			}
			else if (strcmp(tag, "TO") == 0) {
				tag = strtok(NULL, " \n");

				if (tag) {
					if (startip) endip = tag;
					else errprintf("Invalid modembank status logfile %s (no FROM)\n", fn);
				} else errprintf("Invalid modembank status logfile %s (truncated)\n", fn);
			}

			if (tag) tag = strtok(NULL, " \n");
		}

		if ((idx >= 0) && (idx != targethost->banksize)) {
			errprintf("Modembank status log %s has more entries (%d) than expected (%d)\n", 
				  fn, (idx-1), targethost->banksize);
		}
	}
}


state_t *load_state(dispsummary_t **sumhead)
{
	DIR		*bblogs = NULL;
	struct dirent 	*d;
	char		fn[PATH_MAX];
	state_t		*newstate, *topstate;
	dispsummary_t	*newsum, *topsum;
	int		dopurple;
	struct stat	st;
	int		purplecount = 0;
	int		is_purple;
	char 		*board = NULL;
	char		*nextline;
	int		done;
	logdata_t	log;
	char		*logdir;

	logdir = xgetenv("BBLOGS");

	dprintf("load_state()\n");
	if (usehobbitd) {
		int hobbitdresult;

		if (!reportstart && !snapshot) {
			hobbitdresult = sendmessage("hobbitdboard", NULL, NULL, &board, 1, 30);
		}
		else {
			hobbitdresult = sendmessage("hobbitdlist", NULL, NULL, &board, 1, 30);
		}
		if ((hobbitdresult != BB_OK) || (board == NULL) || (*board == '\0')) {
			errprintf("hobbitd status-board not available\n");
			return NULL;
		}
	}
	else {
		if (chdir(logdir) != 0) {
			errprintf("Cannot access the logfile directory %s\n", logdir);
			return NULL;
		}

		bblogs = opendir(logdir);
		if (!bblogs) {
			errprintf("No logs! Cannot read the logfile directory %s\n", logdir);
			return NULL;
		}
	}

	if (reportstart || snapshot || usehobbitd) {
		dopurple = 0;
	}

	if (reportstart || snapshot) {
		oldestentry = time(NULL);
		purplelog = NULL;
		purplelogfn = NULL;
	}
	else {
		sprintf(fn, "%s/.bbstartup", logdir);
		if (stat(fn, &st) == -1) {
			/* Do purple if no ".bbstartup" file */
			dopurple = enable_purpleupd;
		}
		else {
			time_t now;

			/* Starting up - don't do purple hosts ("avoid purple explosion on startup") */
			dopurple = 0;

			/* Check if enough time has passed to remove the startup file */
			time(&now);
			if ((now - st.st_mtime) > 300) {
				remove(fn);
			}
		}

		if (purplelogfn) {
			purplelog = fopen(purplelogfn, "w");
			if (purplelog == NULL) errprintf("Cannot open purplelog file %s\n", purplelogfn);
			else fprintf(purplelog, "Stale (purple) logfiles as of %s\n\n", timestamp);
		}
		if (dopurple) combo_start();
	}

	topstate = NULL;
	topsum = NULL;

	done = 0; nextline = board;
	while (!done) {
		if (usehobbitd) {
			char *bol = nextline;
			char onelog[MAXMSG];
			char *p;
			int i;

			nextline = strchr(nextline, '\n');
			if (nextline) { *nextline = '\0'; nextline++; }

			if (strlen(bol) == 0) {
				done = 1;
				continue;
			}

			strcpy(onelog, bol);;
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
				}

				p = gettok(NULL, "|");
				i++;
			}
			sprintf(fn, "%s.%s", commafy(log.hostname), log.testname);
		}
		else {
			d = readdir(bblogs);
			if (d == NULL) {
				done = 1;
				continue;
			}

			strcpy(fn, d->d_name);
		}

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
		else if (strncmp(fn, "dialup.", 7) == 0) {
			init_modembank_status(fn, &log);
		}
		else {
			is_purple = 0;

			newstate = init_state(fn, &log, dopurple, &is_purple);
			if (newstate) {
				newstate->next = topstate;
				topstate = newstate;
				if (reportstart && (newstate->entry->repinfo->reportstart < oldestentry)) {
					oldestentry = newstate->entry->repinfo->reportstart;
				}
			}

			if (dopurple) {
				if (is_purple) purplecount++;
				if (purplecount > MAX_PURPLE_PER_RUN) {
					dopurple = 0;
					errprintf("Too many purple updates (>%d) - disabling updates for purple logs\n", 
						MAX_PURPLE_PER_RUN);
				}
			}
		}
	}

	if (bblogs) closedir(bblogs);

	if (reportstart) sethostenv_report(oldestentry, reportend, reportwarnlevel, reportgreenlevel);
	if (dopurple) combo_end();
	if (purplelog) fclose(purplelog);

	*sumhead = topsum;
	return topstate;
}

