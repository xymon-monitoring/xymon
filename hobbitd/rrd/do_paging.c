/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles "paging" messages.                                     */
/*                                                                            */
/* Copyright (C) 2006-2009 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2007-2008 Rich Smrcina                                       */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char paging_rcsid[] = "$Id$";

static char *paging_params[] = { "DS:rate:GAUGE:600:0:U", NULL };
static void *paging_tpl      = NULL;

int do_paging_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{ 
	char *pr;
	char *fn = NULL;
	int pagerate, xstore, migrate;

	if (paging_tpl == NULL) paging_tpl = setup_template(paging_params);

	pr=(strstr(msg, "Rate"));
	if (pr) {
		pr += 5;
		sscanf(pr, "%d per", &pagerate);
		setupfn("paging.pagerate.rrd", fn);

		sprintf(rrdvalues, "%d:%d", (int)tstamp, pagerate);
		create_and_update_rrd(hostname, testname, classname, pagepaths, paging_params, paging_tpl);
                if (strstr(msg, "z/VM")) {  /*  Additional handling for z/VM  */
                        pr=strstr(msg,"XSTORE-");
                        if (pr) {    /* Extract values if we find XSTORE in results of 'IND' command  */
                                pr += 7;  /*  Add 7 to get past literal (XSTORE).  */
                                sscanf(pr, "%d/SEC", &xstore);
                                pr=strstr(msg,"MIGRATE-");
                                pr += 8;  /*  Add 8 to get past literal (MIGRATE).  */
                                sscanf(pr, "%d/SEC", &migrate);
 
                                setupfn("paging.xstore.rrd", fn);
                                sprintf(rrdvalues, "%d:%d", (int)tstamp, xstore);
                                create_and_update_rrd(hostname, testname, classname, pagepaths, paging_params, paging_tpl);
 
                                setupfn("paging.migrate.rrd", fn);
                                sprintf(rrdvalues, "%d:%d", (int)tstamp, migrate);
                                create_and_update_rrd(hostname, testname, classname, pagepaths, paging_params, paging_tpl);
                        }

		}

	}
	return 0;
}

