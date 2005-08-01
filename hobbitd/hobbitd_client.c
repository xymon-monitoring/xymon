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

static char rcsid[] = "$Id: hobbitd_client.c,v 1.23 2005-08-01 05:57:34 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>

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
	char *upmsg = NULL;
	int upmsgsz;

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
		else if (strstr(hourmark, "mins") && (sscanf(hourmark, "%ld mins", &upmin) == 1)) {
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
		if (sscanf(p, "%f, %f, %f", &load1, &load5, &load15) == 3) {
			sprintf(loadresult, "%.2f", load5);
		}
	}

	get_cpu_thresholds(hinfo, &loadyellow, &loadred, &recentlimit, &ancientlimit);

	if ((uptimesecs != -1) && (recentlimit != -1) && (uptimesecs < recentlimit)) {
		cpucolor = COL_YELLOW;
		addtobuffer(&upmsg, &upmsgsz, "&yellow Machine recently rebooted\n");
	}
	if ((uptimesecs != -1) && (ancientlimit != -1) && (uptimesecs > ancientlimit)) {
		cpucolor = COL_YELLOW;
		sprintf(msgline, "&yellow Machine has been up more than %d days\n", (ancientlimit / 86400));
		addtobuffer(&upmsg, &upmsgsz, msgline);
	}
	if (load5 > loadyellow) {
		cpucolor = COL_YELLOW;
		addtobuffer(&upmsg, &upmsgsz, "&red Load is HIGH\n");
	}
	if (load5 > loadred) {
		cpucolor = COL_RED;
		addtobuffer(&upmsg, &upmsgsz, "&red Load is CRITICAL\n");
	}

	init_status(cpucolor);
	sprintf(msgline, "status %s.cpu %s %s %s, %d users, %d procs, load=%s\n",
		commafy(hostname), colorname(cpucolor), timestr, 
		myupstr, linecount(whostr), linecount(psstr)-1, loadresult);
	addtostatus(msgline);
	if (upmsg) {
		addtostatus(upmsg);
		addtostatus("\n");
		xfree(upmsg);
	}
	if (topstr) {
		addtostatus("\n");
		addtostatus(topstr);
	}

	addtostatus(fromline);
	finish_status();
}


void unix_disk_report(char *hostname, namelist_t *hinfo, char *fromline, char *timestr, 
		      char *capahdr, char *mnthdr, char *dfstr)
{
	int diskcolor = COL_GREEN;

	int capacol = -1;
	int mntcol  = -1;
	int line1 = 1;
	char *p, *bol, *nl;
	char msgline[4096];
	char *monmsg = NULL;
	int monsz;

	if (!dfstr) return;

	dprintf("Disk check host %s\n", hostname);

	bol = dfstr;
	while (bol) {
		char *fsname, *usestr;

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
				addtobuffer(&monmsg, &monsz, msgline);
				nl = bol = NULL; /* Abandon loop */
			}
		}
		else {
			int usage, warnlevel, paniclevel;

			p = strdup(bol); usestr = getcolumn(p, capacol);
			if (isdigit((int)*usestr)) usage = atoi(usestr); else usage = -1;

			strcpy(p, bol); fsname = getcolumn(p, mntcol);

			if (usage != -1) {
				get_disk_thresholds(hinfo, fsname, &warnlevel, &paniclevel);

				dprintf("Disk check: FS='%s' usage %d (thresholds: %d/%d)\n",
					fsname, usage, warnlevel, paniclevel);

				if (usage >= paniclevel) {
					if (diskcolor < COL_RED) diskcolor = COL_RED;
					sprintf(msgline, "&red %s (%d %%) has reached the PANIC level (%d %%)\n",
						fsname, usage, paniclevel);
					addtobuffer(&monmsg, &monsz, msgline);
				}
				else if (usage >= warnlevel) {
					if (diskcolor < COL_YELLOW) diskcolor = COL_YELLOW;
					sprintf(msgline, "&yellow %s (%d %%) has reached the WARNING level (%d %%)\n",
						fsname, usage, warnlevel);
					addtobuffer(&monmsg, &monsz, msgline);
				}
			}

			xfree(p);
		}

		if (nl) { *nl = '\n'; bol = nl+1; } else bol = NULL;
	}


	/* Now we know the result, so generate a status message */
	init_status(diskcolor);
	sprintf(msgline, "status %s.disk %s %s - Filesystems %s\n",
		commafy(hostname), colorname(diskcolor), timestr, ((diskcolor == COL_GREEN) ? "OK" : "NOT ok"));
	addtostatus(msgline);

	/* And add the info about what's wrong */
	if (monmsg) {
		addtostatus(monmsg);
		addtostatus("\n");
		xfree(monmsg);
	}

	/* And the full df output */
	addtostatus(dfstr);

	addtostatus(fromline);
	finish_status();
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

	memphyspct = (100 * memphysused) / memphystotal;
	if (memphyspct > physyellow) physcolor = COL_YELLOW;
	if (memphyspct > physred)    physcolor = COL_RED;

	memswappct = (100 * memswapused) / memswaptotal;
	if (memswappct > swapyellow) swapcolor = COL_YELLOW;
	if (memswappct > swapred)    swapcolor = COL_RED;

	if (memactused != -1) {
		memactpct = (100 * memactused) / memphystotal;
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

	init_status(memorycolor);
	sprintf(msgline, "status %s.memory %s %s - Memory %s\n",
		commafy(hostname), colorname(memorycolor), timestr, memorysummary);
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
	addtostatus(fromline);
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
	char *monmsg = NULL;
	int monsz;

	if (!psstr) return;

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
		addtobuffer(&monmsg, &monsz, "&green No process checks defined\n");
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
				if (pmin > 0) sprintf(limtxt, " req. %d or more", pmin);
				else if (pmin == 0) sprintf(limtxt, " req. none");
			}
			else {
				if (pmin > 0) sprintf(limtxt, " req. between %d and %d", pmin, pmax);
				else if (pmin == 0) sprintf(limtxt, "req. at most %d", pmax);
			}

			if (pcolor == COL_GREEN) {
				sprintf(msgline, "&green %s (found %d, %s)\n", pname, pcount, limtxt);
				addtobuffer(&monmsg, &monsz, msgline);
			}
			else {
				if (pcolor > pscolor) pscolor = pcolor;
				sprintf(msgline, "&%s %s (found %d, req. %s)\n", 
					colorname(pcolor), pname, pcount, limtxt);
				addtobuffer(&monmsg, &monsz, msgline);
			}
		}
	}
	else {
		pscolor = COL_YELLOW;
		sprintf(msgline, "&yellow Expected string %s not found in ps output header\n", cmdhdr);
		addtobuffer(&monmsg, &monsz, msgline);
	}

	/* Now we know the result, so generate a status message */
	init_status(pscolor);
	sprintf(msgline, "status %s.procs %s %s - Processes %s\n",
		commafy(hostname), colorname(pscolor), timestr, ((pscolor == COL_GREEN) ? "OK" : "NOT ok"));
	addtostatus(msgline);

	/* And add the info about what's wrong */
	if (monmsg) {
		addtostatus(monmsg);
		addtostatus("\n");
		xfree(monmsg);
	}

	/* And the full ps output for those who want it */
	addtostatus(psstr);

	addtostatus(fromline);
	finish_status();
}

void msgs_report(char *hostname, namelist_t *hinfo, char *fromline, char *timestr, char *msgsstr)
{
	int msgscolor = COL_GREEN;
	char msgline[4096];
	char *summary = "All logs OK";

	if (msgsstr) {
		if (strstr(msgsstr, "&clear ")) { msgscolor = COL_CLEAR; summary = "No log data available"; }
		if (strstr(msgsstr, "&yellow ")) { msgscolor = COL_YELLOW; summary = "WARNING"; }
		if (strstr(msgsstr, "&red ")) { msgscolor = COL_RED; summary = "CRITICAL"; }
	}
	else {
		msgscolor = COL_CLEAR; summary = "No log data available";
	}

	init_status(msgscolor);
	sprintf(msgline, "status %s.msgs %s System logs at %s : %s\n",
		commafy(hostname), colorname(msgscolor), timestr, summary);
	addtostatus(msgline);

	if (msgsstr)
		addtostatus(msgsstr);
	else
		addtostatus("The client did not report any logfile data\n");

	addtostatus(fromline);
	finish_status();
}

void unix_netstat_report(char *hostname, namelist_t *hinfo, char *osid, char *netstatstr)
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


void unix_vmstat_report(char *hostname, namelist_t *hinfo, char *osid, char *vmstatstr)
{
	char *msg = NULL;
	int  msgsz;
	char msgline[4096];

	char *p;

	if (!vmstatstr) return;

	p = strrchr(vmstatstr, '\n');
	if (!p) return;  /* No NL in vmstat output ? Unlikely. */

	if (strlen(p) == 1) {
		/* Go back to the previous line */
		do { p--; } while ((p > vmstatstr) && (*p != '\n'));
	}
	sprintf(msgline, "data %s.vmstat\n%s\n", commafy(hostname), osid);
	addtobuffer(&msg, &msgsz, msgline);
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
#include "client/osf.c"

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
		else if (argnmatch(argv[argi], "--config=")) {
			char *lp = strchr(argv[argi], '=');
			configfn = strdup(lp+1);
		}
		else if (argnmatch(argv[argi], "--dump-config")) {
			load_client_config(configfn);
			dump_client_config();
			return 0;
		}
		else if (strcmp(argv[argi], "--test") == 0) {
			namelist_t *hinfo, *oldhinfo = NULL;
			char hostname[100];
			char s[100];

			load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
			load_client_config(configfn);
			*hostname = '\0';

			while (1) {
				hinfo = NULL;
				while (!hinfo) {
					printf("Hostname (.=end, ?=dump, !=reload) [%s]: ", hostname); 
					fflush(stdout); fgets(hostname, sizeof(hostname), stdin);
					sanitize_input(hostname);

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
				fgets(s, sizeof(s), stdin); sanitize_input(s);
				if (strcmp(s, "cpu") == 0) {
					float loadyellow, loadred;
					int recentlimit, ancientlimit;
	
					get_cpu_thresholds(hinfo, &loadyellow, &loadred, &recentlimit, &ancientlimit);

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
					int warnlevel, paniclevel;

					printf("Filesystem: "); fflush(stdout);
					fgets(s, sizeof(s), stdin); sanitize_input(s);
					get_disk_thresholds(hinfo, s, &warnlevel, &paniclevel);
					printf("Yellow at %d, red at %d\n", warnlevel, paniclevel);
				}
				else if (strcmp(s, "proc") == 0) {
					int pchecks = clear_process_counts(hinfo);
					char *pname;
					int pcount, pmin, pmax, pcolor;

					if (pchecks == 0) {
						printf("No process checks for this host\n");
						continue;
					}

					do {
						printf("ps command string: "); fflush(stdout);
						fgets(s, sizeof(s), stdin); sanitize_input(s);
						if (*s) add_process_count(s);
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

		msg = get_hobbitd_message(argv[0], &seq, timeout);
		if (msg == NULL) {
			running = 0;
			continue;
		}

		if (reloadconfig || (time(NULL) >= nextconfigload)) {
			nextconfigload = time(NULL) + 600;
			reloadconfig = 0;
			load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn());
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
			enum ostype_t os;
			namelist_t *hinfo = hostinfo(hostname);

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
				handle_freebsd_client(hostname, hinfo, sender, timestamp, restofmsg);
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
			  case OS_IRIX:
			  case OS_WIN32: 
			  case OS_SNMP: 
			  case OS_UNKNOWN:
				errprintf("No client backend for OS '%s' sent by %s\n", clienttype, sender);
				break;
			}
		}
		else {
			/* Unknown message - ignore it */
		}
	}

	return 0;
}

