/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for Darwin / Mac OS X                                */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char darwin_rcsid[] = "$Id: darwin.c,v 1.5 2006-04-20 07:11:16 henrik Exp $";

void handle_darwin_client(char *hostname, namelist_t *hinfo, char *sender, time_t timestamp, char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *meminfostr;
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *portsstr;

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
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	portsstr = getdata("ports");

	unix_cpu_report(hostname, hinfo, fromline, timestr, uptimestr, whostr, psstr, topstr);
	unix_disk_report(hostname, hinfo, fromline, timestr, "Capacity", "Mounted", dfstr);

	if (meminfostr) {
		unsigned long pagesfree, pagesactive, pagesinactive, pageswireddown, pgsize;
		char *p;

		pagesfree = pagesactive = pagesinactive = pageswireddown = pgsize = -1;

		p = strstr(meminfostr, "page size of");
		if (p && (sscanf(p, "page size of %lu bytes", &pgsize) == 1)) pgsize /= 1024;

		if (pgsize != -1) {
			p = strstr(meminfostr, "\nPages free:");
			if (p) p = strchr(p, ':'); if (p) pagesfree = atol(p+1);
			p = strstr(meminfostr, "\nPages active:");
			if (p) p = strchr(p, ':'); if (p) pagesactive = atol(p+1);
			p = strstr(meminfostr, "\nPages inactive:");
			if (p) p = strchr(p, ':'); if (p) pagesinactive = atol(p+1);
			p = strstr(meminfostr, "\nPages wired down:");
			if (p) p = strchr(p, ':'); if (p) pageswireddown = atol(p+1);

			if ((pagesfree >= 0) && (pagesactive >= 0) && (pagesinactive >= 0) && (pageswireddown >= 0)) {
				unsigned long memphystotal, memphysused;

				memphystotal = (pagesfree+pagesactive+pagesinactive+pageswireddown);
				memphystotal = memphystotal * pgsize / 1024;

				memphysused  = (pagesactive+pagesinactive+pageswireddown);
				memphysused  = memphysused * pgsize / 1024;

				unix_memory_report(hostname, hinfo, fromline, timestr,
						   memphystotal, memphysused, -1, -1, -1);
			}
		}
	}

	unix_procs_report(hostname, hinfo, fromline, timestr, "COMMAND", NULL, psstr);
	msgs_report(hostname, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, hinfo, fromline, timestr);

	unix_netstat_report(hostname, hinfo, "darwin", netstatstr);
	unix_ifstat_report(hostname, hinfo, "darwin", ifstatstr);
	unix_ports_report(hostname, hinfo, fromline, timestr, 3, 4, 5, portsstr);
}

