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

static char solaris_rcsid[] = "$Id: solaris.c,v 1.1 2005-07-20 05:42:15 henrik Exp $";

void handle_solaris_client(char *hostname, char *sender, time_t timestamp, char *clientdata)
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
	char *netstatstr;
	char *vmstatstr;

	char *p;

	unsigned long memphystotal, memphysfree, memswapused, memswapfree;

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
	netstatstr = getdata("netstat");
	vmstatstr = getdata("vmstat");

	combo_start();

	unix_cpu_report(hostname, fromline, timestr, uptimestr, whostr, psstr, topstr);
	unix_disk_report(hostname, fromline, timestr, dfstr);

	memphystotal = memphysfree = memswapfree = memswapused = -1;
	p = strstr(prtconfstr, "\nMemory size:");
	if (p && (sscanf(p, "\nMemory size: %ld Megabytes", &memphystotal) == 1)) ;
	if (memorystr && (sscanf(memorystr, "%*d %*d %*d %*d %ld", &memphysfree) == 1)) memphysfree /= 1024;
	p = strchr(swapstr, '=');
	if (p && sscanf(p, "= %ldk used, %ldk available", &memswapused, &memswapfree) == 2) {
		memswapused /= 1024;
		memswapfree /= 1024;
	}
	if (memphystotal && memphysfree && memswapused && memswapfree) {
		unsigned long memphysused = memphystotal - memphysfree;
		unsigned long memswaptotal = memswapused + memswapfree;

		unix_memory_report(hostname, fromline, timestr,
				   memphystotal, memphysused, -1,
				   memswaptotal, memswapused);
	}

	combo_end();

	unix_netstat_report(hostname, "solaris", netstatstr);
	unix_vmstat_report(hostname, "solaris", vmstatstr);
}

