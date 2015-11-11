/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for SCO_SV                                           */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2006-2008 Charles Goyard <cg@fsck.Fr>                        */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char sco_sv_rcsid[] = "$Id$";

void handle_sco_sv_client(char *hostname, char *clienttype, enum ostype_t os, 
			  void *hinfo, char *sender, time_t timestamp,
			  char *clientdata)
{
        char *timestr;
        char *uptimestr;
        char *clockstr;
        char *proxystr;
        char *msgcachestr;
        char *whostr;
        char *psstr;
        char *topstr;
        char *dfstr;
        char *freememstr;
	char *memsizestr;
	char *swapstr;
        char *msgsstr;
        char *netstatstr;
        char *vmstatstr;
        char *ifstatstr;
        char *portsstr;
        char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);


        timestr = getdata("date");
        uptimestr = getdata("uptime");
        clockstr = getdata("clock");
        proxystr = getdata("proxy");
        msgcachestr = getdata("msgcache");
        whostr = getdata("who");
        psstr = getdata("ps");
        topstr = getdata("top");
        dfstr = getdata("df");
	memsizestr = getdata("memsize");
        freememstr = getdata("freemem");
	swapstr = getdata("swap");
        msgsstr = getdata("msgs");
        netstatstr = getdata("netstat");
        ifstatstr = getdata("ifstat");
        vmstatstr = getdata("vmstat");
        portsstr = getdata("ports");
	
	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, msgcachestr, proxystr, timestamp, 
			whostr, 0, psstr, 0, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Available", "Capacity", "Mounted", dfstr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "COMMAND", NULL, psstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);
	deltacount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	unix_vmstat_report(hostname, clienttype, os, hinfo, fromline, timestr, vmstatstr);
	
	if(freememstr && memsizestr && swapstr) {
		long memphystotal, memphysfree, memswaptotal, memswapfree;
		char *p;

		memphystotal = memphysfree = 0;
		memphystotal = (atol(memsizestr) / 1048576);
		if(sscanf(freememstr, "%*s %ld %ld %*d %*d", &memphysfree, &memswapfree) == 2)
			memphysfree /= 256; /* comes in 4kb pages */
		else
			memphysfree = -1;
		
	        memswaptotal = memswapfree = 0;
                if (swapstr) {
                        p = strchr(swapstr, '\n'); /* Skip the header line */
                        while (p) {
                                long stot, sfree;
                                char *bol;
                                
                                bol = p+1;
                                p = strchr(bol, '\n'); if (p) *p = '\0';

                                if (sscanf(bol, "%*s %*s %*d %ld %ld", &stot, &sfree) == 2) {
                                        memswaptotal += stot;
                                        memswapfree += sfree;
                                }

                                if (p) *p = '\n';
                        }
			memswaptotal /= 2048 ; memswapfree /= 2048;
                }
		unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
				   memphystotal, (memphystotal - memphysfree), -1, memswaptotal, (memswaptotal - memswapfree));
	}

	splitmsg_done();
}
