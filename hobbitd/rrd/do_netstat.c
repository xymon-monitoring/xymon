/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char netstat_rcsid[] = "$Id: do_netstat.c,v 1.6 2004-11-25 11:46:31 henrik Exp $";

static char *netstat_params[] = { "rrdcreate", rrdfn, 
	                          "DS:udpInDatagrams:DERIVE:600:0:U", 
	                          "DS:udpOutDatagrams:DERIVE:600:0:U", 
	                          "DS:udpInErrors:DERIVE:600:0:U", 
	                          "DS:tcpActiveOpens:DERIVE:600:0:U", 
	                          "DS:tcpPassiveOpens:DERIVE:600:0:U", 
	                          "DS:tcpAttemptFails:DERIVE:600:0:U", 
	                          "DS:tcpEstabResets:DERIVE:600:0:U", 
	                          "DS:tcpCurrEstab:GAUGE:600:0:U", 
	                          "DS:tcpOutDataBytes:DERIVE:600:0:U", 
	                          "DS:tcpInInorderBytes:DERIVE:600:0:U", 
	                          "DS:tcpInUnorderBytes:DERIVE:600:0:U", 
	                          "DS:tcpRetransBytes:DERIVE:600:0:U", 
				  rra1, rra2, rra3, rra4, NULL };

/* This one matches the netstat output from Solaris 8, and also the hpux and aix from bf-netstat in larrd 0.43c */
static char *netstat_unix_markers[] = {
	"udpInDatagrams",
	"udpOutDatagrams",
	"udpInErrors",
	"tcpActiveOpens",
	"tcpPassiveOpens",
	"tcpAttemptFails",
	"tcpEstabResets",
	"tcpCurrEstab",
	"tcpOutDataBytes",
	"tcpInInorderBytes",
	"tcpInUnorderBytes",
	"tcpRetransBytes",
	NULL
};

/* This is for the native netstat output from HP-UX (and AIX) */
static char *netstat_hpux_markers[] = {
	"",				/* udpInDatagrams */
	"",				/* udpOutDatagrams */
	"",				/* udpInErrors */
	"connection requests",
	"connection accepts",
	"",				/* tcpAttemptFails */
	"",				/* tcpEstabResets */
	"",				/* tcpCurrEstab */
	"data packets",
	"received in-sequence",
	"out of order packets",
	"retransmitted",
	NULL
};

/* This one matches all Linux systems */
static char *netstat_linux_markers[] = {
	"packets received",
	"packets sent",
	"packet receive errors",
	"active connections openings",
	"passive connection openings",
	"failed connection attempts",
	"connection resets received",
	"connections established",
	"segments send out",
	"segments received",
	"",
	"segments retransmited",
	NULL
};

/* This one matches FreeBSD 4.10. Untested. */
static char *netstat_freebsd_markers[] = {
	"datagrams received",
	"datagrams output",
	"",  /* Multiple counters, wont add them up. */
	"connection requests",
	"connection accepts",
	"bad connection attempts",
	"",  /* Appears not to count resets */
	"connections established",
	"packets sent",
	"received in-sequence",
	"out-of-order packets",
	"",  /* N data packets (X bytes) retransmitted */
	NULL
};

/* This one matches the "snmpnetstat" output from UCD-SNMP */
static char *netstat_snmp_markers[] = {
	"total datagrams received",
	"output datagram requests",
	"datagrams dropped due to errors",
	"active opens",
	"passive opens",
	"failed attempts",
	"resets of established connections",
	"current established connections",
	"segments sent",
	"segments received",
	"",
	"segments retransmitted",
	NULL
};

/* This one is for the Win32 netstat tool */
static char *netstat_win32_markers[] = {
	"udpDatagramsReceived",
	"udpDatagramsSent",
	"udpReceiveErrors",
	"tcpActiveOpens",
	"tcpPassiveOpens",
	"tcpFailedConnectionAttempts",
	"tcpResetConnections",
	"tcpCurrentConnections",
	"tcpSegmentsSent",
	"tcpSegmentsReceived",
	"",
	"tcpSegmentsRetransmitted",
	NULL
};

static int do_valaftermarkerequal(char *layout[], char *msg, char *outp)
{
	int i, gotany = 0;
	char *ln;

	i = 0;
	while (layout[i]) {
		int gotval = 0;

		if (strlen(layout[i])) {
			ln = strstr(msg, layout[i]);
			if (ln) {
				ln += strlen(layout[i]);
				ln += strspn(ln, " \t");
				if (*ln == '=') { outp += sprintf(outp, ":%lu", atol(ln+1)); gotany = gotval = 1; }
			}
		}

		if (!gotval) outp += sprintf(outp, ":U");
		i++;
	}

	return gotany;
}

static int do_valbeforemarker(char *layout[], char *msg, char *outp)
{
	int i, gotany = 0;
	char *ln;

	i = 0;
	while (layout[i]) {
		int gotval = 0;

		if (strlen(layout[i])) {
			ln = strstr(msg, layout[i]);
			while (ln && (ln > msg) && (*ln != '\n')) ln--;
			if (ln) {
				outp += sprintf(outp, ":%lu", atol(ln+1));
				gotany = gotval = 1;
			}
		}

		if (!gotval) outp += sprintf(outp, ":U");
		i++;
	}

	return gotany;
}

int do_netstat_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	enum ostype_t ostype;
	char *datapart = msg;
	char *outp;
	int havedata = 0;

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
		errprintf("Too few lines in netstat report from %s\n", hostname);
		return -1;
	}

	/* Setup the update string */
	outp = rrdvalues + sprintf(rrdvalues, "%d", (int)tstamp);

	switch (ostype) {
	  case OS_SOLARIS: 
		/*
		 * UDP
		 *	udpInDatagrams      =420642     udpInErrors         =     0
		 * TCP     
		 *	tcpRtoAlgorithm     =     4     tcpRtoMin           =   400
		 */
		havedata = do_valaftermarkerequal(netstat_unix_markers, datapart, outp);
		break;

	  case OS_AIX: 
	  case OS_HPUX: 
		/* The bf-netstat from larrd 0.43c claims to report as follows:
		 *
		 * udpInDatagrams = 0
		 * udpOutDatagrams = 0
		 * udpInErrors = 0
		 * tcpActiveOpens = 0
		 * tcpPassiveOpens = $tcpPassiveOpens
		 * tcpAttemptFails = 0
		 * tcpEstabResets = 0
		 * tcpCurrEstab = $tcpCurrEstab
		 * tcpOutDataBytes = $tcpOutDataBytes
		 * tcpInInorderBytes = $tcpInInorderBytes
		 * tcpInUnorderBytes = $tcpInUnorderBytes
		 * tcpRetransBytes = $tcpRetransBytes
		 */
		havedata = do_valaftermarkerequal(netstat_unix_markers, datapart, outp);
		if (!havedata) havedata = do_valbeforemarker(netstat_hpux_markers, datapart, outp);
		break;

	  case OS_LINUX:
	  case OS_REDHAT:
	  case OS_DEBIAN:
	  case OS_DEBIAN3:
		/* These are of the form "<value> <marker" */
		datapart = strstr(datapart, "\nTcp:");	/* Skip to the start of "Tcp" (udp comes after) */
		if (datapart) havedata = do_valbeforemarker(netstat_linux_markers, datapart, outp);
		break;

	  case OS_FREEBSD:
		havedata = do_valbeforemarker(netstat_freebsd_markers, datapart, outp);
		break;

	  case OS_SNMP:
		havedata = do_valbeforemarker(netstat_snmp_markers, datapart, outp);
		break;

	  case OS_WIN32:
		havedata = do_valbeforemarker(netstat_win32_markers, datapart, outp);
		break;

	  case OS_SCO:
		errprintf("Cannot grok sco netstat from host '%s' \n", hostname);
		return -1;

	  case OS_OSF:
		errprintf("Cannot grok osf netstat from host '%s' \n", hostname);
		return -1;

	  case OS_UNKNOWN:
		errprintf("Host '%s' reports netstat for an unknown OS\n", hostname);
		return -1;
	}

	if (havedata) {
		sprintf(rrdfn, "netstat.rrd");
		return create_and_update_rrd(hostname, rrdfn, netstat_params, update_params);
	}
	else {
		return -1;
	}
}

