/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char citrix_rcsid[] = "$Id: do_citrix.c,v 1.6 2005-02-06 08:49:02 henrik Exp $";

static char *citrix_params[] = { "rrdcreate", rrdfn, "DS:users:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_citrix_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char *p;
	int users;

	p = strstr(msg, " users active\n");
	if (p) p = strrchr(p, '\n');
	if (p && (sscanf(p+1, "%d users active\n", &users) == 1)) {
		sprintf(rrdfn, "citrix.rrd");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, users);
		return create_and_update_rrd(hostname, rrdfn, citrix_params, update_params);
	}

	return 0;
}

