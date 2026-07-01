/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char ntpstat_rcsid[] = "$Id$";

/* Find a marker in the message and read the number that follows it. */
static int ntpstat_offset(const char *msg, const char *marker, double *offset)
{
	const char *p = strstr(msg, marker);
	return p && sscanf(p + strlen(marker), "%lf", offset) == 1;
}

int do_ntpstat_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
	static char *ntpstat_params[]     = { "DS:offsetms:GAUGE:600:U:U", NULL };
	static void *ntpstat_tpl          = NULL;

	double offset;

	if (ntpstat_tpl == NULL) ntpstat_tpl = setup_template(ntpstat_params);

	/* "Offset:" = old LARRD script line; "offset=" = "ntpq -c rv" output. */
	if (!ntpstat_offset(msg, "\nOffset:", &offset) &&
	    !ntpstat_offset(msg, "offset=",   &offset)) return 0;

	setupfn("%s.rrd", "ntpstat");
	snprintf(rrdvalues, sizeof(rrdvalues), "%d:%.6f", (int)tstamp, offset);
	return create_and_update_rrd(hostname, testname, classname, pagepaths, ntpstat_params, ntpstat_tpl);
}

