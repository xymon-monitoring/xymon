/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles "getvis" messages.                                     */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/* Copyright (C) 2008 Rich Smrcina                                            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char getvis_rcsid[] = "$Id: do_getvis.c,v 1.00 2008/09/08 20:30:00 henrik Exp $";

static char *getvis_params[]  = { "DS:below:GAUGE:600:0:100", "DS:any:GAUGE:600:0:100", NULL };
static char *getvis_tpl       = NULL;

int do_getvis_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{ 
	char *p;
        char pid[4], jnm[9];
        int j1, j2, j3, j4, j5, j6;    /*  All junk, don't need it here  */
        int used24p, usedanyp;

        if (strstr(msg, "z/VSE Getvis Map")) {

                p = strstr(msg, "PID ");
                if (!p) {
                        return 0;
                        }

                p = strtok(p, "\n");  /*  Skip heading line  */
                if (p) {
                        p = strtok(NULL, "\n");
                        }

                while (p != NULL) {
                        sscanf(p, "%s %s %d %d %d %d %d %d %d %d", pid, jnm, &j1, &j2, &j3, &j4, &j5, &j6, &used24p, &usedanyp);
                        setupfn2("%s.%s.rrd", "getvis", pid);
                        sprintf(rrdvalues, "%d:%d:%d", (int)tstamp, used24p, usedanyp);
                        create_and_update_rrd(hostname, testname, classname, pagepaths, getvis_params, getvis_tpl);
                        p = strtok(NULL, "\n");
                        }

                }

	return 0;
}
