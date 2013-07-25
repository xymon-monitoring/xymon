/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module                                                      */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
/* "PORT" handling (C) Mirko Saam                                             */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>

#include "libxymon.h"
#include "xymond_worker.h"
#include "client_config.h"

#define MAX_META 20	/* The maximum number of meta-data items in a message */

enum msgtype_t { MSG_CPU, MSG_DISK, MSG_INODE, MSG_FILES, MSG_MEMORY, MSG_MSGS, MSG_PORTS, MSG_PROCS, MSG_SVCS, MSG_WHO, MSG_LAST };

typedef struct sectlist_t {
	char *sname;
	char *sdata;
	char *nextsectionrestoreptr, *sectdatarestoreptr;
	char nextsectionrestoreval, sectdatarestoreval;
	struct sectlist_t *next;
} sectlist_t;
static sectlist_t *defsecthead = NULL;

int pslistinprocs = 1;
int portlistinports = 1;
int svclistinsvcs = 1;
int sendclearmsgs = 1;
int sendclearfiles = 1;
int sendclearports = 1;
int sendclearsvcs = 1;
int localmode     = 0;
int unknownclientosok = 0;
int noreportcolor = COL_CLEAR;

int usebackfeedqueue = 0;

typedef struct updinfo_t {
	char *hostname;
	time_t updtime;
	int updseq;
} updinfo_t;
static void * updinfotree;

int add_updateinfo(char *hostname, int seq, time_t tstamp)
{
	xtreePos_t handle;
	updinfo_t *itm;

	handle = xtreeFind(updinfotree, hostname);
	if (handle == xtreeEnd(updinfotree)) {
		itm = (updinfo_t *)calloc(1, sizeof(updinfo_t));
		itm->hostname = strdup(hostname);
		xtreeAdd(updinfotree, itm->hostname, itm);
	}
	else {
		itm = (updinfo_t *)xtreeData(updinfotree, handle);
	}

	if (itm->updtime == tstamp) {
		dbgprintf("%s: Duplicate client message at time %d, seq %d, lastseq %d\n", hostname, (int) tstamp, seq, itm->updseq);
		return 1;
	}

	itm->updtime = tstamp;
	itm->updseq = seq;
	return 0;
}

void nextsection_r_done(void *secthead)
{
	/* Free the old list */
	sectlist_t *swalk, *stmp;

	swalk = (sectlist_t *)secthead;
	while (swalk) {
		if (swalk->nextsectionrestoreptr) *swalk->nextsectionrestoreptr = swalk->nextsectionrestoreval;
		if (swalk->sectdatarestoreptr) *swalk->sectdatarestoreptr = swalk->sectdatarestoreval;

		stmp = swalk;
		swalk = swalk->next;
		xfree(stmp);
	}
}

void splitmsg_r(char *clientdata, sectlist_t **secthead)
{
	char *cursection, *nextsection;
	char *sectname, *sectdata;

	if (clientdata == NULL) {
		errprintf("Got a NULL client data message\n");
		return;
	}

	if (secthead == NULL) {
		errprintf("BUG: splitmsg_r called with NULL secthead\n");
		return;
	}

	if (*secthead) {
		errprintf("BUG: splitmsg_r called with non-empty secthead\n");
		nextsection_r_done(*secthead);
		*secthead = NULL;
	}

	/* Find the start of the first section */
	if (*clientdata == '[') 
		cursection = clientdata; 
	else {
		cursection = strstr(clientdata, "\n[");
		if (cursection) cursection++;
	}

	while (cursection) {
		sectlist_t *newsect = (sectlist_t *)calloc(1, sizeof(sectlist_t));

		/* Find end of this section (i.e. start of the next section, if any) */
		nextsection = strstr(cursection, "\n[");
		if (nextsection) {
			newsect->nextsectionrestoreptr = nextsection;
			newsect->nextsectionrestoreval = *nextsection;
			*nextsection = '\0';
			nextsection++;
		}

		/* Pick out the section name and data */
		sectname = cursection+1;
		sectdata = sectname + strcspn(sectname, "]\n");
		newsect->sectdatarestoreptr = sectdata;
		newsect->sectdatarestoreval = *sectdata;
		*sectdata = '\0'; 
		sectdata++; if (*sectdata == '\n') sectdata++;

		/* Save the pointers in the list */
		newsect->sname = sectname;
		newsect->sdata = sectdata;
		newsect->next = *secthead;
		*secthead = newsect;

		/* Next section, please */
		cursection = nextsection;
	}
}

void splitmsg_done(void)
{
	/*
	 * NOTE: This MUST be called when we're doing using a message,
	 * and BEFORE the next message is read. If called after the
	 * next message is read, the restore-pointers in the "defsecthead"
	 * list will point to data inside the NEW message, and 
	 * if the buffer-usage happens to be setup correctly, then
	 * this will write semi-random data over the new message.
	 */
	if (defsecthead) {
		/* Clean up after the previous message */
		nextsection_r_done(defsecthead);
		defsecthead = NULL;
	}
}

void splitmsg(char *clientdata)
{
	if (defsecthead) {
		errprintf("BUG: splitmsg_done() was not called on previous message - data corruption possible.\n");
		splitmsg_done();
	}

	splitmsg_r(clientdata, &defsecthead);
}

char *nextsection_r(char *clientdata, char **name, void **current, void **secthead)
{
	if (clientdata) {
		*secthead = NULL;
		splitmsg_r(clientdata, (sectlist_t **)secthead);
		*current = *secthead;
	}
	else {
		*current = (*current ? ((sectlist_t *)*current)->next : NULL);
	}

	if (*current) {
		*name = ((sectlist_t *)*current)->sname;
		return ((sectlist_t *)*current)->sdata;
	}

	return NULL;
}

char *nextsection(char *clientdata, char **name)
{
	static void *current = NULL;

	if (clientdata && defsecthead) {
		nextsection_r_done(defsecthead);
		defsecthead = NULL;
	}

	return nextsection_r(clientdata, name, &current, (void **)&defsecthead);
}


char *getdata(char *sectionname)
{
	sectlist_t *swalk;

	for (swalk = defsecthead; (swalk && strcmp(swalk->sname, sectionname)); swalk = swalk->next) ;
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

int want_msgtype(void *hinfo, enum msgtype_t msg)
{
	static void *currhost = NULL;
	static unsigned long currset = 0;

	if (currhost != hinfo) {
		char *val, *tok;

		currhost = hinfo;
		currset = 0;
		val = xmh_item(currhost, XMH_NOCOLUMNS);
		if (val) {
			val = strdup(val);
			tok = strtok(val, ",");
			while (tok) {
				if      (strcmp(tok, "cpu") == 0) currset |= (1 << MSG_CPU);
				else if (strcmp(tok, "disk") == 0) currset |= (1 << MSG_DISK);
				else if (strcmp(tok, "inode") == 0) currset |= (1 << MSG_INODE);
				else if (strcmp(tok, "files") == 0) currset |= (1 << MSG_FILES);
				else if (strcmp(tok, "memory") == 0) currset |= (1 << MSG_MEMORY);
				else if (strcmp(tok, "msgs") == 0) currset |= (1 << MSG_MSGS);
				else if (strcmp(tok, "ports") == 0) currset |= (1 << MSG_PORTS);
				else if (strcmp(tok, "procs") == 0) currset |= (1 << MSG_PROCS);
				else if (strcmp(tok, "svcs") == 0) currset |= (1 << MSG_SVCS);
				else if (strcmp(tok, "who") == 0) currset |= (1 << MSG_WHO);

				tok = strtok(NULL, ",");
			}
			xfree(val);
		}
	}

	return ((currset & (1 << msg)) == 0);
}

char *nocolon(char *txt)
{
	static char *result = NULL;
	char *p;
	/*
	 * This function changes all colons in "txt" to semi-colons.
	 * This is needed because some of the data messages we use 
	 * for reporting things like file- and directory-sizes, or
	 * linecounts in files, use a colon-delimited string that
	 * is sent to the RRD module, and this breaks for the Windows
	 * Powershell client where filenames may contain a colon.
	 */

	if (result) xfree(result);
	result = strdup(txt);

	p = result; while ((p = strchr(p, ':')) != NULL) *p = ';';
	return result;
}

void unix_cpu_report(char *hostname, char *clientclass, enum ostype_t os, 
		     void *hinfo, char *fromline, char *timestr, 
		     char *uptimestr, char *clockstr, char *msgcachestr,
		     char *whostr, int usercount, 
		     char *psstr, int pscount, char *topstr)
{
	char *p;
	float load1, load5, load15;
	float loadyellow, loadred;
	int recentlimit, ancientlimit, maxclockdiff, uptimecolor, clockdiffcolor;
	char loadresult[100];
	long uptimesecs = -1;
	char myupstr[100];

	int cpucolor = COL_GREEN;

	char msgline[4096];
	strbuffer_t *upmsg;

	if (!want_msgtype(hinfo, MSG_CPU)) return;

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
		dbgprintf("CPU check host %s: daymark '%s'\n", hostname, daymark);

		if (daymark) {
			uptimesecs = atoi(uptimeresult) * 86400;
			if (strncmp(daymark, " days ", 6) == 0) {
				hourmark = daymark + 6;
			}
			else if (strncmp(daymark, " day ", 5) == 0) {
				hourmark = daymark + 5;
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
		dbgprintf("CPU check host %s: hourmark '%s'\n", hostname, hourmark);
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

	load5 = 0.0;
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
	else {
		p = strstr(uptimestr, " load=");
		if (p) {
			char *lstart = p+6;
			char savech;

			p = lstart + strspn(lstart, "0123456789.");
			savech = *p; *p = '\0';
			load5 = atof(loadresult);
			strcpy(loadresult, lstart);
			*p = savech;
			if (savech == '%') strcat(loadresult, "%");
		}
	}

	get_cpu_thresholds(hinfo, clientclass, &loadyellow, &loadred, &recentlimit, &ancientlimit, &uptimecolor, &maxclockdiff, &clockdiffcolor);

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
		if (cpucolor != COL_RED) cpucolor = uptimecolor;
		sprintf(msgline, "&%s Machine recently rebooted\n", colorname(uptimecolor));
		addtobuffer(upmsg, msgline);
	}
	if ((uptimesecs != -1) && (ancientlimit != -1) && (uptimesecs > ancientlimit)) {
		if (cpucolor != COL_RED) cpucolor = uptimecolor;
		sprintf(msgline, "&%s Machine has been up more than %d days\n", 
			colorname(uptimecolor), (ancientlimit / 86400));
		addtobuffer(upmsg, msgline);
	}

	if (clockstr) {
		char *p;
		struct timeval clockval;

		p = strstr(clockstr, "epoch:"); 
		if (p && (sscanf(p, "epoch: %ld.%ld", (long int *)&clockval.tv_sec, (long int *)&clockval.tv_usec) == 2)) {
			struct timeval clockdiff;
			struct timezone tz;
			int cachedelay = 0;

			if (msgcachestr) {
				/* Message passed through msgcache, so adjust for the cache delay */
				p = strstr(msgcachestr, "Cachedelay:");
				if (p) cachedelay = atoi(p+11);
			}

			gettimeofday(&clockdiff, &tz);
			clockdiff.tv_sec -= (clockval.tv_sec + cachedelay);
			clockdiff.tv_usec -= clockval.tv_usec;
			if (clockdiff.tv_usec < 0) {
				clockdiff.tv_usec += 1000000;
				clockdiff.tv_sec -= 1;
			}

			if ((maxclockdiff > 0) && (abs(clockdiff.tv_sec) > maxclockdiff)) {
				if (cpucolor != COL_RED) cpucolor = clockdiffcolor;
				sprintf(msgline, "&%s System clock is %ld seconds off (max %ld)\n",
					colorname(clockdiffcolor), (long) clockdiff.tv_sec, (long) maxclockdiff);
				addtobuffer(upmsg, msgline);
			}
			else {
				sprintf(msgline, "System clock is %ld seconds off\n", (long) clockdiff.tv_sec);
				addtobuffer(upmsg, msgline);
			}
		}
	}

	init_status(cpucolor);
	sprintf(msgline, "status %s.cpu %s %s %s, %d users, %d procs, load=%s\n",
		commafy(hostname), colorname(cpucolor), 
		(timestr ? timestr : "<no timestamp data>"), 
		myupstr, 
		(whostr ? linecount(whostr) : usercount), 
		(psstr ? linecount(psstr)-1 : pscount), 
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


void unix_disk_report(char *hostname, char *clientclass, enum ostype_t os,
		      void *hinfo, char *fromline, char *timestr, 
		      char *freehdr, char *capahdr, char *mnthdr, char *dfstr)
{
	int diskcolor = COL_GREEN;

	int freecol = -1;
	int capacol = -1;
	int mntcol  = -1;
	char *p, *bol, *nl;
	char msgline[4096];
	strbuffer_t *monmsg, *dfstr_filtered;
	char *dname;
	int dmin, dmax, dcount, dcolor;
	char *group;

	if (!want_msgtype(hinfo, MSG_DISK)) return;
	if (!dfstr) return;

	dbgprintf("Disk check host %s\n", hostname);

	monmsg = newstrbuffer(0);
	dfstr_filtered = newstrbuffer(0);
	clear_disk_counts(hinfo, clientclass);
	clearalertgroups();

	bol = dfstr; /* Must do this always, to at least grab the column-numbers we need */
	while (bol) {
		int ignored = 0;

		nl = strchr(bol, '\n'); if (nl) *nl = '\0';

		if ((capacol == -1) && (mntcol == -1) && (freecol == -1)) {
			/* First line: Check the header and find the columns we want */
			p = strdup(bol);
			freecol = selectcolumn(p, freehdr);
			strcpy(p, bol);
			capacol = selectcolumn(p, capahdr);
			strcpy(p, bol);
			mntcol = selectcolumn(p, mnthdr);
			xfree(p);
			dbgprintf("Disk check: header '%s', columns %d and %d\n", bol, freecol, capacol, mntcol);
		}
		else {
			char *fsname = NULL, *levelstr = NULL;
			int abswarn, abspanic;
			long levelpct = -1, levelabs = -1, warnlevel, paniclevel;

			p = strdup(bol);
			fsname = getcolumn(p, mntcol); 
			if (fsname) {
				char *msgp = msgline;

				add_disk_count(fsname);
				get_disk_thresholds(hinfo, clientclass, fsname, 
						    &warnlevel, &paniclevel, 
						    &abswarn, &abspanic, 
						    &ignored, &group);

				strcpy(p, bol);
				levelstr = getcolumn(p, freecol); if (levelstr) levelabs = atol(levelstr);
				strcpy(p, bol);
				levelstr = getcolumn(p, capacol); if (levelstr) levelpct = atol(levelstr);

				dbgprintf("Disk check: FS='%s' level %ld%%/%ldU (thresholds: %lu/%lu, abs: %d/%d)\n",
					fsname, levelpct, levelabs, 
					warnlevel, paniclevel, abswarn, abspanic);

				if (ignored) {
					/* Forget about this one */
				}
				else if ( (abspanic && (levelabs <= paniclevel)) || 
				     (!abspanic && (levelpct >= paniclevel)) ) {
					if (diskcolor < COL_RED) diskcolor = COL_RED;

					msgp += sprintf(msgp, "&red %s ", fsname);

					if (abspanic) msgp += sprintf(msgp, "(%lu units free)", levelabs);
					else msgp += sprintf(msgp, "(%lu%% used)", levelpct);

					msgp += sprintf(msgp, " has reached the PANIC level ");

					if (abspanic) msgp += sprintf(msgp, "(%lu units)\n", paniclevel);
					else msgp += sprintf(msgp, "(%lu%%)\n", paniclevel);

					addtobuffer(monmsg, msgline);
					addalertgroup(group);
				}
				else if ( (abswarn && (levelabs <= warnlevel)) || 
				          (!abswarn && (levelpct >= warnlevel)) ) {
					if (diskcolor < COL_YELLOW) diskcolor = COL_YELLOW;

					msgp += sprintf(msgp, "&yellow %s ", fsname);

					if (abswarn) msgp += sprintf(msgp, "(%lu units free)", levelabs);
					else msgp += sprintf(msgp, "(%lu%% used)", levelpct);

					msgp += sprintf(msgp, " has reached the WARNING level ");

					if (abswarn) msgp += sprintf(msgp, "(%lu units)\n", warnlevel);
					else msgp += sprintf(msgp, "(%lu%%)\n", warnlevel);

					addtobuffer(monmsg, msgline);
					addalertgroup(group);
				}
			}

			xfree(p);
		}

		if (!ignored) {
			addtobuffer(dfstr_filtered, bol);
			addtobuffer(dfstr_filtered, "\n");
		}
		if (nl) { *nl = '\n'; bol = nl+1; } else bol = NULL;
	}

	if ((capacol == -1) && (mntcol == -1)) {
		/* If this happens, we havent found our headers so no filesystems have been processed */
		diskcolor = COL_YELLOW;
		sprintf(msgline, "&red Expected strings (%s and %s) not found in df output\n", 
			capahdr, mnthdr);
		addtobuffer(monmsg, msgline);

		errprintf("Host %s (%s) sent incomprehensible disk report - missing columnheaders '%s' and '%s'\n",
			  hostname, osname(os), capahdr, mnthdr);
	}

	/* Check for filesystems that must (not) exist */
	while ((dname = check_disk_count(&dcount, &dmin, &dmax, &dcolor, &group)) != NULL) {
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
			addalertgroup(group);
		}
	}

	/* Now we know the result, so generate a status message */
	init_status(diskcolor);
	group = getalertgroups();
	if (group) sprintf(msgline, "status/group:%s ", group); else strcpy(msgline, "status ");
	addtostatus(msgline);

	sprintf(msgline, "%s.disk %s %s - Filesystems %s\n",
		commafy(hostname), colorname(diskcolor), 
		(timestr ? timestr : "<No timestamp data>"), 
		(((diskcolor == COL_RED) || (diskcolor == COL_YELLOW)) ? "NOT ok" : "ok"));
	addtostatus(msgline);

	/* And add the info about what's wrong */
	if (STRBUFLEN(monmsg)) {
		addtostrstatus(monmsg);
		addtostatus("\n");
	}

	/* And the full df output */
	addtostrstatus(dfstr_filtered);

	if (fromline && !localmode) addtostatus(fromline);
	finish_status();

	freestrbuffer(monmsg);
	freestrbuffer(dfstr_filtered);
}

void unix_inode_report(char *hostname, char *clientclass, enum ostype_t os,
		      void *hinfo, char *fromline, char *timestr, 
		      char *freehdr, char *capahdr, char *mnthdr, char *dfstr)
{
	int inodecolor = COL_GREEN;

	int freecol = -1;
	int capacol = -1;
	int mntcol  = -1;
	char *p, *bol, *nl;
	char msgline[4096];
	strbuffer_t *monmsg, *dfstr_filtered;
	char *iname;
	int imin, imax, icount, icolor;
	char *group;

	if (!want_msgtype(hinfo, MSG_INODE)) return;
	if (!dfstr) return;

	dbgprintf("Inode check host %s\n", hostname);

	monmsg = newstrbuffer(0);
	dfstr_filtered = newstrbuffer(0);
	clear_inode_counts(hinfo, clientclass);
	clearalertgroups();

	bol = dfstr; /* Must do this always, to at least grab the column-numbers we need */
	while (bol) {
		int ignored = 0;

		nl = strchr(bol, '\n'); if (nl) *nl = '\0';

		if ((capacol == -1) && (mntcol == -1) && (freecol == -1)) {
			/* First line: Check the header and find the columns we want */
			p = strdup(bol);
			freecol = selectcolumn(p, freehdr);
			strcpy(p, bol);
			capacol = selectcolumn(p, capahdr);
			strcpy(p, bol);
			mntcol = selectcolumn(p, mnthdr);
			xfree(p);
			dbgprintf("Inode check: header '%s', columns %d and %d\n", bol, freecol, capacol, mntcol);
		}
		else {
			char *fsname = NULL, *levelstr = NULL;
			int abswarn, abspanic;
			long levelpct = -1, levelabs = -1, warnlevel, paniclevel;

			p = strdup(bol);
			fsname = getcolumn(p, mntcol); 
			if (fsname) {
				char *msgp = msgline;

				add_inode_count(fsname);
				get_inode_thresholds(hinfo, clientclass, fsname, 
						    &warnlevel, &paniclevel, 
						    &abswarn, &abspanic, 
						    &ignored, &group);

				strcpy(p, bol);
				levelstr = getcolumn(p, freecol); if (levelstr) levelabs = atol(levelstr);
				strcpy(p, bol);
				levelstr = getcolumn(p, capacol); if (levelstr) levelpct = atol(levelstr);

				dbgprintf("Inode check: FS='%s' level %ld%%/%ldU (thresholds: %lu/%lu, abs: %d/%d)\n",
					fsname, levelpct, levelabs, 
					warnlevel, paniclevel, abswarn, abspanic);

				if (ignored) {
					/* Forget about this one */
				}
				else if ( (abspanic && (levelabs <= paniclevel)) || 
				     (!abspanic && (levelpct >= paniclevel)) ) {
					if (inodecolor < COL_RED) inodecolor = COL_RED;

					msgp += sprintf(msgp, "&red <!-- ID=%s --> %s ", fsname, fsname);

					if (abspanic) msgp += sprintf(msgp, "(%lu units free)", levelabs);
					else msgp += sprintf(msgp, "(%lu%% used)", levelpct);

					msgp += sprintf(msgp, " has reached the PANIC level ");

					if (abspanic) msgp += sprintf(msgp, "(%lu units)\n", paniclevel);
					else msgp += sprintf(msgp, "(%lu%%)\n", paniclevel);

					addtobuffer(monmsg, msgline);
					addalertgroup(group);
				}
				else if ( (abswarn && (levelabs <= warnlevel)) || 
				          (!abswarn && (levelpct >= warnlevel)) ) {
					if (inodecolor < COL_YELLOW) inodecolor = COL_YELLOW;

					msgp += sprintf(msgp, "&yellow <!-- ID=%s --> %s ", fsname, fsname);

					if (abswarn) msgp += sprintf(msgp, "(%lu units free)", levelabs);
					else msgp += sprintf(msgp, "(%lu%% used)", levelpct);

					msgp += sprintf(msgp, " has reached the WARNING level ");

					if (abswarn) msgp += sprintf(msgp, "(%lu units)\n", warnlevel);
					else msgp += sprintf(msgp, "(%lu%%)\n", warnlevel);

					addtobuffer(monmsg, msgline);
					addalertgroup(group);
				}
			}

			xfree(p);
		}

		if (!ignored) {
			addtobuffer(dfstr_filtered, bol);
			addtobuffer(dfstr_filtered, "\n");
		}
		if (nl) { *nl = '\n'; bol = nl+1; } else bol = NULL;
	}

	if ((capacol == -1) && (mntcol == -1)) {
		if (strlen(dfstr) == 0) {
			/* Empty inode report, happens on Solaris when all filesystems are ZFS */
			inodecolor = COL_GREEN;
			sprintf(msgline, "&green No filesystems reporting inode data\n");
			addtobuffer(monmsg, msgline);
		}
		else {
			/* If this happens, we havent found our headers so no filesystems have been processed */
			inodecolor = COL_YELLOW;
			sprintf(msgline, "&red Expected strings (%s and %s) not found in df output\n", 
				capahdr, mnthdr);
			addtobuffer(monmsg, msgline);
	
			errprintf("Host %s (%s) sent incomprehensible inode report - missing columnheaders '%s' and '%s'\n%s\n",
				  hostname, osname(os), capahdr, mnthdr, dfstr);
		}
	}

	/* Check for filesystems that must (not) exist */
	while ((iname = check_inode_count(&icount, &imin, &imax, &icolor, &group)) != NULL) {
		char limtxt[1024];

		*limtxt = '\0';

		if (imax == -1) {
			if (imin > 0) sprintf(limtxt, "%d or more", imin);
			else if (imin == 0) sprintf(limtxt, "none");
		}
		else {
			if (imin > 0) sprintf(limtxt, "between %d and %d", imin, imax);
			else if (imin == 0) sprintf(limtxt, "at most %d", imax);
		}

		if (icolor != COL_GREEN) {
			if (icolor > inodecolor) inodecolor = icolor;
			sprintf(msgline, "&%s <!-- ID=%s -->Filesystem %s (found %d, req. %s)\n",
				colorname(icolor), iname, iname, icount, limtxt);
			addtobuffer(monmsg, msgline);
			addalertgroup(group);
		}
	}

	/* Now we know the result, so generate a status message */
	init_status(inodecolor);
	group = getalertgroups();
	if (group) sprintf(msgline, "status/group:%s ", group); else strcpy(msgline, "status ");
	addtostatus(msgline);

	sprintf(msgline, "%s.inode %s %s - Filesystems %s\n",
		commafy(hostname), colorname(inodecolor), 
		(timestr ? timestr : "<No timestamp data>"), 
		(((inodecolor == COL_RED) || (inodecolor == COL_YELLOW)) ? "NOT ok" : "ok"));
	addtostatus(msgline);

	/* And add the info about what's wrong */
	if (STRBUFLEN(monmsg)) {
		addtostrstatus(monmsg);
		addtostatus("\n");
	}

	/* And the full df output */
	addtostrstatus(dfstr_filtered);

	if (fromline && !localmode) addtostatus(fromline);
	finish_status();

	freestrbuffer(monmsg);
	freestrbuffer(dfstr_filtered);
}

void unix_memory_report(char *hostname, char *clientclass, enum ostype_t os,
		        void *hinfo, char *fromline, char *timestr, 
			long memphystotal, long memphysused, long memactused,
			long memswaptotal, long memswapused)
{
	long memphyspct = 0, memswappct = 0, memactpct = 0;
	int physyellow, physred, swapyellow, swapred, actyellow, actred;

	int memorycolor = COL_GREEN, physcolor = COL_GREEN, swapcolor = COL_GREEN, actcolor = COL_GREEN;
	char *memorysummary = "OK";

	char msgline[4096];

	if (!want_msgtype(hinfo, MSG_MEMORY)) return;
	if (memphystotal == -1) return;
	if (memphysused  == -1) return;

	get_memory_thresholds(hinfo, clientclass, &physyellow, &physred, &swapyellow, &swapred, &actyellow, &actred);

	memphyspct = (memphystotal > 0) ? ((100 * memphysused) / memphystotal) : 0;
	if (memphyspct <= 100) {
		if (memphyspct > physyellow) physcolor = COL_YELLOW;
		if (memphyspct > physred)    physcolor = COL_RED;
	}

	if (memswapused != -1) memswappct = (memswaptotal > 0) ? ((100 * memswapused) / memswaptotal) : 0;
	if (memswappct <= 100) {
		if (memswappct > swapyellow) swapcolor = COL_YELLOW;
		if (memswappct > swapred)    swapcolor = COL_RED;
	}

	if (memactused != -1) memactpct = (memphystotal > 0) ? ((100 * memactused) / memphystotal) : 0;
	if (memactpct <= 100) {
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

	sprintf(msgline, "&%s %-12s%11ldM%11ldM%11ld%%\n", 
		colorname(physcolor), "Physical", memphysused, memphystotal, memphyspct);
	addtostatus(msgline);

	if (memactused != -1) {
		if (memactpct <= 100)
			sprintf(msgline, "&%s %-12s%11ldM%11ldM%11ld%%\n", 
				colorname(actcolor), "Actual", memactused, memphystotal, memactpct);
		else
			sprintf(msgline, "&%s %-12s%11ldM%11ldM%11ld%% - invalid data\n", 
				colorname(COL_CLEAR), "Actual", memactused, memphystotal, 0L);
			
		addtostatus(msgline);
	}

	if (memswapused != -1) {
		if (memswappct <= 100)
			sprintf(msgline, "&%s %-12s%11ldM%11ldM%11ld%%\n", 
				colorname(swapcolor), "Swap", memswapused, memswaptotal, memswappct);
		else
			sprintf(msgline, "&%s %-12s%11ldM%11ldM%11ld%% - invalid data\n", 
				colorname(COL_CLEAR), "Swap", memswapused, memswaptotal, 0L);

		addtostatus(msgline);
	}
	if (fromline && !localmode) addtostatus(fromline);
	finish_status();
}

void unix_procs_report(char *hostname, char *clientclass, enum ostype_t os,
		       void *hinfo, char *fromline, char *timestr, 
		       char *cmdhdr, char *altcmdhdr, char *psstr)
{
	int pscolor = COL_GREEN;

	int pchecks;
	int cmdofs = -1;
	char *p, *eol;
	char msgline[4096];
	strbuffer_t *monmsg;
	static strbuffer_t *countdata = NULL;
	int anycountdata = 0;
	char *group;

	if (!want_msgtype(hinfo, MSG_PROCS)) return;
	if (!psstr) return;

	if (!countdata) countdata = newstrbuffer(0);

	clearalertgroups();
	monmsg = newstrbuffer(0);

	sprintf(msgline, "data %s.proccounts\n", commafy(hostname));
	addtobuffer(countdata, msgline);

	/* 
	 * Find where the command is located. We look for the header for the command,
	 * and calculate the offset from the beginning of the line.
	 *
	 * NOTE: The header strings could show up in the normal "ps" output. So
	 *       we look for it only in the first line of output.
	 */
	eol = strchr(psstr, '\n'); if (eol) *eol = '\0';
	dbgprintf("Host %s need heading %s or %s - ps header line reads '%s'\n", 
		  hostname, cmdhdr, (altcmdhdr ? altcmdhdr : "<none>"), psstr);

	/* Look for the primary key */
	p = strstr(psstr, cmdhdr);
	if (p) {
		cmdofs = (p - psstr);
		dbgprintf("Host %s: Found pri. heading '%s' at offset %d\n", hostname, cmdhdr, cmdofs);
	}

	/* If there's a secondary key, look for that also */
	if (altcmdhdr) {
		p = strstr(psstr, altcmdhdr);
		if (p) {
			dbgprintf("Host %s: Found sec. heading '%s' at offset %d\n", hostname, altcmdhdr, (p - psstr));
			if ((cmdofs == -1) || ((p - psstr) < cmdofs)) {
				/* We'll use the secondary key */
				cmdofs = (p - psstr);
			}
		}
	}
	if (eol) *eol = '\n';

	if (debug) {
		if (cmdofs >= 0) dbgprintf("Host %s: Found ps command line at offset %d\n", hostname, cmdofs);
		else dbgprintf("Host %s: None of the headings found\n", hostname);
	}

	pchecks = clear_process_counts(hinfo, clientclass);

	if (pchecks == 0) {
		/* Nothing to check */
		sprintf(msgline, "&%s No process checks defined\n", colorname(noreportcolor));
		addtobuffer(monmsg, msgline);
		pscolor = noreportcolor;
	}
	else if (cmdofs >= 0) {
		/* Count how many instances of each monitored process is running */
		char *pname, *pid, *bol, *nl;
		int pcount, pmin, pmax, pcolor, ptrack;

		bol = psstr;
		while (bol) {
			nl = strchr(bol, '\n'); 

			/* Take care - the ps output line may be shorter than what we look at */
			if (nl) {
				*nl = '\0';

				if ((nl-bol) > cmdofs) add_process_count(bol+cmdofs);

				*nl = '\n';
				bol = nl+1;
			}
			else {
				if (strlen(bol) > cmdofs) add_process_count(bol+cmdofs);

				bol = NULL;
			}
		}

		/* Check the number found for each monitored process */
		while ((pname = check_process_count(&pcount, &pmin, &pmax, &pcolor, &pid, &ptrack, &group)) != NULL) {
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
				addalertgroup(group);
			}

			if (ptrack) {
				/* Save the count data for later DATA message to track process counts */
				if (!pid) pid = "default";
				sprintf(msgline, "%s:%u\n", pid, pcount);
				addtobuffer(countdata, msgline);
				anycountdata = 1;
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

	group = getalertgroups();
	if (group) sprintf(msgline, "status/group:%s ", group); else strcpy(msgline, "status ");
	addtostatus(msgline);

	sprintf(msgline, "%s.procs %s %s - Processes %s\n",
		commafy(hostname), colorname(pscolor), 
		(timestr ? timestr : "<No timestamp data>"), 
		(((pscolor == COL_RED) || (pscolor == COL_YELLOW)) ? "NOT ok" : "ok"));
	addtostatus(msgline);

	/* And add the info about what's wrong */
	if (STRBUFLEN(monmsg)) {
		addtostrstatus(monmsg);
		addtostatus("\n");
	}

	/* And the full ps output for those who want it */
	if (pslistinprocs) {
		/*
		 * NB: Process listings may contain HTML special characters.
		 *     We must encode these for HTML, cf.
		 *     http://www.w3.org/TR/html4/charset.html#h-5.3.2
		 */
		char *inp, *tagpos;

		inp = psstr;
		do {
			tagpos = inp + strcspn(inp, "<>&\"");
			switch (*tagpos) {
			  case '<':
				*tagpos = '\0'; addtostatus(inp); addtostatus("&lt;"); *tagpos = '<';
				inp = tagpos + 1;
				break;
			  case '>':
				*tagpos = '\0'; addtostatus(inp); addtostatus("&gt;"); *tagpos = '>';
				inp = tagpos + 1;
				break;
			  case '&':
				*tagpos = '\0'; addtostatus(inp); addtostatus("&amp;"); *tagpos = '&';
				inp = tagpos + 1;
				break;
			  case '\"':
				*tagpos = '\0'; addtostatus(inp); addtostatus("&quot;"); *tagpos = '\"';
				inp = tagpos + 1;
				break;
			  default:
				/* We're done */
				addtostatus(inp); inp = NULL;
				break;
			}
		} while (inp && *inp);
	}

	if (fromline && !localmode) addtostatus(fromline);
	finish_status();

	freestrbuffer(monmsg);

	if (anycountdata) {
		if (usebackfeedqueue) sendmessage_local(STRBUF(countdata)); else sendmessage(STRBUF(countdata), NULL, XYMON_TIMEOUT, NULL);
	}
	clearstrbuffer(countdata);
}

static void old_msgs_report(char *hostname, void *hinfo, char *fromline, char *timestr, char *msgsstr)
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
		msgscolor = noreportcolor; summary = "No log data available";
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

void msgs_report(char *hostname, char *clientclass, enum ostype_t os,
		 void *hinfo, char *fromline, char *timestr, char *msgsstr)
{
	static strbuffer_t *greendata = NULL;
	static strbuffer_t *yellowdata = NULL;
	static strbuffer_t *reddata = NULL;
	sectlist_t *swalk;
	strbuffer_t *logsummary;
	int msgscolor = COL_GREEN;
	char msgline[PATH_MAX];
	char *group;

	if (!want_msgtype(hinfo, MSG_MSGS)) return;

	for (swalk = defsecthead; (swalk && strncmp(swalk->sname, "msgs:", 5)); swalk = swalk->next) ;

	if (!swalk) {
		old_msgs_report(hostname, hinfo, fromline, timestr, msgsstr);
		return;
	}

	if (!greendata) greendata = newstrbuffer(0);
	if (!yellowdata) yellowdata = newstrbuffer(0);
	if (!reddata) reddata = newstrbuffer(0);
	clearalertgroups();

	logsummary = newstrbuffer(0);

	while (swalk) {
		int logcolor;

		clearstrbuffer(logsummary);
		logcolor = scan_log(hinfo, clientclass, swalk->sname+5, swalk->sdata, swalk->sname, logsummary);

		if (logcolor > msgscolor) msgscolor = logcolor;

		switch (logcolor) {
		  case COL_GREEN:
			if (!localmode) {
				sprintf(msgline, "\nNo entries in <a href=\"%s\">%s</a>\n", 
					hostsvcclienturl(hostname, swalk->sname), swalk->sname+5);
			}
			else {
				sprintf(msgline, "\nNo entries in %s\n", 
					swalk->sname+5);
			}
			addtobuffer(greendata, msgline);
			break;

		  case COL_YELLOW: 
			if (!localmode) {
				sprintf(msgline, "\n&yellow Warnings in <a href=\"%s\">%s</a>\n", 
					hostsvcclienturl(hostname, swalk->sname), swalk->sname+5);
			}
			else {
				sprintf(msgline, "\n&yellow Warnings in %s\n", 
					swalk->sname+5);
			}
			addtobuffer(yellowdata, msgline);
			addtobuffer(yellowdata, "<pre>\n");
			addtostrbuffer(yellowdata, logsummary);
			addtobuffer(yellowdata, "</pre>\n");
			break;

		  case COL_RED:
			if (!localmode) {
				sprintf(msgline, "\n&red Critical entries in <a href=\"%s\">%s</a>\n", 
					hostsvcclienturl(hostname, swalk->sname), swalk->sname+5);
			}
			else {
				sprintf(msgline, "\n&red Critical entries in %s\n", 
					swalk->sname+5);
			}
			addtobuffer(reddata, msgline);
			addtobuffer(yellowdata, "<pre>\n");
			addtostrbuffer(reddata, logsummary);
			addtobuffer(yellowdata, "</pre>\n");
			break;
		}

		do { swalk=swalk->next; } while (swalk && strncmp(swalk->sname, "msgs:", 5));
	}

	freestrbuffer(logsummary);

	init_status(msgscolor);

	group = getalertgroups();
	if (group) sprintf(msgline, "status/group:%s ", group); else strcpy(msgline, "status ");
	addtostatus(msgline);

	sprintf(msgline, "%s.msgs %s %s - System logs %s\n",
		commafy(hostname), colorname(msgscolor), 
		(timestr ? timestr : "<No timestamp data>"),
		(((msgscolor == COL_RED) || (msgscolor == COL_YELLOW)) ? "NOT ok" : "ok"));
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
	for (swalk = defsecthead; (swalk && strncmp(swalk->sname, "msgs:", 5)); swalk = swalk->next) ;
	while (swalk) {
		if (!localmode) {
			sprintf(msgline, "\nFull log <a href=\"%s\">%s</a>\n", 
				hostsvcclienturl(hostname, swalk->sname), swalk->sname+5);
		}
		else {
			sprintf(msgline, "\nFull log %s\n", 
				swalk->sname+5);
		}
		addtostatus(msgline);
		addtobuffer(yellowdata, "<pre>\n");
		addtostatus(swalk->sdata);
		addtobuffer(yellowdata, "</pre>\n");
		do { swalk=swalk->next; } while (swalk && strncmp(swalk->sname, "msgs:", 5));
	}

	if (fromline && !localmode) addtostatus(fromline);

	finish_status();
}

void file_report(char *hostname, char *clientclass, enum ostype_t os,
		 void *hinfo, char *fromline, char *timestr)
{
	static strbuffer_t *greendata = NULL;
	static strbuffer_t *yellowdata = NULL;
	static strbuffer_t *reddata = NULL;
	static strbuffer_t *sizedata = NULL;
	sectlist_t *swalk;
	strbuffer_t *filesummary;
	int filecolor = -1, onecolor;
	char msgline[PATH_MAX];
	char sectionname[PATH_MAX];
	int anyszdata = 0;
	char *group;

	if (!want_msgtype(hinfo, MSG_FILES)) return;

	if (!greendata) greendata = newstrbuffer(0);
	if (!yellowdata) yellowdata = newstrbuffer(0);
	if (!reddata) reddata = newstrbuffer(0);
	if (!sizedata) sizedata = newstrbuffer(0);
	filesummary = newstrbuffer(0);
	clearalertgroups();

	sprintf(msgline, "data %s.filesizes\n", commafy(hostname));
	addtobuffer(sizedata, msgline);

	for (swalk = defsecthead; (swalk); swalk = swalk->next) {
		int trackit, anyrules;
		char *sfn = NULL;
		char *id = NULL;

		if (strncmp(swalk->sname, "file:", 5) == 0) {
			off_t sz;
			sfn = swalk->sname+5;
			sprintf(sectionname, "file:%s", sfn);
			onecolor = check_file(hinfo, clientclass, sfn, swalk->sdata, sectionname, filesummary, &sz, &id, &trackit, &anyrules);

			if (trackit) {
				/* Save the size data for later DATA message to track file sizes */
				if (id == NULL) id = sfn;
#ifdef _LARGEFILE_SOURCE
				sprintf(msgline, "%s:%lld\n", nocolon(id), (long long int)sz);
#else
				sprintf(msgline, "%s:%ld\n", nocolon(id), (long int)sz);
#endif
				addtobuffer(sizedata, msgline);
				anyszdata = 1;
			}
		}
		else if (strncmp(swalk->sname, "logfile:", 8) == 0) {
			off_t sz;
			sfn = swalk->sname+8;
			sprintf(sectionname, "logfile:%s", sfn);
			onecolor = check_file(hinfo, clientclass, sfn, swalk->sdata, sectionname, filesummary, &sz, &id, &trackit, &anyrules);
			if (trackit) {
				/* Save the size data for later DATA message to track file sizes */
				if (id == NULL) id = sfn;
#ifdef _LARGEFILE_SOURCE
				sprintf(msgline, "%s:%lld\n", nocolon(id), (long long int)sz);
#else
				sprintf(msgline, "%s:%ld\n", nocolon(id), (long int)sz);
#endif
				addtobuffer(sizedata, msgline);
				anyszdata = 1;
			}

			if (!anyrules) {
				/* Dont clutter the display with logfiles unless they have rules */
				continue;
			}
		}
		else if (strncmp(swalk->sname, "dir:", 4) == 0) {
			unsigned long sz;
			sfn = swalk->sname+4;
			sprintf(sectionname, "dir:%s", sfn);
			onecolor = check_dir(hinfo, clientclass, sfn, swalk->sdata, sectionname, filesummary, &sz, &id, &trackit);

			if (trackit) {
				/* Save the size data for later DATA message to track directory sizes */
				if (id == NULL) id = sfn;
				sprintf(msgline, "%s:%lu\n", nocolon(id), sz);
				addtobuffer(sizedata, msgline);
				anyszdata = 1;
			}
		}
		else continue;

		if (onecolor > filecolor) filecolor = onecolor;

		switch (onecolor) {
		  case COL_GREEN:
			if (!localmode) {
				sprintf(msgline, "\n&green <a href=\"%s\">%s</a>\n", 
					hostsvcclienturl(hostname, sectionname), sfn);
			}
			else {
				sprintf(msgline, "\n&green %s\n", sfn);
			}
			addtobuffer(greendata, msgline);
			break;

		  case COL_YELLOW: 
			if (!localmode) {
				sprintf(msgline, "\n&yellow <a href=\"%s\">%s</a>\n", 
					hostsvcclienturl(hostname, sectionname), sfn);
			}
			else {
				sprintf(msgline, "\n&yellow %s\n", sfn);
			}
			addtobuffer(yellowdata, msgline);
			addtostrbuffer(yellowdata, filesummary);
			break;

		  case COL_RED:
			if (!localmode) {
				sprintf(msgline, "\n&red <a href=\"%s\">%s</a>\n", 
					hostsvcclienturl(hostname, sectionname), sfn);
			}
			else {
				sprintf(msgline, "\n&red %s\n", sfn);
			}
			addtobuffer(reddata, msgline);
			addtostrbuffer(reddata, filesummary);
			break;
		}

		clearstrbuffer(filesummary);
	}

	freestrbuffer(filesummary);

	if ((filecolor == -1) && sendclearfiles) {
		filecolor = noreportcolor;
		addtobuffer(greendata, "No files checked\n");
	}

	if (filecolor != -1) {
		init_status(filecolor);

		group = getalertgroups();
		if (group) sprintf(msgline, "status/group:%s ", group); else strcpy(msgline, "status ");
		addtostatus(msgline);

		sprintf(msgline, "%s.files %s %s - Files %s\n",
			commafy(hostname), colorname(filecolor), 
			(timestr ? timestr : "<No timestamp data>"),
			(((filecolor == COL_RED) || (filecolor == COL_YELLOW)) ? "NOT ok" : "ok"));
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
	}
	else {
		clearstrbuffer(reddata);
		clearstrbuffer(yellowdata);
		clearstrbuffer(greendata);
	}

	if (anyszdata) {
		if (usebackfeedqueue) sendmessage_local(STRBUF(sizedata)); else sendmessage(STRBUF(sizedata), NULL, XYMON_TIMEOUT, NULL);
	}
	clearstrbuffer(sizedata);
}

void linecount_report(char *hostname, char *clientclass, enum ostype_t os,
			void *hinfo, char *fromline, char *timestr)
{
	static strbuffer_t *countdata = NULL;
	sectlist_t *swalk;
	char msgline[PATH_MAX];
	int anydata = 0;

	if (!countdata) countdata = newstrbuffer(0);

	sprintf(msgline, "data %s.linecounts\n", commafy(hostname));
	addtobuffer(countdata, msgline);

	for (swalk = defsecthead; (swalk); swalk = swalk->next) {
		if (strncmp(swalk->sname, "linecount:", 10) == 0) {
			char *fn, *boln, *eoln, *id, *countstr;

			anydata = 1;

			fn = strchr(swalk->sname, ':'); fn += 1 + strspn(fn+1, "\t ");

			boln = swalk->sdata;
			while (boln) {
				eoln = strchr(boln, '\n');

				id = strtok(boln, ":");
				countstr = (id ? strtok(NULL, "\n") : NULL);
				if (id && countstr) {
					countstr += strspn(countstr, "\t ");
					sprintf(msgline, "%s#%s:%s\n", nocolon(fn), id, countstr);
					addtobuffer(countdata, msgline);
				}

				boln = (eoln ? eoln + 1 : NULL);
			}
		}
	}

	if (anydata) {
		if (usebackfeedqueue) sendmessage_local(STRBUF(countdata)); else sendmessage(STRBUF(countdata), NULL, XYMON_TIMEOUT, NULL);
	}
	clearstrbuffer(countdata);
}


void unix_netstat_report(char *hostname, char *clientclass, enum ostype_t os,
		 	 void *hinfo, char *fromline, char *timestr,
			 char *netstatstr)
{
	strbuffer_t *msg;
	char msgline[4096];

	if (!netstatstr) return;

	msg = newstrbuffer(0);
	sprintf(msgline, "data %s.netstat\n%s\n", commafy(hostname), osname(os));
	addtobuffer(msg, msgline);
	addtobuffer(msg, netstatstr);
	if (usebackfeedqueue) sendmessage_local(STRBUF(msg)); else sendmessage(STRBUF(msg), NULL, XYMON_TIMEOUT, NULL);

	freestrbuffer(msg);
}

void unix_ifstat_report(char *hostname, char *clientclass, enum ostype_t os,
		 	void *hinfo, char *fromline, char *timestr,
			char *ifstatstr)
{
	strbuffer_t *msg;
	char msgline[4096];

	if (!ifstatstr) return;

	msg = newstrbuffer(0);
	sprintf(msgline, "data %s.ifstat\n%s\n", commafy(hostname), osname(os));
	addtobuffer(msg, msgline);
	addtobuffer(msg, ifstatstr);
	if (usebackfeedqueue) sendmessage_local(STRBUF(msg)); else sendmessage(STRBUF(msg), NULL, XYMON_TIMEOUT, NULL);

	freestrbuffer(msg);
}

void unix_vmstat_report(char *hostname, char *clientclass, enum ostype_t os,
		 	void *hinfo, char *fromline, char *timestr,
			char *vmstatstr)
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
	sprintf(msgline, "data %s.vmstat\n%s\n", commafy(hostname), osname(os));
	addtobuffer(msg, msgline);
	addtobuffer(msg, p+1);
	if (usebackfeedqueue) sendmessage_local(STRBUF(msg)); else sendmessage(STRBUF(msg), NULL, XYMON_TIMEOUT, NULL);

	freestrbuffer(msg);
}


void unix_ports_report(char *hostname, char *clientclass, enum ostype_t os,
		       void *hinfo, char *fromline, char *timestr, 
		       int localcol, int remotecol, int statecol, char *portstr)
{
	int portcolor = -1;
	int pchecks;
	char msgline[4096];
	static strbuffer_t *monmsg = NULL;
	static strbuffer_t *countdata = NULL;
	int anycountdata = 0;
	char *group;

	if (!want_msgtype(hinfo, MSG_PORTS)) return;
	if (!portstr) return;

	if (!monmsg) monmsg = newstrbuffer(0);
	if (!countdata) countdata = newstrbuffer(0);

	clearalertgroups();
	pchecks = clear_port_counts(hinfo, clientclass);

	sprintf(msgline, "data %s.portcounts\n", commafy(hostname));
	addtobuffer(countdata, msgline);

	if (pchecks > 0) {
		/* Count how many instances of each monitored condition are found */
		char *pname, *pid, *bol, *nl;
		int pcount, pmin, pmax, pcolor, ptrack;
		char *localstr, *remotestr, *statestr;

		bol = portstr;
		while (bol) {
			char *p;

			nl = strchr(bol, '\n'); if (nl) *nl = '\0';

			/* Data lines */

			p = strdup(bol); localstr = getcolumn(p, localcol);
			strcpy(p, bol); remotestr = getcolumn(p, remotecol);
			strcpy(p, bol); statestr = getcolumn(p, statecol);

			add_port_count(localstr, remotestr, statestr);

			xfree(p);

			if (nl) { *nl = '\n'; bol = nl+1; } else bol = NULL;
		}

		/* Check the number found for each monitored port */
 		while ((pname = check_port_count(&pcount, &pmin, &pmax, &pcolor, &pid, &ptrack, &group)) != NULL) {
 			char limtxt[1024];

			*limtxt = '\0';
			if (pmax == -1) {
				if (pmin > 0) sprintf(limtxt, "%d or more", pmin);
				else if (pmin == 0) sprintf(limtxt, "none");
			}
			else {
				if (pmin > 0) sprintf(limtxt, "between %d and %d", pmin, pmax);
				else if (pmin == 0) sprintf(limtxt, "at most %d", pmax);
			}

			if (pcolor > portcolor) portcolor = pcolor;

			if (pcolor == COL_GREEN) {
				sprintf(msgline, "&green %s (found %d, req. %s)\n", pname, pcount, limtxt);
				addtobuffer(monmsg, msgline);
			}
			else {
				sprintf(msgline, "&%s %s (found %d, req. %s)\n",
					colorname(pcolor), pname, pcount, limtxt);
				addtobuffer(monmsg, msgline);
				addalertgroup(group);
			}

			if (ptrack) {
				/* Save the size data for later DATA message to track port counts */
				if (!pid) pid = "default";
				sprintf(msgline, "%s:%u\n", pid, pcount);
				addtobuffer(countdata, msgline);
				anycountdata = 1;
			}
 		}
	}

	if ((portcolor == -1) && sendclearports) {
		/* Nothing to check */
		addtobuffer(monmsg, "No port checks defined\n");
		portcolor = noreportcolor;
	}

	if (portcolor != -1) {
		/* Now we know the result, so generate a status message */
		init_status(portcolor);

		group = getalertgroups();
		if (group) sprintf(msgline, "status/group:%s ", group); else strcpy(msgline, "status ");
		addtostatus(msgline);

		sprintf(msgline, "%s.ports %s %s - Ports %s\n",
			commafy(hostname), colorname(portcolor), 
			(timestr ? timestr : "<No timestamp data>"), 
			(((portcolor == COL_RED) || (portcolor == COL_YELLOW)) ? "NOT ok" : "ok"));
		addtostatus(msgline);

		/* And add the info about what's wrong */
		addtostrstatus(monmsg);
		addtostatus("\n");
		clearstrbuffer(monmsg);

		/* And the full port output for those who want it */
		if (portlistinports) addtostatus(portstr);

		if (fromline) addtostatus(fromline);
		finish_status();
	}
	else {
		clearstrbuffer(monmsg);
	}

	if (anycountdata) {
		if (usebackfeedqueue) sendmessage_local(STRBUF(countdata)); else sendmessage(STRBUF(countdata), NULL, XYMON_TIMEOUT, NULL);
	}
	clearstrbuffer(countdata);
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
#include "client/irix.c"
#include "client/sco_sv.c"
#include "client/bbwin.c"
#include "client/powershell.c"	/* Must go after client/bbwin.c */
#include "client/zvm.c"
#include "client/zvse.c"
#include "client/zos.c"
#include "client/mqcollect.c"
#include "client/snmpcollect.c"
#include "client/generic.c"

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

void testmode(char *configfn)
{
	void *hinfo, *oldhinfo = NULL;
	char hostname[1024], clientclass[1024];
	char s[4096];

	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
	load_client_config(configfn);
	*hostname = '\0';
	*clientclass = '\0';

	while (1) {
		hinfo = NULL;
		while (!hinfo) {
			printf("Hostname (.=end, ?=dump, !=reload) [%s]: ", hostname); 
			fflush(stdout); if (!fgets(hostname, sizeof(hostname), stdin)) return;
			clean_instr(hostname);

			if (strlen(hostname) == 0) {
				hinfo = oldhinfo;
				if (hinfo) strcpy(hostname, xmh_item(hinfo, XMH_HOSTNAME));
			}
			else if (strcmp(hostname, ".") == 0) {
				exit(0);
			}
			else if (strcmp(hostname, "!") == 0) {
				load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
				load_client_config(configfn);
				*hostname = '\0';
			}
			else if (strcmp(hostname, "?") == 0) {
				dump_client_config();
				if (oldhinfo) strcpy(hostname, xmh_item(oldhinfo, XMH_HOSTNAME));
			}
			else {
				hinfo = hostinfo(hostname);
				if (!hinfo) printf("Unknown host\n");

				printf("Hosttype [%s]: ", clientclass); 
				fflush(stdout); if (!fgets(clientclass, sizeof(clientclass), stdin)) return;
				clean_instr(clientclass);
			}
		}
		oldhinfo = hinfo;

		printf("Test (cpu, mem, disk, proc, log, port): "); fflush(stdout); 
		if (!fgets(s, sizeof(s), stdin)) return; clean_instr(s);
		if (strcmp(s, "cpu") == 0) {
			float loadyellow, loadred;
			int recentlimit, ancientlimit, uptimecolor;
			int maxclockdiff, clockdiffcolor;

			get_cpu_thresholds(hinfo, clientclass, &loadyellow, &loadred, &recentlimit, &ancientlimit, &uptimecolor, &maxclockdiff, &clockdiffcolor);

			printf("Load: Yellow at %.2f, red at %.2f\n", loadyellow, loadred);
			printf("Uptime: %s from boot until %s,", colorname(uptimecolor), durationstring(recentlimit));
			printf("and after %s uptime\n", durationstring(ancientlimit));
			if (maxclockdiff > 0) printf("Max clock diff: %d (%s)\n", maxclockdiff, colorname(clockdiffcolor));
		}
		else if (strcmp(s, "mem") == 0) {
			int physyellow, physred, swapyellow, swapred, actyellow, actred;

			get_memory_thresholds(hinfo, clientclass, &physyellow, &physred, 
					&swapyellow, &swapred, &actyellow, &actred);
			printf("Phys: Yellow at %d, red at %d\n", physyellow, physred);
			printf("Swap: Yellow at %d, red at %d\n", swapyellow, swapred);
			printf("Act.: Yellow at %d, red at %d\n", actyellow, actred);
		}
		else if (strcmp(s, "disk") == 0) {
			unsigned long warnlevel, paniclevel;
			int abswarn, abspanic, ignored;
			char *groups;

			printf("Filesystem: "); fflush(stdout);
			if (!fgets(s, sizeof(s), stdin)) return; clean_instr(s);
			get_disk_thresholds(hinfo, clientclass, s, &warnlevel, &paniclevel, 
						   &abswarn, &abspanic, &ignored, &groups);
			if (ignored) 
				printf("Ignored\n");
			else
				printf("Yellow at %lu%c, red at %lu%c\n", 
					warnlevel, (abswarn ? 'U' : '%'),
					paniclevel, (abspanic ? 'U' : '%'));
		}
		else if (strcmp(s, "proc") == 0) {
			int pchecks = clear_process_counts(hinfo, clientclass);
			char *pname, *pid;
			int pcount, pmin, pmax, pcolor, ptrack;
			char *groups;
			FILE *fd;

			if (pchecks == 0) {
				printf("No process checks for this host\n");
				continue;
			}

			printf("To read 'ps' data from a file, enter '@FILENAME' at the prompt\n");
			do {
				printf("ps command string: "); fflush(stdout);
				if (!fgets(s, sizeof(s), stdin)) return; clean_instr(s);
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

			while ((pname = check_process_count(&pcount, &pmin, &pmax, &pcolor, &pid, &ptrack, &groups)) != NULL) {
				printf("Process %s color %s: Count=%d, min=%d, max=%d\n",
					pname, colorname(pcolor), pcount, pmin, pmax);
			}
		}
		else if (strcmp(s, "log") == 0) {
			FILE *fd;
			char *sectname;
			strbuffer_t *logdata, *logsummary;
			int logcolor;

			printf("log filename: "); fflush(stdout);
			if (!fgets(s, sizeof(s), stdin)) return; clean_instr(s);
			sectname = (char *)malloc(strlen(s) + 20);
			sprintf(sectname, "msgs:%s", s);

			logdata = newstrbuffer(0);
			logsummary = newstrbuffer(0);

			printf("To read log data from a file, enter '@FILENAME' at the prompt\n");
			do {
				printf("log line: "); fflush(stdout);
				if (!fgets(s, sizeof(s), stdin)) return; clean_instr(s);
				if (*s == '@') {
					fd = fopen(s+1, "r");
					while (fd && fgets(s, sizeof(s), fd)) {
						if (*s) addtobuffer(logdata, s);
					}
					fclose(fd);
				}
				else {
					if (*s) addtobuffer(logdata, s);
				}
			} while (*s);

			clearstrbuffer(logsummary);
			logcolor = scan_log(hinfo, clientclass, sectname+5, STRBUF(logdata), sectname, logsummary);
			printf("Log status is %s\n\n", colorname(logcolor));
			if (STRBUFLEN(logsummary)) printf("%s\n", STRBUF(logsummary));
			freestrbuffer(logsummary);
			freestrbuffer(logdata);
		}
		else if (strcmp(s, "port") == 0) {
			char *localstr, *remotestr, *statestr, *p, *pname, *pid;
			int pcount, pmin, pmax, pcolor, pchecks, ptrack;
			char *groups;
			int localcol = 4, remotecol = 5, statecol = 6, portcolor = COL_GREEN;

			pchecks = clear_port_counts(hinfo, clientclass);
			if (pchecks == 0) {
				printf("No PORT checks for this host\n");
				continue;
			}

			printf("Need to know netstat columns for 'Local address', 'Remote address' and 'State'\n");
			printf("Enter columns [%d %d %d]: ", localcol, remotecol, statecol); fflush(stdout);
			if (!fgets(s, sizeof(s), stdin)) return; clean_instr(s);
			if (*s) sscanf(s, "%d %d %d", &localcol, &remotecol, &statecol);

			printf("To read 'netstat' data from a file, enter '@FILENAME' at the prompt\n");
			do {
				printf("netstat line: "); fflush(stdout);
				if (!fgets(s, sizeof(s), stdin)) return; clean_instr(s);
				if (*s == '@') {
					FILE *fd;

					fd = fopen(s+1, "r");
					while (fd && fgets(s, sizeof(s), fd)) {
						clean_instr(s);
						if (*s) {
							p = strdup(s); localstr = getcolumn(p, localcol-1);
							strcpy(p, s); remotestr = getcolumn(p, remotecol-1);
							strcpy(p, s); statestr = getcolumn(p, statecol-1);
							add_port_count(localstr, remotestr, statestr);
							xfree(p);
						}
					}
					fclose(fd);
				}
				else if (*s) {
					p = strdup(s); localstr = getcolumn(p, localcol-1);
					strcpy(p, s); remotestr = getcolumn(p, remotecol-1);
					strcpy(p, s); statestr = getcolumn(p, statecol-1);
					add_port_count(localstr, remotestr, statestr);
					xfree(p);
				}
			} while (*s);

			/* Check the number found for each monitored port */
 			while ((pname = check_port_count(&pcount, &pmin, &pmax, &pcolor, &pid, &ptrack, &groups)) != NULL) {
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
					printf("&green %s (found %d, req. %s)\n", pname, pcount, limtxt);
				}
				else {
					if (pcolor > portcolor) portcolor = pcolor;
					printf("&%s %s (found %d, req. %s)\n",
						colorname(pcolor), pname, pcount, limtxt);
				}
 			}
		}
	}

	exit(0);
}

int main(int argc, char *argv[])
{
	char *msg;
	int running;
	int argi, seq;
	struct sigaction sa;
	time_t nextconfigload = 0;
	char *configfn = NULL;
	char **collectors = NULL;

	/* Handle program options. */
	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[argi], "--no-update") == 0) {
			dontsendmessages = 1;
		}
		else if (strcmp(argv[argi], "--no-ps-listing") == 0) {
			pslistinprocs = 0;
		}
		else if (strcmp(argv[argi], "--no-port-listing") == 0) {
			portlistinports = 0;
		}
		else if (strcmp(argv[argi], "--no-clear-msgs") == 0) {
			sendclearmsgs = 0;
		}
		else if (strcmp(argv[argi], "--no-clear-files") == 0) {
			sendclearfiles = 0;
		}
		else if (strcmp(argv[argi], "--no-clear-ports") == 0) {
			sendclearports = 0;
		}
		else if (strncmp(argv[argi], "--clear-color=", 14) == 0) {
			char *p = strchr(argv[argi], '=');
			noreportcolor = parse_color(p+1);
		}
		else if (argnmatch(argv[argi], "--config=")) {
			char *lp = strchr(argv[argi], '=');
			configfn = strdup(lp+1);
		}
		else if (argnmatch(argv[argi], "--collectors=")) {
			char *lp = strdup(strchr(argv[argi], '=')+1);
			char *tok;
			int i;

			tok = strtok(lp, ","); i = 0; collectors = (char **)calloc(1, sizeof(char *));
			while (tok) {
				collectors = (char **)realloc(collectors, (i+2)*sizeof(char *));
				if (strcasecmp(tok, "default") == 0) tok = "";
				collectors[i++] = tok; collectors[i] = NULL;
				tok = strtok(NULL, ",");
			}
		}
		else if (strcmp(argv[argi], "--unknownclientosok") == 0) {
			unknownclientosok = 1;
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
			testmode(configfn);
		}
		else if (net_worker_option(argv[argi])) {
			/* Handled in the subroutine */
		}
	}

	save_errbuf = 0;

	if (collectors == NULL) {
		/* Setup the default collectors */
		collectors = (char **)calloc(2, sizeof(char *));
		collectors[0] = "";
		collectors[1] = NULL;
	}

	/* Do the network stuff if needed */
	net_worker_run(ST_CLIENT, LOC_ROAMING, NULL);

	/* Signals */
	setup_signalhandler("xymond_client");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGHUP, &sa, NULL);
	signal(SIGCHLD, SIG_IGN);

	updinfotree = xtreeNew(strcasecmp);
	running = 1;

	usebackfeedqueue = (sendmessage_init_local() > 0);

	while (running) {
		char *eoln, *restofmsg, *p;
		char *metadata[MAX_META+1];
		int metacount;
		time_t nowtimer = gettimer();

		msg = get_xymond_message(C_CLIENT, argv[0], &seq, NULL);
		if (msg == NULL) {
			if (!localmode) errprintf("Failed to get a message, terminating\n");
			running = 0;
			continue;
		}

		if (reloadconfig || (nowtimer >= nextconfigload)) {
			nextconfigload = nowtimer + 600;
			reloadconfig = 0;
			if (!localmode) load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
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
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < MAX_META)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}
		metadata[metacount] = NULL;

		if ((metacount > 4) && (strncmp(metadata[0], "@@client", 8) == 0)) {
			int cnum, havecollector;
			time_t timestamp = atoi(metadata[1]);
			char *sender = metadata[2];
			char *hostname = metadata[3];
			char *clientos = metadata[4];
			char *clientclass = metadata[5];
			char *collectorid = metadata[6];
			enum ostype_t os;
			void *hinfo = NULL;

			dbgprintf("Client report from host %s\n", (hostname ? hostname : "<unknown>"));

			/* Check if we are running a collector module for this type of client */
			if (!collectorid) collectorid = "";
			for (cnum = 0, havecollector = 0; (collectors[cnum] && !havecollector); cnum++) 
				havecollector = (strcmp(collectorid, collectors[cnum]) == 0);
			if (!havecollector) continue;

			hinfo = (localmode ? localhostinfo(hostname) : hostinfo(hostname));
			if (!hinfo) continue;
			os = get_ostype(clientos);

			/* Default clientclass to the OS name */
			if (!clientclass || (*clientclass == '\0')) clientclass = clientos;

			/* Check for duplicates */
			if (add_updateinfo(hostname, seq, timestamp) != 0) continue;

			if (usebackfeedqueue) combo_start_local(); else combo_start();
			switch (os) {
                          case OS_FREEBSD:
                                handle_freebsd_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_NETBSD:
                                handle_netbsd_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_OPENBSD:
                                handle_openbsd_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_LINUX22:
                          case OS_LINUX:
                          case OS_RHEL3:
                                handle_linux_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_DARWIN:
                                handle_darwin_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_SOLARIS:
                                handle_solaris_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_HPUX:
                                handle_hpux_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_OSF:
                                handle_osf_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_AIX:
                                handle_aix_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_IRIX:
                                handle_irix_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_SCO_SV:
                                handle_sco_sv_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

                          case OS_WIN32_BBWIN:
                                handle_win32_bbwin_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
                                break;

			  case OS_WIN_POWERSHELL:
				handle_powershell_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_ZVM:
				handle_zvm_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_ZVSE:
				handle_zvse_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_ZOS:
				handle_zos_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_SNMPCOLLECT:
				handle_snmpcollect_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
				break;

			  case OS_MQCOLLECT:
				handle_mqcollect_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
				break;

			  default:
				if (unknownclientosok) {
					dbgprintf("No client backend for OS '%s' sent by %s; using generic\n", clientos, sender);
					handle_generic_client(hostname, clientclass, os, hinfo, sender, timestamp, restofmsg);
				}
				else errprintf("No client backend for OS '%s' sent by %s\n", clientos, sender);
                                break;
			}
			combo_end();
		}
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			printf("Shutting down\n");
			running = 0;
			continue;
		}
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("XYMONCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				reopen_file(fn, "a", stdout);
				reopen_file(fn, "a", stderr);
			}
			continue;
		}
		else if (strncmp(metadata[0], "@@reload", 8) == 0) {
			reloadconfig = 1;
		}
		else {
			/* Unknown message - ignore it */
		}
	}

	if (usebackfeedqueue) sendmessage_finish_local();

	return 0;
}

