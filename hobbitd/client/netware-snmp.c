/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for Netware/SNMP                                     */
/*                                                                            */
/* Copyright (C) 2005-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char netware_snmp__rcsid[] = "$Id: netware-snmp.c,v 1.8 2008-01-03 10:11:16 henrik Exp $";

void handle_netware_snmp_client(char *hostname, char *clienttype, enum ostype_t os, 
				void *hinfo, char *sender, time_t timestamp,
				char *clientdata)
{
	char *nlmstr;
	char *dfstr;
	char *datestr;
	char *uptimestr;
	char *memorystr;
	char *netstatstr;
	char *portsstr;
	char *cpqhealthstr;

	static pcre *countexp = NULL;
	int  pscount = 0, usercount = 0;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	nlmstr = getdata("nlm");
	dfstr = getdata("df");
	datestr = getdata("date");
	uptimestr = getdata("uptime");
	memorystr = getdata("memory");
	netstatstr = getdata("netstat");
	portsstr = getdata("ports");
	cpqhealthstr = getdata("cpqhealth");

	if (!datestr) return;

	/* Must tweak the datestr slightly */
	{
		char *p;
		
		p = datestr + strcspn(datestr, "\r\n"); *p = '\0';
		p = strchr(datestr, ','); if (p) *p = ' ';
	}

	if (uptimestr) {
		/* Since we only have the counts for number of processes and users, we must get these ourselves */
		if (!countexp) countexp = compileregex(", (\\d+) users, (\\d+) procs,");
		{
			char *buf = strdup(uptimestr);
			char *c1, *c2;

			c1 = c2 = NULL;
			if (pickdata(buf, countexp, 0, &c1, &c2)) {
				if (c1) { usercount = atoi(c1); xfree(c1); }
				if (c2) { pscount = atoi(c2); xfree(c2); }
			}
			xfree(buf);
		}

		unix_cpu_report(hostname, clienttype, os, hinfo, fromline, datestr, uptimestr, NULL, NULL, 
				NULL, usercount, NULL, pscount, NULL);
	}

	if (dfstr) unix_disk_report(hostname, clienttype, os, hinfo, fromline, datestr, "Available", "Use%", "Mounted", dfstr);
	if (nlmstr) unix_procs_report(hostname, clienttype, os, hinfo, fromline, datestr, "CMD", NULL, nlmstr);
	if (portsstr) unix_ports_report(hostname, clienttype, os, hinfo, fromline, datestr, 1, 2, 3, portsstr);

	if (netstatstr) unix_netstat_report(hostname, clienttype, os, hinfo, fromline, datestr, netstatstr);

	if (memorystr) {
		char *p;
		long memphystotal, memphysused, memactused,
		     memallocated, memcodedata, memdos, memcachebufs;

		memphystotal = memphysused = memactused = -1;
		memallocated = memcodedata = memdos = memcachebufs = -1;

		/* The "Total" is at the beginning of the message */
		if (strncmp(memorystr, "Total ", 6) == 0) p = memorystr; else p = strstr(memorystr, "\nTotal ");
		if (p && ((p = strchr(p, ':')) != NULL)) memphystotal = atol(p+1);
		p = strstr(memorystr, "\nAllocated ");
		if (p && ((p = strchr(p, ':')) != NULL)) memallocated = atol(p+1);
		p = strstr(memorystr, "\nCode/Data ");
		if (p && ((p = strchr(p, ':')) != NULL)) memcodedata = atol(p+1);
		p = strstr(memorystr, "\nDOS memory ");
		if (p && ((p = strchr(p, ':')) != NULL)) memdos = atol(p+1);
		p = strstr(memorystr, "\nCache buffers ");
		if (p && ((p = strchr(p, ':')) != NULL)) memcachebufs = atol(p+1);
		if ((memallocated >= 0) && (memcodedata >= 0) && (memdos >= 0) && (memcachebufs >= 0)) {
			memphysused = (memallocated + memcodedata + memdos + memcachebufs);
			memactused = (memallocated + memcodedata + memdos);
		}

		if ((memphysused >= 0) && (memactused >= 0) && (memphystotal >= 0)) {
			unix_memory_report(hostname, clienttype, os, hinfo, fromline, datestr,
					   (memphystotal / 1024), (memphysused / 1024), (memactused / 1024), -1, -1);
		}
	}

	if (cpqhealthstr) {
		char *logtxt, *p, *summarystr;
		int cpqhealthcolor = COL_GREEN;
		time_t cpqhealthupdate = 0;
		char msgline[1024];

		logtxt = strstr(cpqhealthstr, "\n\n");
		if (!logtxt) logtxt = cpqhealthstr;

		p = cpqhealthstr;
		if (p && (strncmp(p, "Health:", 7)) == 0) {
			p += (7 + strspn(p+7, " \t"));
			cpqhealthcolor = parse_color(p);
			p = strchr(p, '\n'); if (p) p++;
		}
		if (p && (strncmp(p, "Last update:", 12)) == 0) {
			cpqhealthupdate = atoi(p+12);
			p = strchr(p, '\n'); if (p) p++;
		}

       		if ((cpqhealthupdate > 0) && (cpqhealthupdate < (getcurrenttime(NULL) - 5*86400))) {
			summarystr = "No recent events";
			cpqhealthcolor = COL_GREEN;
		}
		else {
			summarystr = "IML Eventlog problems";
		}

         	init_status(cpqhealthcolor);

		sprintf(msgline, "status %s.cpqiml %s %s - %s\n", 
			commafy(hostname), colorname(cpqhealthcolor),
                        (datestr ? datestr : "<No timestamp data>"), 
			summarystr);
                addtostatus(msgline);

		strftime(msgline, sizeof(msgline), "\nLast logentry made at %Y-%m-%d %H:%M\n\n", localtime(&cpqhealthupdate));
                addtostatus(msgline);

		addtostatus("Log details\n");
		addtostatus(logtxt);

                if (fromline) addtostatus(fromline);
                finish_status();
	}
}

