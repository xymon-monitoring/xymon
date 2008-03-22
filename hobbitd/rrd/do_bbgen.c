/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbgen_rcsid[] = "$Id: do_bbgen.c,v 1.21 2008-03-22 07:48:55 henrik Exp $";

int do_bbgen_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{ 
	static char *bbgen_params[] = { "DS:runtime:GAUGE:600:0:U", NULL };
	static void *bbgen_tpl      = NULL;
	static char *bbgen2_params[] = { "DS:hostcount:GAUGE:600:0:U", "DS:statuscount:GAUGE:600:0:U", NULL };
	static void *bbgen2_tpl      = NULL;
	static char *bbgen3_params[] = { "DS:redcount:GAUGE:600:0:U", "DS:rednopropcount:GAUGE:600:0:U",
					 "DS:yellowcount:GAUGE:600:0:U", "DS:yellownopropcount:GAUGE:600:0:U",
					 "DS:greencount:GAUGE:600:0:U",
					 "DS:purplecount:GAUGE:600:0:U",
					 "DS:clearcount:GAUGE:600:0:U",
					 "DS:bluecount:GAUGE:600:0:U",
					 "DS:redpct:GAUGE:600:0:100", "DS:rednoproppct:GAUGE:600:0:100",
					 "DS:yellowpct:GAUGE:600:0:100", "DS:yellownoproppct:GAUGE:600:0:100",
					 "DS:greenpct:GAUGE:600:0:100",
					 "DS:purplepct:GAUGE:600:0:100",
					 "DS:clearpct:GAUGE:600:0:100",
					 "DS:bluepct:GAUGE:600:0:100",
					NULL };
	static void *bbgen3_tpl      = NULL;

	char	*p, *bol, *eoln;
	float	runtime;
	int	hostcount, statuscount;
	int	redcount, rednopropcount, yellowcount, yellownopropcount,
		greencount, purplecount, clearcount, bluecount;
	double	pctredcount, pctrednopropcount, pctyellowcount, pctyellownopropcount,
		pctgreencount, pctpurplecount, pctclearcount, pctbluecount;

	if (bbgen_tpl == NULL) bbgen_tpl = setup_template(bbgen_params);
	if (bbgen2_tpl == NULL) bbgen2_tpl = setup_template(bbgen2_params);
	if (bbgen3_tpl == NULL) bbgen3_tpl = setup_template(bbgen3_params);

	runtime = 0.0;
	hostcount = statuscount = 0;
	redcount = rednopropcount = yellowcount = yellownopropcount = 0;
	greencount = purplecount = clearcount = bluecount = 0;
	pctredcount = pctrednopropcount = pctyellowcount = pctyellownopropcount = 0.0;
	pctgreencount = pctpurplecount = pctclearcount = pctbluecount = 0.0;

	bol = msg;
	do {
		int *valptr = NULL;
		double *pctvalptr = NULL;

		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';

		p = bol + strspn(bol, " \t");
		if (strncmp(p, "TIME TOTAL", 10) == 0) sscanf(p, "TIME TOTAL %f", &runtime);
		else if (strncmp(p, "Hosts", 5) == 0) valptr = &hostcount;
		else if (strncmp(p, "Status messages", 15) == 0) valptr = &statuscount;
		else if (strncmp(p, "- Red (non-propagating)", 23) == 0) {
			valptr = &rednopropcount;
			pctvalptr = &pctrednopropcount;
		}
		else if (strncmp(p, "- Red", 5) == 0) {
			valptr = &redcount;
			pctvalptr = &pctredcount;
		}
		else if (strncmp(p, "- Yellow (non-propagating)", 26) == 0) {
			valptr = &yellownopropcount;
			pctvalptr = &pctyellownopropcount;
		}
		else if (strncmp(p, "- Yellow", 8) == 0) {
			valptr = &yellowcount;
			pctvalptr = &pctyellowcount;
		}
		else if (strncmp(p, "- Green", 7) == 0) {
			valptr = &greencount;
			pctvalptr = &pctgreencount;
		}
		else if (strncmp(p, "- Purple", 8) == 0) {
			valptr = &purplecount;
			pctvalptr = &pctpurplecount;
		}
		else if (strncmp(p, "- Clear", 7) == 0) {
			valptr = &clearcount;
			pctvalptr = &pctclearcount;
		}
		else if (strncmp(p, "- Blue", 6) == 0) {
			valptr = &bluecount;
			pctvalptr = &pctbluecount;
		}

		if (valptr) {
			p = strchr(bol, ':');
			if (p) {
				*valptr = atoi(p+1);

				if (pctvalptr) {
					p = strchr(p, '(');
					if (p) *pctvalptr = atof(p+1);
				}
			}
		}

		bol = (eoln ? eoln+1 : NULL);
	} while (bol);


	if (strcmp("bbgen", testname) != 0) {
		setupfn2("%s.%s.rrd", "bbgen", testname);
	}
	else {
		setupfn("%s.rrd", "bbgen");
	}
	sprintf(rrdvalues, "%d:%.2f", (int)tstamp, runtime);
	create_and_update_rrd(hostname, testname, classname, pagepaths, bbgen_params, bbgen_tpl);


	if (strcmp("bbgen", testname) != 0) {
		setupfn2("%s.%s.rrd", "hobbit", testname);
	}
	else {
		setupfn("%s.rrd", "hobbit");
	}
	sprintf(rrdvalues, "%d:%d:%d", (int)tstamp, hostcount, statuscount);
	create_and_update_rrd(hostname, testname, classname, pagepaths, bbgen2_params, bbgen2_tpl);


	if (strcmp("bbgen", testname) != 0) {
		setupfn2("%s.%s.rrd", "hobbit2", testname);
	}
	else {
		setupfn("%s.rrd", "hobbit2");
	}
	sprintf(rrdvalues, "%d:%d:%d:%d:%d:%d:%d:%d:%d:%5.2f:%5.2f:%5.2f:%5.2f:%5.2f:%5.2f:%5.2f:%5.2f", 
		(int)tstamp, 
		redcount, rednopropcount, yellowcount, yellownopropcount,
		greencount, purplecount, clearcount, bluecount,
		pctredcount, pctrednopropcount, pctyellowcount, pctyellownopropcount,
		pctgreencount, pctpurplecount, pctclearcount, pctbluecount);
	create_and_update_rrd(hostname, testname, classname, pagepaths, bbgen3_params, bbgen3_tpl);


	return 0;
}

