/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for Linux                                            */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char linux_rcsid[] = "$Id$";

void handle_linux_client(char *hostname, char *clienttype, enum ostype_t os, 
			 void *hinfo, char *sender, time_t timestamp,
			 char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *clockstr;
	char *proxystr;
	char *msgcachestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *inodestr;
	char *freestr;
	char *msgsstr;
	char *netstatstr;
	char *vmstatstr;
	char *ifstatstr;
	char *portsstr;
	char *mdstatstr;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	clockstr = getdata("clock");
	proxystr = getdata("proxy");
	msgcachestr = getdata("msgcache");
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	inodestr = getdata("inode");
	freestr = getdata("free");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	vmstatstr = getdata("vmstat");
	portsstr = getdata("ports");
	mdstatstr = getdata("mdstat");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, msgcachestr, proxystr, timestamp, 
			whostr, 0, psstr, 0, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Available", "Capacity", "Mounted", dfstr);
	unix_inode_report(hostname, clienttype, os, hinfo, fromline, timestr, "IFree", "IUse%", "Mounted", inodestr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "CMD", NULL, psstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);
	deltacount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	unix_vmstat_report(hostname, clienttype, os, hinfo, fromline, timestr, vmstatstr);

	/*
	 * Sigh. Recent kernels + procps-ng change things up a bit. If 'available' is present
	 * (roughly, 3.14+ and 2.6.27+, but depends on the vendor), then we'll use the inverse of that:
	 * 	(Physical - Available = ACTUALUSED)
	 * Otherwise, it's:
	 *	(Physical Used - (buffers + cached) = ACTUALUSED)
	 * 
	 * See discussions at http://lists.xymon.com/pipermail/xymon/2015-April/041628.html
	 * If the legacy meminfo display is NOT used, we should get the old format still
	 * 
	 */
		
	if (freestr) {
		char *p;
		long memphystotal, memphysused, memphysfree,
		     memacttotal, memactused, memactfree,
		     memswaptotal, memswapused, memswapfree;

		memphystotal = memswaptotal = memphysused = memswapused = memacttotal = memactused = memactfree = -1;

		/* check for old style */
		p = strstr(freestr, "\n-/+ buffers/cache:");
		if (p) {
			p = strstr(freestr, "\nMem:");
			if (p && (sscanf(p, "\nMem: %ld %ld %ld", &memphystotal, &memphysused, &memphysfree) == 3)) {
				memphystotal /= 1024;
				memphysused /= 1024;
				memphysfree /= 1024;
			}
			p = strstr(freestr, "\n-/+ buffers/cache:");
			if (sscanf(p, "\n-/+ buffers/cache: %ld %ld", &memactused, &memactfree) == 2) {
				memacttotal = memphystotal;
				memactused /= 1024;
				memactfree /= 1024;
			}

		}
		/* check for new style */
		else if (strstr(freestr, "available\n")) {
			long shared, buffcache;
			p = strstr(freestr, "\nMem:");
			if (p && (sscanf(p, "\nMem: %ld %ld %ld %ld %ld %ld", &memphystotal, &memphysused, &memphysfree, 
										&shared, &buffcache, &memactfree) == 6)) {
				memphystotal /= 1024;
				memphysused /= 1024;
				memphysfree /= 1024;
				/* Provide a Physical Used value that's compatible with previous thresholds. However, use the */
				/* new 'Available' line as the basis for "Actual Used", since it'll be more accurate. */
				memacttotal = memphystotal;
				memactfree /= 1024;
				memactused = memacttotal - memactfree; if (memactused < 0) memactused = 0;
				memphysused += (buffcache / 1024);

			}
		}
		else errprintf(" -> No readable memory data for %s in freestr\n", hostname);

		/* There's always a swap line */
		p = strstr(freestr, "\nSwap:");
		if (p && (sscanf(p, "\nSwap: %ld %ld %ld", &memswaptotal, &memswapused, &memswapfree) == 3)) {
			memswaptotal /= 1024;
			memswapused /= 1024;
			memswapfree /= 1024;
		}

		unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
				   memphystotal, memphysused, 
				   memacttotal, memactused, 
				   memswaptotal, memswapused);
	}

	if (mdstatstr) {
		char *statcopy, *bol, *eol;
		int color = COL_GREEN;
		char *mdname = NULL, *mdstatus = NULL;
		int mddevices = 0, mdactive = 0, recovering = 0;
		strbuffer_t *alerttext = newstrbuffer(0);
		char msgline[1024];
		char *summary = NULL;
		int arraycount = 0;

		statcopy = (char *)malloc(strlen(mdstatstr) + 10);
		sprintf(statcopy, "%s\nmd999\n", mdstatstr);

		bol = statcopy;
		while (bol) {
			eol = strchr(bol, '\n'); if (eol) *eol = '\0';

			if ((strncmp(bol, "md", 2) == 0) && (isdigit(*(bol+2)))) {
				char *tok;

				if (mdname && (mddevices >= 0) && (mdactive >= 0)) {
					int onecolor = COL_GREEN;

					/* Got a full md device status, flush it before we start on the next one */
					arraycount++;
					if (mddevices != mdactive) {
						if (!recovering) {
							onecolor = COL_RED;
							snprintf(msgline, sizeof(msgline), "&red %s : Disk failure in array : %d devices of %d active\n", mdname, mdactive, mddevices);
							addtobuffer(alerttext, msgline);
							summary = "failure";
						}
						else {
							onecolor = COL_YELLOW;
							snprintf(msgline, sizeof(msgline), "&yellow %s status %s : %d devices of %d active\n", mdname, mdstatus, mdactive, mddevices);
							addtobuffer(alerttext, msgline);
							if (!summary) summary = "recovering";
						}
					}
					else {
						snprintf(msgline, sizeof(msgline), "&green %s : %d devices of %d active\n", mdname, mdactive, mddevices);
						addtobuffer(alerttext, msgline);
					}

					if (onecolor > color) {
						color = onecolor;
					}
				}

				/* First line, holds the name of the array and the active/inactive status */
				mddevices = mdactive = -1; recovering = 0;

				mdname = strtok(bol, " ");
				tok = strtok(NULL, " ");	// Skip the ':'
				mdstatus = strtok(NULL, " ");
			}


			if (mdname && ((mddevices == -1) && (mdactive == -1)) && (strchr(bol, '/'))) {
				char *p = strchr(bol, '/');

				/* Second line: Holds the number of configured/active devices */
				mdactive = atoi(p+1);
				while ((p > bol) && (isdigit(*(p-1)))) p--;
				mddevices = atoi(p);
			}

			if (mdname && (mddevices != mdactive) && strstr(bol, "recovery = ")) {
				/* Third line: Only present during recovery */
				mdstatus = "recovery in progress";
				recovering = 1;
			}

			bol = (eol ? eol+1 : NULL);
		}


		if (arraycount > 0) {
			init_status(color);
			sprintf(msgline, "status %s.raid %s %s - RAID %s\n\n",
				commafy(hostname), colorname(color), 
				(timestr ? timestr : "<No timestamp data>"),
				(summary ? summary : "OK"));
			addtostatus(msgline);
			if (STRBUFLEN(alerttext) > 0) {
				addtostrstatus(alerttext);
				addtostatus("\n\n");
			}
			addtostatus("============================ /proc/mdstat ===========================\n\n");
			addtostatus(mdstatstr);
			finish_status();
		}
	
		xfree(statcopy);
		freestrbuffer(alerttext);
	}

	splitmsg_done();
}

