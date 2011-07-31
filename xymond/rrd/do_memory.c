/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char memory_rcsid[] = "$Id$";

static char *memory_params[]      = { "DS:realmempct:GAUGE:600:0:U", NULL };
static void *memory_tpl           = NULL;

/*
 * Use the R/B tree to hold names of the hosts
 * that we receive "memory" status from. When handling
 * "cpu" reports, those hosts that are in the tree do
 * NOT take memory data from the cpu data.
 */
RbtHandle memhosts;
int memhosts_init = 0;

static int get_mem_percent(char *l)
{
	char *p;

	p = strchr(l, '%');
	if (p == NULL) return 0;
	p--; while ( (p > l) && (isdigit((int) *p) || (*p == '.')) ) p--;

	return atoi(p+1);
}

void do_memory_rrd_update(time_t tstamp, char *hostname, char *testname, char *classname, char *pagepaths, int physval, int swapval, int actval)
{
	if (memory_tpl == NULL) memory_tpl = setup_template(memory_params);

	setupfn2("%s.%s.rrd", "memory", "real");
	sprintf(rrdvalues, "%d:%d", (int)tstamp, physval);
	create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);

	setupfn2("%s.%s.rrd", "memory", "swap");
	sprintf(rrdvalues, "%d:%d", (int)tstamp, swapval);
	create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);

	if ((actval >= 0) && (actval <= 100)) {
		setupfn2("%s.%s.rrd", "memory", "actual");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, actval);
		create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);
	}
}

/* bb-xsnmp.pl memory update - Marco Avissano */
void do_memory_rrd_update_router(time_t tstamp, char *hostname, char *testname, char *classname, char *pagepaths, int procval, int ioval, int fastval)
{
	if (memory_tpl == NULL) memory_tpl = setup_template(memory_params);

	if ((procval >= 0) && (procval <= 100)) { 
		setupfn2("%s.%s.rrd", "memory", "processor");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, procval);
		create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);
	}

	if ((ioval >= 0) && (ioval <= 100)) {  
		setupfn2("%s.%s.rrd", "memory", "io");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, ioval);
		create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);
	}

	if ((fastval >= 0) && (fastval <= 100)) {
		setupfn2("%s.%s.rrd", "memory", "fast");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, fastval);
		create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);
	}
}

int do_memory_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
	char *phys = NULL;
	char *swap = NULL;
	char *actual = NULL;
	RbtIterator hwalk;

	/* Log this hostname in the list of hosts we get true "memory" reports from. */
	if (!memhosts_init) { memhosts = rbtNew(string_compare); memhosts_init = 1; }
	hwalk = rbtFind(memhosts, hostname);
	if (hwalk == rbtEnd(memhosts)) {
		char *keyp = xstrdup(hostname);
		if (rbtInsert(memhosts, keyp, NULL)) {
			errprintf("Insert into memhosts failed\n");
		}
	}

	if (strstr(msg, "z/OS Memory Map")) {
		long j1, j2, j3;
		int csautil, ecsautil, sqautil, esqautil;
		char *p;

		/* z/OS Memory Utilization:
		 Area    Alloc     Used      HWM  Util
		 CSA      3524     3034     3436   86.1
		 ECSA    20172    19979    20014   99.0
		 SQA      1540      222      399   14.4
		 ESQA    13988     4436     4726   31.7  */
	
        	p = strstr(msg, "CSA ") + 4;
        	if (p) {
			sscanf(p, "%ld %ld %ld %d", &j1, &j2, &j3, &csautil);
                	}

        	p = strstr(msg, "ECSA ") + 5;
        	if (p) {
			sscanf(p, "%ld %ld %ld %d", &j1, &j2, &j3, &ecsautil);
                	}

        	p = strstr(msg, "SQA ") + 4;
        	if (p) {
			sscanf(p, "%ld %ld %ld %d", &j1, &j2, &j3, &sqautil);
                	}

        	p = strstr(msg, "ESQA ") + 5;
        	if (p) {
			sscanf(p, "%ld %ld %ld %d", &j1, &j2, &j3, &esqautil);
                	}

		if (memory_tpl == NULL) memory_tpl = setup_template(memory_params);

		setupfn2("%s.%s.rrd", "memory", "CSA");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, csautil);
		create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);

		setupfn2("%s.%s.rrd", "memory", "ECSA");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, ecsautil);
		create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);

		setupfn2("%s.%s.rrd", "memory", "SQA");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, sqautil);
		create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);

		setupfn2("%s.%s.rrd", "memory", "ESQA");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, esqautil);
		create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);

		return 0;
	}	  

	if (strstr(msg, "z/VSE VSIZE Utilization")) {
		char *p;
		float pctused;

        	p = strstr(msg, "Utilization ") + 12;
        	if (p) {
			sscanf(p, "%f%%", &pctused);
                	}

		if (memory_tpl == NULL) memory_tpl = setup_template(memory_params);

		setupfn2("%s.%s.rrd", "memory", "vsize");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, (int)pctused);
		create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);

		return 0;
	}	  

	if (strstr(msg, "bb-xsnmp.pl")) {  
		/* bb-xsnmp.pl memory update - Marco Avissano */

		/*               Cisco Routers memory report.
		 *  Aug 22 10:52:17 2006

		 *           Memory      Used      Total   Percentage
		 * green   Processor   2710556   13032864       20.80%
		 * green         I/O   1664400    4194304       39.68%
		 * green        Fast   1987024    8388608       23.69%
		 */

		char *proc, *io, *fast;

		proc = strstr(msg, "Processor"); if (proc == NULL) proc = strstr(msg, "Processor");
		io = strstr(msg, "I/O"); if (io == NULL) io = strstr(msg, "I/O");
		fast = strstr(msg, "Fast"); if (fast == NULL) fast = strstr(msg, "Fast");

		if (proc) {
			char *eoln;
			int procval = -1, ioval = -1, fastval = -1;

			eoln = strchr(proc, '\n'); if (eoln) *eoln = '\0';
			procval = get_mem_percent(proc);
			if (eoln) *eoln = '\n';

			if (io) {
				eoln = strchr(io, '\n'); if (eoln) *eoln = '\0';
				ioval = get_mem_percent(io);
				if (eoln) *eoln = '\n';
			}

			if (fast) {
				eoln = strchr(fast, '\n'); if (eoln) *eoln = '\0';
				fastval = get_mem_percent(fast);
				if (eoln) *eoln = '\n';
			}

			do_memory_rrd_update_router(tstamp, hostname, testname, classname, pagepaths, procval, ioval, fastval);
		}

		return 0;
	}

	if (strstr(msg, "Total Cache Buffers")) {
		/* Netware nwstat2bb memory report.
		 *
		 * some.host.com|memory|green||1111681798|1111681982|1111699982|0|0|127.0.0.1|-1||
		 * green Thu Mar 24 17:33:02 CET 2005 - Memory OK
		 * &green Original Cache Buffers                                   : 326622
		 * &green Total Cache Buffers                                      : 56%
		 * &green Dirty Cache Buffers                                      : 0%
		 * &green Long Term Cache Hit Percentage                           : 0%
		 * &green Least Recently Used (LRU) sitting time                   : 3 weeks, 2 days, 4 hours, 25 minutes, 36 seconds
		 */
		char *p;
		int val;

		p = strstr(msg, "Total Cache Buffers");
		if (p) {
			p = strchr(p, ':');
			if (p) {
				val = atoi(p+1);
				setupfn2("%s.%s.rrd", "memory", "tcb");
				sprintf(rrdvalues, "%d:%d", (int)tstamp, val);
				create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);
			}
		}

		p = strstr(msg, "Dirty Cache Buffers");
		if (p) {
			p = strchr(p, ':');
			if (p) {
				val = atoi(p+1);
				setupfn2("%s.%s.rrd", "memory", "dcb");
				sprintf(rrdvalues, "%d:%d", (int)tstamp, val);
				create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);
			}
		}

		p = strstr(msg, "Long Term Cache Hit Percentage");
		if (p) {
			p = strchr(p, ':');
			if (p) {
				val = atoi(p+1);
				setupfn2("%s.%s.rrd", "memory", "ltch");
				sprintf(rrdvalues, "%d:%d", (int)tstamp, val);
				create_and_update_rrd(hostname, testname, classname, pagepaths, memory_params, memory_tpl);
			}
		}

		return 0;
	}
	else {
		phys = strstr(msg, "Physical"); if (phys == NULL) phys = strstr(msg, "Real");
		swap = strstr(msg, "Swap"); if (swap == NULL) swap = strstr(msg, "Page");
		actual = strstr(msg, "Actual"); if (actual == NULL) actual = strstr(msg, "Virtual");

		if (phys) {
			char *eoln;
			int physval = -1, swapval = -1, actval = -1;

			eoln = strchr(phys, '\n'); if (eoln) *eoln = '\0';
			physval = get_mem_percent(phys);
			if (eoln) *eoln = '\n';

			if (swap) {
				eoln = strchr(swap, '\n'); if (eoln) *eoln = '\0';
				swapval = get_mem_percent(swap);
				if (eoln) *eoln = '\n';
			}

			if (actual) {
				eoln = strchr(actual, '\n'); if (eoln) *eoln = '\0';
				actval = get_mem_percent(actual);
				if (eoln) *eoln = '\n';
			}

			do_memory_rrd_update(tstamp, hostname, testname, classname, pagepaths, physval, swapval, actval);
		}
	}

	return 0;
}

