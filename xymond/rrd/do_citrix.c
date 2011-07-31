/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char citrix_rcsid[] = "$Id$";

int do_citrix_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
	static char *citrix_params[] = { "DS:users:GAUGE:600:0:U", NULL };
	static void *citrix_tpl      = NULL;

	char *p;
	int users;

	if (citrix_tpl == NULL) citrix_tpl = setup_template(citrix_params);

	p = strstr(msg, " users active\n");
	while (p && (p > msg) && (*p != '\n')) p--;
	if (p && (sscanf(p+1, "\n%d users active\n", &users) == 1)) {
		setupfn("%s.rrd", "citrix");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, users);
		return create_and_update_rrd(hostname, testname, classname, pagepaths, citrix_params, citrix_tpl);
	}

	return 0;
}

