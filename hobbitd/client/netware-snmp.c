/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for Netware/SNMP                                     */
/*                                                                            */
/* Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char netware_snmp__rcsid[] = "$Id: netware-snmp.c,v 1.1 2007-01-15 14:22:30 henrik Exp $";

void handle_netware_snmp_client(char *hostname, char *clienttype, enum ostype_t os, 
				namelist_t *hinfo, char *sender, time_t timestamp,
				char *clientdata)
{
	char *nlmstr;
	char *dfstr;
	char *datestr;
	char *uptimestr;
	char *memorystr;
	char *netstatstr;
	char *portsstr;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	nlmstr = getdata("nlm");
	dfstr = getdata("df");
	datestr = getdata("date");
	uptimestr = getdata("uptime");
	memorystr = getdata("memory");
	netstatstr = getdata("netstat");
	portsstr = getdata("ports");

	/* Must tweak the datestr slightly */
	{
		char *p;
		
		p = datestr + strcspn(datestr, "\r\n"); *p = '\0';
		p = strchr(datestr, ','); if (p) *p = ' ';
	}

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, datestr, uptimestr, NULL, NULL, NULL, NULL, NULL);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, datestr, "Available", "Use%", "Mounted", dfstr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, datestr, "CMD", NULL, nlmstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, datestr, 1, 2, 3, portsstr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, datestr, netstatstr);

#if 0
	if (memorystr) {
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

		unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
				   memphystotal, memphysused, memactused, memswaptotal, memswapused);
	}
#endif

}

