/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for HMDC/Win32                                       */
/*                                                                            */
/* Copyright (C) 2006-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char hmdc_rcsid[] = "$Id: hmdc.c,v 1.1 2008-08-02 08:41:11 henrik Exp $";

typedef struct process_info_t {
	int pid, ppid;
	float cpuload, memload;
	char *cmd;
} process_info_t;

RbtHandle pitree;


void handle_win32_hmdc_client(char *hostname, char *clienttype, enum ostype_t os, 
				void *hinfo, char *sender, time_t timestamp,
				char *clientdata)
{
	char *p;
	char *datestr;
	time_t clienttime = 0;
	char *cpustr;
	char *procstr;
	int processcount = 0;
	float cpuidle = 0.0;
	float cpuload = 0.0;
	strbuffer_t *unixpsstr = NULL;
	char ent[4096];

	char fromline[1024];
	RbtIterator handle;
	char timestr[20];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	p = getdata("Date");
	if (p) {
		/* The "Date" section also has the Unix clock timestamp */
		datestr = strdup(p);
		p = strchr(datestr, '\n'); 
		if (p) {
			*p = '\0';
			p++;
			p = strstr(p, "UTCtimeUNIX:");
			if (p) {
				p += strcspn(p, "0123456789\r\n");
				clienttime = atol(p);
			}
		}
	}
	strcpy(timestr, ctime(&clienttime));

	cpustr = getdata("CPU");
	procstr = getdata("Proc");
	if (cpustr && procstr) {
		char *boln, *eoln;

		int p_id, pp_id;
		float cload, mload;
		char cmd[1024];

		pitree = rbtNew(int_compare);

		boln = procstr;
		while (boln) {
			/* Processid : Filename : ParentProcessid */
			eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';

			if (sscanf(boln, "%d : %1023s : %d", &p_id, cmd, &pp_id) == 3) {
				process_info_t *newitem = (process_info_t *)calloc(1, sizeof(process_info_t));

				newitem->pid = p_id; newitem->ppid = pp_id; newitem->cmd = strdup(cmd);
				rbtInsert(pitree, &newitem->pid, newitem);
				processcount++;
			}

			if (eoln) { *eoln = '\n'; boln = eoln+1; } else boln = NULL;
		}

		boln = cpustr;
		while (boln) {
			process_info_t *newitem;
			int n;

			/* processname : processid : cpu-usage */
			eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';

			if (strncmp(boln, "Idle=", 5) == 0) {
				cpuidle = atof(boln+5);
				cpuload = 100 - cpuidle;
				goto nextline;
			}
			if (strncmp(boln, "Load=", 5) == 0) {
				cpuload = atof(boln+5);
				cpuidle = 100 - cpuload;
				goto nextline;
			}

			n = sscanf(boln, "%s : %d : %f : %f", cmd, &p_id, &cload, &mload);
			if (n == 3) mload = 0.0;
			if (n >= 3) {
				handle = rbtFind(pitree, &p_id);
				if (handle == rbtEnd(pitree)) {
					/* unknown process id */
					newitem = (process_info_t *)calloc(1, sizeof(process_info_t));

					newitem->pid = p_id; newitem->ppid = 0; newitem->cmd = strdup(cmd);
					rbtInsert(pitree, &newitem->pid, newitem);
					handle = rbtFind(pitree, &p_id);
					processcount++;
				}

				newitem = (process_info_t *)gettreeitem(pitree, handle);
				if (newitem) {
					newitem->cpuload = cload;
					newitem->memload = mload;
				}
			}
nextline:
			if (eoln) { *eoln = '\n'; boln = eoln+1; } else boln = NULL;
		}
	}

	/* PID PPID USER STARTED S PRI %CPU   TIME %MEM  RSZ  VSZ  CMD */
	unixpsstr = newstrbuffer(0);
	sprintf(ent, "%8s %8s %10s %10s %3s %3s %5s %10s %5s %8s %8s %s\n",
		"PID",
		"PPID",
		"USER",
		"STARTED",
		"S",
		"PRI",
		"%CPU",
		"TIME",
		"%MEM",
		"RSZ",
		"VSZ",
		"CMD");
	addtobuffer(unixpsstr, ent);

	for (handle = rbtBegin(pitree); (handle != rbtEnd(pitree)); handle = rbtNext(pitree, handle)) {
		process_info_t *itm = (process_info_t *)gettreeitem(pitree, handle);

		if (!itm) continue;

		sprintf(ent, "%8d %8d %10s %10s %3s %3d %5.2f %10s %5.2f %8d %8d %s\n",
			itm->pid, /* PID */
			itm->ppid, /* PPID */
			"-", /* USER */
			"-", /* STARTED */
			"-", /* S */
			0, /* PRI */
			itm->cpuload, /* %CPU */
			"-", /* TIME */
			0.0, /* %MEM */
			0, /* RSZ */
			0, /* VSZ */
			itm->cmd);
		addtobuffer(unixpsstr, ent);
	}

	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "CMD", NULL, STRBUF(unixpsstr));
	freestrbuffer(unixpsstr);

#if 0
	ipstr = getdata("IP");
	diskstr = getdata("Disk");
	memorystr = getdata("Memory");
	servicesstr = getdata("Services");
	netstatstr = getdata("netstat -s");
	ipconfigstr = getdata("ipconfig /all");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, datestr, uptimestr, NULL, msgcachestr, 
			whostr, 0, psstr, 0, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Available", "Capacity", "Mounted", dfstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	unix_vmstat_report(hostname, clienttype, os, hinfo, fromline, timestr, vmstatstr);

	if (memorystr) {
		char *p;
		long memphystotal, memphysused, memphysfree,
		     memvirttotal, memvirsused, memvirtfree,
		     mempagetotal, mempageused, mempagefree;

		memphystotal = memphysused = memphysfree = \
		memvirttotal = memvirsused = memvirtfree = \
		mempagetotal = mempageused = mempagefree = -1;

		p = strstr(memorystr, " Physical:");
		if (p && (sscanf(p, " Physical: %ld %ld", &memphysused, &memphystotal) == 2)) {
			memphysfree = memphystotal - memphysused;
		}
		p = strstr(memorystr, " Virtual:");
		if (p && (sscanf(p, " Virtual: %ld %ld", &memvirtused, &memvirttotal) == 2)) {
			memvirtfree = memvirttotal - memvirtused;
		}
		p = strstr(memorystr, " Page:");
		if (p && (sscanf(p, " Page: %ld %ld", &mempageused, &mempagetotal) == 2)) {
			mempagefree = mempagetotal = mempageused;
		}

		unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
				   memphystotal, memphysused, memactused, memswaptotal, memswapused);
	}
#endif

}

