/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for Solaris                                          */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char solaris_rcsid[] = "$Id: solaris.c,v 1.9 2005-11-10 21:19:56 henrik Exp $";

void handle_solaris_client(char *hostname, namelist_t *hinfo, char *sender, time_t timestamp, char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *prtconfstr;
	char *memorystr;
	char *swapstr;
	char *dfstr;
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *vmstatstr;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	prtconfstr = getdata("prtconf");
	memorystr = getdata("memory");
	swapstr = getdata("swap");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	vmstatstr = getdata("vmstat");

	combo_start();

	unix_cpu_report(hostname, hinfo, fromline, timestr, uptimestr, whostr, psstr, topstr);
	unix_disk_report(hostname, hinfo, fromline, timestr, "Capacity", "Mounted", dfstr);

	if (prtconfstr && memorystr && swapstr) {
		long memphystotal, memphysfree, memswapused, memswapfree;
		char *p;

		memphystotal = memphysfree = memswapfree = memswapused = -1;
		p = strstr(prtconfstr, "\nMemory size:");
		if (p && (sscanf(p, "\nMemory size: %ld Megabytes", &memphystotal) == 1)) ;
		if (sscanf(memorystr, "%*d %*d %*d %*d %ld", &memphysfree) == 1) memphysfree /= 1024;
		p = strchr(swapstr, '=');
		if (p && sscanf(p, "= %ldk used, %ldk available", &memswapused, &memswapfree) == 2) {
			memswapused /= 1024;
			memswapfree /= 1024;
		}
		if ((memphystotal>=0) && (memphysfree>=0) && (memswapused>=0) && (memswapfree>=0)) {
			unix_memory_report(hostname, hinfo, fromline, timestr,
					   memphystotal, (memphystotal - memphysfree), -1,
					   (memswapused + memswapfree), memswapused);
		}
	}

	unix_procs_report(hostname, hinfo, fromline, timestr, "CMD", "COMMAND", psstr);
	msgs_report(hostname, hinfo, fromline, timestr, msgsstr);

	combo_end();

	unix_netstat_report(hostname, hinfo, "solaris", netstatstr);
	unix_ifstat_report(hostname, hinfo, "solaris", ifstatstr);
	unix_vmstat_report(hostname, hinfo, "solaris", vmstatstr);
}

