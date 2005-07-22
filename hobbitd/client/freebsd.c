/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for FreeBSD                                          */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char freebsd_rcsid[] = "$Id: freebsd.c,v 1.4 2005-07-22 11:03:47 henrik Exp $";

void handle_freebsd_client(char *hostname, char *sender, time_t timestamp, char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *meminfostr;
	char *swapinfostr;
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
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	meminfostr = getdata("meminfo");
	swapinfostr = getdata("swapinfo");
	netstatstr = getdata("netstat");
	vmstatstr = getdata("vmstat");

	combo_start();

	unix_cpu_report(hostname, fromline, timestr, uptimestr, whostr, psstr, topstr);
	unix_disk_report(hostname, fromline, timestr, "Capacity", "Mounted on", dfstr);

	if (meminfostr && swapinfostr) {
		unsigned long memphystotal, memphysfree, memphysused;
		unsigned long memswaptotal, memswapfree, memswapused;
		int found = 0;

		p = strstr(meminfostr, "Total:"); if (p) { memphystotal = atol(p+6); found++; }
		p = strstr(meminfostr, "Free:");  if (p) { memphysfree  = atol(p+5); found++; }
		memphysused = memphystotal - memphysfree;

		memswaptotal = memswapfree = memswapused = 0;
		if (swapinfostr) {
			found++;
			p = strchr(swapinfostr, '\n'); /* Skip the header line */
			do {
				long stot, sused, sfree;
				char *bol = (p+1);
				p = strchr(bol, '\n'); if (p) *p = '\0';

				if (sscanf(bol, "%*s %ld %ld %ld", &stot, &sused, &sfree) == 3) {
					memswaptotal += stot;
					memswapused += sused;
					memswapfree += sfree;
				}

				if (p) *p = '\n';
			} while (p);

			memswaptotal /= 1024; memswapused /= 1024; memswapfree /= 1024;
		}

		if (found >= 2) {
			unix_memory_report(hostname, fromline, timestr,
				   memphystotal, memphysused, -1, memswaptotal, memswapused);
		}
	}

	unix_procs_report(hostname, fromline, timestr, "COMMAND", psstr);

	combo_end();

	unix_netstat_report(hostname, "freebsd", netstatstr);
	unix_vmstat_report(hostname, "freebsd", vmstatstr);
}

