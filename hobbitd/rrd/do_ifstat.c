/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char ifstat_rcsid[] = "$Id: do_ifstat.c,v 1.16 2008-01-02 14:35:01 henrik Exp $";

static char *ifstat_params[] = { "DS:bytesSent:DERIVE:600:0:U", 
	                         "DS:bytesReceived:DERIVE:600:0:U", 
				 NULL };
static char *ifstat_tpl       = NULL;


/* eth0   Link encap:                                                 */
/*        RX bytes: 1829192 (265.8 MiB)  TX bytes: 1827320 (187.7 MiB */
static const char *ifstat_linux_exprs[] = {
	"^([a-z]+[0123456789.:]+|lo)\\s",
	"^\\s+RX bytes:([0-9]+) .*TX bytes.([0-9]+) "
};

/* Name MTU  Network        IP            Ipkts Ierrs Ibytes Opkts Oerrs Obytes Coll */
/* lnc0 1500 172.16.10.0/24 172.16.10.151 26    -     1818   26    -     1802   -    */
static const char *ifstat_freebsd_exprs[] = {
	"^([a-z0-9]+)\\s+\\d+\\s+[0-9.\\/]+\\s+[0-9.]+\\s+\\d+\\s+[0-9-]+\\s+(\\d+)\\s+\\d+\\s+[0-9-]+\\s+(\\d+)\\s+[0-9-]+"
};

/* Name MTU  Network        IP            Ibytes Obytes */
/* lnc0 1500 172.16.10.0/24 172.16.10.151 1818   1802   */
static const char *ifstat_openbsd_exprs[] = {
	"^([a-z0-9]+)\\s+\\d+\\s+[0-9.\\/]+\\s+[0-9.]+\\s+(\\d+)\\s+(\\d+)"
};

/* Name MTU  Network        IP            Ibytes Obytes */
/* lnc0 1500 172.16.10.0/24 172.16.10.151 1818   1802   */
static const char *ifstat_netbsd_exprs[] = {
	"^([a-z0-9]+)\\s+\\d+\\s+[0-9.\\/]+\\s+[0-9.]+\\s+(\\d+)\\s+(\\d+)"
};

/*
Name  Mtu   Network       Address            Ipkts Ierrs     Ibytes        Opkts   Oerrs Obytes  Coll
en0   1500  fe80::20d:9 fe80::20d:93ff:fe 2013711826     - 2131205566781 331648829     - 41815551289     -
en0   1500  130.223.20/24 130.223.20.20   2013711826     - 2131205566781 331648829     - 41815551289     -
*/
static const char *ifstat_darwin_exprs[] = {
	"^([a-z0-9]+)\\s+\\d+\\s+[0-9.\\/]+\\s+[0-9.]+\\s+\\d+\\s+[0-9-]+\\s+(\\d+)\\s+\\d+\\s+[0-9-]+\\s+(\\d+)\\s+[0-9-]+"
};

/* dmfe:0:dmfe0:obytes64   107901705585  */
/* dmfe:0:dmfe0:rbytes64   1224808818952 */
/* dmfe:1:dmfe1:obytes64   0             */
/* dmfe:1:dmfe1:rbytes64   0             */
static const char *ifstat_solaris_exprs[] = {
	"^[a-z0-9]+:\\d+:([a-z0-9]+):obytes64\\s+(\\d+)",
	"^[a-z0-9]+:\\d+:([a-z0-9]+):rbytes64\\s+(\\d+)"
};

/*
ETHERNET STATISTICS (ent0) :
Device Type: 2-Port 10/100/1000 Base-TX PCI-X Adapter (14108902)
Hardware Address: 00:11:25:e6:0d:36
Elapsed Time: 45 days 20 hours 18 minutes 41 seconds

Transmit Statistics:                          Receive Statistics:
--------------------                          -------------------
Packets: 1652404                              Packets: 768800
Bytes: 1966314449                             Bytes: 78793615
*/
static const char *ifstat_aix_exprs[] = {
	"^ETHERNET STATISTICS \\(([a-z0-9]+)\\) :",
	"^Bytes:\\s+(\\d+)\\s+Bytes:\\s+(\\d+)"
};


/* (lines dropped)
PPA Number                      = 0
Description                     = lan0 Hewlett-Packard LAN Interface Hw Rev 0
Type (value)                    = ethernet-csmacd(6)
MTU Size                        = 1500
Operation Status (value)        = up(1)
Inbound Octets                  = 3111235429
Outbound Octets                 = 3892111463
*/
static const char *ifstat_hpux_exprs[] = {
	"^PPA Number\\s+= (\\d+)",
	"^Inbound Octets\\s+= (\\d+)",
	"^Outbound Octets\\s+= (\\d+)",
};

/*
Name  Mtu   Network     Address         Ipkts    Ierrs Opkts    Oerrs  Coll 
net0  1500  195.75.9    10.1.1.2        13096313 0     12257642 0      0     
lo0   8232  127         127.0.0.1       26191    0     26191    0      0
Attention, theses numbers are packets, not bytes !
*/
static const char *ifstat_sco_sv_exprs[] = {
	"^([a-z]+[0-9]+)\\s+[0-9]+\\s+[0-9.]+\\s+[0-9.]+\\s+([0-9]+)\\s+[0-9]+\\s+([0-9]+)\\s+"
};

int do_ifstat_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	static int pcres_compiled = 0;
	static pcre **ifstat_linux_pcres = NULL;
	static pcre **ifstat_freebsd_pcres = NULL;
	static pcre **ifstat_openbsd_pcres = NULL;
	static pcre **ifstat_netbsd_pcres = NULL;
	static pcre **ifstat_darwin_pcres = NULL;
	static pcre **ifstat_solaris_pcres = NULL;
	static pcre **ifstat_aix_pcres = NULL;
	static pcre **ifstat_hpux_pcres = NULL;
	static pcre **ifstat_sco_sv_pcres = NULL;

	enum ostype_t ostype;
	char *datapart = msg;
	char *outp;
	char *bol, *eoln, *ifname, *rxstr, *txstr, *dummy;
	int dmatch;

	if (pcres_compiled == 0) {
		pcres_compiled = 1;
		ifstat_linux_pcres = compile_exprs("LINUX", ifstat_linux_exprs, 
						 (sizeof(ifstat_linux_exprs) / sizeof(ifstat_linux_exprs[0])));
		ifstat_freebsd_pcres = compile_exprs("FREEBSD", ifstat_freebsd_exprs, 
						 (sizeof(ifstat_freebsd_exprs) / sizeof(ifstat_freebsd_exprs[0])));
		ifstat_openbsd_pcres = compile_exprs("OPENBSD", ifstat_openbsd_exprs, 
						 (sizeof(ifstat_openbsd_exprs) / sizeof(ifstat_openbsd_exprs[0])));
		ifstat_netbsd_pcres = compile_exprs("NETBSD", ifstat_netbsd_exprs, 
						 (sizeof(ifstat_netbsd_exprs) / sizeof(ifstat_netbsd_exprs[0])));
		ifstat_darwin_pcres = compile_exprs("DARWIN", ifstat_darwin_exprs, 
						 (sizeof(ifstat_darwin_exprs) / sizeof(ifstat_darwin_exprs[0])));
		ifstat_solaris_pcres = compile_exprs("SOLARIS", ifstat_solaris_exprs, 
						 (sizeof(ifstat_solaris_exprs) / sizeof(ifstat_solaris_exprs[0])));
		ifstat_aix_pcres = compile_exprs("AIX", ifstat_aix_exprs, 
						 (sizeof(ifstat_aix_exprs) / sizeof(ifstat_aix_exprs[0])));
		ifstat_hpux_pcres = compile_exprs("HPUX", ifstat_hpux_exprs, 
						 (sizeof(ifstat_hpux_exprs) / sizeof(ifstat_hpux_exprs[0])));
		ifstat_sco_sv_pcres = compile_exprs("SCO_SV", ifstat_sco_sv_exprs, 
						 (sizeof(ifstat_sco_sv_exprs) / sizeof(ifstat_sco_sv_exprs[0])));
	}


	if (ifstat_tpl == NULL) ifstat_tpl = setup_template(ifstat_params);

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

	dmatch = 0;
	ifname = rxstr = txstr = dummy = NULL;

	bol = datapart;
	while (bol) {
		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';

		switch (ostype) {
		  case OS_LINUX22:
		  case OS_LINUX:
		  case OS_RHEL3:
		  case OS_ZVM:
		  case OS_ZVSE:
			if (pickdata(bol, ifstat_linux_pcres[0], 1, &ifname)) {
				/*
				 * Linux' netif aliases mess up things. 
				 * Clear everything when we see an interface name.
				 * But we dont want to track the "lo" interface.
				 */
				if (strcmp(ifname, "lo") == 0) {
					xfree(ifname); ifname = NULL;
				}
				else {
					dmatch = 1;
					if (rxstr) { xfree(rxstr); rxstr = NULL; }
					if (txstr) { xfree(txstr); txstr = NULL; }
				}
			}
			else if (pickdata(bol, ifstat_linux_pcres[1], 1, &rxstr, &txstr)) dmatch |= 6;
			break;

		  case OS_FREEBSD:
			if (pickdata(bol, ifstat_freebsd_pcres[0], 0, &ifname, &rxstr, &txstr)) dmatch = 7;
			break;

		  case OS_OPENBSD:
			if (pickdata(bol, ifstat_openbsd_pcres[0], 0, &ifname, &rxstr, &txstr)) dmatch = 7;
			break;

		  case OS_NETBSD:
			if (pickdata(bol, ifstat_netbsd_pcres[0], 0, &ifname, &rxstr, &txstr)) dmatch = 7;
			break;

		  case OS_SOLARIS: 
			if (pickdata(bol, ifstat_solaris_pcres[0], 0, &ifname, &txstr)) dmatch |= 1;
			else if (pickdata(bol, ifstat_solaris_pcres[1], 0, &dummy, &rxstr)) dmatch |= 6;

			if (ifname && dummy && (strcmp(ifname, dummy) != 0)) {
				/* They must match, drop the data */
				errprintf("Host %s has weird ifstat data - device name mismatch %s:%s\n", hostname, ifname, dummy);
				xfree(ifname); xfree(txstr); xfree(rxstr); xfree(dummy);
				dmatch = 0;
			}
			break;

		  case OS_AIX: 
			if (pickdata(bol, ifstat_aix_pcres[0], 1, &ifname)) {
				/* Interface names comes first, so any rx/tx data is discarded */
				dmatch |= 1;
				if (rxstr) { xfree(rxstr); rxstr = NULL; }
				if (txstr) { xfree(txstr); txstr = NULL; }
			}
			else if (pickdata(bol, ifstat_aix_pcres[1], 1, &txstr, &rxstr)) dmatch |= 6;
			break;

		  case OS_HPUX: 
			if (pickdata(bol, ifstat_hpux_pcres[0], 1, &ifname)) {
				/* Interface names comes first, so any rx/tx data is discarded */
				dmatch |= 1;
				if (rxstr) { xfree(rxstr); rxstr = NULL; }
				if (txstr) { xfree(txstr); txstr = NULL; }
			}
			else if (pickdata(bol, ifstat_hpux_pcres[1], 1, &rxstr)) dmatch |= 2;
			else if (pickdata(bol, ifstat_hpux_pcres[2], 1, &txstr)) dmatch |= 4;
			break;

		  case OS_DARWIN:
			if (pickdata(bol, ifstat_darwin_pcres[0], 0, &ifname, &rxstr, &txstr)) dmatch = 7;
			break;
			
 		  case OS_SCO_SV:
		        if (pickdata(bol, ifstat_sco_sv_pcres[0], 0, &ifname, &rxstr, &txstr)) dmatch = 7;
			break;
			
		  case OS_WIN32_BBWIN:
			/* Uses same layout as OpenBSD */
			if (pickdata(bol, ifstat_openbsd_pcres[0], 0, &ifname, &rxstr, &txstr)) dmatch = 7;
			break;

		  case OS_OSF:
		  case OS_IRIX:
		  case OS_SNMP:
		  case OS_WIN32:
		  case OS_NETWARE_SNMP:
		  case OS_UNKNOWN:
			break;
		}

		if ((dmatch == 7) && ifname && rxstr && txstr) {
			setupfn2("%s.%s.rrd", "ifstat", ifname);
			sprintf(rrdvalues, "%d:%s:%s", (int)tstamp, txstr, rxstr);
			create_and_update_rrd(hostname, testname, ifstat_params, ifstat_tpl);
			xfree(ifname); xfree(rxstr); xfree(txstr);
			if (dummy) xfree(dummy);
			ifname = rxstr = txstr = dummy = NULL;
			dmatch = 0;
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
	if (dummy) xfree(dummy);

	return 0;
}

