/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* This module handles "cics" messages.                                       */
/*                                                                            */
/* Copyright (C) 2006-2011 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2008 Rich Smrcina                                            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char cics_rcsid[] = "$Id: do_cics.c 6585 2010-11-14 15:12:56Z storner $";

static char *cicsntrans_params[]  = { "DS:numtrans:GAUGE:600:0:U", NULL };
static char *cicsdsa_params[]  = { "DS:dsa:GAUGE:600:0:100", "DS:edsa:GAUGE:600:0:100", NULL };
static char *cics_tpl      = NULL;

int do_cics_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{ 
	char *pr;
	char *fn = NULL;
	int numtrans;
	float dsapct, edsapct;
	char cicsappl[9], rrdfn[20];

	pr=(strstr(msg, "Appl"));
	if (!pr) {
		return 0;
		}
	pr=(strstr(pr, "\n"));
	if (pr) {
		pr += 1;
		pr = strtok(pr, "\n");
		while (pr != NULL) {
			sscanf(pr, "%s %d %f %f", cicsappl, &numtrans, &dsapct, &edsapct); 
			snprintf(rrdfn, sizeof(rrdvalues), "cics.%-s.rrd", cicsappl);
			setupfn(rrdfn, fn);
			snprintf(rrdvalues, sizeof(rrdvalues), "%d:%d", (int)tstamp, numtrans);
			create_and_update_rrd(hostname, testname, classname, pagepaths, cicsntrans_params, cics_tpl);
			snprintf(rrdfn, sizeof(rrdvalues), "dsa.%-s.rrd", cicsappl);
			setupfn(rrdfn, fn);
			snprintf(rrdvalues, sizeof(rrdvalues), "%d:%d:%d", (int)tstamp, (int)dsapct, (int)edsapct);
			create_and_update_rrd(hostname, testname, classname, pagepaths, cicsdsa_params, cics_tpl);
			pr = strtok(NULL, "\n");
			}
	}
	return 0;
}
