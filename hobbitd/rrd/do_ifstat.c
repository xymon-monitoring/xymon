/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char ifstat_rcsid[] = "$Id: do_ifstat.c,v 1.1 2005-12-29 23:20:20 henrik Exp $";

static char *ifstat_params[] = { "rrdcreate", rrdfn, 
	                         "DS:bytesSent:DERIVE:600:0:U", 
	                         "DS:bytesReceived:DERIVE:600:0:U", 
				 rra1, rra2, rra3, rra4, NULL };
static char *ifstat_tpl       = NULL;


static const char *ifstat_aix_exprs[] = {
};

static const char *ifstat_hpux_exprs[] = {
};

static const char *ifstat_sunos_exprs[] = {
};

static const char *ifstat_linux_exprs[] = {
	"^([a-z]+[0-9]+)\\s",
	"^\\s+RX bytes:([0-9]+) .*TX bytes.([0-9]+) "
};

int do_ifstat_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	static int pcres_compiled = 0;
	static pcre **ifstat_aix_pcres = NULL;
	static pcre **ifstat_hpux_pcres = NULL;
	static pcre **ifstat_sunos_pcres = NULL;
	static pcre **ifstat_linux_pcres = NULL;

	enum ostype_t ostype;
	char *datapart = msg;
	char *outp;

	if (ifstat_tpl == NULL) ifstat_tpl = setup_template(ifstat_params);
	if (pcres_compiled == 0) {
		pcres_compiled = 1;
		ifstat_aix_pcres = compile_exprs("AIX", ifstat_aix_exprs, 
						 (sizeof(ifstat_aix_exprs) / sizeof(ifstat_aix_exprs[0])));
		ifstat_hpux_pcres= compile_exprs("HP-UX", ifstat_hpux_exprs, 
						 (sizeof(ifstat_hpux_exprs) / sizeof(ifstat_hpux_exprs[0])));
		ifstat_sunos_pcres = compile_exprs("SOLARIS", ifstat_sunos_exprs, 
						 (sizeof(ifstat_sunos_exprs) / sizeof(ifstat_sunos_exprs[0])));
		ifstat_linux_pcres = compile_exprs("LINUX", ifstat_linux_exprs, 
						 (sizeof(ifstat_linux_exprs) / sizeof(ifstat_linux_exprs[0])));
	}

	if ((strncmp(msg, "status", 6) == 0) || (strncmp(msg, "data", 4) == 0)) {
		/* Skip the first line of full status- and data-messages. */
		datapart = strchr(msg, '\n');
		if (datapart) datapart++; else datapart = msg;
	}

	ostype = get_ostype(datapart);
	datapart = strchr(datapart, '\n');
	if (datapart) {
		datapart++; 
	}
	else {
		errprintf("Too few lines in ifstat report from %s\n", hostname);
		return -1;
	}

	/* Setup the update string */
	outp = rrdvalues + sprintf(rrdvalues, "%d", (int)tstamp);

	switch (ostype) {
	  case OS_SOLARIS: 
		break;

	  case OS_AIX: 
		break;

	  case OS_HPUX: 
		break;

	  case OS_LINUX22:
	  case OS_LINUX:
	  case OS_RHEL3:
		{
			char *bol, *eoln, *ifname, *rxstr, *txstr;
			int dcount;
			
			dcount = 0;
			ifname = rxstr = txstr = NULL;

			bol = datapart;
			while (bol) {
				eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';

				if (pickdata(bol, ifstat_linux_pcres[0], &ifname)) dcount++;
				else if (pickdata(bol, ifstat_linux_pcres[1], &rxstr, &txstr)) dcount += 2;

				if (ifname && rxstr && txstr) {
					sprintf(rrdfn, "ifstat.%s.rrd", ifname);
					sprintf(rrdvalues, "%d:%s:%s", (int)tstamp, txstr, rxstr);
					create_and_update_rrd(hostname, rrdfn, ifstat_params, ifstat_tpl);
					xfree(ifname); xfree(rxstr); xfree(txstr);
					ifname = rxstr = txstr = NULL;
					dcount = 0;
				}

				if (eoln) {
					*eoln = '\n';
					bol = eoln+1;
					if (*bol == '\0') bol = NULL;
				}
				else {
					bol = NULL;
				}
			}

			if (ifname) xfree(ifname);
			if (rxstr) xfree(rxstr);
			if (txstr) xfree(txstr);
		}
		break;

	  case OS_OSF:
	  case OS_FREEBSD:
	  case OS_NETBSD:
	  case OS_OPENBSD:
	  case OS_DARWIN:
	  case OS_SNMP:
	  case OS_WIN32:
	  case OS_IRIX:
	  case OS_UNKNOWN:
		break;
	}

	return 0;
}

