/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for z/VM                                             */
/*                                                                            */
/* Copyright (C) 2005-2009 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2006-2008 Rich Smrcina                                       */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char zvm_rcsid[] = "$Id$";

static void zvm_cpu_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr,
                     char *cpuutilstr, char *uptimestr)
{
        char *p;
        float load1, loadyellow, loadred;
        int recentlimit, ancientlimit, maxclockdiff;
        int  uphour, upmin;
        char loadresult[100];
        char myupstr[100];
        long uptimesecs = -1;
        long upday;

        int cpucolor = COL_GREEN;

        char msgline[1024];
        strbuffer_t *upmsg;

	if (!want_msgtype(hinfo, MSG_CPU)) return;

        if (!cpuutilstr) return;
        if (!uptimestr) return;

        uptimesecs = 0;

        /*
         * z/VM: "Uptime: 1 Days, 13 Hours, 38 Minutes"
         */

	sscanf(uptimestr,"Uptime: %ld Days, %d Hours, %d Minutes", &upday, &uphour, &upmin);
        uptimesecs = upday * 86400;
        uptimesecs += 60*(60*uphour + upmin);
        sprintf(myupstr, "%s\n", uptimestr);

        /*
         *  Looking for average CPU Utilization in 'IND' command response
         *  AVGPROC-000% 
         */
        *loadresult = '\0';
        p = strstr(cpuutilstr, "AVGPROC-") + 8 ;
        if (p) {
                if (sscanf(p, "%f%%", &load1) == 1) {
                        sprintf(loadresult, "z/VM CPU Utilization %3.0f%%\n", load1);
                }
        }

        get_cpu_thresholds(hinfo, clientclass, &loadyellow, &loadred, &recentlimit, &ancientlimit, &maxclockdiff);

        upmsg = newstrbuffer(0);

        if (load1 > loadred) {
                cpucolor = COL_RED;
                addtobuffer(upmsg, "&red Load is CRITICAL\n");
        }
        else if (load1 > loadyellow) {
                cpucolor = COL_YELLOW;
                addtobuffer(upmsg, "&yellow Load is HIGH\n");
        }

        if ((uptimesecs != -1) && (recentlimit != -1) && (uptimesecs < recentlimit)) {
                if (cpucolor == COL_GREEN) cpucolor = COL_YELLOW;
                addtobuffer(upmsg, "&yellow Machine recently rebooted\n");
        }
        if ((uptimesecs != -1) && (ancientlimit != -1) && (uptimesecs > ancientlimit)) {
                if (cpucolor == COL_GREEN) cpucolor = COL_YELLOW;
                sprintf(msgline, "&yellow Machine has been up more than %d days\n", (ancientlimit / 86400));
                addtobuffer(upmsg, msgline);
        }

        init_status(cpucolor);
        sprintf(msgline, "status %s.cpu %s %s %s %s %s\n",
                commafy(hostname), colorname(cpucolor),
                (timestr ? timestr : "<no timestamp data>"),
                loadresult,
		myupstr,
		cpuutilstr); 
        addtostatus(msgline);
        if (STRBUFLEN(upmsg)) {
                addtostrstatus(upmsg);
                addtostatus("\n");
        }

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(upmsg);
}

static void zvm_paging_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr, char *cpuutilstr)
{
        char *p;
        int pagerate, pagingyellow, pagingred;
        char pagingresult[100];

        int pagingcolor = COL_GREEN;
        char msgline[256];
        strbuffer_t *upmsg;

        if (!cpuutilstr) return;
        /*
         *  Looking for Paging rate info in 'IND' command response
         *  PAGING-0000/SEC
         */
        *pagingresult = '\0';
	/*  Skip past three newlines in message to the PAGING text  */
	p=strstr(cpuutilstr,"PAGING-") + 7;
	if (sscanf(p, "%d/SEC", &pagerate) == 1) {
               	sprintf(pagingresult, "z/VM Paging Rate %d per second\n", pagerate);
        }

        get_paging_thresholds(hinfo, clientclass, &pagingyellow, &pagingred);

        upmsg = newstrbuffer(0);

        if (pagerate > pagingred) {
                pagingcolor = COL_RED;
                addtobuffer(upmsg, "&red Paging Rate is CRITICAL\n");
        }
        else if (pagerate > pagingyellow) {
                pagingcolor = COL_YELLOW;
                addtobuffer(upmsg, "&yellow Paging Rate is HIGH\n");
        }

        init_status(pagingcolor);
        sprintf(msgline, "status %s.paging %s %s %s %s\n",
                commafy(hostname), colorname(pagingcolor),
                (timestr ? timestr : "<no timestamp data>"),
                pagingresult,
                cpuutilstr);
        addtostatus(msgline);
        if (STRBUFLEN(upmsg)) {
                addtostrstatus(upmsg);
                addtostatus("\n");
        }

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(upmsg);
}

static void zvm_mdc_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr, char *cpuutilstr)
{
        char *p;
        int mdcreads, mdcwrites, mdchitpct;
        char mdcresult[100];

        char msgline[256];
        strbuffer_t *msg;

        if (!cpuutilstr) return;
        msg = newstrbuffer(0);

        /*
         *  Looking for MDC info in 'IND' command response
         *  MDC READS-000001/SEC WRITES-000001/SEC HIT RATIO-098%
         */
        *mdcresult = '\0';
        /*  Skip past three newlines in message to the PAGING text  */
        p=strstr(cpuutilstr,"READS-");
        if (p) {
		p += 6;
        	sscanf(p, "%d/SEC", &mdcreads);
        	p=strstr(cpuutilstr,"WRITES-") + 7;
        	sscanf(p, "%d/SEC", &mdcwrites);
        	p=strstr(cpuutilstr,"RATIO-") + 6;
        	sscanf(p, "%d", &mdchitpct);

	        sprintf(msgline, "data %s.mdc\n%s\n%d:%d:%d\n", commafy(hostname), osname(os), mdcreads, mdcwrites, mdchitpct);
        	addtobuffer(msg, msgline);
		sendmessage(STRBUF(msg), NULL, BBTALK_TIMEOUT, NULL);
        	}

        freestrbuffer(msg);
}

static void zvm_users_report(char *hostname, char *clientclass, enum ostype_t os,
                         void *hinfo, char *fromline, char *timestr,
		         char *psstr)
{
        int pscolor = COL_GREEN;

        int pchecks;
        int cmdofs = -1;
        char msgline[4096];
        strbuffer_t *monmsg;
        static strbuffer_t *countdata = NULL;
        int anycountdata = 0;
        char *group;

        if (!want_msgtype(hinfo, MSG_PROCS)) return;
        if (!psstr) return;

        if (!countdata) countdata = newstrbuffer(0);

        clearalertgroups();
        monmsg = newstrbuffer(0);

        sprintf(msgline, "data %s.proccounts\n", commafy(hostname));
        addtobuffer(countdata, msgline);

	cmdofs = 0;   /*  Command offset for z/VM isn't necessary  */

        pchecks = clear_process_counts(hinfo, clientclass);

        if (pchecks == 0) {
                /* Nothing to check */
                sprintf(msgline, "&%s No process checks defined\n", colorname(noreportcolor));
                addtobuffer(monmsg, msgline);
                pscolor = noreportcolor;
        }
        else if (cmdofs >= 0) {
                /* Count how many instances of each monitored process is running */
                char *pname, *pid, *bol, *nl;
                int pcount, pmin, pmax, pcolor, ptrack;

                bol = psstr;
                while (bol) {
                        nl = strchr(bol, '\n');

                        /* Take care - the ps output line may be shorter than what we look at */
                        if (nl) {
                                *nl = '\0';

                                if ((nl-bol) > cmdofs) add_process_count(bol+cmdofs);

                                *nl = '\n';
                                bol = nl+1;
                        }
                        else {
                                if (strlen(bol) > cmdofs) add_process_count(bol+cmdofs);

                                bol = NULL;
                        }
                }

                /* Check the number found for each monitored process */
                while ((pname = check_process_count(&pcount, &pmin, &pmax, &pcolor, &pid, &ptrack, &group)) != NULL) {
                        char limtxt[1024];

                        if (pmax == -1) {
                                if (pmin > 0) sprintf(limtxt, "%d or more", pmin);
                                else if (pmin == 0) sprintf(limtxt, "none");
                        }
                        else {
                                if (pmin > 0) sprintf(limtxt, "between %d and %d", pmin, pmax);
                                else if (pmin == 0) sprintf(limtxt, "at most %d", pmax);
                        }

                        if (pcolor == COL_GREEN) {
                                sprintf(msgline, "&green %s (found %d, req. %s)\n", pname, pcount, limtxt);
                                addtobuffer(monmsg, msgline);
                        }
                        else {
                                if (pcolor > pscolor) pscolor = pcolor;
                                sprintf(msgline, "&%s %s (found %d, req. %s)\n",
                                        colorname(pcolor), pname, pcount, limtxt);
                                addtobuffer(monmsg, msgline);
                                addalertgroup(group);
                        }

                        if (ptrack) {
                                /* Save the count data for later DATA message to track process counts */
                                if (!pid) pid = "default";
                                sprintf(msgline, "%s:%u\n", pid, pcount);
                                addtobuffer(countdata, msgline);
                                anycountdata = 1;
                        }
                }
        }
        else {
                pscolor = COL_YELLOW;
                sprintf(msgline, "&yellow Expected string not found in ps output header\n");
                addtobuffer(monmsg, msgline);
        }

        /* Now we know the result, so generate a status message */
        init_status(pscolor);

        group = getalertgroups();
        if (group) sprintf(msgline, "status/group:%s ", group); else strcpy(msgline, "status ");
        addtostatus(msgline);

        sprintf(msgline, "%s.procs %s %s - Processes %s\n",
                commafy(hostname), colorname(pscolor),
                (timestr ? timestr : "<No timestamp data>"),
                ((pscolor == COL_GREEN) ? "OK" : "NOT ok"));
        addtostatus(msgline);

        /* And add the info about what's wrong */
        if (STRBUFLEN(monmsg)) {
                addtostrstatus(monmsg);
                addtostatus("\n");
        }

        /* And the full virtual machine names output for those who want it */
        if (pslistinprocs) {
                /*
                 * Format the list of virtual machines into four per line, 
                 * this list could be fairly long.
                 */
                char *tmpstr, *tok, *nm[4];
		int nmidx = 0;

		/*  Make a copy of psstr, strtok() will be changing it  */
		tmpstr = strdup(psstr);

		/*  Use strtok() to split string into pieces delimited by newline  */
		tok = strtok(tmpstr, "\n");

		while (tok) {
                        nm[nmidx++] = tok;

			if (nmidx == 4) {
				sprintf(msgline, "%-8s %-8s %-8s %-8s\n", nm[0], nm[1], nm[2], nm[3]);
				addtostatus(msgline);
				nmidx = 0;
				nm[0] = nm[1] = nm[2] = nm[3] = " ";
			}
			tok = strtok(NULL, "\n");
		}
		/*  Print any remaining names  */
		if (nmidx > 0) {
			sprintf(msgline, "%-8s %-8s %-8s %-8s\n", nm[0], nm[1], nm[2], nm[3]);
			addtostatus(msgline);
		}

		free(tmpstr);
        }

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(monmsg);

        if (anycountdata) sendmessage(STRBUF(countdata), NULL, BBTALK_TIMEOUT, NULL);
        clearstrbuffer(countdata);
}


void handle_zvm_client(char *hostname, char *clienttype, enum ostype_t os, 
			 void *hinfo, char *sender, time_t timestamp,
			 char *clientdata)
{
	char *timestr;
	char *cpuutilstr;
	char *uptimestr;
	char *msgcachestr;
	char *dfstr;
	char *usersstr;		/* Logged on z/VM Users  */
	char *msgsstr;
	char *netstatstr;
	char *ifstatstr;
	char *portsstr;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	cpuutilstr = getdata("cpu");
	msgcachestr = getdata("msgcache");
	dfstr = getdata("df");
	usersstr = getdata("UserID");
	msgsstr = getdata("msgs");
	portsstr = getdata("ports");
	ifstatstr = getdata("ifstat");

	zvm_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, cpuutilstr, uptimestr);
	zvm_paging_report(hostname, clienttype, os, hinfo, fromline, timestr, cpuutilstr);
        zvm_mdc_report(hostname, clienttype, os, hinfo, fromline, timestr, cpuutilstr);
	zvm_users_report(hostname, clienttype, os, hinfo, fromline, timestr, usersstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Available", "Capacity", "Mounted", dfstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);


	splitmsg_done();
}
