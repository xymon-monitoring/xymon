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

#include "libxymon.h"

const static struct {
	char *name;
	char *val;
} xymonenv[] = {
	{ "XYMONDREL", VERSION },
	{ "XYMONSERVERROOT", XYMONTOPDIR },
	{ "XYMONSERVERLOGS", XYMONLOGDIR },
	{ "XYMONSERVERHOSTNAME", XYMONHOSTNAME },
	{ "XYMONSERVERIP", XYMONHOSTIP },
	{ "XYMONSERVEROS", XYMONHOSTOS },
	{ "XYMONSERVERWWWNAME", XYMONHOSTNAME },
	{ "XYMONSERVERWWWURL", "/xymon" },
	{ "XYMONSERVERCGIURL", "/xymon-cgi" },
	{ "XYMONSERVERSECURECGIURL", "/xymon-seccgi" },
	{ "XYMONNETWORK", "" },
	{ "BBLOCATION", "" },
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
	{ "PINGCOLUMN", "conn" },
	{ "INFOCOLUMN", "info" },
	{ "TRENDSCOLUMN", "trends" },
	{ "CLIENTCOLUMN", "clientlog" },
	{ "DOCOMBO", "TRUE" },
	{ "MAXMSGSPERCOMBO", "100" },
	{ "SLEEPBETWEENMSGS", "0" },
	{ "SERVEROSTYPE", "$XYMONSERVEROS" },
	{ "MACHINEDOTS", "$XYMONSERVERHOSTNAME" },
	{ "MACHINEADDR", "$XYMONSERVERIP" },
	{ "XYMONWEBHOST", "http://$XYMONSERVERWWWNAME" },
	{ "XYMONWEBHOSTURL", "$XYMONWEBHOST$XYMONSERVERWWWURL" },
	{ "XYMONWEBHTMLLOGS", "$XYMONWEBHOSTURL/html"	 },
	{ "XYMONWEB", "$XYMONSERVERWWWURL" },
	{ "XYMONSKIN", "$XYMONSERVERWWWURL/gifs" },
	{ "XYMONHELPSKIN", "$XYMONSERVERWWWURL/help" },
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
	{ "MAILC", "mail" },
	{ "MAIL", "$MAILC -s" },
	{ "SVCCODES", "disk:100,cpu:200,procs:300,svcs:350,msgs:400,conn:500,http:600,dns:800,smtp:725,telnet:723,ftp:721,pop:810,pop3:810,pop-3:810,ssh:722,imap:843,ssh1:722,ssh2:722,imap2:843,imap3:843,imap4:843,pop2:809,pop-2:809,nntp:819,test:901" },
	{ "ALERTCOLORS", "red,yellow,purple" },
	{ "OKCOLORS", "green,blue,clear" },
	{ "ALERTREPEAT", "30" },
	{ "CONNTEST", "TRUE" },
	{ "IPTEST_2_CLEAR_ON_FAILED_CONN", "TRUE" },
	{ "NONETPAGE", "" },
	{ "FPING", "xymonping" },
	{ "NTPDATE", "ntpdate" },
	{ "TRACEROUTE", "traceroute" },
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
	{ NULL, NULL }
};

char *xgetenv(const char *name)
{
	char *result, *newstr;
	int i;

	result = getenv(name);
	if ((result == NULL) && (strcmp(name, "MACHINE") == 0) && xgetenv("MACHINEDOTS")) {
		/* If MACHINE is undefined, but MACHINEDOTS is there, create MACHINE  */
		char *oneenv, *p;
		
#ifdef HAVE_SETENV
		oneenv = strdup(xgetenv("MACHINEDOTS"));
		p = oneenv; while ((p = strchr(p, '.')) != NULL) *p = ',';
		setenv(name, oneenv, 1);
		xfree(oneenv);
#else
		oneenv = (char *)malloc(10 + strlen(xgetenv("MACHINEDOTS")));
		sprintf(oneenv, "%s=%s", name, xgetenv("MACHINEDOTS"));
		p = oneenv; while ((p = strchr(p, '.')) != NULL) *p = ',';
		putenv(oneenv);
#endif
		result = getenv(name);
	}

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
#ifdef HAVE_SETENV
		setenv(name, result, 1);
#else
		newstr = malloc(strlen(name) + strlen(result) + 2);
		sprintf(newstr, "%s=%s", name, result);
		putenv(newstr);
#endif
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

void loadenv(char *envfile, char *area)
{
	FILE *fd;
	strbuffer_t *inbuf;
	char *p, *marker, *oneenv;

	MEMDEFINE(l);
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
	}
	else {
		errprintf("Cannot open env file %s - %s\n", envfile, strerror(errno));
	}

	freestrbuffer(inbuf);
	MEMUNDEFINE(l);
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

