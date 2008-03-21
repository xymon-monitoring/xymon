/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles "paging" messages.                                     */
/*                                                                            */
/* Copyright (C) 2006-2008 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2007-2008 Rich Smrcina                                       */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char paging_rcsid[] = "$Id: do_paging.c,v 1.7 2008-03-21 11:53:55 henrik Exp $";

static char *paging_params[] = { "DS:paging:GAUGE:600:0:U", NULL };
static void *paging_tpl      = NULL;

int do_paging_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	char *pr;
	int pagerate;

	if (paging_tpl == NULL) paging_tpl = setup_template(paging_params);

	if (strstr(msg, "z/VM") || strstr(msg, "z/VSE") || strstr(msg, "z/OS")) {
		pr=(strstr(msg, "Rate"));
		if (pr) {
			pr += 5;
			sscanf(pr, "%d per", &pagerate);
			setupfn("%s.rrd", "paging");

			sprintf(rrdvalues, "%d:%d", (int)tstamp, pagerate);
			create_and_update_rrd(hostname, testname, paging_params, paging_tpl);
		}

	}
	return 0;
}

