/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for HP-UX                                            */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char hpux_rcsid[] = "$Id: hpux.c,v 1.1 2005-07-20 05:42:15 henrik Exp $";

void handle_hpux_client(char *hostname, char *sender, time_t timestamp, char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *netstatstr;
	char *vmstatstr;

	char *p;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	netstatstr = getdata("netstat");
	vmstatstr = getdata("vmstat");

	combo_start();

	unix_cpu_report(hostname, fromline, timestr, uptimestr, whostr, psstr, topstr);
	unix_disk_report(hostname, fromline, timestr, dfstr);

#if 0
	unix_memory_report(hostname, fromline, timestr,
			   memphystotal, memphysused, memactused, memswaptotal, memswapused);
#endif

	combo_end();

	unix_netstat_report(hostname, "hpux", netstatstr);
	unix_vmstat_report(hostname, "hpux", vmstatstr);
}

