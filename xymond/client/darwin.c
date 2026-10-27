/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for Darwin / Mac OS X                                */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char darwin_rcsid[] = "$Id$";

/* The count after a vm_stat label, "\nPages free:" and the like, or -1 when the line is absent */
static long darwin_pagecount(char *meminfostr, char *label)
{
	char *p = strstr(meminfostr, label);

	if (!p) return -1;
	p = strchr(p, ':');
	return (p ? atol(p+1) : -1);
}

void handle_darwin_client(char *hostname, char *clienttype, enum ostype_t os, 
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
	char *inodestr;
	char *meminfostr;
	char *swapusagestr;
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *portsstr;

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
	inodestr = getdata("inode");
	meminfostr = getdata("meminfo");
	swapusagestr = getdata("swapusage");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	portsstr = getdata("ports");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, msgcachestr, 
			whostr, 0, psstr, 0, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Avail", "Capacity", "Mounted", dfstr);
	unix_inode_report(hostname, clienttype, os, hinfo, fromline, timestr, "ifree", "%iused", "Mounted", inodestr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "COMMAND", NULL, psstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);
	deltacount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	/* No vmstat on Darwin */

	if (meminfostr) {
		long pgsize = -1;
		long pagesfree, pagesactive, pagesinactive, pagesspeculative, pageswireddown, pagescompressor;
		char *p;

		p = strstr(meminfostr, "page size of");
		if (p && (sscanf(p, "page size of %ld bytes", &pgsize) == 1)) pgsize /= 1024;

		pagesfree        = darwin_pagecount(meminfostr, "\nPages free:");
		pagesactive      = darwin_pagecount(meminfostr, "\nPages active:");
		pagesinactive    = darwin_pagecount(meminfostr, "\nPages inactive:");
		pagesspeculative = darwin_pagecount(meminfostr, "\nPages speculative:");
		pageswireddown   = darwin_pagecount(meminfostr, "\nPages wired down:");
		pagescompressor  = darwin_pagecount(meminfostr, "\nPages occupied by compressor:");

		if ((pgsize > 0) && (pagesfree >= 0) && (pagesactive >= 0) && (pagesinactive >= 0) && (pageswireddown >= 0)) {
			long memphystotal, memphysused, memactused;
			long memswaptotal = -1, memswapused = -1;

			/* vm_stat before OS X 10.9 prints neither line; the pages are then counted elsewhere */
			if (pagesspeculative < 0) pagesspeculative = 0;
			if (pagescompressor < 0) pagescompressor = 0;

			/*
			 * Real/Physical counts the inactive and speculative pages as used, like
			 * the page cache in the Linux row; Actual/Virtual leaves them out, since
			 * the kernel reclaims them on demand, and keeps the compressor's pages,
			 * which hold live data of processes.
			 */
			memphystotal = (pagesfree+pagesactive+pagesinactive+pagesspeculative+pageswireddown+pagescompressor) * pgsize / 1024;
			memphysused  = (pagesactive+pagesinactive+pagesspeculative+pageswireddown+pagescompressor) * pgsize / 1024;
			memactused   = (pagesactive+pageswireddown+pagescompressor) * pgsize / 1024;

			/* sysctl vm.swapusage: "total = 2048.00M  used = 1536.00M  free = 512.00M  (encrypted)" */
			if (swapusagestr) {
				double stot, sused;
				char utot, uused;

				p = strstr(swapusagestr, "total =");
				if (p && (sscanf(p, "total = %lf%c used = %lf%c", &stot, &utot, &sused, &uused) == 4) &&
				    (utot == 'M') && (uused == 'M')) {
					memswaptotal = (long)stot;
					memswapused  = (long)sused;
				}
			}

			unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
					   memphystotal, memphysused,
					   memphystotal, memactused,
					   memswaptotal, memswapused);
		}
	}

	splitmsg_done();
}

