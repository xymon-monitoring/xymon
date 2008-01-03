/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char memory_rcsid[] = "$Id: do_memory.c,v 1.22 2008-01-03 10:13:50 henrik Exp $";

static char *memory_params[]      = { "DS:realmempct:GAUGE:600:0:U", NULL };
static char *memory_tpl           = NULL;

/*
 * Use the R/B tree to hold names of the hosts
 * that we receive "memory" status from. When handling
 * "cpu" reports, those hosts that are in the tree do
 * NOT take memory data from the cpu data.
 */
RbtHandle memhosts;
int memhosts_init = 0;
static int string_compare(void *a, void *b) {
	return strcmp((char *)a, (char *)b);
}

static int get_mem_percent(char *l)
{
	char *p;

	p = strchr(l, '%');
	if (p == NULL) return 0;
	p--; while ( (p > l) && isdigit((int) *p)) p--;

	return atoi(p+1);
}

void do_memory_rrd_update(time_t tstamp, char *hostname, char *testname, int physval, int swapval, int actval)
{
	if (memory_tpl == NULL) memory_tpl = setup_template(memory_params);

	setupfn2("%s.%s.rrd", "memory", "real");
	sprintf(rrdvalues, "%d:%d", (int)tstamp, physval);
	create_and_update_rrd(hostname, testname, memory_params, memory_tpl);

	setupfn2("%s.%s.rrd", "memory", "swap");
	sprintf(rrdvalues, "%d:%d", (int)tstamp, swapval);
	create_and_update_rrd(hostname, testname, memory_params, memory_tpl);

	if ((actval >= 0) && (actval <= 100)) {
		setupfn2("%s.%s.rrd", "memory", "actual");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, actval);
		create_and_update_rrd(hostname, testname, memory_params, memory_tpl);
	}
}

int do_memory_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
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
				create_and_update_rrd(hostname, testname, memory_params, memory_tpl);
			}
		}

		p = strstr(msg, "Dirty Cache Buffers");
		if (p) {
			p = strchr(p, ':');
			if (p) {
				val = atoi(p+1);
				setupfn2("%s.%s.rrd", "memory", "dcb");
				sprintf(rrdvalues, "%d:%d", (int)tstamp, val);
				create_and_update_rrd(hostname, testname, memory_params, memory_tpl);
			}
		}

		p = strstr(msg, "Long Term Cache Hit Percentage");
		if (p) {
			p = strchr(p, ':');
			if (p) {
				val = atoi(p+1);
				setupfn2("%s.%s.rrd", "memory", "ltch");
				sprintf(rrdvalues, "%d:%d", (int)tstamp, val);
				create_and_update_rrd(hostname, testname, memory_params, memory_tpl);
			}
		}
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

			do_memory_rrd_update(tstamp, hostname, testname, physval, swapval, actval);
		}
	}

	return 0;
}

