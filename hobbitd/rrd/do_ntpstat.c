/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char ntpstat_rcsid[] = "$Id: do_ntpstat.c,v 1.15 2007-07-21 10:19:16 henrik Exp $";

int do_ntpstat_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	static char *ntpstat_params[]     = { "rrdcreate", rrdfn, "DS:offsetms:GAUGE:600:U:U", NULL };
	static char *ntpstat_tpl          = NULL;

	char *p;
	float offset;
	int gotdata = 0;

	if (ntpstat_tpl == NULL) ntpstat_tpl = setup_template(ntpstat_params);

	/* First check for the old LARRD ntpstat BF script */
	p = strstr(msg, "\nOffset:");
	gotdata = (p && (sscanf(p+1, "Offset: %f", &offset) == 1));

	/* Or maybe it's just the "ntpq -c rv" output */
	if (!gotdata) {
		p = strstr(msg, "offset=");
		if (p && (isspace((int)*(p-1)) || (*(p-1) == ','))) {
			gotdata = (p && (sscanf(p, "offset=%f", &offset) == 1));
		}
	}

	if (gotdata) {
		sprintf(rrdfn, "ntpstat.rrd");
		sprintf(rrdvalues, "%d:%.6f", (int)tstamp, offset);
		return create_and_update_rrd(hostname, testname, rrdfn, ntpstat_params, ntpstat_tpl);
	}

	return 0;
}

