/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: do_rrd.c,v 1.12 2005-01-15 08:32:51 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>

#include <rrd.h>

#include "libbbgen.h"

#include "do_larrd.h"

char *rrddir = NULL;

static char rrdfn[PATH_MAX];
static char rrdvalues[MAX_LINE_LEN];
static char rra1[] = "RRA:AVERAGE:0.5:1:576";
static char rra2[] = "RRA:AVERAGE:0.5:6:576";
static char rra3[] = "RRA:AVERAGE:0.5:24:576";
static char rra4[] = "RRA:AVERAGE:0.5:288:576";
static char *update_params[]      = { "rrdupdate", rrdfn, rrdvalues, NULL };

static int create_and_update_rrd(char *hostname, char *fn, char *creparams[], char *updparams[])
{
	char filedir[PATH_MAX];
	struct stat st;
	int pcount, result;

	if ((fn == NULL) || (strlen(fn) == 0)) {
		errprintf("RRD update for no file\n");
		return -1;
	}

	sprintf(filedir, "%s/%s", rrddir, hostname);
	if (stat(filedir, &st) == -1) {
		if (mkdir(filedir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) == -1) {
			errprintf("Cannot create rrd directory %s : %s\n", filedir, strerror(errno));
			return -1;
		}
	}
	strcat(filedir, "/"); strcat(filedir, fn);
	creparams[1] = updparams[1] = filedir;	/* Icky */

	if (stat(filedir, &st) == -1) {
		dprintf("Creating rrd %s\n", filedir);

		for (pcount = 0; (creparams[pcount]); pcount++);
		result = rrd_create(pcount, creparams);
		if (result != 0) {
			errprintf("RRD error creating %s: %s\n", filedir, rrd_get_error());
			return 1;
		}
	}

	dprintf("Updating %s with '%s'\n", filedir, rrdvalues);
	for (pcount = 0; (updparams[pcount]); pcount++);
	rrd_clear_error();
	result = rrd_update(pcount, updparams);
	if (result != 0) {
		errprintf("RRD error updating %s: %s\n", filedir, rrd_get_error());
		return 2;
	}

	return 0;
}


/* Include all of the sub-modules. */
#include "larrd/do_bbgen.c"
#include "larrd/do_bbtest.c"
#include "larrd/do_bbproxy.c"
#include "larrd/do_hobbitd.c"
#include "larrd/do_citrix.c"
#include "larrd/do_ntpstat.c"

#include "larrd/do_memory.c"	/* Must go before do_la.c */
#include "larrd/do_la.c"
#include "larrd/do_disk.c"
#include "larrd/do_netstat.c"
#include "larrd/do_vmstat.c"
#include "larrd/do_iostat.c"

#include "larrd/do_apache.c"
#include "larrd/do_bind.c"
#include "larrd/do_sendmail.c"
#include "larrd/do_mailq.c"
#include "larrd/do_bea.c"
#include "larrd/do_iishealth.c"

#include "larrd/do_net.c"


void update_larrd(char *hostname, char *testname, char *msg, time_t tstamp, larrdrrd_t *ldef)
{
	int res = 0;
	char *id;

	if (ldef) id = ldef->larrdrrdname; else id = testname;

	if      (strcmp(id, "bbgen") == 0)     res = do_bbgen_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "bbtest") == 0)    res = do_bbtest_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "bbproxy") == 0)   res = do_bbproxy_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "hobbitd") == 0)   res = do_hobbitd_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "citrix") == 0)    res = do_citrix_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "ntpstat") == 0)   res = do_ntpstat_larrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "la") == 0)        res = do_la_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "disk") == 0)      res = do_disk_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "memory") == 0)    res = do_memory_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "netstat") == 0)   res = do_netstat_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "vmstat") == 0)    res = do_vmstat_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "iostat") == 0)    res = do_iostat_larrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "apache") == 0)    res = do_apache_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "bind") == 0)      res = do_bind_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "sendmail") == 0)  res = do_sendmail_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "mailq") == 0)     res = do_mailq_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "bea") == 0)       res = do_bea_larrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "iishealth") == 0) res = do_iishealth_larrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "tcp") == 0)       res = do_net_larrd(hostname, testname, msg, tstamp);
}

