/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char memory_rcsid[] = "$Id: do_memory.c,v 1.3 2004-11-08 17:11:41 henrik Exp $";

static char *memory_params[]      = { "rrdcreate", rrdfn, "DS:realmempct:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

static htnames_t *memhosts = NULL;

static int get_mem_percent(char *l)
{
	char *p;

	p = strchr(l, '%');
	if (p == NULL) return 0;
	p--; while ( (p > l) && isdigit((int) *p)) p--;

	return atoi(p+1);
}

void do_memory_larrd_update(time_t tstamp, char *hostname, int physval, int swapval)
{
	sprintf(rrdfn, "%s.%s.real.rrd", commafy(hostname), "memory");
	sprintf(rrdvalues, "%d:%d", (int)tstamp, physval);
	create_and_update_rrd(hostname, rrdfn, memory_params, update_params);

	sprintf(rrdfn, "%s.%s.swap.rrd", commafy(hostname), "memory");
	sprintf(rrdvalues, "%d:%d", (int)tstamp, swapval);
	create_and_update_rrd(hostname, rrdfn, memory_params, update_params);
}

int do_memory_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char *phys = NULL;
	char *swap = NULL;
	htnames_t *hwalk;

	/* Log this hostname in the list of hosts we get true "memory" reports from. */
	for (hwalk = memhosts; (hwalk && strcmp(hwalk->name, hostname)); hwalk = hwalk->next);
	if (hwalk == NULL) {
		hwalk = (htnames_t *)malloc(sizeof(htnames_t));
		hwalk->name = strdup(hostname);
		hwalk->next = memhosts;
		memhosts = hwalk;
	}

	phys = strstr(msg, "Physical"); if (phys == NULL) phys = strstr(msg, "Real");
	swap = strstr(msg, "Swap"); if (swap == NULL) swap = strstr(msg, "Page");

	if (phys && swap) {
		char *eoln;
		int physval, swapval;

		eoln = strchr(phys, '\n'); if (eoln) *eoln = '\0';
		physval = get_mem_percent(phys);
		if (eoln) *eoln = '\n';

		eoln = strchr(swap, '\n'); if (eoln) *eoln = '\0';
		swapval = get_mem_percent(swap);
		if (eoln) *eoln = '\n';

		do_memory_larrd_update(tstamp, hostname, physval, swapval);
	}

	return 0;
}

