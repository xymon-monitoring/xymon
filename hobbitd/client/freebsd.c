/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for FreeBSD                                          */
/*                                                                            */
/* Copyright (C) 2005-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char freebsd_rcsid[] = "$Id: freebsd.c 5819 2008-09-30 16:37:31Z storner $";

void handle_freebsd_client(char *hostname, char *clienttype, enum ostype_t os, 
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
	char *meminfostr;
	char *swapinfostr;
	char *vmtotalstr;
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *portsstr;
	char *vmstatstr;

	char *p;
	char fromline[1024];

	unsigned long memphystotal = 0, memphysfree = 0, memphysused = 0;
	unsigned long memswaptotal = 0, memswapfree = 0, memswapused = 0;
	int found = 0;

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	clockstr = getdata("clockstr");
	msgcachestr = getdata("msgcache");
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	meminfostr = getdata("meminfo");
	swapinfostr = getdata("swapinfo");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	portsstr = getdata("ports");
	vmstatstr = getdata("vmstat");
	vmtotalstr = getdata("vmtotal");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, msgcachestr, 
			whostr, 0, psstr, 0, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Avail", "Capacity", "Mounted", dfstr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "COMMAND", NULL, psstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	unix_vmstat_report(hostname, clienttype, os, hinfo, fromline, timestr, vmstatstr);

	if (meminfostr) {
		p = strstr(meminfostr, "Total:"); if (p) { memphystotal = atol(p+6); found++; }
	}

	if (vmtotalstr) {
		p = strstr(vmtotalstr, "\nFree Memory Pages:");
		if (p) {
			memphysfree = atol(p + 18);
			found++;
		}
	}

	if ((found == 1) && meminfostr) {
		p = strstr(meminfostr, "Free:");  if (p) { memphysfree  = atol(p+5); found++; }
		memphysused = memphystotal - memphysfree;
	}

	if (swapinfostr) {
		found++;
		p = strchr(swapinfostr, '\n'); /* Skip the header line */
		while (p) {
			long stot, sused, sfree;
			char *bol;
				
			bol = p+1;
			p = strchr(bol, '\n'); if (p) *p = '\0';

			if (sscanf(bol, "%*s %ld %ld %ld", &stot, &sused, &sfree) == 3) {
				memswaptotal += stot;
				memswapused += sused;
				memswapfree += sfree;
			}

			if (p) *p = '\n';
		}

		memswaptotal /= 1024; memswapused /= 1024; memswapfree /= 1024;
	}

	if (found >= 2) {
		unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
			   memphystotal, memphysused, -1, memswaptotal, memswapused);
	}
}

