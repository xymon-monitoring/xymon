/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for Darwin / Mac OS X                                */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char darwin_rcsid[] = "$Id$";

void handle_darwin_client(char *hostname, char *clienttype, enum ostype_t os, 
			  void *hinfo, char *sender, time_t timestamp, 
			  char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *clockstr;
	char *msgcachestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *inodestr;
	char *meminfostr;
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *portsstr;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	clockstr = getdata("clock");
	msgcachestr = getdata("msgcache");
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	inodestr = getdata("inode");
	meminfostr = getdata("meminfo");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	portsstr = getdata("ports");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, msgcachestr, 
			whostr, 0, psstr, 0, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Avail", "Capacity", "Mounted", dfstr);
	unix_inode_report(hostname, clienttype, os, hinfo, fromline, timestr, "ifree", "%iused", "Mounted", inodestr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "COMMAND", NULL, psstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);
	deltacount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	/* No vmstat on Darwin */

	if (meminfostr) {
		unsigned long pagesfree, pagesactive, pagesinactive, pageswireddown, pgsize;
		long memswaptotal, memswapused;
		char *p;

		pagesfree = pagesactive = pagesinactive = pageswireddown = pgsize = -1;
		memswaptotal = memswapused = -1;

		/*
		 * "vm.swapusage: total = 2048.00M  used = 1189.19M  free = 858.81M"
		 * Converted to MB. Trend data for the "swap" status below only - it is
		 * deliberately NOT fed to the memory report: macOS swap is dynamic, so
		 * a used/total percentage would false-alarm on healthy machines.
		 */
		p = strstr(meminfostr, "vm.swapusage:");
		if (p) {
			double swt, swu;
			char unt, unu;

			if (sscanf(p, "vm.swapusage: total = %lf%c used = %lf%c", &swt, &unt, &swu, &unu) == 4) {
				if (unt == 'K') swt /= 1024; else if (unt == 'G') swt *= 1024;
				if (unu == 'K') swu /= 1024; else if (unu == 'G') swu *= 1024;
				memswaptotal = (long)swt;
				memswapused  = (long)swu;
			}
		}

		/*
		 * Darwin-only "swap" status, driven by the kernel's memory-pressure
		 * verdict (kern.memorystatus_vm_pressure_level: 1=normal, 2=warn,
		 * 4=critical) - the signal Apple provides precisely because swap
		 * capacity numbers stopped meaning anything. The swap growth ceiling
		 * (container free space) is already covered by the disk report.
		 * Body lines are NCV-formatted, so graphing needs no new code. Use the
		 * split variant - one RRD per metric, so mixed units graph separately
		 * and future metrics need no DS surgery:
		 * TEST2RRD+=",swap=ncv" SPLITNCV_swap="Pressure:GAUGE,SwapUsedMB:GAUGE"
		 */
		p = strstr(meminfostr, "kern.memorystatus_vm_pressure_level:");
		if (p) {
			long presslevel = -1;
			int presscolor;
			char *pressname;
			char msgline[1024];

			sscanf(p, "kern.memorystatus_vm_pressure_level: %ld", &presslevel);
			if (presslevel == 1)      { presscolor = COL_GREEN;  pressname = "normal"; }
			else if (presslevel == 2) { presscolor = COL_YELLOW; pressname = "warn"; }
			else if (presslevel >= 4) { presscolor = COL_RED;    pressname = "critical"; }
			else                      { presscolor = COL_YELLOW; pressname = "unknown"; }

			init_status(presscolor);
			sprintf(msgline, "status %s.swap %s %s - Memory pressure %s\n",
				commafy(hostname), colorname(presscolor),
				(timestr ? timestr : "<no timestamp data>"), pressname);
			addtostatus(msgline);
			sprintf(msgline, "\nPressure : %ld\n", presslevel);
			addtostatus(msgline);
			if (memswapused != -1) {
				sprintf(msgline, "SwapUsedMB : %ld\nSwapTotalMB : %ld\n", memswapused, memswaptotal);
				addtostatus(msgline);
			}
			if (fromline && !localmode) addtostatus(fromline);
			finish_status();
		}

		p = strstr(meminfostr, "page size of");
		if (p && (sscanf(p, "page size of %lu bytes", &pgsize) == 1)) pgsize /= 1024;

		if (pgsize != -1) {
			p = strstr(meminfostr, "\nPages free:");
			if (p) p = strchr(p, ':'); if (p) pagesfree = atol(p+1);
			p = strstr(meminfostr, "\nPages active:");
			if (p) p = strchr(p, ':'); if (p) pagesactive = atol(p+1);
			p = strstr(meminfostr, "\nPages inactive:");
			if (p) p = strchr(p, ':'); if (p) pagesinactive = atol(p+1);
			p = strstr(meminfostr, "\nPages wired down:");
			if (p) p = strchr(p, ':'); if (p) pageswireddown = atol(p+1);

			if ((pagesfree >= 0) && (pagesactive >= 0) && (pagesinactive >= 0) && (pageswireddown >= 0)) {
				unsigned long memphystotal, memphysused;

				memphystotal = (pagesfree+pagesactive+pagesinactive+pageswireddown);
				memphystotal = memphystotal * pgsize / 1024;

				memphysused  = (pagesactive+pagesinactive+pageswireddown);
				memphysused  = memphysused * pgsize / 1024;

				unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
						   memphystotal, memphysused, 
						   -1, -1, 
						   -1, -1);
			}
		}
	}

	splitmsg_done();
}

