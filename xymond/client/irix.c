/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for IRIX                                             */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char irix_rcsid[] = "$Id$";

void handle_irix_client(char *hostname, char *clienttype, enum ostype_t os, 
			void *hinfo, char *sender, time_t timestamp,
			char *clientdata)
{
	static pcre2_code *memptn = NULL;
	char *timestr;
	char *uptimestr;
	char *clockstr;
	char *msgcachestr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *msgsstr;
	char *netstatstr;
	// char *sarstr;
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
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	// sarstr = getdata("sar");
	portsstr = getdata("ports");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, msgcachestr, 
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
	/* unix_sar_report(hostname, clienttype, os, hinfo, fromline, timestr, sarstr); */

	if (topstr) {
		char *memline, *eoln = NULL;
		int res;
		pcre2_match_data *ovector;
		char w[20];
		PCRE2_SIZE l;
		long memphystotal = -1, memphysused = -1, memphysfree = 0,
		     memacttotal = -1, memactused = -1, memactfree = -1,
		     memswaptotal = -1, memswapused = -1, memswapfree = 0;

		if (!memptn) {
			memptn = compileregex("^Memory: (\\d+)M max, (\\d+)M avail, (\\d+)M free, (\\d+)M swap, (\\d+)M free swap");
		}

		ovector = pcre2_match_data_create(20, NULL);
		memline = strstr(topstr, "\nMemory:");
		if (memline) {
			memline++;
			eoln = strchr(memline, '\n'); if (eoln) *eoln = '\0';

			res = pcre2_match(memptn, memline, strlen(memline), 0, 0, ovector, NULL);
		}
		else res = -1;

		if (res > 1) {
			l = sizeof(w);
			pcre2_substring_copy_bynumber(ovector, 1, w, &l);
			memphystotal = atol(w);
		}
		if (res > 2) {
			l = sizeof(w);
			pcre2_substring_copy_bynumber(ovector, 2, w, &l);
			memactfree = atol(w);
			memacttotal = memphystotal;
			memactused = memphystotal - memactfree;
		}
		if (res > 3) {
			l = sizeof(w);
			pcre2_substring_copy_bynumber(ovector, 3, w, &l);
			memphysfree = atol(w);
			memphysused = memphystotal - memphysfree;
		}

		if (res > 4) {
			l = sizeof(w);
			pcre2_substring_copy_bynumber(ovector, 4, w, &l);
			memswaptotal = atol(w);
		}
		if (res > 5) {
			l = sizeof(w);
			pcre2_substring_copy_bynumber(ovector, 5, w, &l);
			memswapfree = atol(w);
		}
		memswapused = memswaptotal - memswapfree;
		pcre2_match_data_free(ovector);

		if (eoln) *eoln = '\n';

		unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
				   memphystotal, memphysused, memacttotal, memactused, memswaptotal, memswapused);
	}

	splitmsg_done();
}

