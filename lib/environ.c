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
	{ "MAXMSGSPERCOMBO", "100" },
	{ "SLEEPBETWEENMSGS", "0" },
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
	{ "XYMONWEBHTMLLOGS", "$XYMONWEBHOSTURL/html"	 },
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
	{ "WMLMAXCHARS", "1500"	},
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

#ifdef	HAVE_UNAME
static struct utsname u_name;
#endif


static void xymon_default_machine(void)
{
	/* 
	 * If not set, determine MACHINE from the first of:
	 * - MACHINEDOTS environment
	 * - HOSTNAME environment
	 * - The "nodename" setting in the uname-struct
	 * - The output from "uname -n"
	 */
	char *machinebase, *evar;
	char buf[1024];
	
	machinebase = getenv("MACHINE"); if (machinebase) return;

	if (!machinebase) machinebase = getenv("MACHINEDOTS");
	if (!machinebase) machinebase = getenv("HOSTNAME");

#ifdef	HAVE_UNAME
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

	if (!machinebase) {
		errprintf("Cannot determine hostname, defaulting to localhost\n");
		machinebase = "localhost";
	}

	evar = (char *)malloc(9+strlen(machinebase));
	sprintf(evar, "MACHINE=%s", machinebase);
	commafy(evar);
	dbgprintf("Setting %s\n", evar);
	putenv(evar);
}

static void xymon_default_machinedots(void)
{
	/* If not set, make MACHINEDOTS be the dotted form of MACHINE */
	char *machinebase;
	
	machinebase = getenv("MACHINEDOTS"); if (machinebase) return;

	xymon_default_machine();
	machinebase = getenv("MACHINE");
	if (machinebase) {
		char *evar = (char *)malloc(13 + strlen(machinebase));
		sprintf(evar, "MACHINEDOTS=%s", machinebase);
		uncommafy(evar);
		dbgprintf("Setting %s\n", evar);
		putenv(evar);
	}
	else {
		/* Not possible ... since xymon_default_machine() sets MACHINE */
		errprintf("MACHINE not set, cannot set MACHINEDOTS");
	}
}

static void xymon_default_clienthostname(void)
{
	/* If not set, set CLIENTHOSTNAME to MACHINEDOTS */
	char *machinebase, *evar;
	
	if (getenv("CLIENTHOSTNAME")) return;

	xymon_default_machinedots();
	machinebase = getenv("MACHINEDOTS");
	evar = (char *)malloc(strlen(machinebase) + 16);
	sprintf(evar, "CLIENTHOSTNAME=%s", machinebase);
	dbgprintf("Setting %s\n", evar);
	putenv(evar);
}


static void xymon_default_serverostype(void)
{
	/* If not set, make SERVEROSTYPE be output from "uname -s" */
	char *ostype = NULL, *evar;
	char buf[128];

	if (NULL != (ostype = getenv("SERVEROSTYPE"))) return;

#ifdef	HAVE_UNAME
	if (uname(&u_name) == 0) {
		strncpy(buf, u_name.sysname, sizeof(buf));
		ostype = buf;
	}
#endif

	if (!ostype) {
		FILE *fd;
		char *p;

		fd = popen("uname -s", "r");
		if (fd && fgets(buf, sizeof(buf), fd)) {
			p = strchr(buf, '\n'); if (p) *p = '\0';
			pclose(fd);
			ostype = buf;
		}
	}

	if (!ostype) {
		errprintf("Cannot determine OS type, defaulting to unix\n");
		ostype = "unix";
	}
	else {
		char *p;
		for (p=ostype; (*p); p++) *p = (char) tolower((int)*p);
	}

	evar = (char *)malloc(strlen(ostype) + 14);
	sprintf(evar, "SERVEROSTYPE=%s", ostype);
	dbgprintf("Setting %s\n", evar);
	putenv(evar);
}


void xymon_default_xymonhome(char *programname)
{
	char buf[PATH_MAX];

	if (getenv("XYMONHOME") && getenv("XYMONCLIENTHOME")) return;

	if (!getenv("XYMONHOME")) {
		char *dbuf, *evar;

		dbgprintf("Looking for XYMONHOME based on command %s\n", programname);

		/* First check if programname has no path-element, then we need to scan $PATH */
		if (strchr(programname, '/') == NULL) {
			char *path = strdup(getenv("PATH")), *pathelem, *tokr;
			int found = 0;
			char pathpgm[PATH_MAX];
			struct stat st;

			pathelem = strtok_r(path, ":", &tokr);
			while (pathelem && !found) {
				snprintf(pathpgm, sizeof(pathpgm), "%s/%s", pathelem, programname);
				found = (stat(pathpgm, &st) == 0);
				if (!found) pathelem = strtok_r(NULL, ":", &tokr);
			} 

			free(path);
			realpath((found ? pathpgm: programname), buf);
		}
		else {
			realpath(programname, buf);
		}
		dbuf = dirname(buf);

		if ((strlen(dbuf) > 4) && (strcmp(dbuf+strlen(dbuf)-4, "/bin") == 0)) {
			*(dbuf+strlen(dbuf)-4) = '\0';
		}
		else {
			strcpy(buf, getenv("HOME"));
			dbuf = buf;
		}

		evar = (char *)malloc(strlen(dbuf)+11);
		sprintf(evar, "XYMONHOME=%s", dbuf);
		dbgprintf("Setting %s\n", evar);
		putenv(evar);
	}

	if (!getenv("XYMONCLIENTHOME")) {
		char *evar;

		strcpy(buf, getenv("XYMONHOME"));
		if (strcmp(basename(buf), "server") == 0) {
			struct stat st;

			strcpy(buf, getenv("XYMONHOME"));
			sprintf(buf+strlen(buf)-6, "client");
			if (stat(buf, &st) != 0) {
				/* No "../client/" directory, so just use XYMONHOME */
				*buf = '\0';
			}
		}

		if (*buf == '\0') strcpy(buf, getenv("XYMONHOME"));

		evar = (char *)malloc(17+strlen(buf));
		sprintf(evar, "XYMONCLIENTHOME=%s", buf);
		dbgprintf("Setting %s\n", evar);
		putenv(evar);
	}
}


char *xgetenv(const char *name)
{
	char *result, *newstr;
	int i;


	result = getenv(name);
	if (result == NULL) {
		for (i=0; (xymonenv[i].name && (strcmp(xymonenv[i].name, name) != 0)); i++) ;
		if (xymonenv[i].name) result = expand_env(xymonenv[i].val);
		if (result == NULL) {
			errprintf("xgetenv: Cannot find value for variable %s\n", name);
			return NULL;
		}

		/* 
		 * If we got a result, put it into the environment so it will stay there.
		 * Allocate memory for this new environment string - this stays allocated.
		 */
		newstr = malloc(strlen(name) + strlen(result) + 2);
		sprintf(newstr, "%s=%s", name, result);
		putenv(newstr);

		/*
		 * Return pointer to the environment string.
		 */
		result = getenv(name);
	}

	return result;
}


void envcheck(char *envvars[])
{
	int i;
	int ok = 1;

	for (i = 0; (envvars[i]); i++) {
		if (xgetenv(envvars[i]) == NULL) {
			errprintf("Environment variable %s not defined\n", envvars[i]);
			ok = 0;
		}
	}

	if (!ok) {
		errprintf("Aborting\n");
		exit (1);
	}
}

void initenv(void)
{
	if (haveinitenv++) return;

	xymon_default_machine();
	xymon_default_machinedots();
	xymon_default_clienthostname();
	xymon_default_serverostype();
	return;
}

int loaddefaultenv(void)
{
	struct stat st;
	char envfn[PATH_MAX];
	char *defhome = NULL;

	/* Don't load a default environment file on top of an existing one */
	if (haveenv) return 1;
	if (!haveinitenv) initenv();

	/* NB: Chicken-and-egg issue here for $XYMONHOME. If unset, don't check first. */
	if (getenv("XYMONHOME")) {
		snprintf(envfn, sizeof(envfn), "%s/etc/xymonserver.cfg", getenv("XYMONHOME"));
		if (stat(envfn, &st) == -1) snprintf(envfn, sizeof(envfn), "/etc/xymon/xymonserver.cfg");
	}
	else snprintf(envfn, sizeof(envfn), "/etc/xymon/xymonserver.cfg");

	if (getenv("XYMONHOME") && (stat(envfn, &st) == -1)) snprintf(envfn, sizeof(envfn), "%s/etc/xymonclient.cfg", getenv("XYMONHOME"));
	if (getenv("XYMONCLIENTHOME") && (stat(envfn, &st) == -1)) snprintf(envfn, sizeof(envfn), "%s/etc/xymonclient.cfg", getenv("XYMONCLIENTHOME"));
	if (stat(envfn, &st) == -1) snprintf(envfn, sizeof(envfn), "/etc/xymon-client/xymonclient.cfg");
	if (stat(envfn, &st) == -1) snprintf(envfn, sizeof(envfn), "xymonserver.cfg");
	if (stat(envfn, &st) == -1) snprintf(envfn, sizeof(envfn), "xymonclient.cfg");

	if (stat(envfn, &st) == 0) {
		dbgprintf("Using default environment file %s\n", envfn);
		loadenv(envfn, envarea);
		/* Return failure on loadenv failure? */
	}
	else {
		errprintf("Could not find an environment file to load\n");
		return 0;
	}
	return 1;
}


void loadenv(char *envfile, char *area)
{
	FILE *fd;
	strbuffer_t *inbuf;
	char *p, *marker, *evar, *oneenv;

	if (haveenv) {
		errprintf("loadenv(): Loading file '%s' over existing file '%s'; results might be unexpected\n",
			envfile, textornull(getenv("XYMONENV")) );
	}

	haveenv = 1;	/* Set whether this succeeds or not */

	inbuf = newstrbuffer(0);

	fd = stackfopen(envfile, "r", NULL);
	if (fd) {
		while (stackfgets(inbuf, NULL)) {
			char *equalpos;
			int appendto = 0;

			sanitize_input(inbuf, 1, 1);

			if ((STRBUFLEN(inbuf) == 0) || ((equalpos = strchr(STRBUF(inbuf), '=')) == NULL)) continue;

			appendto = ((equalpos > STRBUF(inbuf)) && (*(equalpos-1) == '+'));

			/*
			 * Do the environment "area" stuff: If the input
			 * is of the form AREA/NAME=VALUE, then setup the variable
			 * only if we're called with the correct AREA setting.
			 */
			oneenv = NULL;

			p = STRBUF(inbuf);

			/* Skip ahead for anyone who thinks this is a shell include */
			if ((strncmp(p, "export ", 7) == 0) || (strncmp(p, "export\t", 7) == 0)) { p += 6; p += strspn(p, " \t"); }

			marker = p + strcspn(p, "=/");
			if (*marker == '/') {
				if (area) {
					*marker = '\0';
					if (strcasecmp(p, area) == 0) oneenv = strdup(expand_env(marker+1));
				}
			}
			else oneenv = strdup(expand_env(p));

			if (oneenv) {
				p = strchr(oneenv, '=');
				if (*(p+1) == '"') {
					/* Move string over the first '"' */
					memmove(p+1, p+2, strlen(p+2)+1);
					/* Kill a trailing '"' */
					if (*(oneenv + strlen(oneenv) - 1) == '"') *(oneenv + strlen(oneenv) - 1) = '\0';
				}

				if (appendto) {
					char *oldval, *addstring, *p;

					addstring = strchr(oneenv, '='); if (addstring) { *addstring = '\0'; addstring++; }
					p = strchr(oneenv, '+'); if (p) *p = '\0';

					oldval = getenv(oneenv);
					if (oldval) {
						char *combinedenv = (char *)malloc(strlen(oneenv) + strlen(oldval) + strlen(addstring) + 2);
						sprintf(combinedenv, "%s=%s%s", oneenv, oldval, (addstring));
						xfree(oneenv);
						oneenv = combinedenv;
					}
					else {
						/* oneenv is now VARxxVALUE, so fix it to be a normal env. variable format */
						strcat(oneenv, "=");
						memmove(oneenv+strlen(oneenv), addstring, strlen(addstring) + 1);
					}
				}

				putenv(oneenv);
			}
		}
		stackfclose(fd);

		evar = (char *)malloc(10+strlen(envfile));
		sprintf(evar, "XYMONENV=%s", envfile);
		dbgprintf("Setting %s\n", evar);
		putenv(evar);

	}
	else {
		errprintf("Cannot open env file %s - %s\n", envfile, strerror(errno));
	}

	freestrbuffer(inbuf);
}

char *getenv_default(char *envname, char *envdefault, char **buf)
{
	static char *val;

	val = getenv(envname);	/* Don't use xgetenv() here! */
	if (!val) {
		val = (char *)malloc(strlen(envname) + strlen(envdefault) + 2);
		sprintf(val, "%s=%s", envname, envdefault);
		putenv(val);
		/* Don't free the string - it must be kept for the environment to work */
		val = xgetenv(envname);	/* OK to use xgetenv here */
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

	if ((depth == 0) && res) xfree(res);
	depth++;

	myxp = (envxp_t *)malloc(sizeof(envxp_t));
	myxp->next = xps;
	xps = myxp;

	myxp->resultlen = 4096;
	myxp->result = (char *)malloc(myxp->resultlen);
	*(myxp->result) = '\0';

	sCopy = strdup(s);
	bot = sCopy;
	do {
		tstart = strchr(bot, '$');
		if (tstart) *tstart = '\0'; 

		if ((strlen(myxp->result) + strlen(bot) + 1) > myxp->resultlen) {
			myxp->resultlen += strlen(bot) + 4096;
			myxp->result = (char *)realloc(myxp->result, myxp->resultlen);
		}
		strcat(myxp->result, bot);

		if (tstart) {
			tstart++;
			envval = NULL;

			if (*tstart == '{') {
				tstart++;
				tend = strchr(tstart, '}');
				if (tend) { 
					*tend = '\0'; 
					envval = xgetenv(tstart);
					bot = tend+1;
				} 
				else {
					envval = xgetenv(tstart);
					bot = NULL;
				}
			}
			else {
				tend = tstart + strspn(tstart, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
				savech = *tend;
				*tend = '\0';
				envval = xgetenv(tstart);
				*tend = savech;
				bot = tend;
			}

			if (envval) {
				if ((strlen(myxp->result) + strlen(envval) + 1) > myxp->resultlen) {
					myxp->resultlen += strlen(envval) + 4096;
					myxp->result = (char *)realloc(myxp->result, myxp->resultlen);
				}
				strcat(myxp->result, envval);
			}
		}
		else {
			bot = NULL;
		}
	} while (bot);
	xfree(sCopy);

	depth--;
	if (depth == 0) {
		envxp_t *tmp;
		
		/* Free all xps except the last one (which is myxp) */
		while (xps->next) { tmp = xps; xps = xps->next; xfree(tmp->result); xfree(tmp); }
		if (xps != myxp) {
			errprintf("Assertion failed: xps != myxp\n");
			abort();
		}

		/* We KNOW that xps == myxp */
		res = myxp->result;
		xfree(myxp); 
		xps = NULL;

		return res;
	}
	else return myxp->result;
}

