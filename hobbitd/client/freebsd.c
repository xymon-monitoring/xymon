/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for FreeBSD                                          */
/*                                                                            */
/* Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char freebsd_rcsid[] = "$Id: freebsd.c,v 1.20 2007-01-16 10:02:58 henrik Exp $";

void handle_freebsd_client(char *hostname, char *clienttype, enum ostype_t os, 
			   namelist_t *hinfo, char *sender, time_t timestamp,
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
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *portsstr;
	char *vmstatstr;

	char *p;
	char fromline[1024];

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

	if (meminfostr && swapinfostr) {
		unsigned long memphystotal, memphysfree, memphysused;
		unsigned long memswaptotal, memswapfree, memswapused;
		int found = 0;

		memphystotal = memphysfree = memphysused = 0;
		memswaptotal = memswapfree = memswapused = 0;

		p = strstr(meminfostr, "Total:"); if (p) { memphystotal = atol(p+6); found++; }
		p = strstr(meminfostr, "Free:");  if (p) { memphysfree  = atol(p+5); found++; }
		memphysused = memphystotal - memphysfree;

		memswaptotal = memswapfree = memswapused = 0;
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
}

