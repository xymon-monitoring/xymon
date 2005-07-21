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

static char hpux_rcsid[] = "$Id: hpux.c,v 1.4 2005-07-21 21:36:00 henrik Exp $";

void handle_hpux_client(char *hostname, char *sender, time_t timestamp, char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *memorystr;
	char *swapinfostr;
	char *netstatstr;
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
	netstatstr = getdata("netstat");
	vmstatstr = getdata("vmstat");

	combo_start();

	unix_cpu_report(hostname, fromline, timestr, uptimestr, whostr, psstr, topstr);
	unix_disk_report(hostname, fromline, timestr, "Capacity", "Mounted on", dfstr);

	if (memorystr && swapinfostr) {
		unsigned long memphystotal, memphysfree, memphysused;
		unsigned long memswaptotal, memswapfree, memswapused;
		int found = 0;

		p = strstr(memorystr, "Total:"); if (p) { memphystotal = atol(p+6); found++; }
		p = strstr(memorystr, "Free:");  if (p) { memphysfree  = atol(p+5); found++; }
		memphysused = memphystotal - memphysfree;

		p = strstr(swapinfostr, "\ntotal");
		if (p && (sscanf(p, "\ntotal %ld %ld %ld", &memswaptotal, &memswapused, &memswapfree) >= 2)) {
			found++;
		}

		if (found == 3) {
			unix_memory_report(hostname, fromline, timestr,
				   memphystotal, memphysused, -1, memswaptotal, memswapused);
		}
	}

	unix_procs_report(hostname, fromline, timestr, "COMMAND", psstr);

	combo_end();

	unix_netstat_report(hostname, "hpux", netstatstr);
	unix_vmstat_report(hostname, "hpux", vmstatstr);
}

