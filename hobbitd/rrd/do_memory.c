/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char memory_rcsid[] = "$Id: do_memory.c,v 1.7 2004-12-28 21:13:44 henrik Exp $";

static char *memory_params[]      = { "rrdcreate", rrdfn, "DS:realmempct:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

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

void do_memory_larrd_update(time_t tstamp, char *hostname, int physval, int swapval, int actval)
{
	sprintf(rrdfn, "memory.real.rrd");
	sprintf(rrdvalues, "%d:%d", (int)tstamp, physval);
	create_and_update_rrd(hostname, rrdfn, memory_params, update_params);

	sprintf(rrdfn, "memory.swap.rrd");
	sprintf(rrdvalues, "%d:%d", (int)tstamp, swapval);
	create_and_update_rrd(hostname, rrdfn, memory_params, update_params);

	if ((actval >= 0) && (actval <= 100)) {
		sprintf(rrdfn, "memory.actual.rrd");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, actval);
		create_and_update_rrd(hostname, rrdfn, memory_params, update_params);
	}
}

int do_memory_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char *phys = NULL;
	char *swap = NULL;
	char *actual = NULL;
	RbtIterator hwalk;

	/* Log this hostname in the list of hosts we get true "memory" reports from. */
	if (!memhosts_init) { memhosts = rbtNew(string_compare); memhosts_init = 1; }
	hwalk = rbtFind(memhosts, hostname);
	if (hwalk == rbtEnd(memhosts)) {
		char *keyp = strdup(hostname);
		if (rbtInsert(memhosts, keyp, NULL)) {
			errprintf("Insert into memhosts failed\n");
		}
	}

	phys = strstr(msg, "Physical"); if (phys == NULL) phys = strstr(msg, "Real");
	swap = strstr(msg, "Swap"); if (swap == NULL) swap = strstr(msg, "Page");
	actual = strstr(msg, "Actual");

	if (phys && swap) {
		char *eoln;
		int physval = -1, swapval = -1, actval = -1;

		eoln = strchr(phys, '\n'); if (eoln) *eoln = '\0';
		physval = get_mem_percent(phys);
		if (eoln) *eoln = '\n';

		eoln = strchr(swap, '\n'); if (eoln) *eoln = '\0';
		swapval = get_mem_percent(swap);
		if (eoln) *eoln = '\n';

		if (actual) {
			eoln = strchr(actual, '\n'); if (eoln) *eoln = '\0';
			actval = get_mem_percent(actual);
			if (eoln) *eoln = '\n';
		}

		do_memory_larrd_update(tstamp, hostname, physval, swapval, actval);
	}

	return 0;
}

