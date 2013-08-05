/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for Solaris                                          */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char solaris_rcsid[] = "$Id$";

void handle_solaris_client(char *hostname, char *clienttype, enum ostype_t os,
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
	char *prtconfstr;
	char *memorystr;
	char *swapstr;
	char *swapliststr;
	char *dfstr;
	char *inodestr;
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *portsstr;
	char *vmstatstr;
	char *iostatdiskstr;

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
	prtconfstr = getdata("prtconf");
	memorystr = getdata("memory");
	swapstr = getdata("swap");
	swapliststr = getdata("swaplist");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	portsstr = getdata("ports");
	vmstatstr = getdata("vmstat");
	iostatdiskstr = getdata("iostatdisk");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, msgcachestr, 
			whostr, 0, psstr, 0, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "avail", "capacity", "Mounted", dfstr);
	unix_inode_report(hostname, clienttype, os, hinfo, fromline, timestr, "ifree", "%iused", "Mounted", inodestr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "CMD", "COMMAND", psstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 0, 1, 6, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	unix_vmstat_report(hostname, clienttype, os, hinfo, fromline, timestr, vmstatstr);

	if (prtconfstr && memorystr && (swapstr || swapliststr)) {
		long memphystotal, memphysfree, memswapused, memswapfree;
		char *p;

		memphystotal = memphysfree = memswapfree = memswapused = -1;
		p = strstr(prtconfstr, "\nMemory size:");
		if (p && (sscanf(p, "\nMemory size: %ld Megabytes", &memphystotal) == 1)) ;
		if (sscanf(memorystr, "%*d %*d %*d %*d %ld", &memphysfree) == 1) memphysfree /= 1024;

		if (!swapliststr) {
			/*
			 * No "swap -l" output, so use what "swap -s" reports. 
			 * Xymon clients prior to 2010-Dec-14 (roughly 4.3.0 release) does not report "swap -l".
			 */
			p = strchr(swapstr, '=');
			if (p && sscanf(p, "= %ldk used, %ldk available", &memswapused, &memswapfree) == 2) {
				memswapused /= 1024;
				memswapfree /= 1024;
			}
		}
		else {
			/* We prefer using "swap -l" output since it matches what other system tools report */
			char *bol;
			long blktotal, blkfree;

			blktotal = blkfree = 0;

			bol = swapliststr;
			while (bol) {
				char *nl, *tmpline;

				nl = strchr(bol, '\n'); if (nl) *nl = '\0';
				tmpline = strdup(bol);
				/* According to the Solaris man-page for versions 8 thru 10, the "swap -l" output is always 5 columns */
				/* Note: getcolumn() is zero-based (thanks, Dominique Frise) */
				p = getcolumn(tmpline, 3);
				if (p) blktotal += atol(p);
				strcpy(tmpline, bol);
				p = getcolumn(tmpline, 4);
				if (p) blkfree += atol(p);
				xfree(tmpline);

				if (nl) {
					*nl = '\n';
					bol = nl+1;
				}
				else {
					bol = NULL;
				}
			}

			if ((blktotal > 0) && (blkfree > 0)) {
				/* Values from swap -l are numbers of 512-byte blocks. Convert to MB = N*512/(1024*1024) = N/2048 */
				memswapused = (blktotal - blkfree) / 2048;
				memswapfree = blkfree / 2048;
			}
		}

		if ((memphystotal>=0) && (memphysfree>=0) && (memswapused>=0) && (memswapfree>=0)) {
			unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
					   memphystotal, (memphystotal - memphysfree), -1,
					   (memswapused + memswapfree), memswapused);
		}
	}

	if (iostatdiskstr) {
		char msgline[1024];
		strbuffer_t *msg = newstrbuffer(0);
		char *p;

		p = strchr(iostatdiskstr, '\n'); 
		if (p) {
			p++;
			sprintf(msgline, "data %s.iostatdisk\n%s\n", commafy(hostname), osname(os));
			addtobuffer(msg, msgline);
			addtobuffer(msg, p);
			combo_add(msg);
		}
		freestrbuffer(msg);
	}

	splitmsg_done();
}

