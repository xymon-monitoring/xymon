/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for BBWin/Windoes client                             */
/*                                                                            */
/* Copyright (C) 2006-2008 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2007-2008 Francois Lacroix				      */
/* Copyright (C) 2007-2008 Etienne Grignon <etienne.grignon@gmail.com>        */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbwin_rcsid[] = "$Id: bbwin.c,v 1.4 2008-01-03 10:10:39 henrik Exp $";

static void bbwin_uptime_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr,
                     char *uptimestr)
{
        char *p, *myuptimestr = NULL;
	float loadyellow, loadred;
        int recentlimit, ancientlimit, maxclockdiff;
        long uptimesecs = -1;
        int uptimecolor = COL_GREEN;
        char msgline[4096];
        strbuffer_t *upmsg;

	if (!want_msgtype(hinfo, MSG_CPU)) return;
        if (!uptimestr) return;

	dbgprintf("Uptime check host %s\n", hostname);

        uptimesecs = 0;

	/* Parse to check data */
        p = strstr(uptimestr, "sec:");
        if (p) {
		p += strcspn(p, "0123456789\r\n");
                uptimesecs = atol(p);
                dbgprintf("uptimestr [%d]\n", uptimesecs); /* DEBUG TODO REMOVE */
	}
	/* Parse to show a nice msg */
        myuptimestr = strchr(uptimestr, '\n');
        if (myuptimestr) {
		++myuptimestr;
        }
	get_cpu_thresholds(hinfo, clientclass, &loadyellow, &loadred, &recentlimit, &ancientlimit, &maxclockdiff);
	dbgprintf("DEBUG recentlimit: [%d] ancienlimit: [%d]\n", recentlimit, ancientlimit); /* DEBUG TODO REMOVE */

        upmsg = newstrbuffer(0);
        if ((uptimesecs != -1) && (recentlimit != -1) && (uptimesecs < recentlimit)) {
                if (uptimecolor == COL_GREEN) uptimecolor = COL_YELLOW;
                addtobuffer(upmsg, "&yellow Machine recently rebooted\n");
        }
        if ((uptimesecs != -1) && (ancientlimit != -1) && (uptimesecs > ancientlimit)) {
                if (uptimecolor == COL_GREEN) uptimecolor = COL_YELLOW;
                sprintf(msgline, "&yellow Machine has been up more than %d days\n", (ancientlimit / 86400));
                addtobuffer(upmsg, msgline);
        }

        init_status(uptimecolor);
        sprintf(msgline, "status %s.uptime %s %s %s\n",
                commafy(hostname), colorname(uptimecolor),
                (timestr ? timestr : "<No timestamp data>"),
                ((uptimecolor == COL_GREEN) ? "OK" : "NOT ok"));

        addtostatus(msgline);
	/* And add the info if pb */
        if (STRBUFLEN(upmsg)) {
                addtostrstatus(upmsg);
                addtostatus("\n");
        }
        /* And add the msg we recevied */
        if (myuptimestr) {
                addtostatus(myuptimestr);
                addtostatus("\n");
        }

	dbgprintf("msgline %s", msgline); /* DEBUG TODO REMOVE */ 

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(upmsg);
}


static void bbwin_cpu_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr,
                     char *cpuutilstr)
{
        char *p, *topstr;
        float load1, loadyellow, loadred;
        int recentlimit, ancientlimit, maxclockdiff;
        int cpucolor = COL_GREEN;

        char msgline[4096];
        strbuffer_t *cpumsg;

        if (!want_msgtype(hinfo, MSG_CPU)) return;
        if (!cpuutilstr) return;

	dbgprintf("CPU check host %s\n", hostname);	

	load1 = 0;

        p = strstr(cpuutilstr, "load=");
        if (p) {
                p += strcspn(p, "0123456789%\r\n");
                load1 = atol(p);
                dbgprintf("load1 [%d]\n", load1); /* DEBUG TODO REMOVE */ 
        }
	topstr = strstr(cpuutilstr, "CPU states");
	if (topstr) {
		*(topstr - 1) = '\0';
	}
	
	get_cpu_thresholds(hinfo, clientclass, &loadyellow, &loadred, &recentlimit, &ancientlimit, &maxclockdiff);
	dbgprintf("loadyellow: %d, loadred: %d\n", loadyellow, loadred);

        cpumsg = newstrbuffer(0);
        if (load1 > loadred) {
                cpucolor = COL_RED;
                addtobuffer(cpumsg, "&red Load is CRITICAL\n");
        }
        else if (load1 > loadyellow) {
                cpucolor = COL_YELLOW;
                addtobuffer(cpumsg, "&yellow Load is HIGH\n");
        }
        init_status(cpucolor);
        sprintf(msgline, "status %s.cpu %s %s %s",
                commafy(hostname), colorname(cpucolor),
                (timestr ? timestr : "<No timestamp data>"),
                cpuutilstr);

        addtostatus(msgline);
        /* And add the info if pb */
        if (STRBUFLEN(cpumsg)) {
                addtostrstatus(cpumsg);
                addtostatus("\n");
        }
	/* And add the msg we recevied */
        if (topstr) {
                addtostatus(topstr);
                addtostatus("\n");
        }

	dbgprintf("msgline %s", msgline); /* DEBUG TODO REMOVE */

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(cpumsg);
}

static void bbwin_clock_report(char *hostname, char *clientclass, enum ostype_t os,
                     void *hinfo, char *fromline, char *timestr,
                     char *clockstr, char *msgcachestr)
{
       	char *myclockstr;
        int clockcolor = COL_GREEN;
        float loadyellow, loadred;
        int recentlimit, ancientlimit, maxclockdiff;
        char msgline[4096];
        strbuffer_t *clockmsg;

        if (!want_msgtype(hinfo, MSG_CPU)) return;
        if (!clockstr) return;

	dbgprintf("Clock check host %s\n", hostname);

	clockmsg = newstrbuffer(0);

        myclockstr = strstr(clockstr, "local");
        if (myclockstr) {
                *(myclockstr - 1) = '\0';
        }

	get_cpu_thresholds(hinfo, clientclass, &loadyellow, &loadred, &recentlimit, &ancientlimit, &maxclockdiff);

        if (clockstr) {
                char *p;
                struct timeval clockval;

                p = strstr(clockstr, "epoch:");
                if (p && (sscanf(p, "epoch: %ld.%ld", (long int *)&clockval.tv_sec, (long int *)&clockval.tv_usec) == 2)) {
                        struct timeval clockdiff;
                        struct timezone tz;
                        int cachedelay = 0;

                        if (msgcachestr) {
                                /* Message passed through msgcache, so adjust for the cache delay */
                                p = strstr(msgcachestr, "Cachedelay:");
                                if (p) cachedelay = atoi(p+11);
                        }

                        gettimeofday(&clockdiff, &tz);
                        clockdiff.tv_sec -= (clockval.tv_sec + cachedelay);
                        clockdiff.tv_usec -= clockval.tv_usec;
                        if (clockdiff.tv_usec < 0) {
                                clockdiff.tv_usec += 1000000;
                                clockdiff.tv_sec -= 1;
                        }

                        if ((maxclockdiff > 0) && (abs(clockdiff.tv_sec) > maxclockdiff)) {
                                if (clockcolor == COL_GREEN) clockcolor = COL_YELLOW;
                                sprintf(msgline, "&yellow System clock is %ld seconds off (max %ld)\n",
                                        (long) clockdiff.tv_sec, (long) maxclockdiff);
                                addtobuffer(clockmsg, msgline);
                        }
                        else {
                                sprintf(msgline, "System clock is %ld seconds off\n", (long) clockdiff.tv_sec);
                                addtobuffer(clockmsg, msgline);
                        }
                }
        }

        init_status(clockcolor);
        sprintf(msgline, "status %s.timediff %s %s %s\n",
                commafy(hostname), colorname(clockcolor),
                (timestr ? timestr : "<No timestamp data>"),
                ((clockcolor == COL_GREEN) ? "OK" : "NOT ok"));

        addtostatus(msgline);
        /* And add the info if pb */
        if (STRBUFLEN(clockmsg)) {
                addtostrstatus(clockmsg);
                addtostatus("\n");
        }
        /* And add the msg we recevied */
        if (myclockstr) {
                addtostatus(myclockstr);
                addtostatus("\n");
        }

        dbgprintf("msgline %s", msgline); /* DEBUG TODO REMOVE */

        if (fromline && !localmode) addtostatus(fromline);
        finish_status();

        freestrbuffer(clockmsg);
}

void handle_win32_bbwin_client(char *hostname, char *clienttype, enum ostype_t os, 
			 void *hinfo, char *sender, time_t timestamp,
			 char *clientdata)
{
	char *timestr;
	char *cpuutilstr;
	char *uptimestr;
	char *clockstr;
	char *msgcachestr;
	char *diskstr;
	char *procsstr;	
	char *msgsstr;
	char *portsstr;
	char *memorystr;
	char *netstatstr;
	char *ifstatstr;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	/* Get all data by section timestr is the date time for all status */ 
	timestr = getdata("date");
	if (!timestr) return;	

	uptimestr = getdata("uptime");
	clockstr = getdata("clock");
	msgcachestr = getdata("msgcache"); /* TODO check when it is usefull */
	cpuutilstr = getdata("cpu");
	procsstr = getdata("procs");
	diskstr = getdata("disk");
	portsstr = getdata("ports");
	memorystr = getdata("memory");
	msgsstr = getdata("msg");
        netstatstr = getdata("netstat");
        ifstatstr = getdata("ifstat");	

	bbwin_uptime_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr);
	bbwin_clock_report(hostname, clienttype, os, hinfo, fromline, timestr, clockstr, msgcachestr); 
	bbwin_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, cpuutilstr);
        unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "Name", NULL, procsstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 1, 2, 3, portsstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Avail", "Capacity", "Filesystem", diskstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
        file_report(hostname, clienttype, os, hinfo, fromline, timestr);
        linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	/* Data status */
        unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
        unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);

        if (memorystr) {
                char *p;
                long memphystotal, memphysused,
                     memactused, memacttotal,
                     memswaptotal, memswapused;

                memphystotal = memswaptotal = memphysused = memswapused = memactused = memacttotal = -1;
                p = strstr(memorystr, "\nphysical:");
                if (p) sscanf(p, "\nphysical: %ld %ld", &memphystotal, &memphysused);
		p = strstr(memorystr, "\npage:");
                if (p) sscanf(p, "\npage: %ld %ld", &memswaptotal, &memswapused);
                p = strstr(memorystr, "\nvirtual:");
		if (p) sscanf(p, "\nvirtual: %ld %ld", &memacttotal, &memactused);
		dbgprintf("DEBUG Memory %ld %ld %ld %ld %ld\n", memphystotal, memphysused, memactused, memswaptotal, memswapused); /* DEBUG TODO Remove*/
                unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
                                   memphystotal, memphysused, memactused, memswaptotal, memswapused);
        }
}

