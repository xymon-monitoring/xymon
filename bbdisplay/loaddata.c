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
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: loaddata.c,v 1.121 2004-08-02 13:21:27 henrik Exp $";

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
#include "sendmsg.h"
#include "loadhosts.h"
#include "loaddata.h"
#include "reportdata.h"
#include "larrdgen.h"
#include "infogen.h"
#include "debug.h"

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
			result = malcop(flagstart);
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


state_t *init_state(const char *filename, int dopurple, int *is_purple)
{
	FILE 		*fd;
	char		*p;
	char		*hostname;
	char		*testname;
	state_t 	*newstate;
	char		l[MAXMSG];
	char		fullfn[MAX_PATH];
	host_t		*host;
	struct stat 	log_st;
	time_t		now = time(NULL);
	time_t		histentry_start;

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
		if (!enable_larrdgen && ((strcmp(p, "larrd") == 0) || (strcmp(p, "trends") == 0))) {
			return NULL;
		}
	}

	sprintf(fullfn, "%s/%s", getenv(((reportstart || snapshot) ? "BBHIST" : "BBLOGS")), filename);

	/* Check that we can access this file */
	if ( (stat(fullfn, &log_st) == -1)       || 
	     (!S_ISREG(log_st.st_mode))            ||
	     ((fd = fopen(fullfn, "r")) == NULL)   ) {
		errprintf("Weird file %s/%s skipped\n", fullfn);
		return NULL;
	}

	/* Pick out host- and test-name */
	hostname = malcop(filename);
	p = strrchr(hostname, '.');

	/* Skip files that have no '.' in filename */
	if (p) {
		/* Pick out the testname ... */
		*p = '\0'; p++;
		testname = malcop(p);

		/* ... and change hostname back into normal form */
		for (p=hostname; (*p); p++) {
			if (*p == ',') *p='.';
		}
	}
	else {
		free(hostname);
		fclose(fd);
		return NULL;
	}

	sprintf(l, ",%s,", testname);
	if (ignorecolumns && strstr(ignorecolumns, l)) {
		free(hostname);
		free(testname);
		fclose(fd);
		return NULL;	/* Ignore this type of test */
	}

	host = find_host(hostname);

	/* If the host is a modem-bank host, dont mix in normal status messages */
	if (host && (host->banksize > 0)) {
		errprintf("Modembank %s has additional status-logs - ignored\n", hostname);
		return NULL;
	}

	newstate = (state_t *) malloc(sizeof(state_t));
	newstate->entry = (entry_t *) malloc(sizeof(entry_t));
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
		newstate->entry->color = history_color(fd, snapshot, &histentry_start, &newstate->entry->histlogname);
	}
	else if (fgets(l, sizeof(l), fd)) {
		newstate->entry->color = parse_color(l);
		newstate->entry->testflags = parse_testflags(l);
		if (testflag_set(newstate->entry, 'D')) newstate->entry->skin = dialupskin;
		if (testflag_set(newstate->entry, 'R')) newstate->entry->skin = reverseskin;
	}
	else if (!enable_larrdgen && ((strcmp(testname, "larrd") == 0) || (strcmp(testname, "trends") == 0))) {
		/* 
		 * Unreadable LARRD file without us doing larrd -->
		 * it's the standard larrd-html.pl script building
		 * files while we run. Don't complain about these,
		 * just assume they are green.
		 * Spotted by Tom Schmidt.
		 */
		newstate->entry->color = COL_GREEN;
	}
	else {
		errprintf("Empty or unreadable status file %s/%s\n", ((reportstart || snapshot) ? "BBHIST" : "BBLOGS"), filename);
		newstate->entry->color = COL_CLEAR;
	}

	if ( !reportstart && !snapshot && (log_st.st_mtime <= now) && (strcmp(testname, larrdcol) != 0) && (strcmp(testname, infocol) != 0) ) {
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
	if (!reportstart && !snapshot && (newstate->entry->color != COL_GREEN)) {
		struct stat ack_st;
		char ackfilename[MAX_PATH];

		/*
		 * ACK's are named by the client alias, if that exists.
		 */
		sprintf(ackfilename, "%s/ack.%s.%s", getenv("BBACKS"), 
			(host->clientalias ? host->clientalias : host->hostname), testname);
		newstate->entry->acked = (stat(ackfilename, &ack_st) == 0);
	}
	else {
		newstate->entry->acked = 0;
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
	else if (dopurple && *is_purple) {
		/* Send a message to update status to purple */

		char *p;
		char *purplemsg;
		int bufleft = log_st.st_size + 1024;

		init_status(newstate->entry->color);

		for (p = strchr(l, ' '); (p && (*p == ' ')); p++); /* Skip old color */

		purplemsg = (char *) malloc(bufleft);
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
				getenv("BBHOME"));
			strcat(purplemsg, l);

			sprintf(l, "purple message, please delete the log files for this host located in\n");
			strcat(purplemsg, l);

			sprintf(l, "%s, %s and %s if this host is no longer monitored.\n",
				getenv("BBLOGS"), getenv("BBHIST"), getenv("BBHISTLOGS"));
			strcat(purplemsg, l);
		}

		addtostatus(purplemsg);
		free(purplemsg);
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

	free(hostname);
	free(testname);
	fclose(fd);

	return newstate;
}

dispsummary_t *init_displaysummary(char *fn)
{
	FILE *fd;
	char sumfn[MAX_PATH];
	struct stat st;
	char l[MAX_LINE_LEN];
	dispsummary_t *newsum = NULL;

	dprintf("init_displaysummary(%s)\n", textornull(fn));

	sprintf(sumfn, "%s/%s", getenv("BBLOGS"), fn);

	/* Check that we can access this file */
	if ( (stat(sumfn, &st) == -1)          || 
	     (!S_ISREG(st.st_mode))            ||     /* Not a regular file */
	     ((fd = fopen(sumfn, "r")) == NULL)   ) {
		errprintf("Weird summary file BBLOGS/%s skipped\n", fn);
		return NULL;
	}

	if (st.st_mtime < time(NULL)) {
		/* Stale summary file - ignore and delete */
		errprintf("Stale summary file BBLOGS/%s - deleted\n", fn);
		unlink(sumfn);
		return NULL;
	}

	if (fgets(l, sizeof(l), fd)) {
		char *p, *rowcol;
		char *color = (char *) malloc(strlen(l));

		newsum = (dispsummary_t *) malloc(sizeof(dispsummary_t));
		newsum->url = (char *) malloc(strlen(l));

		sscanf(l, "%s %s", color, newsum->url);

		if (strncmp(color, "green", 5) == 0) {
			newsum->color = COL_GREEN;
		}
		else if (strncmp(color, "yellow", 6) == 0) {
			newsum->color = COL_YELLOW;
		}
		else if (strncmp(color, "red", 3) == 0) {
			newsum->color = COL_RED;
		}
		else if (strncmp(color, "blue", 4) == 0) {
			newsum->color = COL_BLUE;
		}
		else if (strncmp(color, "clear", 5) == 0) {
			newsum->color = COL_CLEAR;
		}
		else if (strncmp(color, "purple", 6) == 0) {
			newsum->color = COL_PURPLE;
		}

		rowcol = (char *) malloc(strlen(fn) + 1);
		strcpy(rowcol, fn+8);
		p = strrchr(rowcol, '.');
		if (p) *p = ' ';

		newsum->column = (char *) malloc(strlen(rowcol)+1);
		newsum->row = (char *) malloc(strlen(rowcol)+1);
		sscanf(rowcol, "%s %s", newsum->row, newsum->column);
		newsum->next = NULL;

		free(color);
		free(rowcol);
	}
	else {
		errprintf("Read error reading from file %s\n", sumfn);
		newsum = NULL;
	}


	fclose(fd);
	return newsum;
}

void init_modembank_status(char *fn)
{
	FILE *fd;
	char statusfn[MAX_PATH];
	struct stat st;
	char l[MAXMSG];
	host_t *targethost;

	dprintf("init_modembank_status(%s)\n", textornull(fn));

	sprintf(statusfn, "%s/%s", getenv("BBLOGS"), fn);

	/* Check that we can access this file */
	if ( (stat(statusfn, &st) == -1)          || 
	     (!S_ISREG(st.st_mode))            ||     /* Not a regular file */
	     ((fd = fopen(statusfn, "r")) == NULL)   ) {
		errprintf("Weird modembank/dialup logfile BBLOGS/%s skipped\n", fn);
		return;
	}

	if (st.st_mtime < time(NULL)) {
		/* Stale summary file - ignore and delete */
		errprintf("Stale modembank summary file BBLOGS/%s - deleted\n", fn);
		fclose(fd);
		unlink(statusfn);
		return;
	}

	targethost = find_host(fn+strlen("dialup."));
	if (targethost == NULL) {
		dprintf("Modembank status from unknown host %s - ignored\n", fn+strlen("dialup."));
		fclose(fd);
		return;
	}

	if (fgets(l, sizeof(l), fd)) {
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

	fclose(fd);
}


state_t *load_state(dispsummary_t **sumhead)
{
	DIR		*bblogs;
	struct dirent 	*d;
	char		fn[MAX_PATH];
	state_t		*newstate, *topstate;
	dispsummary_t	*newsum, *topsum;
	int		dopurple;
	struct stat	st;
	int		purplecount = 0;
	int		is_purple;

	dprintf("load_state()\n");

	if (chdir(getenv("BBLOGS")) != 0) {
		errprintf("Cannot access the BBLOGS directory %s\n", getenv("BBLOGS"));
		return NULL;
	}

	if (reportstart || snapshot) {
		dopurple = 0;
		purplelog = NULL;
		oldestentry = time(NULL);
	}
	else {
		if (stat(".bbstartup", &st) == -1) {
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
				remove(".bbstartup");
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

	bblogs = opendir(getenv("BBLOGS"));
	if (!bblogs) {
		errprintf("No logs! Cannot read the BBLOGS directory %s\n", getenv("BBLOGS"));
		return NULL;
	}

	while ((d = readdir(bblogs))) {
		strcpy(fn, d->d_name);

		if (strncmp(fn, "summary.", 8) == 0) {
			if (!reportstart && !snapshot) {
				newsum = init_displaysummary(fn);
				if (newsum) {
					newsum->next = topsum;
					topsum = newsum;
				}
			}
		}
		else if (strncmp(fn, "dialup.", 7) == 0) {
			init_modembank_status(fn);
		}
		else {
			is_purple = 0;

			newstate = init_state(fn, dopurple, &is_purple);
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
					errprintf("%s : Too many purple updates (>%d) - disabling updates for purple logs\n", 
						timestamp, MAX_PURPLE_PER_RUN);
				}
			}
		}
	}

	closedir(bblogs);

	if (reportstart) sethostenv_report(oldestentry, reportend, reportwarnlevel, reportgreenlevel);
	if (dopurple) combo_end();
	if (purplelog) fclose(purplelog);

	*sumhead = topsum;
	return topstate;
}

