/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for OSF                                              */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char osf_rcsid[] = "$Id: osf.c,v 1.4 2005-08-11 20:49:40 henrik Exp $";

void handle_osf_client(char *hostname, enum ostype_t os, namelist_t *hinfo, char *sender, time_t timestamp, char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *msgsstr;
	char *netstatstr;
	char *vmstatstr;
	char *memorystr;
	char *swapstr;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	vmstatstr = getdata("vmstat");
	memorystr = getdata("memory");
	swapstr = getdata("swap");

	combo_start();

	unix_cpu_report(hostname, hinfo, fromline, timestr, uptimestr, whostr, psstr, topstr);
	unix_disk_report(hostname, hinfo, fromline, timestr, "Capacity", "Mounted", dfstr);
	unix_procs_report(hostname, hinfo, fromline, timestr, "CMD", NULL, psstr);
	msgs_report(hostname, hinfo, fromline, timestr, msgsstr);

	if (memorystr && swapstr) {
		char *p, *bol, *eoln;
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
			if (p) n1 = sscanf(p, "Allocated space: %ld pages", &swappages);
			p = strstr(bol, "Available space:");
			if (p) n2 = sscanf(p, "Available space: %ld pages", &freepages);
			if ((n1 == 1) && (n2 == 1)) {
				swaptotal = swappages * pagesize / 1024;
				swapfree = freepages  * pagesize / 1024;
			}
		}

		unix_memory_report(hostname, hinfo, fromline, timestr,
				   phystotal, (phystotal - physfree), -1, 
				   swaptotal, (swaptotal - swapfree));
	}

	combo_end();

	unix_netstat_report(hostname, hinfo, "osf", netstatstr);
	unix_vmstat_report(hostname, hinfo, "osf", vmstatstr);
}

