/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module                                                      */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_client.c,v 1.58 2006-04-15 09:37:25 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>

#include "libbbgen.h"
#include "hobbitd_worker.h"
#include "client_config.h"

#define MAX_META 20	/* The maximum number of meta-data items in a message */


typedef struct sectlist_t {
	char *sname;
	char *sdata;
	struct sectlist_t *next;
} sectlist_t;
sectlist_t *sections = NULL;
int pslistinprocs = 1;
int sendclearmsgs = 1;
int localmode     = 0;

void splitmsg(char *clientdata)
{
	char *cursection, *nextsection;
	char *sectname, *sectdata;

	/* Free the old list */
	if (sections) {
		sectlist_t *swalk, *stmp;

		swalk = sections;
		while (swalk) {
			stmp = swalk;
			swalk = swalk->next;
			xfree(stmp);
		}

		sections = NULL;
	}

	if (clientdata == NULL) {
		errprintf("Got a NULL client data message\n");
		return;
	}

	/* Find the start of the first section */
	if (*clientdata == '[') 
		cursection = clientdata; 
	else {
		cursection = strstr(clientdata, "\n[");
		if (cursection) cursection++;
	}

	while (cursection) {
		sectlist_t *newsect = (sectlist_t *)malloc(sizeof(sectlist_t));

		/* Find end of this section (i.e. start of the next section, if any) */
		nextsection = strstr(cursection, "\n[");
		if (nextsection) {
			*nextsection = '\0';
			nextsection++;
		}

		/* Pick out the section name and data */
		sectname = cursection+1;
		sectdata = sectname + strcspn(sectname, "]\n");
		*sectdata = '\0'; sectdata++; if (*sectdata == '\n') sectdata++;

		/* Save the pointers in the list */
		newsect->sname = sectname;
		newsect->sdata = sectdata;
		newsect->next = sections;
		sections = newsect;

		/* Next section, please */
		cursection = nextsection;
	}
}

char *getdata(char *sectionname)
{
	sectlist_t *swalk;

	for (swalk = sections; (swalk && strcmp(swalk->sname, sectionname)); swalk = swalk->next) ;
	if (swalk) return swalk->sdata;

	return NULL;
}

int linecount(char *msg)
{
	int result = 0;
	char *nl;

	if (!msg) return 0;

	nl = msg - 1;
	while (nl) {
		nl++; if (*nl >= ' ') result++;
		nl = strchr(nl, '\n');
	}

	return result;
}

void unix_cpu_report(char *hostname, namelist_t *hinfo, char *fromline, char *timestr, 
		     char *uptimestr, char *whostr, char *psstr, char *topstr)
{
	char *p;
	float load1, load5, load15;
	float loadyellow, loadred;
	int recentlimit, ancientlimit;
	char loadresult[100];
	long uptimesecs = -1;
	char myupstr[100];

	int cpucolor = COL_GREEN;

	char msgline[4096];
	strbuffer_t *upmsg;

	if (!uptimestr) return;

	p = strstr(uptimestr, " up ");
	if (p) {
		char *uptimeresult;
		char *daymark;
		char *hourmark;
		long uphour, upmin, upsecs;
		uptimesecs = 0;

		uptimeresult = strdup(p+3);

		/* 
		 * Linux: " up 178 days,  9:15,"
		 * BSD: ""
		 * Solaris: " up 21 days 20:58,"
		 */
		daymark = strstr(uptimeresult, " day");
		dprintf("CPU check host %s: daymark '%s'\n", hostname, daymark);

		if (daymark) {
			uptimesecs = atoi(uptimeresult) * 86400;
			if (strncmp(daymark, " days ", 6) == 0) {
				hourmark = daymark + 6;
			}
			else {
				hourmark = strchr(daymark, ',');
				if (hourmark) hourmark++; else hourmark = "";
			}
		}
		else {
			hourmark = uptimeresult;
		}

		hourmark += strspn(hourmark, " ");
		dprintf("CPU check host %s: hourmark '%s'\n", hostname, hourmark);
		if (sscanf(hourmark, "%ld:%ld", &uphour, &upmin) == 2) {
			uptimesecs += 60*(60*uphour + upmin);
		}
		else if (sscanf(hourmark, "%ld hours %ld mins", &uphour, &upmin) == 2) {
			uptimesecs += 60*(60*uphour + upmin);
		}
		else if (strstr(hourmark, " secs") && (sscanf(hourmark, "%ld secs", &upsecs) == 1)) {
			uptimesecs += upsecs;
		}
		else if (strstr(hourmark, "min") && (sscanf(hourmark, "%ld min", &upmin) == 1)) {
			uptimesecs += 60*upmin;
		}
		else if (strncmp(hourmark, "1 hr", 4) == 0) {
			uptimesecs = 3600;
		}
		else {
			uptimesecs = -1;
		}

		xfree(uptimeresult);
	}

	if (uptimesecs != -1) {
		int days = (uptimesecs / 86400);
		int hours = (uptimesecs % 86400) / 3600;
		int mins = (uptimesecs % 3600) / 60;

		if (days) sprintf(myupstr, "up: %d days", days);
		else sprintf(myupstr, "up: %02d:%02d", hours, mins);
	}
	else *myupstr = '\0';

	*loadresult = '\0';
	p = strstr(uptimestr, "load average: ");
	if (!p) p = strstr(uptimestr, "load averages: "); /* Many BSD's */
	if (p) {
		p = strchr(p, ':') + 1; p += strspn(p, " ");
		if ((sscanf(p, "%f, %f, %f", &load1, &load5, &load15) == 3) ||
		    (sscanf(p, "%f %f %f", &load1, &load5, &load15) == 3)) {
			sprintf(loadresult, "%.2f", load5);
		}
	}

	get_cpu_thresholds(hinfo, &loadyellow, &loadred, &recentlimit, &ancientlimit);

	upmsg = newstrbuffer(0);

	if (load5 > loadred) {
		cpucolor = COL_RED;
		addtobuffer(upmsg, "&red Load is CRITICAL\n");
	}
	else if (load5 > loadyellow) {
		cpucolor = COL_YELLOW;
		addtobuffer(upmsg, "&yellow Load is HIGH\n");
	}

	if ((uptimesecs != -1) && (recentlimit != -1) && (uptimesecs < recentlimit)) {
		if (cpucolor == COL_GREEN) cpucolor = COL_YELLOW;
		addtobuffer(upmsg, "&yellow Machine recently rebooted\n");
	}
	if ((uptimesecs != -1) && (ancientlimit != -1) && (uptimesecs > ancientlimit)) {
		if (cpucolor == COL_GREEN) cpucolor = COL_YELLOW;
		sprintf(msgline, "&yellow Machine has been up more than %d days\n", (ancientlimit / 86400));
		addtobuffer(upmsg, msgline);
	}

	init_status(cpucolor);
	sprintf(msgline, "status %s.cpu %s %s %s, %d users, %d procs, load=%s\n",
		commafy(hostname), colorname(cpucolor), 
		(timestr ? timestr : "<no timestamp data>"), 
		myupstr, 
		(whostr ? linecount(whostr) : 0), 
		(psstr ? linecount(psstr)-1 : 0), 
		loadresult);
	addtostatus(msgline);
	if (STRBUFLEN(upmsg)) {
		addtostrstatus(upmsg);
		addtostatus("\n");
	}
	if (topstr) {
		addtostatus("\n");
		addtostatus(topstr);
	}

	if (fromline && !localmode) addtostatus(fromline);
	finish_status();

	freestrbuffer(upmsg);
}


void unix_disk_report(char *hostname, namelist_t *hinfo, char *fromline, char *timestr, 
		      char *capahdr, char *mnthdr, char *dfstr)
{
	int diskcolor = COL_GREEN;

	int dchecks = 0;
	int capacol = -1;
	int mntcol  = -1;
	char *p, *bol, *nl;
	char msgline[4096];
	strbuffer_t *monmsg;
	char *dname;
	int dmin, dmax, dcount, dcolor;

	if (!dfstr) return;

	dprintf("Disk check host %s\n", hostname);

	monmsg = newstrbuffer(0);
	dchecks = clear_disk_counts(hinfo);

	bol = (dchecks ? dfstr : NULL);	/* No need to go through it if no disk checks defined */
	while (bol) {
		char *fsname = NULL, *usestr = NULL;

		nl = strchr(bol, '\n'); if (nl) *nl = '\0';

		if ((capacol == -1) && (mntcol == -1)) {
			/* First line: Check the header and find the columns we want */
			p = strdup(bol);
			capacol = selectcolumn(p, capahdr);
			strcpy(p, bol);
			mntcol = selectcolumn(p, mnthdr);
			xfree(p);
			dprintf("Disk check: header '%s', columns %d and %d\n", bol, capacol, mntcol);

			if ((capacol == -1) && (mntcol == -1)) {
				diskcolor = COL_YELLOW;
				sprintf(msgline, "&red Expected string (%s and %s) not found in df output header\n", 
					capahdr, mnthdr);
				addtobuffer(monmsg, msgline);
				nl = bol = NULL; /* Abandon loop */
			}
		}
		else {
			int absolutes;
			unsigned long usage, warnlevel, paniclevel;

			p = strdup(bol); usestr = getcolumn(p, capacol);
			if (usestr && isdigit((int)*usestr)) usage = atoi(usestr); else usage = -1;
			strcpy(p, bol); fsname = getcolumn(p, mntcol);
			if (fsname) add_disk_count(fsname);

			if (fsname && (usage != -1)) {
				get_disk_thresholds(hinfo, fsname, &warnlevel, &paniclevel, &absolutes);

				dprintf("Disk check: FS='%s' usage %lu (thresholds: %lu/%lu, abs: %d)\n",
					fsname, usage, warnlevel, paniclevel, absolutes);

				if (usage >= paniclevel) {
					if (diskcolor < COL_RED) diskcolor = COL_RED;
					sprintf(msgline, "&red %s (%lu) has reached the PANIC level (%lu %c)\n",
						fsname, usage, paniclevel,
						((absolutes & 1) ? 'K' : '%'));
					addtobuffer(monmsg, msgline);
				}
				else if (usage >= warnlevel) {
					if (diskcolor < COL_YELLOW) diskcolor = COL_YELLOW;
					sprintf(msgline, "&yellow %s (%lu) has reached the WARNING level (%lu %c)\n",
						fsname, usage, warnlevel,
						((absolutes & 2) ? 'K' : '%'));
					addtobuffer(monmsg, msgline);
				}
			}

			xfree(p);
		}

		if (nl) { *nl = '\n'; bol = nl+1; } else bol = NULL;
	}

	/* Check for filesystems that must (not) exist */
	while ((dname = check_disk_count(&dcount, &dmin, &dmax, &dcolor)) != NULL) {
		char limtxt[1024];

		*limtxt = '\0';

		if (dmax == -1) {
			if (dmin > 0) sprintf(limtxt, "%d or more", dmin);
			else if (dmin == 0) sprintf(limtxt, "none");
		}
		else {
			if (dmin > 0) sprintf(limtxt, "between %d and %d", dmin, dmax);
			else if (dmin == 0) sprintf(limtxt, "at most %d", dmax);
		}

		if (dcolor != COL_GREEN) {
			if (dcolor > diskcolor) diskcolor = dcolor;
			sprintf(msgline, "&%s Filesystem %s (found %d, req. %s)\n", 
				colorname(dcolor), dname, dcount, limtxt);
			addtobuffer(monmsg, msgline);
		}
	}

	/* Now we know the result, so generate a status message */
	init_status(diskcolor);
	sprintf(msgline, "status %s.disk %s %s - Filesystems %s\n",
		commafy(hostname), colorname(diskcolor), 
		(timestr ? timestr : "<No timestamp data>"), 
		((diskcolor == COL_GREEN) ? "OK" : "NOT ok"));
	addtostatus(msgline);

	/* And add the info about what's wrong */
	if (STRBUFLEN(monmsg)) {
		addtostrstatus(monmsg);
		addtostatus("\n");
	}

	/* And the full df output */
	addtostatus(dfstr);

	if (fromline && !localmode) addtostatus(fromline);
	finish_status();

	freestrbuffer(monmsg);
}

void unix_memory_report(char *hostname, namelist_t *hinfo, char *fromline, char *timestr, 
			long memphystotal, long memphysused, long memactused,
			long memswaptotal, long memswapused)
{
	unsigned long memphyspct = 0, memswappct = 0, memactpct = 0;
	int physyellow, physred, swapyellow, swapred, actyellow, actred;

	int memorycolor = COL_GREEN, physcolor = COL_GREEN, swapcolor = COL_GREEN, actcolor = COL_GREEN;
	char *memorysummary = "OK";

	char msgline[4096];

	if (memphystotal == -1) return;
	if (memphysused  == -1) return;
	if (memswaptotal == -1) return;
	if (memswapused  == -1) return;

	get_memory_thresholds(hinfo, &physyellow, &physred, &swapyellow, &swapred, &actyellow, &actred);

	memphyspct = (memphystotal > 0) ? ((100 * memphysused) / memphystotal) : 0;
	if (memphyspct > physyellow) physcolor = COL_YELLOW;
	if (memphyspct > physred)    physcolor = COL_RED;

	memswappct = (memswaptotal > 0) ? ((100 * memswapused) / memswaptotal) : 0;
	if (memswappct > swapyellow) swapcolor = COL_YELLOW;
	if (memswappct > swapred)    swapcolor = COL_RED;

	if (memactused != -1) {
		memactpct = (memphystotal > 0) ? ((100 * memactused) / memphystotal) : 0;
		if (memactpct  > actyellow)  actcolor  = COL_YELLOW;
		if (memactpct  > actred)     actcolor  = COL_RED;
	}

	if ((physcolor == COL_YELLOW) || (swapcolor == COL_YELLOW) || (actcolor == COL_YELLOW)) {
		memorycolor = COL_YELLOW;
		memorysummary = "low";
	}
	if ((physcolor == COL_RED) || (swapcolor == COL_RED) || (actcolor == COL_RED)) {
		memorycolor = COL_RED;
		memorysummary = "CRITICAL";
	}

	if ((memphystotal == 0) && (memorycolor == COL_GREEN)) {
		memorycolor = COL_YELLOW;
		memorysummary = "detection FAILED";
	}

	init_status(memorycolor);
	sprintf(msgline, "status %s.memory %s %s - Memory %s\n",
		commafy(hostname), colorname(memorycolor), 
		(timestr ? timestr : "<No timestamp data>"),
		memorysummary);
	addtostatus(msgline);

	sprintf(msgline, "   %-12s%12s%12s%12s\n", "Memory", "Used", "Total", "Percentage");
	addtostatus(msgline);

	sprintf(msgline, "&%s %-12s%11luM%11luM%11lu%%\n", 
		colorname(physcolor), "Physical", memphysused, memphystotal, memphyspct);
	addtostatus(msgline);

	if (memactused != -1) {
		sprintf(msgline, "&%s %-12s%11luM%11luM%11lu%%\n", 
			colorname(actcolor), "Actual", memactused, memphystotal, memactpct);
		addtostatus(msgline);
	}

	sprintf(msgline, "&%s %-12s%11luM%11luM%11lu%%\n", 
		colorname(swapcolor), "Swap", memswapused, memswaptotal, memswappct);
	addtostatus(msgline);
	if (fromline && !localmode) addtostatus(fromline);
	finish_status();
}

void unix_procs_report(char *hostname, namelist_t *hinfo, char *fromline, char *timestr, 
		       char *cmdhdr, char *altcmdhdr, char *psstr)
{
	int pscolor = COL_GREEN;

	int pchecks;
	int cmdofs = -1;
	char *p;
	char msgline[4096];
	strbuffer_t *monmsg;

	if (!psstr) return;

	monmsg = newstrbuffer(0);

	/* 
	 * Find where the command is located. We look for the header for the command,
	 * and calculate the offset from the beginning of the line.
	 */
	p = strstr(psstr, cmdhdr);
	if ((p == NULL) && (altcmdhdr != NULL)) p = strstr(psstr, altcmdhdr);
	if (p) cmdofs = (p - psstr);

	pchecks = clear_process_counts(hinfo);

	if (pchecks == 0) {
		/* Nothing to check */
		addtobuffer(monmsg, "&green No process checks defined\n");
	}
	else if (cmdofs >= 0) {
		/* Count how many instances of each monitored process is running */
		char *pname, *bol, *nl;
		int pcount, pmin, pmax, pcolor;

		bol = psstr;
		while (bol) {
			nl = strchr(bol, '\n'); if (nl) *nl = '\0';

			add_process_count(bol+cmdofs);

			if (nl) { *nl = '\n'; bol = nl+1; } else bol = NULL;
		}

		/* Check the number found for each monitored process */
		while ((pname = check_process_count(&pcount, &pmin, &pmax, &pcolor)) != NULL) {
			char limtxt[1024];

			if (pmax == -1) {
				if (pmin > 0) sprintf(limtxt, "%d or more", pmin);
				else if (pmin == 0) sprintf(limtxt, "none");
			}
			else {
				if (pmin > 0) sprintf(limtxt, "between %d and %d", pmin, pmax);
				else if (pmin == 0) sprintf(limtxt, "at most %d", pmax);
			}

			if (pcolor == COL_GREEN) {
				sprintf(msgline, "&green %s (found %d, req. %s)\n", pname, pcount, limtxt);
				addtobuffer(monmsg, msgline);
			}
			else {
				if (pcolor > pscolor) pscolor = pcolor;
				sprintf(msgline, "&%s %s (found %d, req. %s)\n", 
					colorname(pcolor), pname, pcount, limtxt);
				addtobuffer(monmsg, msgline);
			}
		}
	}
	else {
		pscolor = COL_YELLOW;
		sprintf(msgline, "&yellow Expected string %s not found in ps output header\n", cmdhdr);
		addtobuffer(monmsg, msgline);
	}

	/* Now we know the result, so generate a status message */
	init_status(pscolor);
	sprintf(msgline, "status %s.procs %s %s - Processes %s\n",
		commafy(hostname), colorname(pscolor), 
		(timestr ? timestr : "<No timestamp data>"), 
		((pscolor == COL_GREEN) ? "OK" : "NOT ok"));
	addtostatus(msgline);

	/* And add the info about what's wrong */
	if (STRBUFLEN(monmsg)) {
		addtostrstatus(monmsg);
		addtostatus("\n");
	}

	/* And the full ps output for those who want it */
	if (pslistinprocs) addtostatus(psstr);

	if (fromline && !localmode) addtostatus(fromline);
	finish_status();

	freestrbuffer(monmsg);
}

static void old_msgs_report(char *hostname, namelist_t *hinfo, char *fromline, char *timestr, char *msgsstr)
{
	int msgscolor = COL_GREEN;
	char msgline[4096];
	char *summary = "All logs OK";

	if (msgsstr) {
		if (strstr(msgsstr, "&clear ")) { msgscolor = COL_CLEAR; summary = "No log data available"; }
		if (strstr(msgsstr, "&yellow ")) { msgscolor = COL_YELLOW; summary = "WARNING"; }
		if (strstr(msgsstr, "&red ")) { msgscolor = COL_RED; summary = "CRITICAL"; }
	}
	else if (sendclearmsgs) {
		msgscolor = COL_CLEAR; summary = "No log data available";
	}
	else 
		return;

	init_status(msgscolor);
	sprintf(msgline, "status %s.msgs %s System logs at %s : %s\n",
		commafy(hostname), colorname(msgscolor), 
		(timestr ? timestr : "<No timestamp data>"), 
		summary);
	addtostatus(msgline);

	if (msgsstr)
		addtostatus(msgsstr);
	else
		addtostatus("The client did not report any logfile data\n");

	if (fromline && !localmode) addtostatus(fromline);
	finish_status();
}

void msgs_report(char *hostname, namelist_t *hinfo, char *fromline, char *timestr, char *msgsstr)
{
	static strbuffer_t *greendata = NULL;
	static strbuffer_t *yellowdata = NULL;
	static strbuffer_t *reddata = NULL;
	sectlist_t *swalk;
	strbuffer_t *logsummary;
	int msgscolor = COL_GREEN;
	char msgline[PATH_MAX];
	char sectionname[PATH_MAX];

	for (swalk = sections; (swalk && strncmp(swalk->sname, "msgs:", 5)); swalk = swalk->next) ;

	if (!swalk) {
		old_msgs_report(hostname, hinfo, fromline, timestr, msgsstr);
		return;
	}

	if (!greendata) greendata = newstrbuffer(0);
	if (!yellowdata) yellowdata = newstrbuffer(0);
	if (!reddata) reddata = newstrbuffer(0);

	logsummary = newstrbuffer(0);

	while (swalk) {
		int logcolor;

		clearstrbuffer(logsummary);
		sprintf(sectionname, "msgs:%s", swalk->sname+5);
		logcolor = scan_log(hinfo, swalk->sname+5, swalk->sdata, sectionname, logsummary);
		if (logcolor > msgscolor) msgscolor = logcolor;

		switch (logcolor) {
		  case COL_GREEN:
			sprintf(msgline, "\nNo entries in <a href=\"%s\">%s</a>\n", 
				hostsvcclienturl(hostname, sectionname), swalk->sname+5);
			addtobuffer(greendata, msgline);
			break;

		  case COL_YELLOW: 
			sprintf(msgline, "\nWarnings in <a href=\"%s\">%s</a>\n", 
				hostsvcclienturl(hostname, sectionname), swalk->sname+5);
			addtobuffer(yellowdata, msgline);
			addtostrbuffer(yellowdata, logsummary);
			break;

		  case COL_RED:
			sprintf(msgline, "\nCritical entries in <a href=\"%s\">%s</a>\n", 
				hostsvcclienturl(hostname, sectionname), swalk->sname+5);
			addtobuffer(reddata, msgline);
			addtostrbuffer(reddata, logsummary);
			break;
		}

		do { swalk=swalk->next; } while (swalk && strncmp(swalk->sname, "msgs:", 5));
	}

	freestrbuffer(logsummary);

	init_status(msgscolor);
	sprintf(msgline, "status %s.msgs %s System logs at %s\n",
		commafy(hostname), colorname(msgscolor), 
		(timestr ? timestr : "<No timestamp data>"));
	addtostatus(msgline);

	if (STRBUFLEN(reddata)) {
		addtostrstatus(reddata);
		clearstrbuffer(reddata);
		addtostatus("\n");
	}

	if (STRBUFLEN(yellowdata)) {
		addtostrstatus(yellowdata);
		clearstrbuffer(yellowdata);
		addtostatus("\n");
	}

	if (STRBUFLEN(greendata)) {
		addtostrstatus(greendata);
		clearstrbuffer(greendata);
		addtostatus("\n");
	}

	/* 
	 * Add the full log message data from each logfile.
	 * It's probably faster to re-walk the section list than
	 * stuffing the full messages into a temporary buffer.
	 */
	for (swalk = sections; (swalk && strncmp(swalk->sname, "msgs:", 5)); swalk = swalk->next) ;
	while (swalk) {
		sprintf(msgline, "\nFull log <a href=\"%s\">%s</a>\n", 
			hostsvcclienturl(hostname, sectionname), swalk->sname+5);
		addtostatus(msgline);
		addtostatus(swalk->sdata);
		do { swalk=swalk->next; } while (swalk && strncmp(swalk->sname, "msgs:", 5));
	}

	if (fromline && !localmode) addtostatus(fromline);

	finish_status();
}

void file_report(char *hostname, namelist_t *hinfo, char *fromline, char *timestr)
{
	static strbuffer_t *greendata = NULL;
	static strbuffer_t *yellowdata = NULL;
	static strbuffer_t *reddata = NULL;
	static strbuffer_t *sizedata = NULL;
	sectlist_t *swalk;
	strbuffer_t *filesummary;
	int filecolor = COL_GREEN, onecolor;
	char msgline[PATH_MAX];
	char sectionname[PATH_MAX];
	int anyszdata = 0;

	if (!greendata) greendata = newstrbuffer(0);
	if (!yellowdata) yellowdata = newstrbuffer(0);
	if (!reddata) reddata = newstrbuffer(0);
	if (!sizedata) sizedata = newstrbuffer(0);

	filesummary = newstrbuffer(0);

	sprintf(msgline, "data %s.filesizes\n", commafy(hostname));
	addtobuffer(sizedata, msgline);

	for (swalk = sections; (swalk); swalk = swalk->next) {
		unsigned long sz;
		int trackit;

		if (strncmp(swalk->sname, "file:", 5) == 0) {
			sprintf(sectionname, "file:%s", swalk->sname+5);
			onecolor = check_file(hinfo, swalk->sname+5, swalk->sdata, sectionname, filesummary, &sz, &trackit);

			if (trackit) {
				/* Save the size data for later DATA message to track file sizes */
				sprintf(msgline, "%s:%lu\n", swalk->sname+5, sz);
				addtobuffer(sizedata, msgline);
				anyszdata = 1;
			}
		}
		else if (strncmp(swalk->sname, "logfile:", 8) == 0) {
			sprintf(sectionname, "logfile:%s", swalk->sname+8);
			onecolor = check_file(hinfo, swalk->sname+5, swalk->sdata, sectionname, filesummary, &sz, &trackit);
		}
		else if (strncmp(swalk->sname, "dir:", 4) == 0) {
			sprintf(sectionname, "dir:%s", swalk->sname+4);
			onecolor = check_dir(hinfo, swalk->sname+4, swalk->sdata, sectionname, filesummary, &sz, &trackit);

			if (trackit) {
				/* Save the size data for later DATA message to track directory sizes */
				sprintf(msgline, "%s:%lu\n", swalk->sname+4, sz);
				addtobuffer(sizedata, msgline);
				anyszdata = 1;
			}
		}
		else continue;

		if (onecolor > filecolor) filecolor = onecolor;

		switch (onecolor) {
		  case COL_GREEN:
			sprintf(msgline, "\n&green <a href=\"%s\">%s</a>\n", 
				hostsvcclienturl(hostname, sectionname), swalk->sname+5);
			addtobuffer(greendata, msgline);
			break;

		  case COL_YELLOW: 
			sprintf(msgline, "\n&yellow <a href=\"%s\">%s</a>\n", 
				hostsvcclienturl(hostname, sectionname), swalk->sname+5);
			addtobuffer(yellowdata, msgline);
			addtostrbuffer(yellowdata, filesummary);
			break;

		  case COL_RED:
			sprintf(msgline, "\n&red <a href=\"%s\">%s</a>\n", 
				hostsvcclienturl(hostname, sectionname), swalk->sname+5);
			addtobuffer(reddata, msgline);
			addtostrbuffer(reddata, filesummary);
			break;
		}

		clearstrbuffer(filesummary);
	}

	freestrbuffer(filesummary);

	init_status(filecolor);
	sprintf(msgline, "status %s.files %s Files status at %s\n",
		commafy(hostname), colorname(filecolor), 
		(timestr ? timestr : "<No timestamp data>"));
	addtostatus(msgline);

	if (STRBUFLEN(reddata)) {
		addtostrstatus(reddata);
		clearstrbuffer(reddata);
		addtostatus("\n");
	}

	if (STRBUFLEN(yellowdata)) {
		addtostrstatus(yellowdata);
		clearstrbuffer(yellowdata);
		addtostatus("\n");
	}

	if (STRBUFLEN(greendata)) {
		addtostrstatus(greendata);
		clearstrbuffer(greendata);
		addtostatus("\n");
	}

	if (fromline && !localmode) addtostatus(fromline);

	finish_status();

	if (anyszdata) sendmessage(STRBUF(sizedata), NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
	clearstrbuffer(sizedata);
}

void unix_netstat_report(char *hostname, namelist_t *hinfo, char *osid, char *netstatstr)
{
	strbuffer_t *msg;
	char msgline[4096];

	if (!netstatstr) return;

	msg = newstrbuffer(0);
	sprintf(msgline, "data %s.netstat\n%s\n", commafy(hostname), osid);
	addtobuffer(msg, msgline);
	addtobuffer(msg, netstatstr);
	sendmessage(STRBUF(msg), NULL, NULL, NULL, 0, BBTALK_TIMEOUT);

	freestrbuffer(msg);
}

void unix_ifstat_report(char *hostname, namelist_t *hinfo, char *osid, char *ifstatstr)
{
	strbuffer_t *msg;
	char msgline[4096];

	if (!ifstatstr) return;

	msg = newstrbuffer(0);
	sprintf(msgline, "data %s.ifstat\n%s\n", commafy(hostname), osid);
	addtobuffer(msg, msgline);
	addtobuffer(msg, ifstatstr);
	sendmessage(STRBUF(msg), NULL, NULL, NULL, 0, BBTALK_TIMEOUT);

	freestrbuffer(msg);
}

void unix_vmstat_report(char *hostname, namelist_t *hinfo, char *osid, char *vmstatstr)
{
	strbuffer_t *msg;
	char msgline[4096];

	char *p;

	if (!vmstatstr) return;

	p = strrchr(vmstatstr, '\n');
	if (!p) return;  /* No NL in vmstat output ? Unlikely. */

	msg = newstrbuffer(0);
	if (strlen(p) == 1) {
		/* Go back to the previous line */
		do { p--; } while ((p > vmstatstr) && (*p != '\n'));
	}
	sprintf(msgline, "data %s.vmstat\n%s\n", commafy(hostname), osid);
	addtobuffer(msg, msgline);
	addtobuffer(msg, p+1);
	sendmessage(STRBUF(msg), NULL, NULL, NULL, 0, BBTALK_TIMEOUT);

	freestrbuffer(msg);
}

#include "client/linux.c"
#include "client/freebsd.c"
#include "client/netbsd.c"
#include "client/openbsd.c"
#include "client/solaris.c"
#include "client/hpux.c"
#include "client/osf.c"
#include "client/aix.c"
#include "client/darwin.c"

static volatile int reloadconfig = 0;

void sig_handler(int signum)
{
	switch (signum) {
	  case SIGHUP:
		reloadconfig = 1;
		break;
	  default:
		break;
	}
}

void clean_instr(char *s)
{
	char *p;

	p = s + strcspn(s, "\r\n");
	*p = '\0';
}

int main(int argc, char *argv[])
{
	char *msg;
	int running;
	int argi, seq;
	struct sigaction sa;
	struct timeval *timeout = NULL;
	time_t nextconfigload = 0;
	char *configfn = NULL;

	/* Handle program options. */
	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[argi], "--no-ps-listing") == 0) {
			pslistinprocs = 0;
		}
		else if (strcmp(argv[argi], "--no-clear-msgs") == 0) {
			sendclearmsgs = 0;
		}
		else if (argnmatch(argv[argi], "--config=")) {
			char *lp = strchr(argv[argi], '=');
			configfn = strdup(lp+1);
		}
		else if (argnmatch(argv[argi], "--dump-config")) {
			load_client_config(configfn);
			dump_client_config();
			return 0;
		}
		else if (strcmp(argv[argi], "--local") == 0) {
			localmode = 1;
		}
		else if (strcmp(argv[argi], "--test") == 0) {
			namelist_t *hinfo, *oldhinfo = NULL;
			char hostname[1024];
			char s[4096];
			int cfid;

			load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
			load_client_config(configfn);
			*hostname = '\0';

			while (1) {
				hinfo = NULL;
				while (!hinfo) {
					printf("Hostname (.=end, ?=dump, !=reload) [%s]: ", hostname); 
					fflush(stdout); fgets(hostname, sizeof(hostname), stdin);
					clean_instr(hostname);

					if (strlen(hostname) == 0) {
						hinfo = oldhinfo;
						strcpy(hostname, bbh_item(hinfo, BBH_HOSTNAME));
					}
					else if (strcmp(hostname, ".") == 0) 
						return 0;
					else if (strcmp(hostname, "!") == 0) {
						load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
						load_client_config(configfn);
						*hostname = '\0';
					}
					else if (strcmp(hostname, "?") == 0) {
						dump_client_config();
						if (oldhinfo) strcpy(hostname, bbh_item(oldhinfo, BBH_HOSTNAME));
					}
					else {
						hinfo = hostinfo(hostname);
						if (!hinfo) printf("Unknown host\n");
					}
				}
				oldhinfo = hinfo;

				printf("Test (cpu, mem, disk, proc): "); fflush(stdout); 
				fgets(s, sizeof(s), stdin); clean_instr(s);
				if (strcmp(s, "cpu") == 0) {
					float loadyellow, loadred;
					int recentlimit, ancientlimit;
	
					cfid = get_cpu_thresholds(hinfo, &loadyellow, &loadred, &recentlimit, &ancientlimit);

					printf("Load: Yellow at %.2f, red at %.2f\n", loadyellow, loadred);
					printf("Uptime: From boot until %s,", durationstring(recentlimit));
					printf("and after %s uptime\n", durationstring(ancientlimit));
				}
				else if (strcmp(s, "mem") == 0) {
					int physyellow, physred, swapyellow, swapred, actyellow, actred;

					get_memory_thresholds(hinfo, &physyellow, &physred, 
							&swapyellow, &swapred, &actyellow, &actred);
					printf("Phys: Yellow at %d, red at %d\n", physyellow, physred);
					printf("Swap: Yellow at %d, red at %d\n", swapyellow, swapred);
					printf("Act.: Yellow at %d, red at %d\n", actyellow, actred);
				}
				else if (strcmp(s, "disk") == 0) {
					unsigned long warnlevel, paniclevel;
					int absolutes;

					printf("Filesystem: "); fflush(stdout);
					fgets(s, sizeof(s), stdin); clean_instr(s);
					cfid = get_disk_thresholds(hinfo, s, &warnlevel, &paniclevel, &absolutes);
					printf("Yellow at %lu%c, red at %lu%c\n", 
						warnlevel, ((absolutes & 1) ? 'K' : '%'),
						paniclevel, ((absolutes & 2) ? 'K' : '%'));
				}
				else if (strcmp(s, "proc") == 0) {
					int pchecks = clear_process_counts(hinfo);
					char *pname;
					int pcount, pmin, pmax, pcolor;
					FILE *fd;

					if (pchecks == 0) {
						printf("No process checks for this host\n");
						continue;
					}

					do {
						printf("ps command string: "); fflush(stdout);
						fgets(s, sizeof(s), stdin); clean_instr(s);
						if (*s == '@') {
							fd = fopen(s+1, "r");
							while (fd && fgets(s, sizeof(s), fd)) {
								clean_instr(s);
								if (*s) add_process_count(s);
							}
							fclose(fd);
						}
						else {
							if (*s) add_process_count(s);
						}
					} while (*s);

					while ((pname = check_process_count(&pcount, &pmin, &pmax, &pcolor)) != NULL) {
						printf("Process %s color %s: Count=%d, min=%d, max=%d\n",
							pname, colorname(pcolor), pcount, pmin, pmax);
					}
				}
			}
		}
	}

	/* Signals */
	setup_signalhandler("hobbitd_client");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGHUP, &sa, NULL);
	signal(SIGCHLD, SIG_IGN);

	save_errbuf = 0;
	running = 1;

	while (running) {
		char *eoln, *restofmsg, *p;
		char *metadata[MAX_META+1];
		int metacount;

		msg = get_hobbitd_message(C_CLIENT, argv[0], &seq, timeout);
		if (msg == NULL) {
			running = 0;
			continue;
		}

		if (reloadconfig || (time(NULL) >= nextconfigload)) {
			nextconfigload = time(NULL) + 600;
			reloadconfig = 0;
			if (!localmode) load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
			load_client_config(configfn);
		}

		/* Split the message in the first line (with meta-data), and the rest */
 		eoln = strchr(msg, '\n');
		if (eoln) {
			*eoln = '\0';
			restofmsg = eoln+1;
		}
		else {
			restofmsg = "";
		}

		metacount = 0; 
		p = gettok(msg, "|");
		while (p && (metacount < MAX_META)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;

		if ((metacount > 4) && (strncmp(metadata[0], "@@client", 8) == 0)) {
			time_t timestamp = atoi(metadata[1]);
			char *sender = metadata[2];
			char *hostname = metadata[3];
			char *clienttype = metadata[4];
			enum ostype_t os;
			namelist_t *hinfo = NULL;

			hinfo = (localmode ? localhostinfo(hostname) : hostinfo(hostname));
			if (!hinfo) continue;
			os = get_ostype(clienttype);

			switch (os) {
			  case OS_FREEBSD: 
				handle_freebsd_client(hostname, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_NETBSD: 
				handle_netbsd_client(hostname, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_OPENBSD: 
				handle_openbsd_client(hostname, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_LINUX22: 
			  case OS_LINUX: 
			  case OS_RHEL3: 
				handle_linux_client(hostname, os, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_DARWIN:
				handle_darwin_client(hostname, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_SOLARIS: 
				handle_solaris_client(hostname, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_HPUX: 
				handle_hpux_client(hostname, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_OSF: 
				handle_osf_client(hostname, os, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_AIX: 
				handle_aix_client(hostname, os, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_IRIX:
			  case OS_WIN32: 
			  case OS_SNMP: 
			  case OS_UNKNOWN:
				errprintf("No client backend for OS '%s' sent by %s\n", clienttype, sender);
				break;
			}
		}
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			printf("Shutting down\n");
			running = 0;
			continue;
		}
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("HOBBITCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				freopen(fn, "a", stdout);
				freopen(fn, "a", stderr);
			}
			continue;
		}
		else {
			/* Unknown message - ignore it */
		}
	}

	return 0;
}

