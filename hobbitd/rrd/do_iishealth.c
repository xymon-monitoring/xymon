/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char iishealth_rcsid[] = "$Id: do_iishealth.c,v 1.1 2005-01-15 17:38:33 henrik Exp $";

static char *iishealth_params[] = { "rrdcreate", rrdfn, "DS:realmempct:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_iishealth_larrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	return 0;
}
