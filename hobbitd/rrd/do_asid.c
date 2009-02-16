/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles combined z/OS and z/VSE ASID and NPARTS messages.      */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/* Copyright (C) 2007-2009 Rich Smrcina <rsmrcina@wi.rr.com>                  */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char asid_rcsid[] = "$Id: do_asid.c,v 1.0 2008/11/06 20:45:01 henrik Exp $";

static char *asid_params[]     = { "DS:util:GAUGE:600:0:100", NULL };
static char *asid_tpl          = NULL;

int do_asid_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{ 
	char *p;

	p=(strstr(msg, "Maxuser"));
	if (p) {	
		long maxuser, maxufree, maxuused, rsvtstrt, rsvtfree, rsvtused, rsvnonr, rsvnfree, rsvnused;
		float maxupct, rsvtpct, rsvnpct;
		sscanf(p, "Maxuser: %ld Free: %ld Used: %ld %f", &maxuser, &maxufree, &maxuused, &maxupct);
	
  		p=(strstr(p, "RSVTSTRT"));
		sscanf(p, "RSVTSTRT: %ld Free: %ld Used: %ld %f", &rsvtstrt, &rsvtfree, &rsvtused, &rsvtpct);

  		p=(strstr(p, "RSVNONR"));
		sscanf(p, "RSVNONR: %ld Free: %ld Used: %ld %f", &rsvnonr, &rsvnfree, &rsvnused, &rsvnpct);

        	setupfn2("%s.%s.rrd", "maxuser", "maxuser");
        	sprintf(rrdvalues, "%d:%d", (int)tstamp, (int)maxupct);
        	create_and_update_rrd(hostname, testname, classname, pagepaths, asid_params, asid_tpl);

        	setupfn2("%s.%s.rrd", "maxuser", "rsvtstrt");
        	sprintf(rrdvalues, "%d:%d", (int)tstamp, (int)rsvtpct);
        	create_and_update_rrd(hostname, testname, classname, pagepaths, asid_params, asid_tpl);

        	setupfn2("%s.%s.rrd", "maxuser", "rsvnonr");
        	sprintf(rrdvalues, "%d:%d", (int)tstamp, (int)rsvnpct);
        	create_and_update_rrd(hostname, testname, classname, pagepaths, asid_params, asid_tpl);
		}

	p=(strstr(msg, "Nparts"));
	if (p) {	
	        char *fn = NULL;
		long nparts, partfree, partused;
		float partupct;
		sscanf(p, "Nparts: %ld Free: %ld Used: %ld %f", &nparts, &partfree, &partused, &partupct);

                setupfn("nparts.rrd", fn);
                sprintf(rrdvalues, "%d:%d", (int)tstamp, (int)partupct);
                create_and_update_rrd(hostname, testname, classname, pagepaths, asid_params, asid_tpl);
		}

	return 0;
}
