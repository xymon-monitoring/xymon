/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for Solaris                                          */
/*                                                                            */
/* Copyright (C) 2005-2009 Henrik Storner <henrik@hswn.dk>                    */
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
	char *dfstr;
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
	prtconfstr = getdata("prtconf");
	memorystr = getdata("memory");
	swapstr = getdata("swap");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	portsstr = getdata("ports");
	vmstatstr = getdata("vmstat");
	iostatdiskstr = getdata("iostatdisk");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, msgcachestr, 
			whostr, 0, psstr, 0, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "avail", "capacity", "Mounted", dfstr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "CMD", "COMMAND", psstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 0, 1, 6, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	unix_vmstat_report(hostname, clienttype, os, hinfo, fromline, timestr, vmstatstr);

	if (prtconfstr && memorystr && swapstr) {
		long memphystotal, memphysfree, memswapused, memswapfree;
		char *p;

		memphystotal = memphysfree = memswapfree = memswapused = -1;
		p = strstr(prtconfstr, "\nMemory size:");
		if (p && (sscanf(p, "\nMemory size: %ld Megabytes", &memphystotal) == 1)) ;
		if (sscanf(memorystr, "%*d %*d %*d %*d %ld", &memphysfree) == 1) memphysfree /= 1024;
		p = strchr(swapstr, '=');
		if (p && sscanf(p, "= %ldk used, %ldk available", &memswapused, &memswapfree) == 2) {
			memswapused /= 1024;
			memswapfree /= 1024;
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
			sendmessage(STRBUF(msg), NULL, BBTALK_TIMEOUT, NULL);
		}
		freestrbuffer(msg);
	}

	splitmsg_done();
}

