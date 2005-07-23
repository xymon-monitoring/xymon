/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for Linux                                            */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char linux_rcsid[] = "$Id: linux.c,v 1.5 2005-07-23 19:30:39 henrik Exp $";

void handle_linux_client(char *hostname, namelist_t *hinfo, char *sender, time_t timestamp, char *clientdata)
{
	char *timestr;
	char *uptimestr, *uptimesecsstr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *freestr;
	char *msgsstr;
	char *netstatstr;
	char *vmstatstr;

	char *p;

	unsigned long memphystotal, memphysused, memphysfree,
		      memactused, memactfree,
		      memswaptotal, memswapused, memswapfree;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	uptimesecsstr = getdata("uptimesecs");
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	freestr = getdata("free");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	vmstatstr = getdata("vmstat");

	combo_start();

	unix_cpu_report(hostname, hinfo, fromline, timestr, uptimestr, whostr, psstr, topstr);
	unix_disk_report(hostname, hinfo, fromline, timestr, "Capacity", "Mounted on", dfstr);

	memphystotal = memswaptotal = memphysused = memswapused = memactused = -1;
	p = strstr(freestr, "\nMem:");
	if (p && (sscanf(p, "\nMem: %ld %ld %ld", &memphystotal, &memphysused, &memphysfree) == 3)) {
		memphystotal /= 1024;
		memphysused /= 1024;
		memphysfree /= 1024;
	}
	p = strstr(freestr, "\nSwap:");
	if (p && (sscanf(p, "\nSwap: %ld %ld %ld", &memswaptotal, &memswapused, &memswapfree) == 3)) {
		memswaptotal /= 1024;
		memswapused /= 1024;
		memswapfree /= 1024;
	}
	p = strstr(freestr, "\n-/+ buffers/cache:");
	if (p && (sscanf(p, "\n-/+ buffers/cache: %ld %ld", &memactused, &memactfree) == 2)) {
		memactused /= 1024;
		memactfree /= 1024;
	}
	else memactused = memactfree = -1;
	unix_memory_report(hostname, hinfo, fromline, timestr,
			   memphystotal, memphysused, memactused, memswaptotal, memswapused);

	unix_procs_report(hostname, hinfo, fromline, timestr, "CMD", psstr);
	msgs_report(hostname, hinfo, fromline, timestr, msgsstr);

	combo_end();

	unix_netstat_report(hostname, hinfo, "linux", netstatstr);
	unix_vmstat_report(hostname, hinfo, "linux", vmstatstr);
}

