/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char apache_rcsid[] = "$Id: do_apache.c,v 1.1 2004-11-07 18:24:24 henrik Exp $";

int do_apache_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	errprintf("Apache larrd not implemented\n");
	return -1;
}

