/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char ifmib_rcsid[] = "$Id: do_ifmib.c,v 1.1 2007-09-10 12:39:37 henrik Exp $";

static char *ifmib_params[] = { "DS:ifInOctets:DERIVE:600:0:U", 
	                        "DS:ifInUcastPkts:DERIVE:600:0:U", 
	                        "DS:ifInNUcastPkts:DERIVE:600:0:U", 
	                        "DS:ifInDiscards:DERIVE:600:0:U", 
	                        "DS:ifInErrors:DERIVE:600:0:U", 
	                        "DS:ifInUnknownProtos:DERIVE:600:0:U", 
	                        "DS:ifOutOctets:DERIVE:600:0:U", 
	                        "DS:ifOutUcastPkts:DERIVE:600:0:U", 
	                        "DS:ifOutNUcastPkts:DERIVE:600:0:U", 
	                        "DS:ifOutDiscards:DERIVE:600:0:U", 
	                        "DS:ifOutErrors:DERIVE:600:0:U", 
	                        "DS:ifOutQLen:GAUGE:600:0:U", 
				NULL };
static char *ifmib_tpl      = NULL;

static char *ifmib_valnames[] = {
	"ifInOctets", 
	"ifInUcastPkts", 
	"ifInNUcastPkts", 
	"ifInDiscards", 
	"ifInErrors", 
	"ifInUnknownProtos", 
	"ifOutOctets", 
	"ifOutUcastPkts", 
	"ifOutNUcastPkts", 
	"ifOutDiscards", 
	"ifOutErrors", 
	"ifOutQLen", 
	NULL
};

int do_ifmib_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char *datapart = msg;
	char *bol, *eoln;
	char *devname = NULL;
	char *values[12];
	int valcount = 0;

	if (ifmib_tpl == NULL) ifmib_tpl = setup_template(ifmib_params);

	if ((strncmp(msg, "status", 6) == 0) || (strncmp(msg, "data", 4) == 0)) {
		/* Skip the first line of full status- and data-messages. */
		datapart = strchr(msg, '\n');
		if (datapart) datapart++; else datapart = msg;
	}

	memset(values, 0, sizeof(values));
	valcount = 0;
	devname = NULL;

	bol = datapart;
	while (bol) {
		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
		bol += strspn(bol, " \t");
		if (*bol == '\0') {
			/* Nothing */
		}
		else if (*bol == '[') {
			/* New interface data begins */
			if (devname && (valcount == 12)) {
				setupfn("ifmib.%s.rrd", devname);
				sprintf(rrdvalues, "%d:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s",
					(int)tstamp,
					values[0], values[1], values[2], values[3],
					values[4], values[5], values[6], values[7],
					values[8], values[9], values[10], values[11]);
				create_and_update_rrd(hostname, testname, ifmib_params, ifmib_tpl);

				memset(values, 0, sizeof(values));
				valcount = 0;
				devname = NULL;
			}

			devname = bol+1; bol = strchr(bol, ']'); if (bol) *bol = '\0';
		}
		else {
			char *valnam, *valstr = NULL;

			valnam = strtok(bol, " =");
			if (valnam) valstr = strtok(NULL, " =");

			if (valnam && valstr) {
				int validx;
				for (validx = 0; (ifmib_valnames[validx] && strcmp(ifmib_valnames[validx], valnam)); validx++) ;
				if (ifmib_valnames[validx]) {
					values[validx] = (strcmp(valstr, "NODATA") != 0) ? valstr : "U";
					valcount++;
				}
			}
		}

nextline:
		bol = (eoln ? eoln+1 : NULL);
	}

	/* Flush the last device */
	if (devname && (valcount == 12)) {
		setupfn("ifmib.%s.rrd", devname);
		sprintf(rrdvalues, "%d:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s",
			(int)tstamp,
			values[0], values[1], values[2], values[3],
			values[4], values[5], values[6], values[7],
			values[8], values[9], values[10], values[11]);
		create_and_update_rrd(hostname, testname, ifmib_params, ifmib_tpl);
		valcount = 0;
	}

	return 0;
}

