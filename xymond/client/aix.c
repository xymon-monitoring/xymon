/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for AIX                                              */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char aix_rcsid[] = "$Id$";

void handle_aix_client(char *hostname, char *clienttype, enum ostype_t os,
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
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *portsstr;
	char *vmstatstr;
	char *realmemstr;
	char *freememstr;
	char *swapmemstr;

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
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	portsstr = getdata("ports");
	vmstatstr = getdata("vmstat");
	realmemstr = getdata("realmem");
	freememstr = getdata("freemem");
	swapmemstr = getdata("swap");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, msgcachestr, 
			whostr, 0, psstr, 0, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Free", "%Used", "Mounted", dfstr);
	unix_inode_report(hostname, clienttype, os, hinfo, fromline, timestr, "avail", "%iused", "Mounted", inodestr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "COMMAND", "CMD", psstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);
	deltacount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_vmstat_report(hostname, clienttype, os, hinfo, fromline, timestr, vmstatstr);

	if (realmemstr && freememstr && swapmemstr) {
		long memphystotal = 0, memphysfree = 0, memswaptotal = 0, memswappct = 0;
		char *p;

		if (strncmp(realmemstr, "realmem ", 8) == 0) memphystotal = atol(realmemstr+8) / 1024L;
		if (sscanf(freememstr, "%*d %*d %*d %ld", &memphysfree) == 1) memphysfree /= 256L;

		p = strchr(swapmemstr, '\n'); if (p) p++;
		if (p && (sscanf(p, " %ldMB %ld%%", &memswaptotal, &memswappct) != 2)) {
			memswaptotal = memswappct = -1L;
		}

		unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
				memphystotal, (memphystotal - memphysfree), -1L,
				memswaptotal, ((memswaptotal * memswappct) / 100L));
	}

	splitmsg_done();
}

