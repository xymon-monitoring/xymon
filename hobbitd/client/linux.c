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

static char linux_rcsid[] = "$Id: linux.c,v 1.14 2006-04-19 20:17:44 henrik Exp $";

void handle_linux_client(char *hostname, enum ostype_t os, namelist_t *hinfo, char *sender, time_t timestamp, char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *freestr;
	char *msgsstr;
	char *netstatstr;
	char *vmstatstr;
	char *ifstatstr;
	char *portsstr;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	freestr = getdata("free");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	vmstatstr = getdata("vmstat");
	portsstr = getdata("ports");

	unix_cpu_report(hostname, hinfo, fromline, timestr, uptimestr, whostr, psstr, topstr);
	unix_disk_report(hostname, hinfo, fromline, timestr, "Capacity", "Mounted", dfstr);

	if (freestr) {
		char *p;
		long memphystotal, memphysused, memphysfree,
		     memactused, memactfree,
		     memswaptotal, memswapused, memswapfree;

		memphystotal = memswaptotal = memphysused = memswapused = memactused = memactfree = -1;
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

		unix_memory_report(hostname, hinfo, fromline, timestr,
				   memphystotal, memphysused, memactused, memswaptotal, memswapused);
	}

	unix_procs_report(hostname, hinfo, fromline, timestr, "CMD", NULL, psstr);
	msgs_report(hostname, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, hinfo, fromline, timestr);

	unix_netstat_report(hostname, hinfo, "linux", netstatstr);
	unix_ifstat_report(hostname, hinfo, "linux", ifstatstr);
	unix_ports_report(hostname, hinfo, fromline, timestr, 3, 4, 5, portsstr);

	switch (os) {
	  case OS_LINUX22:
		unix_vmstat_report(hostname, hinfo, "linux22", vmstatstr);
		break;

	  case OS_RHEL3:
		unix_vmstat_report(hostname, hinfo, "rhel3", vmstatstr);
		break;

	  default:
		unix_vmstat_report(hostname, hinfo, "linux", vmstatstr);
		break;
	}
}

