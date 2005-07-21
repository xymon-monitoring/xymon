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

static char rcsid[] = "$Id: hobbitd_client.c,v 1.6 2005-07-21 17:27:57 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>

#include "libbbgen.h"
#include "hobbitd_worker.h"

#define MAX_META 20	/* The maximum number of meta-data items in a message */


typedef struct sectlist_t {
	char *sname;
	char *sdata;
	struct sectlist_t *next;
} sectlist_t;
sectlist_t *sections = NULL;

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

void unix_cpu_report(char *hostname, char *fromline, char *timestr, char *uptimestr, char *whostr, char *psstr, char *topstr)
{
	char *p;
	char *uptimeresult = NULL;
	float load1, load5, load15;
	char loadresult[100];
	long uptimesecs = -1;
	char myupstr[100];

	int cpucolor = COL_GREEN;

	char *msg = NULL;
	int  msgsz;
	char msgline[4096];

	if (!uptimestr) return;

	p = strstr(uptimestr, " up ");
	if (p) {
		char *daymark;
		char *hourmark;
		long uphour, upmin, upsecs;
		uptimesecs = 0;

		p += 3;
		uptimeresult = strdup(p);
		daymark = strstr(uptimeresult, " day");

		if (daymark) {
			uptimesecs = atoi(uptimeresult) * 86400;
			hourmark = strchr(daymark, ',');
			if (hourmark) hourmark++; else hourmark = "";
		}
		else {
			hourmark = uptimeresult;
		}
		hourmark += strspn(hourmark, " ");
		if (sscanf(hourmark, "%ld:%ld", &uphour, &upmin) == 2) {
			uptimesecs += 60*(60*uphour + upmin);
		}
		else if (sscanf(hourmark, "%ld hours %ld mins", &uphour, &upmin) == 2) {
			uptimesecs += 60*(60*uphour + upmin);
		}
		else if (strstr(hourmark, " secs") && (sscanf(hourmark, "%ld secs", &upsecs) == 1)) {
			uptimesecs += upsecs;
		}
		else if (strstr(hourmark, "mins") && (sscanf(hourmark, "%ld mins", &upmin) == 1)) {
			uptimesecs += 60*upmin;
		}
		else if (strncmp(hourmark, "1 hr", 4) == 0) {
			uptimesecs = 3600;
		}
		else {
			uptimesecs = -1;
		}
		p = strchr(hourmark, ','); if (p) *p = '\0';
	}

	*loadresult = '\0';
	p = strstr(uptimestr, "load average: ");
	if (!p) p = strstr(uptimestr, "load averages: "); /* Many BSD's */
	if (p) {
		p = strchr(p, ':') + 1; p += strspn(p, " ");
		if (sscanf(p, "%f, %f, %f", &load1, &load5, &load15) == 3) {
			sprintf(loadresult, "%.2f", load5);
		}
	}

	if (load5 > 5.0) cpucolor = COL_YELLOW;
	if ((uptimesecs != -1) && (uptimesecs < 3600)) cpucolor = COL_YELLOW;
	if (load5 > 8.0) cpucolor = COL_RED;

	if (uptimesecs != -1) {
		int days = (uptimesecs / 86400);
		int hours = (uptimesecs % 86400) / 3600;
		int mins = (uptimesecs % 3600) / 60;

		if (days) sprintf(myupstr, "up: %d days", days);
		else sprintf(myupstr, "up: %02d:%02d", hours, mins);
	}
	else *myupstr = '\0';

	sprintf(msgline, "status %s.cpu %s %s %s, %d users, %d procs, load=%s\n",
		commafy(hostname), colorname(cpucolor), timestr, 
		myupstr, linecount(whostr), linecount(psstr)-1, loadresult);
	addtobuffer(&msg, &msgsz, msgline);
	if ((uptimesecs != -1) && (uptimesecs < 3600)) addtobuffer(&msg, &msgsz, "Machine recently rebooted");
	if (topstr) {
		addtobuffer(&msg, &msgsz, "\n\n");
		addtobuffer(&msg, &msgsz, topstr);
	}

	init_status(cpucolor);
	addtostatus(msg);
	addtostatus(fromline);
	finish_status();

	if (msg) xfree(msg);
}


void unix_disk_report(char *hostname, char *fromline, char *timestr, char *dfstr)
{
	int diskcolor = COL_GREEN;

	char *msg = NULL;
	int  msgsz;
	char msgline[4096];

	if (!dfstr) return;

	sprintf(msgline, "status %s.disk %s %s - Disk partitions OK\n",
		commafy(hostname), colorname(diskcolor), timestr);
	addtobuffer(&msg, &msgsz, msgline);
	addtobuffer(&msg, &msgsz, dfstr);

	init_status(diskcolor);
	addtostatus(msg);
	addtostatus(fromline);
	finish_status();

	if (msg) xfree(msg);
}

void unix_memory_report(char *hostname, char *fromline, char *timestr, 
			long memphystotal, long memphysused, long memactused,
			long memswaptotal, long memswapused)
{
	unsigned long memphyspct, memswappct, memactpct;

	int memorycolor = COL_GREEN;

	char *msg = NULL;
	int  msgsz;
	char msgline[4096];

	char *memorysummary = "OK";

	if (memphystotal == -1) return;
	if (memphysused  == -1) return;
	if (memswaptotal == -1) return;
	if (memswapused  == -1) return;

	memphyspct = (100 * memphysused) / memphystotal;
	memswappct = (100 * memswapused) / memswaptotal;
	if (memactused != -1) memactpct = (100 * memactused) / memphystotal; else memactpct = 0;

	if ((memswappct > 50) || ((memactused != -1) && (memactpct > 90))) {
		memorycolor = COL_YELLOW;
		memorysummary = "low";
	}
	if ((memswappct > 70) || ((memactused != -1) && (memactpct > 95))) {
		memorycolor = COL_RED;
		memorysummary = "CRITICAL";
	}

	sprintf(msgline, "status %s.memory %s %s - Memory %s\n",
		commafy(hostname), colorname(memorycolor), timestr, memorysummary);
	addtobuffer(&msg, &msgsz, msgline);

	sprintf(msgline, "   %-12s%12s%12s%12s\n", "Memory", "Used", "Total", "Percentage");
	addtobuffer(&msg, &msgsz, msgline);

	sprintf(msgline, "&green %-12s%11luM%11luM%11lu%%\n", 
		"Physical", memphysused, memphystotal, memphyspct);
	addtobuffer(&msg, &msgsz, msgline);

	if (memactused != -1) {
		sprintf(msgline, "&green %-12s%11luM%11luM%11lu%%\n", 
			"Actual", memactused, memphystotal, memactpct);
		addtobuffer(&msg, &msgsz, msgline);
	}

	sprintf(msgline, "&green %-12s%11luM%11luM%11lu%%\n", 
		"Swap", memswapused, memswaptotal, memswappct);
	addtobuffer(&msg, &msgsz, msgline);

	init_status(memorycolor);
	addtostatus(msg);
	addtostatus(fromline);
	finish_status();

	if (msg) xfree(msg);
}

void unix_procs_report(char *hostname, char *fromline, char *timestr, char *cmdhdr, char *psstr)
{
	int pscolor = COL_GREEN;

	char *msg = NULL;
	int  msgsz;
	char msgline[4096];
	int cmdofs = -1;
	char *p, *bol, *nl;
	char *monmsg = NULL;
	int monsz;

	int hlcount = 0;

	if (!psstr) return;

	/* 
	 * Find where the command is located. We look for the header for the command,
	 * and calculate the offset from the beginning of the line.
	 */
	p = strstr(psstr, cmdhdr);
	if (p) cmdofs = (p - psstr);

	if (cmdofs >= 0) {
		/* Count how many instances of each monitored process is running */
		bol = psstr;
		while (bol) {
			nl = strchr(bol, '\n'); if (nl) *nl = '\0';
	
			if (strlen(bol) > cmdofs) {
				if (strstr(bol+cmdofs, "hobbitlaunch")) hlcount++;
			}

			if (nl) { *nl = '\n'; bol = nl+1; } else bol = NULL;
		}

		/* Check the number found for each monitored process */
		if (hlcount < 1) {
			pscolor = COL_RED;
			addtobuffer(&monmsg, &monsz, "&red hobbitlaunch NOT running\n");
		}
		else {
			sprintf(msgline, "&green hobbitlaunch running (%d)\n", hlcount);
			addtobuffer(&monmsg, &monsz, msgline);
		}
	}
	else {
		pscolor = COL_YELLOW;
		sprintf(msgline, "&red Expected string %s not found in ps output header\n", cmdhdr);
		addtobuffer(&monmsg, &monsz, msgline);
	}

	/* Now we know the result, so generate a status message */
	sprintf(msgline, "status %s.procs %s %s - Processes %s\n",
		commafy(hostname), colorname(pscolor), timestr, ((pscolor == COL_GREEN) ? "OK" : "Not OK"));
	addtobuffer(&msg, &msgsz, msgline);

	/* And add the info about what's wrong */
	addtobuffer(&monmsg, &monsz, msgline);
	addtobuffer(&msg, &msgsz, "\n");

	/* And the full ps output for those who want it */
	addtobuffer(&msg, &msgsz, psstr);

	init_status(pscolor);
	addtostatus(msg);
	addtostatus(fromline);
	finish_status();

	if (msg) xfree(msg);
}

void unix_netstat_report(char *hostname, char *osid, char *netstatstr)
{
	char *msg = NULL;
	int  msgsz;
	char msgline[4096];

	if (!netstatstr) return;

	sprintf(msgline, "data %s.netstat\n%s\n", commafy(hostname), osid);
	addtobuffer(&msg, &msgsz, msgline);
	addtobuffer(&msg, &msgsz, netstatstr);
	sendmessage(msg, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);

	if (msg) xfree(msg);
}


void unix_vmstat_report(char *hostname, char *osid, char *vmstatstr)
{
	char *msg = NULL;
	int  msgsz;
	char msgline[4096];

	char *p;

	if (!vmstatstr) return;

	sprintf(msgline, "data %s.vmstat\n%s\n", commafy(hostname), osid);
	addtobuffer(&msg, &msgsz, msgline);
	p = strrchr(vmstatstr, '\n');
	if (strlen(p) == 1) {
		/* Go back to the previous line */
		do { p--; } while ((p > vmstatstr) && (*p != '\n'));
	}
	addtobuffer(&msg, &msgsz, p+1);
	sendmessage(msg, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);

	if (msg) xfree(msg);
}

#include "client/linux.c"
#include "client/freebsd.c"
#include "client/netbsd.c"
#include "client/openbsd.c"
#include "client/solaris.c"
#include "client/hpux.c"

int main(int argc, char *argv[])
{
	char *msg;
	int running;
	int argi, seq;
	struct timeval *timeout = NULL;

	/* Handle program options. */
	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
	}

	setup_signalhandler("hobbitd_client");
	save_errbuf = 0;
	signal(SIGCHLD, SIG_IGN);

	running = 1;
	while (running) {
		char *eoln, *restofmsg, *p;
		char *metadata[MAX_META+1];
		int metacount;

		msg = get_hobbitd_message(argv[0], &seq, timeout);
		if (msg == NULL) {
			running = 0;
			continue;
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

		if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
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
		else if ((metacount > 4) && (strncmp(metadata[0], "@@client", 8) == 0)) {
			time_t timestamp = atoi(metadata[1]);
			char *sender = metadata[2];
			char *hostname = metadata[3];
			char *clienttype = metadata[4];

			if (strcasecmp(clienttype, "Linux") == 0) {
				handle_linux_client(hostname, sender, timestamp, restofmsg);
			}
			else if (strcasecmp(clienttype, "freebsd") == 0) {
				handle_freebsd_client(hostname, sender, timestamp, restofmsg);
			}
			else if (strcasecmp(clienttype, "netbsd") == 0) {
				handle_netbsd_client(hostname, sender, timestamp, restofmsg);
			}
			else if (strcasecmp(clienttype, "openbsd") == 0) {
				handle_openbsd_client(hostname, sender, timestamp, restofmsg);
			}
			else if (strcasecmp(clienttype, "SunOS") == 0) {
				handle_solaris_client(hostname, sender, timestamp, restofmsg);
			}
			else if (strcasecmp(clienttype, "Solaris") == 0) {
				handle_solaris_client(hostname, sender, timestamp, restofmsg);
			}
			else if (strcasecmp(clienttype, "hpux") == 0) {
				handle_hpux_client(hostname, sender, timestamp, restofmsg);
			}
		}
		else {
			/* Unknown message - ignore it */
		}
	}

	return 0;
}

