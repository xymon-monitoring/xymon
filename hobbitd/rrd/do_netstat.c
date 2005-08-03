/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char netstat_rcsid[] = "$Id: do_netstat.c,v 1.20 2005-08-03 13:20:17 henrik Exp $";

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
	                          "DS:tcpOutDataPackets:DERIVE:600:0:U", 
	                          "DS:tcpInInorderPackets:DERIVE:600:0:U", 
	                          "DS:tcpInUnorderPackets:DERIVE:600:0:U", 
	                          "DS:tcpRetransPackets:DERIVE:600:0:U", 
				  rra1, rra2, rra3, rra4, NULL };
static char *netstat_tpl       = NULL;

static char *udpreceived = NULL,
	    *udpsent = NULL,
	    *udperrors = NULL;

static char *tcpconnrequests = NULL,
	    *tcpconnaccepts = NULL,
	    *tcpconnfails = NULL,
	    *tcpconncurrent = NULL;

static char *tcpoutdatabytes = NULL, *tcpoutdatapackets = NULL,
	    *tcpinorderbytes = NULL, *tcpinorderpackets = NULL,
	    *tcpoutorderbytes = NULL, *tcpoutorderpackets = NULL,
            *tcpretransbytes = NULL, *tcpretranspackets = NULL;


/* This one matches the netstat output from Solaris 8, and also the hpux and aix from bf-netstat */
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
	"tcpOutDataSegs",
	"tcpInInorderSegs",
	"tcpInUnorderSegs",
	"tcpRetransSegs",
	NULL
};

static pcre **compile_exprs(char *id, const char **patterns, int count)
{
	pcre **result = NULL;
	int i;

	result = (pcre **)calloc(count, sizeof(pcre *));
	for (i=0; (i < count); i++) {
		result[i] = compileregex(patterns[i]);
		if (!result[i]) {
			errprintf("Internal error: %s netstat PCRE-compile failed\n", id);
			for (i=0; (i < count); i++) if (result[i]) pcre_free(result[i]);
			xfree(result);
			return NULL;
		}
	}

	return result;
}

static int pickdata(char *buf, pcre *expr, ...)
{
	int res, i;
	int ovector[30];
	va_list ap;
	char **ptr;
	char w[100];

	res = pcre_exec(expr, NULL, buf, strlen(buf), 0, 0, ovector, (sizeof(ovector)/sizeof(int)));
	if (res < 0) return 0;

	va_start(ap, expr);

	for (i=1; (i < res); i++) {
		*w = '\0';
		pcre_copy_substring(buf, ovector, res, i, w, sizeof(w));
		ptr = va_arg(ap, char **);
		if (*ptr == NULL) {
			*ptr = strdup(w);
		}
		else {
			errprintf("Internal error: Duplicate match ignored\n");
		}
	}

	va_end(ap);

	return 1;
}

static void prepare_update(char *outp)
{
	outp += sprintf(outp, ":%s", (udpreceived ? udpreceived : "U")); if (udpreceived) xfree(udpreceived);
	outp += sprintf(outp, ":%s", (udpsent ? udpsent : "U")); if (udpsent) xfree(udpsent);
	outp += sprintf(outp, ":%s", (udperrors ? udperrors : "U")); if (udperrors) xfree(udperrors);
	outp += sprintf(outp, ":%s", (tcpconnrequests ? tcpconnrequests : "U")); if (tcpconnrequests) xfree(tcpconnrequests);
	outp += sprintf(outp, ":%s", (tcpconnaccepts ? tcpconnaccepts : "U")); if (tcpconnaccepts) xfree(tcpconnaccepts);
	outp += sprintf(outp, ":%s", (tcpconnfails ? tcpconnfails : "U")); if (tcpconnfails) xfree(tcpconnfails);
	outp += sprintf(outp, ":%s", (tcpconncurrent ? tcpconncurrent : "U")); if (tcpconncurrent) xfree(tcpconncurrent);
	outp += sprintf(outp, ":%s", (tcpoutdatabytes ? tcpoutdatabytes : "U")); if (tcpoutdatabytes) xfree(tcpoutdatabytes);
	outp += sprintf(outp, ":%s", (tcpinorderbytes ? tcpinorderbytes : "U")); if (tcpinorderbytes) xfree(tcpinorderbytes);
	outp += sprintf(outp, ":%s", (tcpoutorderbytes ? tcpoutorderbytes : "U")); if (tcpoutorderbytes) xfree(tcpoutorderbytes);
	outp += sprintf(outp, ":%s", (tcpretransbytes ? tcpretransbytes : "U")); if (tcpretransbytes) xfree(tcpretransbytes);
	outp += sprintf(outp, ":%s", (tcpoutdatapackets ? tcpoutdatapackets : "U")); if (tcpoutdatapackets) xfree(tcpoutdatapackets);
	outp += sprintf(outp, ":%s", (tcpinorderpackets ? tcpinorderpackets : "U")); if (tcpinorderpackets) xfree(tcpinorderpackets);
	outp += sprintf(outp, ":%s", (tcpoutorderpackets ? tcpoutorderpackets : "U")); if (tcpoutorderpackets) xfree(tcpoutorderpackets);
	outp += sprintf(outp, ":%s", (tcpretranspackets ? tcpretranspackets : "U")); if (tcpretranspackets) xfree(tcpretranspackets);
}

static int handle_osf_netstat(char *msg, char *outp)
{
	static const char *netstat_osf_exprs[] = {
		/* TCP patterns */
		"^[\t ]*([0-9]+) data packets \\(([0-9]+) bytes\\) retransmitted$",
		"^[\t ]*([0-9]+) data packets \\(([0-9]+) bytes\\)$",
		"^[\t ]*([0-9]+) packets \\(([0-9]+) bytes\\) received in-sequence$",
		"^[\t ]*([0-9]+) out-of-order packets \\(([0-9]+) bytes\\)$",
		"^[\t ]*([0-9]+) connection requests$",
		"^[\t ]*([0-9]+) connection accepts$",
		/* UDP patterns */
		"^[\t ]*([0-9]+) packets received$",
		"^[\t ]*([0-9]+) packets sent$"
	};
	static pcre **netstat_osf_pcres = NULL;
	enum { AT_NONE, AT_TCP, AT_UDP } sect = AT_NONE;
	int havedata = 0;
	char *datapart, *eoln;

	if (netstat_osf_pcres == NULL) {
		netstat_osf_pcres = compile_exprs("OSF", netstat_osf_exprs, 
						 (sizeof(netstat_osf_exprs) / sizeof(netstat_osf_exprs[0])));
		if (netstat_osf_pcres == NULL) return -1;
	}
	
	datapart = strstr(msg, "\ntcp:");	/* Skip to the start of "tcp" (udp comes after) */
	if (!datapart) return -1; else datapart++;

	while (datapart) {
		eoln = strchr(datapart, '\n'); if (eoln) *eoln = '\0';

		if (strncmp(datapart, "tcp:", 4) == 0) 
			sect = AT_TCP;
		else if (strncmp(datapart, "udp:", 4) == 0)
			sect = AT_UDP;
		else {
			switch (sect) {
			  case AT_TCP:
				if (pickdata(datapart, netstat_osf_pcres[0], &tcpretranspackets, &tcpretransbytes)   ||
				    pickdata(datapart, netstat_osf_pcres[1], &tcpoutdatapackets, &tcpoutdatabytes)   ||
				    pickdata(datapart, netstat_osf_pcres[2], &tcpinorderpackets, &tcpinorderbytes)   ||
				    pickdata(datapart, netstat_osf_pcres[3], &tcpoutorderpackets, &tcpoutorderbytes) ||
				    pickdata(datapart, netstat_osf_pcres[4], &tcpconnrequests)                       ||
				    pickdata(datapart, netstat_osf_pcres[5], &tcpconnaccepts)) havedata++;
				break;

			  case AT_UDP:
				if (pickdata(datapart, netstat_osf_pcres[6], &udpreceived)   ||
				    pickdata(datapart, netstat_osf_pcres[7], &udpsent)) havedata++;
				break;

			  default:
				break;
			}
		}
		if (eoln) { *eoln = '\n'; datapart = (eoln+1); } else datapart = NULL;
	}

	prepare_update(outp);
	return (havedata == 8);
}

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
	"",
	"",
	"",
	"",
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
	"",
	"",
	"",
	"",
	"segments send out",	/* Yes, they really do write "send" */
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
	"",
	"",
	"",
	"",
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
	"",
	"",
	"",
	"",
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
	"",
	"",
	"",
	"",
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
				if (*ln == '=') { 
					int numlen;
					ln++; ln += strspn(ln, " \t");
					numlen = strspn(ln, "0123456789");
					*outp = ':'; outp++; memcpy(outp, ln, numlen); outp += numlen; *outp = '\0';
					gotany = gotval = 1;
				}
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
				int numlen;
				if (*ln == '\n') ln++; ln += strspn(ln, " \t");
				numlen = strspn(ln, "0123456789");
				*outp = ':'; outp++; memcpy(outp, ln, numlen); outp += numlen; *outp = '\0';
				gotany = gotval = 1;
			}
		}

		if (!gotval) outp += sprintf(outp, ":U");
		i++;
	}

	return gotany;
}

int do_netstat_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	enum ostype_t ostype;
	char *datapart = msg;
	char *outp;
	int havedata = 0;

	if (netstat_tpl == NULL) netstat_tpl = setup_template(netstat_params);

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
		/* The bf-netstat claims to report as follows:
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

	  case OS_WIN32:
		havedata = do_valaftermarkerequal(netstat_win32_markers, datapart, outp);
		break;

	  case OS_OSF:
		havedata = handle_osf_netstat(datapart, outp);
		break;

	  case OS_LINUX22:
	  case OS_LINUX:
	  case OS_RHEL3:
		/* These are of the form "<value> <marker" */
		datapart = strstr(datapart, "\nTcp:");	/* Skip to the start of "Tcp" (udp comes after) */
		if (datapart) havedata = do_valbeforemarker(netstat_linux_markers, datapart, outp);
		break;

	  case OS_FREEBSD:
	  case OS_NETBSD:
	  case OS_OPENBSD:
	  case OS_DARWIN:
		havedata = do_valbeforemarker(netstat_freebsd_markers, datapart, outp);
		break;

	  case OS_SNMP:
		havedata = do_valbeforemarker(netstat_snmp_markers, datapart, outp);
		break;

	  case OS_IRIX:
		errprintf("Cannot grok irix netstat from host '%s' \n", hostname);
		return -1;

	  case OS_UNKNOWN:
		errprintf("Host '%s' reports netstat for an unknown OS\n", hostname);
		return -1;
	}

	if (havedata) {
		sprintf(rrdfn, "netstat.rrd");
		return create_and_update_rrd(hostname, rrdfn, netstat_params, netstat_tpl);
	}
	else {
		return -1;
	}
}

