static char *la_params[]          = { "rrdcreate", rrdfn, "DS:la:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_la_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char *p, *eoln;
	int gotusers=0, gotprocs=0, gotload=0;
	int users=0, procs=0, load=0;
	htnames_t *hwalk;

	eoln = strchr(msg, '\n'); if (eoln) *eoln = '\0';
	p = strstr(msg, "up: ");
	if (p) {
		/* First line of cpu report, contains "up: 159 days, 1 users, 169 procs, load=21" */
		p = strchr(p, ',');
		if (p) {
			gotusers = (sscanf(p, ", %d users", &users) == 1);
			p = strchr(p+1, ',');
		}

		if (p) {
			gotprocs = (sscanf(p, ", %d procs", &procs) == 1);
			p = strchr(p+1, ',');
		}

		/*
		 * Load can be either 
		 * -  "load=xx.xx" (Unix, DISPREALLOADAVG=TRUE)
		 * -  "load=xx%"   (Windows)
		 * -  "load=xx"    (Unix, DISPREALLOADAVG=FALSE)
		 *
		 * We want the load in percent (Windows), or LA*100 (Unix).
		 */
		p = strstr(msg, "load="); if (p) { gotload = 1; p += 5; }
		if (strchr(p, '.')) {
			load = 100*atoi(p);
			p = strchr(p, '.');
			load += (atoi(p+1) % 100);
		}
		else if (strchr(p, '%')) {
			load = atoi(p);
		}
		else {
			load = atoi(p);
		}

	}
	if (eoln) *eoln = '\n';

	if (gotload) {
		sprintf(rrdfn, "%s/%s.la.rrd", rrddir, hostname);
		sprintf(rrdvalues, "%d:%d", (int)tstamp, load);
		create_and_update_rrd(rrdfn, la_params, update_params);
	}

	if (gotprocs) {
		sprintf(rrdfn, "%s/%s.procs.rrd", rrddir, hostname);
		sprintf(rrdvalues, "%d:%d", (int)tstamp, procs);
		create_and_update_rrd(rrdfn, la_params, update_params);
	}

	if (gotusers) {
		sprintf(rrdfn, "%s/%s.users.rrd", rrddir, hostname);
		sprintf(rrdvalues, "%d:%d", (int)tstamp, users);
		create_and_update_rrd(rrdfn, la_params, update_params);
	}

	for (hwalk = memhosts; (hwalk && strcmp(hwalk->name, hostname)); hwalk = hwalk->next) ;
	if (hwalk == NULL) {
		/* Pick up memory statistics */
		int found, realuse, swapuse;
		unsigned long phystotal, physavail, pagetotal, pageavail;

		found = realuse = swapuse = 0;
		phystotal = physavail = pagetotal = pageavail = 0;

		p = strstr(msg, "Total Physical memory:");
		if (p) { found++; phystotal = atol(strchr(p, ':') + 1); }

		if (found == 1) {
			p = strstr(msg, "Available Physical memory:");
			if (p) { found++; physavail = atol(strchr(p, ':') + 1); }
		}

		if (found == 2) {
			p = strstr(msg, "Total PageFile size:"); 
			if (p) { found++; pagetotal = atol(strchr(p, ':') + 1); }
		}

		if (found == 2) {
			p = strstr(msg, "Available PageFile size:"); 
			if (p) { found++; pageavail = atol(strchr(p, ':') + 1); }
		}

		if (found == 4) {
			realuse = 100 - ((physavail * 100) / phystotal);
			swapuse = 100 - ((pageavail * 100) / pagetotal);
		}

		do_memory_larrd_update(tstamp, hostname, realuse, swapuse);
	}

	return 0;
}

