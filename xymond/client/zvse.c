/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for z/VSE or VSE/ESA                                 */
/*                                                                            */
/* Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2006-2008 Rich Smrcina                                       */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char zvse_rcsid[] = "$Id$";

static void zvse_cpu_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr,
                     char *cpuutilstr, char *uptimestr)
{
        char *p;
        float load1, loadyellow, loadred;
        int recentlimit, ancientlimit, uptimecolor, maxclockdiff, clockdiffcolor;
        int  uphour, upmin;
        char loadresult[100];
        char myupstr[100];
        long uptimesecs = -1;
        long upday;

        int cpucolor = COL_GREEN;

        char msgline[4096];
        strbuffer_t *upmsg;

        if (!want_msgtype(hinfo, MSG_CPU)) return;

        if (!uptimestr) return;
        if (!cpuutilstr) return;

        uptimesecs = 0;

        /*
         * z/VSE: "Uptime: 1 Days, 13 Hours, 38 Minutes"
         */

        sscanf(uptimestr,"Uptime: %ld Days, %d Hours, %d Minutes", &upday, &uphour, &upmin);
        uptimesecs = upday * 86400;
        uptimesecs += 60*(60*uphour + upmin);
        sprintf(myupstr, "%s\n", uptimestr);

        /*
         *  Looking for average CPU Utilization in CPU message
         *  Avg CPU=000%
         */
        *loadresult = '\0';
        p = strstr(cpuutilstr, "Avg CPU=") + 8 ;
        if (p) {
                if (sscanf(p, "%f%%", &load1) == 1) {
                        sprintf(loadresult, "z/VSE CPU Utilization %3.0f%%\n", load1);
                }
        }

        get_cpu_thresholds(hinfo, clientclass, &loadyellow, &loadred, &recentlimit, &ancientlimit, &uptimecolor, &maxclockdiff, &clockdiffcolor);

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
                if (cpucolor != COL_RED) cpucolor = uptimecolor;
                sprintf(msgline, "&%s Machine recently rebooted\n", colorname(uptimecolor));
                addtobuffer(upmsg, msgline);
        }
        if ((uptimesecs != -1) && (ancientlimit != -1) && (uptimesecs > ancientlimit)) {
                if (cpucolor != COL_RED) cpucolor = uptimecolor;
                sprintf(msgline, "&%s Machine has been up more than %d days\n", colorname(uptimecolor), (ancientlimit / 86400));
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

static void zvse_paging_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr, char *pagingstr)
{
	char *p;
        int ipagerate, pagingyellow, pagingred;
        float fpagerate=0.0;
        char pagingresult[100];

        int pagingcolor = COL_GREEN;
        char msgline[4096];
        strbuffer_t *upmsg;

        if (!pagingstr) return;
        /*
         *  Looking for Paging rate in message
         *  Page Rate=0.00 /sec
         */
        *pagingresult = '\0';

	ipagerate=0;
	p = strstr(pagingstr, "Page Rate=") + 10; 
	if (p) {
        	if (sscanf(p, "%f", &fpagerate) == 1) {
			ipagerate=fpagerate + 0.5;   /*  Rounding up */
                	sprintf(pagingresult, "z/VSE Paging Rate %d per second\n", ipagerate);
                	}
		}
	else
		sprintf(pagingresult, "Can not find page rate value in:\n%s\n", pagingstr);

        get_paging_thresholds(hinfo, clientclass, &pagingyellow, &pagingred);

        upmsg = newstrbuffer(0);

        if (ipagerate > pagingred) {
                pagingcolor = COL_RED;
                addtobuffer(upmsg, "&red Paging Rate is CRITICAL\n");
        }
        else if (ipagerate > pagingyellow) {
                pagingcolor = COL_YELLOW;
                addtobuffer(upmsg, "&yellow Paging Rate is HIGH\n");
        }

        init_status(pagingcolor);
        sprintf(msgline, "status %s.paging %s %s %s %s\n",
                commafy(hostname), colorname(pagingcolor),
                (timestr ? timestr : "<no timestamp data>"),
                pagingresult, pagingstr);
        addtostatus(msgline);
        if (STRBUFLEN(upmsg)) {
                addtostrstatus(upmsg);
                addtostatus("\n");
        }

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(upmsg);
}

static void zvse_cics_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr, char *cicsstr)
{
        char cicsappl[9], cicsdate[11], cicstime[9];
        int numtrans=0, cicsok=1;
        float dsapct=0.0;
        float edsapct=0.0;
        char cicsresult[100];
        char tempresult[100];
        char *cicsentry = NULL;

        int cicscolor = COL_GREEN;
        int dsayel, dsared, edsayel, edsared;
        char msgline[4096];
        char cicsokmsg[]="All CICS Systems OK";
        char cicsnotokmsg[]="One or more CICS Systems not OK";
        strbuffer_t *headline;
        strbuffer_t *upmsg;
        strbuffer_t *cicsmsg;

        if (!cicsstr) return;
        cicsmsg = newstrbuffer(0);
        upmsg   = newstrbuffer(0);
        headline= newstrbuffer(0);
        addtobuffer(headline, "Appl ID   Trans    DSA Pct    EDSA Pct\n");

        /*
         *
         *  Each CICS system reporting uses one line in the message, the format is:
         *  applid date time numtrans dsapct edsapct
         *  applid is the CICS application id
         *  date and time are the date and time of the report
         *  numtrans is the number of transactions that were executed in CICS since the last report
         *  dsapct  is the DSA  utilization percentage
         *  edsapct is the EDSA utilization percentage
         *
         */

        if (cicsstr) {
                cicsentry=strtok(cicsstr, "\n");
                while (cicsentry != NULL) {
                        sscanf(cicsentry, "%8s %10s %8s %d %f %f", cicsappl, cicsdate, cicstime, &numtrans, &dsapct, &edsapct);
                        sprintf(cicsresult,"%-8s %6d       %3.1f        %3.1f\n", cicsappl, numtrans, dsapct, edsapct);
                        addtobuffer(cicsmsg, cicsresult);
                        if (numtrans == -1 ) {
                                if (cicscolor < COL_YELLOW) cicscolor = COL_YELLOW;
                                cicsok=0;
                                sprintf(tempresult,"&yellow CICS system %s not responding, removed\n", cicsappl);
                                addtobuffer(upmsg, tempresult);
                                }

        /*  Get CICS thresholds for this application ID. */
                        get_cics_thresholds(hinfo, clientclass, cicsappl, &dsayel, &dsared, &edsayel, &edsared);

        /*  The threshold of the DSA values for each CICS must be checked in this loop.  */
                        if (dsapct > dsared) {
                                if (cicscolor < COL_RED) cicscolor = COL_RED;
                                cicsok=0;
                                sprintf(tempresult,"&red %s DSA Utilization is CRITICAL\n", cicsappl);
                                addtobuffer(upmsg, tempresult);
                        }
                        else if (dsapct > dsayel) {
                                if (cicscolor < COL_YELLOW) cicscolor = COL_YELLOW;
                                cicsok=0;
                                sprintf(tempresult,"&yellow %s DSA Utilization is HIGH\n", cicsappl);
                                addtobuffer(upmsg, tempresult);
                        }

                        if (edsapct > edsared) {
                                if (cicscolor < COL_RED) cicscolor = COL_RED;
                                cicsok=0;
                                sprintf(tempresult,"&red %s EDSA Utilization is CRITICAL\n", cicsappl);
                                addtobuffer(upmsg, tempresult);
                        }
                        else if (edsapct > edsayel) {
                                if (cicscolor < COL_YELLOW) cicscolor = COL_YELLOW;
                                cicsok=0;
                                sprintf(tempresult,"&yellow %s EDSA Utilization is HIGH\n", cicsappl);
                                addtobuffer(upmsg, tempresult);
                        }

                        init_status(cicscolor);
                        cicsentry=strtok(NULL, "\n");
                        }
                }

        sprintf(msgline, "status %s.cics %s %s %s\n",
                commafy(hostname), colorname(cicscolor),
                (timestr ? timestr : "<no timestamp data>"),
                ( (cicsok==1) ? cicsokmsg : cicsnotokmsg) );

        addtostatus(msgline);
        if (STRBUFLEN(upmsg)) {
                addtostrstatus(upmsg);
        }

        if (STRBUFLEN(cicsmsg)) {
                addtostrstatus(headline);
                addtostrstatus(cicsmsg);
                addtostatus("\n");
        }

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(headline);
        freestrbuffer(upmsg);
        freestrbuffer(cicsmsg);
}

static void zvse_jobs_report(char *hostname, char *clientclass, enum ostype_t os,
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

        cmdofs = 0;   /*  Command offset for z/VSE isn't necessary  */

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

        /* And the full list of jobs for those who want it */
        if (pslistinprocs) {
                /*
                 * Format the list of virtual machines into four per line,
                 * this list could be fairly long.
                 */
                char *tmpstr, *tok;

                /*  Make a copy of psstr, strtok() will be changing it  */
                tmpstr = strdup(psstr);

                /*  Use strtok() to split string into pieces delimited by newline  */
                tok = strtok(tmpstr, "\n");

                while (tok) {
                        sprintf(msgline, "%s\n", tok);
                        addtostatus(msgline);
                        tok = strtok(NULL, "\n");
                }

                free(tmpstr);
        }

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(monmsg);

        if (anycountdata) {
		combo_add(countdata);
	}
        clearstrbuffer(countdata);
}

static void zvse_memory_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr, char *memstr)
{
        int usedyellow, usedred;  /* Thresholds for total used system memory */
	int sysmemok=1;
	long totmem, availmem;
	float pctavail, pctused;
        char memorystr[1024];

        int memorycolor = COL_GREEN;
        char memokmsg[]="Memory OK";
        char memnotokmsg[]="Memory Not OK";
        char msgline[4096];

        strbuffer_t *upmsg;

        if (!memstr) return;
        upmsg     = newstrbuffer(0);
 
        /*
         *  The message is just two values, the total system memory and
	 *  the available memory; both values are in K. 
         *  tttttt aaaaaa
         */

        sscanf(memstr, "%ld %ld", &totmem, &availmem);
	pctavail = ((float)availmem / (float)totmem) * 100;
	pctused = 100 - pctavail;

        sprintf(memorystr, "z/VSE VSIZE Utilization %3.1f%%\nMemory Allocated %ldK, Memory Available %ldK\n", pctused, totmem, availmem);
        get_zvsevsize_thresholds(hinfo, clientclass, &usedyellow, &usedred);

        if (pctused > (float)usedred) {
                memorycolor = COL_RED;
		sysmemok=0;
                addtobuffer(upmsg, "&red VSIZE Utilization is CRITICAL\n");
        	}
        else if (pctused > (float)usedyellow) {
                memorycolor = COL_YELLOW;
		sysmemok=0;
                addtobuffer(upmsg, "&yellow VSIZE Utilization is HIGH\n");
        	}

        init_status(memorycolor);
        sprintf(msgline, "status %s.memory %s %s %s\n%s",
                commafy(hostname), colorname(memorycolor),
                (timestr ? timestr : "<no timestamp data>"),
                ( (sysmemok==1) ? memokmsg : memnotokmsg ),
                memorystr);

        addtostatus(msgline);
        if (STRBUFLEN(upmsg)) {
                addtostrstatus(upmsg);
                addtostatus("\n");
        	}

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(upmsg);
}

static void zvse_getvis_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr, char *gvstr)
{
        char *q;
        int gv24yel, gv24red, gvanyyel, gvanyred;  /* Thresholds for getvis  */
        int getvisok=1;
        float used24p, usedanyp;
        char jinfo[11], pid[4], jobname[9];
        int size24, used24, free24, sizeany, usedany, freeany;
        char *getvisentry = NULL;
        char tempresult[128];
        char getvisresult[128];

        char msgline[4096];
        int memorycolor = COL_GREEN;
        char getvisokmsg[]="Getvis OK";
        char getvisnotokmsg[]="Getvis Not OK";

        strbuffer_t *getvismsg;
        strbuffer_t *upmsg;
        strbuffer_t *headline;

	if (!gvstr) return;
        getvismsg = newstrbuffer(0);
        upmsg     = newstrbuffer(0);
        headline  = newstrbuffer(0);

        /*
         *  The getvis message is a table if the partitions requested including the SVA.
         *  The format of the table is:
         *
         *   Partition    Used/24  Free/24  Used/Any  Free/Any
         *   SVA              748     1500      2056      5604
         *   F1               824      264       824       264
         *   Z1-CICSICCF     7160     3844     27516      4992
         *   O1-CICS1        5912     5092     31584     19352
         */

        addtobuffer(headline, "z/VSE Getvis Map\nPID Jobname    Size24    Used24    Free24    SizeAny    UsedAny    FreeAny Used24% UsedAny%\n");

        getvisentry=strtok(gvstr, "\n");
        getvisentry=strtok(NULL, "\n");    /*  Skip heading line */
        while (getvisentry != NULL) {
                sscanf(getvisentry, "%s %d %d %d %d", jinfo, &used24, &free24, &usedany, &freeany);
                q = strchr(jinfo, '-');              /* Check if jobname passed  */
                if (q) {
                        strncpy(pid, jinfo, 2);          /*  Copy partition ID  */
                        q++;                             /*  Increment pointer  */
			strcpy(jobname,q);		 /*  Copy jobname       */
                        }
                else {
                        strcpy(pid,jinfo);            /* Just copy jinfo into partition ID */
			strcpy(jobname, "-       ");  /* Jobname placeholder               */
                        }
                size24  = used24  + free24;
                sizeany = usedany + freeany;
                used24p  = ( (float)used24  / (float)size24  ) * 100;
                usedanyp = ( (float)usedany / (float)sizeany ) * 100;
                sprintf(getvisresult,"%-3s %-8s   %6d    %6d    %6d     %6d     %6d     %6d    %3.0f      %3.0f\n", pid, jobname, size24, used24, free24, sizeany, usedany, freeany, used24p, usedanyp);
                get_zvsegetvis_thresholds(hinfo, clientclass, pid, &gv24yel, &gv24red, &gvanyyel, &gvanyred);

                if (used24p > (float)gv24red) {
                        memorycolor = COL_RED;
                        getvisok=0;
                        sprintf(tempresult,"&red 24-bit Getvis utilization for %s is CRITICAL\n", pid);
                        addtobuffer(upmsg, tempresult);
                        }
                else if (used24p > (float)gv24yel) {
                        memorycolor = COL_YELLOW;
                        getvisok=0;
                        sprintf(tempresult,"&yellow 24-bit Getvis utilization for %s is HIGH\n", pid);
                        addtobuffer(upmsg, tempresult);
                        }

                if (usedanyp > (float)gvanyred) {
                        memorycolor = COL_RED;
                        getvisok=0;
                        sprintf(tempresult,"&red Any Getvis utilization for %s is CRITICAL\n", pid);
                        addtobuffer(upmsg, tempresult);
                        }
                else if (usedanyp > (float)gvanyyel) {
                        memorycolor = COL_YELLOW;
                        getvisok=0;
                        sprintf(tempresult,"&yellow Any Getvis utilization for %s is HIGH\n", pid);
                        addtobuffer(upmsg, tempresult);
                        }

                addtobuffer(getvismsg, getvisresult);
                getvisentry=strtok(NULL, "\n");
                }

        init_status(memorycolor);
        sprintf(msgline, "status %s.getvis %s %s %s\n",
                commafy(hostname), colorname(memorycolor),
                (timestr ? timestr : "<no timestamp data>"),
                ( (getvisok==1) ? getvisokmsg : getvisnotokmsg ) );

        addtostatus(msgline);
        if (STRBUFLEN(upmsg)) {
                addtostrstatus(upmsg);
                addtostatus("\n");
        }

        if (STRBUFLEN(getvismsg)) {
                addtostrstatus(headline);
                addtostrstatus(getvismsg);
                addtostatus("\n");
        }

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(headline);
        freestrbuffer(upmsg);
        freestrbuffer(getvismsg);
}

void zvse_nparts_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr, char *npartstr)
{
	char npdispstr[256];
        long nparts, runparts, partsavail;
	int npartsyellow, npartsred;
        float partutil;

        int npartcolor = COL_GREEN;
        char msgline[4096];
        strbuffer_t *upmsg;

        if (!npartstr) return;

        sscanf(npartstr, "%ld %ld", &nparts, &runparts);

        /*
         *  The nparts message is two values that indicate the maximum number of partitions
	 *  configured in the system (based on the NPARTS value in the IPL proc) and
	 *  the number of partitions currently running jobs:
         *  The format of the data is:
         *
         *   nnnnnnn mmmmmmm
	 *
         */

        partsavail = nparts - runparts;
        partutil  = ((float)runparts / (float)nparts) * 100;

        get_asid_thresholds(hinfo, clientclass, &npartsyellow, &npartsred);

        upmsg = newstrbuffer(0);

        if ((int)partutil > npartsred) {
                if (npartcolor < COL_RED) npartcolor = COL_RED;
                addtobuffer(upmsg, "&red NPARTS Utilization is CRITICAL\n");
                }
        else if ((int)partutil > npartsyellow) {
                if (npartcolor < COL_YELLOW) npartcolor = COL_YELLOW;
                addtobuffer(upmsg, "&yellow NPARTS Utilization is HIGH\n");
                }

        *npdispstr = '\0';
        sprintf(npdispstr, "Nparts: %8ld  Free: %8ld  Used: %8ld  %3.1f\n",nparts,partsavail,runparts,partutil);

        init_status(npartcolor);
        sprintf(msgline, "status %s.nparts %s %s\n%s",
                commafy(hostname), colorname(npartcolor),
                (timestr ? timestr : "<no timestamp data>"),
                npdispstr);
        addtostatus(msgline);
        if (STRBUFLEN(upmsg)) {
                addtostrstatus(upmsg);
                addtostatus("\n");
        }

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(upmsg);
}

void handle_zvse_client(char *hostname, char *clienttype, enum ostype_t os, 
			 void *hinfo, char *sender, time_t timestamp,
			 char *clientdata)
{
	char *timestr;
	char *cpuutilstr;
	char *pagingstr;
	char *cicsstr;
	char *uptimestr;
	char *dfstr;
	char *jobsstr;		/* z/VSE Running jobs  */
	char *portsstr;
	char *memstr;		/* System Memory data  */
	char *gvstr;		/* GETVIS data	       */
	char *npartstr;		/* Num Parts	       */

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	cpuutilstr = getdata("cpu");
	pagingstr = getdata("paging");
	cicsstr = getdata("cics");
	dfstr = getdata("df");
	jobsstr = getdata("jobs");
	portsstr = getdata("ports");
	memstr = getdata("memory");
	gvstr = getdata("getvis");
	npartstr = getdata("nparts");

	zvse_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, cpuutilstr, uptimestr);
	zvse_paging_report(hostname, clienttype, os, hinfo, fromline, timestr, pagingstr);
	zvse_cics_report(hostname, clienttype, os, hinfo, fromline, timestr, cicsstr);
	zvse_jobs_report(hostname, clienttype, os, hinfo, fromline, timestr, jobsstr);
	zvse_memory_report(hostname, clienttype, os, hinfo, fromline, timestr, memstr);
	zvse_getvis_report(hostname, clienttype, os, hinfo, fromline, timestr, gvstr);
	zvse_nparts_report(hostname, clienttype, os, hinfo, fromline, timestr, npartstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Available", "Cap", "Mounted", dfstr);
  	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);


	splitmsg_done();
}

