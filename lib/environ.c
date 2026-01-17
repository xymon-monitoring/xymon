/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains environment variable handling routines.                        */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <unistd.h>
#include <limits.h>

#include "libxymon.h"

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

static int haveinitenv = 0;
static int haveenv = 0;

const static struct {
	char *name;
	char *val;
} xymonenv[] = {
	{ "XYMONDREL", VERSION },
	{ "XYMONSERVERROOT", XYMONTOPDIR },
	{ "XYMONSERVERLOGS", XYMONLOGDIR },
	{ "XYMONRUNDIR", XYMONLOGDIR },
	{ "XYMONSERVERHOSTNAME", XYMONHOSTNAME },
	{ "XYMONSERVERIP", XYMONHOSTIP },
	{ "XYMONSERVEROS", XYMONHOSTOS },
	{ "XYMONSERVERWWWNAME", XYMONHOSTNAME },
	{ "XYMONSERVERWWWURL", "/xymon" },
	{ "XYMONSERVERCGIURL", "/xymon-cgi" },
	{ "XYMONSERVERSECURECGIURL", "/xymon-seccgi" },
	{ "XYMONNETWORK", "" },
	{ "XYMONNETWORKS", "" },
	{ "XYMONEXNETWORKS", "" },
	{ "BBLOCATION", "" },
	{ "TESTUNTAGGED", "FALSE" },
	{ "PATH", "/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin:/usr/local/sbin:"XYMONHOME"/bin" },
	{ "DELAYRED", "" },
	{ "DELAYYELLOW", "" },
	{ "XYMONDPORT", "1984" },
	{ "XYMSRV", "$XYMONSERVERIP" },
	{ "XYMSERVERS", "" },
	{ "FQDN", "TRUE" },
	{ "PAGELEVELS", "red yellow purple" },
	{ "PURPLEDELAY", "30" },
	{ "XYMONLOGSTATUS", "DYNAMIC" },
	{ "XYMONDTCPINTERVAL", "1" },
	{ "MAXACCEPTSPERLOOP", "20" },
	{ "BFQCHUNKSIZE", "50" },
	{ "PINGCOLUMN", "conn" },
	{ "INFOCOLUMN", "info" },
	{ "TRENDSCOLUMN", "trends" },
	{ "CLIENTCOLUMN", "clientlog" },
	{ "COMPRESSION", "FALSE" },
	{ "COMPRESSTYPE", "lzo" },
	{ "DOCOMBO", "TRUE" },
	{ "XYMONV5SERVER", "FALSE" },
	{ "MAXMSGSPERCOMBO", "100" },
	{ "SLEEPBETWEENMSGS", "0" },
	{ "XYMONTIMEOUT", "15" },
	{ "IDLETIMEOUT", "0" },
	{ "MAXMSG_STATUS", "256" },
	{ "MAXMSG_CLIENT", "512" },
	{ "MAXMSG_DATA", "256" },
	{ "MAXMSG_NOTES", "256" },
	{ "MAXMSG_ENADIS", "32" },
	{ "MAXMSG_USER", "128" },
	{ "MAXMSG_BFQ", "$MAXMSG_STATUS" },
	{ "MAXMSG_PAGE", "$MAXMSG_STATUS" },
	{ "MAXMSG_STACHG", "$MAXMSG_STATUS" },
	{ "MAXMSG_CLICHG", "$MAXMSG_CLIENT" },
	{ "SERVEROSTYPE", "$XYMONSERVEROS" },
	{ "MACHINEDOTS", "$XYMONSERVERHOSTNAME" },
	{ "MACHINEADDR", "$XYMONSERVERIP" },
	{ "XYMONWEBHOST", "http://$XYMONSERVERWWWNAME" },
	{ "XYMONWEBHOSTURL", "$XYMONWEBHOST$XYMONSERVERWWWURL" },
	{ "XYMONWEBHTMLLOGS", "$XYMONWEBHOSTURL/html" },
	{ "XYMONWEB", "$XYMONSERVERWWWURL" },
	{ "XYMONSKIN", "$XYMONSERVERWWWURL/gifs" },
	{ "XYMONHELPSKIN", "$XYMONSERVERWWWURL/help" },
	{ "DU", "du -k" },
	{ "XYMONNOTESSKIN", "$XYMONSERVERWWWURL/notes" },
	{ "XYMONMENUSKIN", "$XYMONSERVERWWWURL/menu" },
	{ "XYMONREPURL", "$XYMONSERVERWWWURL/rep" },
	{ "XYMONSNAPURL", "$XYMONSERVERWWWURL/snap" },
	{ "XYMONWAP", "$XYMONSERVERWWWURL/wml" },
	{ "CGIBINURL", "$XYMONSERVERCGIURL" },
	{ "XYMONHOME", XYMONHOME },
	{ "XYMONTMP", "$XYMONHOME/tmp" },
	{ "HOSTSCFG", "$XYMONHOME/etc/hosts.cfg" },
	{ "XYMON", "$XYMONHOME/bin/xymon" },
	{ "XYMONGEN", "$XYMONHOME/bin/xymongen" },
	{ "XYMONVAR", "$XYMONSERVERROOT/data" },
	{ "XYMONACKDIR", "$XYMONVAR/acks" },
	{ "XYMONDATADIR", "$XYMONVAR/data" },
	{ "XYMONDISABLEDDIR", "$XYMONVAR/disabled" },
	{ "XYMONHISTDIR", "$XYMONVAR/hist" },
	{ "XYMONHISTLOGS", "$XYMONVAR/histlogs" },
	{ "XYMONRAWSTATUSDIR", "$XYMONVAR/logs" },
	{ "XYMONWWWDIR", "$XYMONHOME/www" },
	{ "XYMONHTMLSTATUSDIR", "$XYMONWWWDIR/html" },
	{ "XYMONNOTESDIR", "$XYMONWWWDIR/notes" },
	{ "XYMONREPDIR", "$XYMONWWWDIR/rep" },
	{ "XYMONSNAPDIR", "$XYMONWWWDIR/snap" },
	{ "XYMONALLHISTLOG", "TRUE" },
	{ "XYMONHOSTHISTLOG", "TRUE" },
	{ "SAVESTATUSLOG", "TRUE" },
	{ "CLIENTLOGS", "$XYMONVAR/hostdata" },
	{ "SHELL", "/bin/sh" },
	{ "MAILC", "mail" },
	{ "MAIL", "$MAILC -s" },
	{ "SVCCODES", "disk:100,cpu:200,procs:300,svcs:350,msgs:400,conn:500,http:600,dns:800,smtp:725,telnet:723,ftp:721,pop:810,pop3:810,pop-3:810,ssh:722,imap:843,ssh1:722,ssh2:722,imap2:843,imap3:843,imap4:843,pop2:809,pop-2:809,nntp:819,test:901" },
	{ "ALERTCOLORS", "red,yellow,purple" },
	{ "OKCOLORS", "green,blue,clear" },
	{ "ALERTREPEAT", "30" },
	{ "XYMWEBREFRESH", "60" },
	{ "MAXMSG_ALERTSCRIPT", "8164" },
	{ "CONNTEST", "TRUE" },
	{ "IPTEST_2_CLEAR_ON_FAILED_CONN", "TRUE" },
	{ "NONETPAGE", "" },
	{ "FPING", "xymonping" },
	{ "FPINGOPTS", "-Ae" },
	{ "SNTP", "sntp" },
	{ "SNTPOPTS", "-u" },
	{ "NTPDATE", "ntpdate" },
	{ "NTPDATEOPTS", "-u -q -p 1" },
	{ "TRACEROUTE", "traceroute" },
	{ "TRACEROUTEOPTS", "-n -q 2 -w 2 -m 15" },
	{ "RPCINFO", "rpcinfo" },
	{ "XYMONROUTERTEXT", "router" },
	{ "NETFAILTEXT", "not OK" },
	{ "XYMONRRDS", "$XYMONVAR/rrd" },
	{ "TEST2RRD", "cpu=la,disk,memory,$PINGCOLUMN=tcp,http=tcp,dns=tcp,dig=tcp,time=ntpstat,vmstat,iostat,netstat,temperature,apache,bind,sendmail,nmailq,socks,bea,iishealth,citrix,xymongen,xymonnet,xymonproxy,xymond" },
	{ "GRAPHS", "la,disk:disk_part:5,memory,users,vmstat,iostat,tcp.http,tcp,netstat,temperature,ntpstat,apache,bind,sendmail,nmailq,socks,bea,iishealth,citrix,xymongen,xymonnet,xymonproxy,xymond" },
	{ "RRDADDUPDATED", "TRUE" },
	{ "SUMMARY_SET_BKG", "FALSE" },
	{ "XYMONNONGREENEXT", "eventlog.sh acklog.sh" },
	{ "DOTHEIGHT", "16" },
	{ "DOTWIDTH", "16" },
	{ "IMAGEFILETYPE", "gif" },
	{ "RRDHEIGHT", "120" },
	{ "RRDWIDTH", "576" },
	{ "COLUMNDOCURL", "$CGIBINURL/columndoc.sh?%s" },
	{ "HOSTDOCURL", "" },
	{ "XYMONLOGO", "Xymon" },
	{ "XYMONPAGELOCAL", "<B><I>Pages Hosted Locally</I></B>" },
	{ "XYMONPAGEREMOTE", "<B><I>Remote Status Display</I></B>" },
	{ "XYMONPAGESUBLOCAL", "<B><I>Subpages Hosted Locally</I></B>" },
	{ "XYMONPAGEACKFONT", "COLOR=\"#33ebf4\" SIZE=\"-1\"" },
	{ "XYMONPAGECOLFONT", "COLOR=\"#87a9e5\" SIZE=\"-1\"" },
	{ "XYMONPAGEROWFONT", "SIZE=\"+1\" COLOR=\"#FFFFCC\" FACE=\"Tahoma, Arial, Helvetica\"" },
	{ "XYMONPAGETITLE", "COLOR=\"ivory\" SIZE=\"+1\"" },
	{ "XYMONDATEFORMAT", "%a %b %d %H:%M:%S %Y" },
	{ "XYMONRSSTITLE", "Xymon Alerts" },
	{ "ACKUNTILMSG", "Next update at: %H:%M %Y-%m-%d" },
	{ "WMLMAXCHARS", "1500" },
	{ "XYMONREPWARN", "97" },
	{ "XYMONGENREPOPTS", "--recentgifs --subpagecolumns=2" },
	{ "XYMONGENSNAPOPTS", "--recentgifs --subpagecolumns=2" },
	{ "XYMONSTDEXT", "" },
	{ "XYMONHISTEXT", "" },
	{ "TASKSLEEP", "300" },
	{ "XYMONPAGECOLREPEAT", "0" },
	{ "ALLOWALLCONFIGFILES", "" },
	{ "XYMONHTACCESS", "" },
	{ "XYMONPAGEHTACCESS", "" },
	{ "XYMONSUBPAGEHTACCESS", "" },
	{ "XYMONNETSVCS", "smtp telnet ftp pop pop3 pop-3 ssh imap ssh1 ssh2 imap2 imap3 imap4 pop2 pop-2 nntp" },
	{ "HTMLCONTENTTYPE", "text/html" },
	{ "HOLIDAYFORMAT", "%d/%m" },
	{ "WEEKSTART", "1" },
	{ "XYMONBODYCSS", "$XYMONSKIN/xymonbody.css" },
	{ "XYMONBODYMENUCSS", "$XYMONMENUSKIN/xymonmenu.css" },
	{ "XYMONBODYHEADER", "file:$XYMONHOME/etc/xymonmenu.cfg" },
	{ "XYMONBODYFOOTER", "" },
	{ "LOGFETCHSKIPTEXT", "<...SKIPPED...>" },
	{ "LOGFETCHCURRENTTEXT", "<...CURRENT...>" },
	{ "XYMONALLOKTEXT", "<FONT SIZE=+2 FACE=\"Arial, Helvetica\"><BR><BR><I>All Monitored Systems OK</I></FONT><BR><BR>" },
	{ "HOSTPOPUP", "CDI" },
	{ "STATUSLIFETIME", "30" },
	{ "ACK_COOKIE_EXPIRATION", "86400" },
	{ NULL, NULL }
};

#ifdef HAVE_UNAME
static struct utsname u_name;
#endif

static void xymon_default_machine(void)
{
	char *machinebase, *evar;
	char buf[1024];

	machinebase = getenv("MACHINE"); if (machinebase) return;
	if (!machinebase) machinebase = getenv("MACHINEDOTS");
	if (!machinebase) machinebase = getenv("HOSTNAME");

#ifdef HAVE_UNAME
	if (uname(&u_name) == 0) machinebase = u_name.nodename;
#endif

	if (!machinebase) {
		FILE *fd;
		char *p;

		fd = popen("uname -n", "r");
		if (fd && fgets(buf, sizeof(buf), fd)) {
			p = strchr(buf, '\n'); if (p) *p = '\0';
			pclose(fd);
		}
		machinebase = buf;
	}

	if (!machinebase) machinebase = "localhost";

	evar = (char *)malloc(9 + strlen(machinebase));
	sprintf(evar, "MACHINE=%s", machinebase);
	commafy(evar);
	putenv(evar);
}

static void xymon_default_machinedots(void)
{
	char *machinebase;

	machinebase = getenv("MACHINEDOTS"); if (machinebase) return;

	xymon_default_machine();
	machinebase = getenv("MACHINE");
	if (machinebase) {
		char *evar = (char *)malloc(13 + strlen(machinebase));
		sprintf(evar, "MACHINEDOTS=%s", machinebase);
		uncommafy(evar);
		putenv(evar);
	}
}

static void xymon_default_clienthostname(void)
{
	char *machinebase, *evar;

	if (getenv("CLIENTHOSTNAME")) return;

	xymon_default_machinedots();
	machinebase = getenv("MACHINEDOTS");
	evar = (char *)malloc(strlen(machinebase) + 16);
	sprintf(evar, "CLIENTHOSTNAME=%s", machinebase);
	putenv(evar);
}

static void xymon_default_serverostype(void)
{
	char *ostype = NULL, *evar;
	char buf[128];

	if (getenv("SERVEROSTYPE")) return;

#ifdef HAVE_UNAME
	if (uname(&u_name) == 0) {
		strncpy(buf, u_name.sysname, sizeof(buf));
		ostype = buf;
	}
#endif

	if (!ostype) ostype = "unix";

	for (char *p = ostype; *p; p++) *p = (char)tolower((int)*p);

	evar = (char *)malloc(strlen(ostype) + 14);
	sprintf(evar, "SERVEROSTYPE=%s", ostype);
	putenv(evar);
}

void initenv(void)
{
	if (haveinitenv++) return;

	xymon_default_machine();
	xymon_default_machinedots();
	xymon_default_clienthostname();
	xymon_default_serverostype();
}

char *xgetenv(const char *name)
{
	char *result;
	SBUF_DEFINE(newstr);
	int i;

	result = getenv(name);
	if (!result) {
		for (i = 0; xymonenv[i].name && strcmp(xymonenv[i].name, name); i++);
		if (xymonenv[i].name) result = expand_env(xymonenv[i].val);
		if (!result) return NULL;

#ifdef HAVE_SETENV
		setenv(name, result, 1);
#else
		SBUF_MALLOC(newstr, strlen(name) + strlen(result) + 2);
		snprintf(newstr, newstr_buflen, "%s=%s", name, result);
		putenv(newstr);
#endif
		result = getenv(name);
	}
	return result;
}

void envcheck(char *envvars[])
{
	for (int i = 0; envvars[i]; i++) {
		if (!xgetenv(envvars[i])) exit(1);
	}
}

int loaddefaultenv(void)
{
	struct stat st;
	char envfn[PATH_MAX];

	if (haveenv) return 1;
	if (!haveinitenv) initenv();

	snprintf(envfn, sizeof(envfn), "%s/etc/xymonserver.cfg", getenv("XYMONHOME") ? getenv("XYMONHOME") : "");
	if (stat(envfn, &st) == 0) loadenv(envfn, envarea);
	else return 0;

	return 1;
}

void loadenv(char *envfile, char *area)
{
	FILE *fd;
	strbuffer_t *inbuf;
	char *p, *marker;
	SBUF_DEFINE(oneenv);

	inbuf = newstrbuffer(0);
	fd = stackfopen(envfile, "r", NULL);
	if (!fd) return;

	while (stackfgets(inbuf, NULL)) {
		char *equalpos;
		int appendto = 0;

		sanitize_input(inbuf, 1, 1);
		if (!STRBUFLEN(inbuf) || !(equalpos = strchr(STRBUF(inbuf), '='))) continue;
		appendto = (equalpos > STRBUF(inbuf) && *(equalpos - 1) == '+');

		oneenv = strdup(expand_env(STRBUF(inbuf)));
		if (appendto) {
			char *oldval = getenv(oneenv);
			if (oldval) {
				SBUF_DEFINE(combinedenv);
				SBUF_MALLOC(combinedenv, strlen(oneenv) + strlen(oldval) + 2);
				snprintf(combinedenv, combinedenv_buflen, "%s%s", oldval, oneenv);
				xfree(oneenv);
				oneenv = combinedenv;
			}
		}
		putenv(oneenv);
	}
	stackfclose(fd);
	freestrbuffer(inbuf);
}

char *getenv_default(char *envname, char *envdefault, char **buf)
{
	char *val = getenv(envname);
	if (!val) {
		size_t len = strlen(envname) + strlen(envdefault) + 2;
		val = malloc(len);
		snprintf(val, len, "%s=%s", envname, envdefault);
		putenv(val);
		val = xgetenv(envname);
	}
	if (buf) *buf = val;
	return val;
}

typedef struct envxp_t {
	char *result;
	int resultlen;
	struct envxp_t *next;
} envxp_t;

static envxp_t *xps = NULL;

char *expand_env(char *s)
{
	static char *res = NULL;
	static int depth = 0;
	char *sCopy, *bot, *tstart, *tend, *envval;
	char savech;
	envxp_t *myxp;

	if (!depth && res) xfree(res);
	depth++;

	myxp = malloc(sizeof(envxp_t));
	myxp->next = xps;
	xps = myxp;
	myxp->resultlen = 4096;
	myxp->result = malloc(myxp->resultlen);
	*myxp->result = '\0';

	sCopy = strdup(s);
	bot = sCopy;
	do {
		tstart = strchr(bot, '$');
		if (tstart) *tstart = '\0';
		strncat(myxp->result, bot, myxp->resultlen - strlen(myxp->result) - 1);

		if (tstart) {
			tstart++;
			tend = tstart + strspn(tstart, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
			savech = *tend;
			*tend = '\0';
			envval = xgetenv(tstart);
			*tend = savech;
			if (envval) strncat(myxp->result, envval, myxp->resultlen - strlen(myxp->result) - 1);
			bot = tend;
		}
		else bot = NULL;
	} while (bot);

	xfree(sCopy);
	depth--;

	if (!depth) {
		res = myxp->result;
		xfree(myxp);
		xps = NULL;
		return res;
	}
	return myxp->result;
}

