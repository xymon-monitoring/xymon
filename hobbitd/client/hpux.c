/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for HP-UX                                            */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char hpux_rcsid[] = "$Id: hpux.c,v 1.11 2006-04-13 16:31:29 henrik Exp $";

void handle_hpux_client(char *hostname, namelist_t *hinfo, char *sender, time_t timestamp, char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *memorystr;
	char *swapinfostr;
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *vmstatstr;

	char *p;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	memorystr = getdata("memory");
	swapinfostr = getdata("swapinfo");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	vmstatstr = getdata("vmstat");

	combo_start();

	unix_cpu_report(hostname, hinfo, fromline, timestr, uptimestr, whostr, psstr, topstr);
	unix_disk_report(hostname, hinfo, fromline, timestr, "Capacity", "Mounted", dfstr);

	if (memorystr && swapinfostr) {
		unsigned long memphystotal, memphysfree, memphysused;
		unsigned long memswaptotal, memswapfree, memswapused;
		int found = 0;

		memphystotal = memphysfree = memphysused = 0;
		memswaptotal = memswapfree = memswapused = 0;

		p = strstr(memorystr, "Total:"); if (p) { memphystotal = atol(p+6); found++; }
		p = strstr(memorystr, "Free:");  if (p) { memphysfree  = atol(p+5); found++; }
		memphysused = memphystotal - memphysfree;

		p = strstr(swapinfostr, "\ntotal");
		if (p && (sscanf(p, "\ntotal %ld %ld %ld", &memswaptotal, &memswapused, &memswapfree) >= 2)) {
			found++;
		}

		if (found == 3) {
			unix_memory_report(hostname, hinfo, fromline, timestr,
				   memphystotal, memphysused, -1, memswaptotal, memswapused);
		}
	}

	unix_procs_report(hostname, hinfo, fromline, timestr, "COMMAND", NULL, psstr);
	msgs_report(hostname, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, hinfo, fromline, timestr);

	combo_end();

	unix_netstat_report(hostname, hinfo, "hpux", netstatstr);
	unix_ifstat_report(hostname, hinfo, "hpux", ifstatstr);
	unix_vmstat_report(hostname, hinfo, "hpux", vmstatstr);
}

