/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles z/VM "mdc" data messages                               */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/* Copyright (C) 2007 Rich Smrcina                                            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char mdc_rcsid[] = "$Id: do_mdc.c 6125 2009-02-12 13:09:34Z storner $";

static char *mdc_params[]     = { "DS:reads:GAUGE:600:0:U", "DS:writes:GAUGE:600:0:U", NULL };
static char *mdcpct_params[]  = { "DS:hitpct:GAUGE:600:0:100", NULL };
static char *mdc_tpl          = NULL;

int do_mdc_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{ 
	char *pr;
	char *fn = NULL;
	int mdcreads, mdcwrites, mdchitpct;

	pr=(strstr(msg, "\n"));
	pr++;
	pr=(strstr(pr, "\n")); /* There are two of them...  */
	if (pr) {
		pr++;
		sscanf(pr, "%d:%d:%d", &mdcreads, &mdcwrites, &mdchitpct);
		setupfn("mdc.rrd", fn);
		sprintf(rrdvalues, "%d:%d:%d", (int)tstamp, mdcreads, mdcwrites);
		create_and_update_rrd(hostname, testname, classname, pagepaths, mdc_params, mdc_tpl);

		setupfn("mdchitpct.rrd", fn);
		sprintf(rrdvalues, "%d:%d", (int)tstamp, mdchitpct);
		create_and_update_rrd(hostname, testname, classname, pagepaths, mdcpct_params, mdc_tpl);

	}
	return 0;
}

