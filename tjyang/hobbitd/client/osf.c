/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for OSF                                              */
/*                                                                            */
/* Copyright (C) 2005-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char osf_rcsid[] = "$Id: osf.c 6125 2009-02-12 13:09:34Z storner $";

void handle_osf_client(char *hostname, char *clienttype, enum ostype_t os, 
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
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *portsstr;
	char *vmstatstr;
	char *memorystr;
	char *swapstr;

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
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	portsstr = getdata("ports");
	vmstatstr = getdata("vmstat");
	memorystr = getdata("memory");
	swapstr = getdata("swap");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, msgcachestr, 
			whostr, 0, psstr, 0, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Available", "Capacity", "Mounted", dfstr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "CMD", "COMMAND", psstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	unix_vmstat_report(hostname, clienttype, os, hinfo, fromline, timestr, vmstatstr);

	if (memorystr && swapstr) {
		char *p, *bol;
		long phystotal, physfree, swaptotal, swapfree, pagecnt, pagesize;

		/*
		 * Total Physical Memory =  5120.00 M
		 *	              =   655360 pages
		 *
		 * ...
		 *
		 * Managed Pages Break Down:
		 *
		 *        free pages = 499488
		 *
		 */

		phystotal = physfree = swaptotal = swapfree = -1;
		pagesize = 8; /* Default - appears to be the OSF/1 standard */

		bol = strstr(memorystr, "\nTotal Physical Memory =");
		if (bol) {
			p = strchr(bol, '=');
			phystotal = atol(p+1);
			bol = strchr(p, '\n');
			if (bol) {
				bol++;
				bol += strspn(bol, " \t");
				if (*bol == '=') {
					pagecnt = atol(bol+1);
					pagesize = (phystotal * 1024) / pagecnt;
				}
			}
		}

		bol = strstr(memorystr, "\nManaged Pages Break Down:");
		if (bol) {
			bol = strstr(bol, "free pages =");
			if (bol) {
				p = strchr(bol, '=');
				physfree = atol(p+1) * pagesize / 1024;
			}
		}

		bol = strstr(swapstr, "\nTotal swap allocation:");
		if (bol) {
			unsigned long swappages, freepages;
			int n1, n2;

			n1 = n2 = 0;
			p = strstr(bol, "Allocated space:");
			if (p) n1 = sscanf(p, "Allocated space: %lu pages", &swappages);
			p = strstr(bol, "Available space:");
			if (p) n2 = sscanf(p, "Available space: %lu pages", &freepages);
			if ((n1 == 1) && (n2 == 1)) {
				swaptotal = swappages * pagesize / 1024;
				swapfree = freepages  * pagesize / 1024;
			}
		}

		unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
				   phystotal, (phystotal - physfree), -1, 
				   swaptotal, (swaptotal - swapfree));
	}
}

