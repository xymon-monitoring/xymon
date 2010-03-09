/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for z/VSE, VSE/ESA and z/OS                          */
/*                                                                            */
/* Copyright (C) 2005-2009 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2006-2009 Rich Smrcina                                       */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char zos_rcsid[] = "$Id: zos.c 6132 2009-02-16 12:58:11Z storner $";


void zos_cpu_report(char *hostname, char *clientclass, enum ostype_t os,
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

        char msgline[4096];
        strbuffer_t *upmsg;

        if (!want_msgtype(hinfo, MSG_CPU)) return;

        if (!uptimestr) return;
        if (!cpuutilstr) return;

        uptimesecs = 0;

        /*
         * z/OS: "Uptime: 1 Days, 13 Hours, 38 Minutes"
         */

        sscanf(uptimestr,"Uptime: %ld Days, %d Hours, %d Minutes", &upday, &uphour, &upmin);
        uptimesecs = upday * 86400;
        uptimesecs += 60*(60*uphour + upmin);
        sprintf(myupstr, "%s\n", uptimestr);

        /*
         *  Looking for average CPU Utilization in CPU message
         *  CPU Utilization=000%
         */
        *loadresult = '\0';
        p = strstr(cpuutilstr, "CPU Utilization ") + 16 ;
        if (p) {
                if (sscanf(p, "%f%%", &load1) == 1) {
                        sprintf(loadresult, "z/OS CPU Utilization %3.0f%%\n", load1);
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

void zos_paging_report(char *hostname, char *clientclass, enum ostype_t os,
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
         *  Paging Rate=0
         */
        *pagingresult = '\0';

	ipagerate=0;
	p = strstr(pagingstr, "Paging Rate ") + 12; 
	if (p) {
        	if (sscanf(p, "%f", &fpagerate) == 1) {
			ipagerate=fpagerate + 0.5;   /*  Rounding up */
                	sprintf(pagingresult, "z/OS Paging Rate %d per second\n", ipagerate);
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

void zos_memory_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr, char *memstr)
{
        char *p;
        char headstr[100], csastr[100], ecsastr[100], sqastr[100], esqastr[100];
	long csaalloc, csaused, csahwm, ecsaalloc, ecsaused, ecsahwm;
	long sqaalloc, sqaused, sqahwm, esqaalloc, esqaused, esqahwm;
        float csautil, ecsautil, sqautil, esqautil;
	int csayellow, csared, ecsayellow, ecsared, sqayellow, sqared, esqayellow, esqared;

        int memcolor = COL_GREEN;
        char msgline[4096];
        strbuffer_t *upmsg;

        if (!memstr) return;
        /*
         *  Looking for memory eyecatchers in message
         */

        p = strstr(memstr, "CSA ") + 4;
        if (p) {
                sscanf(p, "%ld %ld %ld", &csaalloc, &csaused, &csahwm);
                }

        p = strstr(memstr, "ECSA ") + 5;
        if (p) {
                sscanf(p, "%ld %ld %ld", &ecsaalloc, &ecsaused, &ecsahwm);
                }

        p = strstr(memstr, "SQA ") + 4;
        if (p) {
                sscanf(p, "%ld %ld %ld", &sqaalloc, &sqaused, &sqahwm);
                }

        p = strstr(memstr, "ESQA ") + 5;
        if (p) {
                sscanf(p, "%ld %ld %ld", &esqaalloc, &esqaused, &esqahwm);
                }

        csautil = ((float)csaused / (float)csaalloc) * 100; 
        ecsautil = ((float)ecsaused / (float)ecsaalloc) * 100; 
        sqautil = ((float)sqaused / (float)sqaalloc) * 100; 
        esqautil = ((float)esqaused / (float)esqaalloc) * 100;
	
	get_zos_memory_thresholds(hinfo, clientclass, &csayellow, &csared, &ecsayellow, &ecsared, &sqayellow, &sqared, &esqayellow, &esqared);

        upmsg = newstrbuffer(0);

	if (csautil > csared) {
		if (memcolor < COL_RED) memcolor = COL_RED;
		addtobuffer(upmsg, "&red CSA Utilization is CRITICAL\n");
		}
	else if (csautil > csayellow) {
		if (memcolor < COL_YELLOW) memcolor = COL_YELLOW;
		addtobuffer(upmsg, "&yellow CSA Utilization is HIGH\n");
		}
	if (ecsautil > ecsared) {
		if (memcolor < COL_RED) memcolor = COL_RED;
		addtobuffer(upmsg, "&red ECSA Utilization is CRITICAL\n");
		}
	else if (ecsautil > ecsayellow) {
		if (memcolor < COL_YELLOW) memcolor = COL_YELLOW;
		addtobuffer(upmsg, "&yellow ECSA Utilization is HIGH\n");
		}
	if (sqautil > sqared) {
		if (memcolor < COL_RED) memcolor = COL_RED;
		addtobuffer(upmsg, "&red SQA Utilization is CRITICAL\n");
		}
	else if (sqautil > sqayellow) {
		if (memcolor < COL_YELLOW) memcolor = COL_YELLOW;
		addtobuffer(upmsg, "&yellow SQA Utilization is HIGH\n");
		}
	if (esqautil > esqared) {
		if (memcolor < COL_RED) memcolor = COL_RED;
		addtobuffer(upmsg, "&red ESQA Utilization is CRITICAL\n");
		}
	else if (esqautil > esqayellow) {
		if (memcolor < COL_YELLOW) memcolor = COL_YELLOW;
		addtobuffer(upmsg, "&yellow ESQA Utilization is HIGH\n");
		}

        *headstr = '\0';
        *csastr = '\0';
        *ecsastr = '\0';
        *sqastr = '\0';
        *esqastr = '\0';
	strcpy(headstr, "z/OS Memory Map\n Area    Alloc     Used      HWM  Util%\n");
	sprintf(csastr, "CSA  %8ld %8ld %8ld   %3.1f\n", csaalloc, csaused, csahwm, csautil);
	sprintf(ecsastr, "ECSA %8ld %8ld %8ld   %3.1f\n", ecsaalloc, ecsaused, ecsahwm, ecsautil);
	sprintf(sqastr, "SQA  %8ld %8ld %8ld   %3.1f\n", sqaalloc, sqaused, sqahwm, sqautil);
	sprintf(esqastr, "ESQA %8ld %8ld %8ld   %3.1f\n", esqaalloc, esqaused, esqahwm, esqautil);

        init_status(memcolor);
        sprintf(msgline, "status %s.memory %s %s\n%s %s %s %s %s",
                commafy(hostname), colorname(memcolor),
                (timestr ? timestr : "<no timestamp data>"),
                headstr, csastr, ecsastr, sqastr, esqastr);
        addtostatus(msgline);
        if (STRBUFLEN(upmsg)) {
                addtostrstatus(upmsg);
                addtostatus("\n");
        }

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(upmsg);
}

static void zos_cics_report(char *hostname, char *clientclass, enum ostype_t os,
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

void zos_jobs_report(char *hostname, char *clientclass, enum ostype_t os,
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

        cmdofs = 0;   /*  Command offset for z/OS isn't necessary  */

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

        if (anycountdata) sendmessage(STRBUF(countdata), NULL, BBTALK_TIMEOUT, NULL);
        clearstrbuffer(countdata);
}

void zos_maxuser_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr, char *maxuserstr)
{
        char *p;
        char maxustr[256];
	long maxusers, maxufree, maxuused, rsvtstrt, rsvtfree, rsvtused, rsvnonr, rsvnfree, rsvnused;
	int maxyellow, maxred;
	float maxutil, rsvtutil, rsvnutil;

        int maxcolor = COL_GREEN;
        char msgline[4096];
        strbuffer_t *upmsg;

        if (!maxuserstr) return;
        /*
         *  Looking for eyecatchers in message
         */

        p = strstr(maxuserstr, "Maxusers: ") + 9;
        if (p) {
                sscanf(p, "%ld Free: %ld", &maxusers, &maxufree);
                }

        p = strstr(maxuserstr, "RSVTSTRT: ") + 9;
        if (p) {
                sscanf(p, "%ld Free: %ld", &rsvtstrt, &rsvtfree);
                }

        p = strstr(maxuserstr, "RSVNONR: ") + 8;
        if (p) {
                sscanf(p, "%ld Free: %ld", &rsvnonr, &rsvnfree);
                }

	maxuused = maxusers - maxufree;
	rsvtused = rsvtstrt - rsvtfree;
	rsvnused = rsvnonr  - rsvnfree;

	if ( maxuused == 0.0 )
		maxutil = 0;
	else
        	maxutil  = ((float)maxuused / (float)maxusers) * 100;

	if ( rsvtused == 0.0 )
		rsvtutil = 0;
	else
        	rsvtutil = ((float)rsvtused / (float)rsvtstrt) * 100;

	if ( rsvnused == 0.0 )
		rsvnutil = 0;
	else
        	rsvnutil = ((float)rsvnused / (float)rsvnonr)  * 100;

        get_asid_thresholds(hinfo, clientclass, &maxyellow, &maxred);

        upmsg = newstrbuffer(0);

        if ((int)maxutil > maxred) {
                if (maxcolor < COL_RED) maxcolor = COL_RED;
                addtobuffer(upmsg, "&red ASID (Maxuser) Utilization is CRITICAL\n");
                }
        else if ((int)maxutil > maxyellow) {
                if (maxcolor < COL_YELLOW) maxcolor = COL_YELLOW;
                addtobuffer(upmsg, "&yellow ASID (Maxuser) Utilization is HIGH\n");
                }

        *maxustr = '\0';
        sprintf(maxustr, " Maxuser: %8ld  Free: %8ld  Used: %8ld  %3.1f\nRSVTSTRT: %8ld  Free: %8ld  Used: %8ld  %3.1f\n RSVNONR: %8ld  Free: %8ld  Used: %8ld  %3.1f\n",maxusers,maxufree,maxuused,maxutil,rsvtstrt,rsvtfree,rsvtused,rsvtutil,rsvnonr,rsvnfree,rsvnused,rsvnutil);

        init_status(maxcolor);
        sprintf(msgline, "status %s.maxuser %s %s\n%s",
                commafy(hostname), colorname(maxcolor),
                (timestr ? timestr : "<no timestamp data>"),
                maxustr);
        addtostatus(msgline);
        if (STRBUFLEN(upmsg)) {
                addtostrstatus(upmsg);
                addtostatus("\n");
        }

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(upmsg);
}

void handle_zos_client(char *hostname, char *clienttype, enum ostype_t os, 
			 void *hinfo, char *sender, time_t timestamp,
			 char *clientdata)
{
	char *timestr;
	char *cpuutilstr;
	char *pagingstr;
	char *uptimestr;
	char *msgcachestr;
	char *dfstr;
        char *cicsstr;          /* z/OS CICS Information */
	char *jobsstr;		/* z/OS Running jobs  */
	char *memstr;		/* z/OS Memory Utilization  */
	char *maxuserstr;	/* z/OS Maxuser */
	char *portsstr;
	char *ifstatstr;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	cpuutilstr = getdata("cpu");
	pagingstr = getdata("paging");
	msgcachestr = getdata("msgcache");
	dfstr = getdata("df");
        cicsstr = getdata("cics");
	jobsstr = getdata("jobs");
	memstr = getdata("memory");
	maxuserstr = getdata("maxuser");
	portsstr = getdata("ports");
	ifstatstr = getdata("ifstat");

	zos_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, cpuutilstr, uptimestr);
	zos_paging_report(hostname, clienttype, os, hinfo, fromline, timestr, pagingstr);
        zos_cics_report(hostname, clienttype, os, hinfo, fromline, timestr, cicsstr);
	zos_jobs_report(hostname, clienttype, os, hinfo, fromline, timestr, jobsstr);
	zos_memory_report(hostname, clienttype, os, hinfo, fromline, timestr, memstr);
	zos_maxuser_report(hostname, clienttype, os, hinfo, fromline, timestr, maxuserstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Available", "Cap", "Mounted", dfstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);

}

