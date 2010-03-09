/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains environment variable handling routines.                        */
/*                                                                            */
/* Copyright (C) 2002-2008 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: environ.c 5819 2008-09-30 16:37:31Z storner $";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "libbbgen.h"

const static struct {
	char *name;
	char *val;
} hobbitenv[] = {
	{ "HOBBITDREL", VERSION },
	{ "BBSERVERROOT", "$BBTOPDIR" },
	{ "BBSERVERLOGS", "$BBLOGDIR" },
	{ "BBSERVERHOSTNAME", "$BBHOSTNAME" },
	{ "BBSERVERIP", "$BBHOSTIP" },
	{ "BBSERVEROS", "$BBHOSTOS" },
	{ "BBSERVERWWWNAME", "$BBHOSTNAME" },
	{ "BBSERVERWWWURL", "/hobbit" },
	{ "BBSERVERCGIURL", "/hobbit-cgi" },
	{ "BBSERVERSECCGIURL", "/hobbit-cgisecure" },
	{ "BBLOCATION", "" },
	{ "PATH", "/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin:/usr/local/sbin:$BUILD_HOME/bin" },
	{ "BBPORT", "1984" },
	{ "BBDISP", "$BBSERVERIP" },
	{ "BBDISPLAYS", "" },
	{ "BBPAGE", "$BBSERVERIP" },
	{ "BBPAGERS", "" },
	{ "FQDN", "TRUE" },
	{ "USEHOBBITD", "TRUE" },
	{ "BBGHOSTS", "1" },
	{ "PAGELEVELS", "red yellow purple" },
	{ "PURPLEDELAY", "30" },
	{ "BBLOGSTATUS", "DYNAMIC" },
	{ "PINGCOLUMN", "conn" },
	{ "INFOCOLUMN", "info" },
	{ "TRENDSCOLUMN", "trends" },
	{ "DOCOMBO", "TRUE" },
	{ "BBMAXMSGSPERCOMBO", "100" },
	{ "BBSLEEPBETWEENMSGS", "0" },
	{ "BBOSTYPE", "$BBSERVEROS" },
	{ "MACHINEDOTS", "$BBSERVERHOSTNAME" },
	{ "MACHINEADDR", "$BBSERVERIP" },
	{ "BBWEBHOST", "http://$BBSERVERWWWNAME" },
	{ "BBWEBHOSTURL", "$BBWEBHOST$BBSERVERWWWURL" },
	{ "BBWEBHTMLLOGS", "$BBWEBHOSTURL/html"	 },
	{ "BBWEB", "$BBSERVERWWWURL" },
	{ "BBSKIN", "$BBSERVERWWWURL/gifs" },
	{ "BBHELPSKIN", "$BBSERVERWWWURL/help" },
	{ "BBNOTESSKIN", "$BBSERVERWWWURL/notes" },
	{ "BBMENUSKIN", "$BBSERVERWWWURL/menu" },
	{ "BBREPURL", "$BBSERVERWWWURL/rep" },
	{ "BBSNAPURL", "$BBSERVERWWWURL/snap" },
	{ "BBWAP", "$BBSERVERWWWURL/wml" },
	{ "CGIBINURL", "$BBSERVERCGIURL" },
	{ "BBHOME", "$BUILD_HOME" },
	{ "BBTMP", "$BBHOME/tmp" },
	{ "BBHOSTS", "$BBHOME/etc/bb-hosts" },
	{ "BB", "$BBHOME/bin/bb" },
	{ "BBGEN", "$BBHOME/bin/bbgen" },
	{ "BBVAR", "$BBSERVERROOT/data" },
	{ "BBACKS", "$BBVAR/acks" },
	{ "BBDATA", "$BBVAR/data" },
	{ "BBDISABLED", "$BBVAR/disabled" },
	{ "BBHIST", "$BBVAR/hist" },
	{ "BBHISTLOGS", "$BBVAR/histlogs" },
	{ "BBLOGS", "$BBVAR/logs" },
	{ "BBWWW", "$BBHOME/www" },
	{ "BBHTML", "$BBWWW/html" },
	{ "BBNOTES", "$BBWWW/notes" },
	{ "BBREP", "$BBWWW/rep" },
	{ "BBSNAP", "$BBWWW/snap" },
	{ "BBALLHISTLOG", "TRUE" },
	{ "BBHOSTHISTLOG", "TRUE" },
	{ "SAVESTATUSLOG", "TRUE" },
	{ "CLIENTLOGS", "$BBVAR/hostdata" },
	{ "MAILC", "mail" },
	{ "MAIL", "$MAILC -s" },
	{ "SVCCODES", "disk:100,cpu:200,procs:300,svcs:350,msgs:400,conn:500,http:600,dns:800,smtp:725,telnet:723,ftp:721,pop:810,pop3:810,pop-3:810,ssh:722,imap:843,ssh1:722,ssh2:722,imap2:843,imap3:843,imap4:843,pop2:809,pop-2:809,nntp:819,test:901" },
	{ "ALERTCOLORS", "red,yellow,purple" },
	{ "OKCOLORS", "green,blue,clear" },
	{ "ALERTREPEAT", "30" },
	{ "CONNTEST", "TRUE" },
	{ "IPTEST_2_CLEAR_ON_FAILED_CONN", "TRUE" },
	{ "NONETPAGE", "" },
	{ "FPING", "hobbitping" },
	{ "NTPDATE", "ntpdate" },
	{ "TRACEROUTE", "traceroute" },
	{ "RPCINFO", "rpcinfo" },
	{ "BBROUTERTEXT", "router" },
	{ "NETFAILTEXT", "not OK" },
	{ "BBRRDS", "$BBVAR/rrd" },
	{ "TEST2RRD", "cpu=la,disk,memory,$PINGCOLUMN=tcp,http=tcp,dns=tcp,dig=tcp,time=ntpstat,vmstat,iostat,netstat,temperature,apache,bind,sendmail,nmailq,socks,bea,iishealth,citrix,bbgen,bbtest,bbproxy,hobbitd" },
	{ "GRAPHS", "la,disk:disk_part:5,memory,users,vmstat,iostat,tcp.http,tcp,netstat,temperature,ntpstat,apache,bind,sendmail,nmailq,socks,bea,iishealth,citrix,bbgen,bbtest,bbproxy,hobbitd" },
	{ "SUMMARY_SET_BKG", "FALSE" },
	{ "BBMKBB2EXT", "eventlog.sh acklog.sh" },
	{ "BBREL", "Hobbit" },
	{ "BBRELDATE", "" },
	{ "DOTHEIGHT", "16" },
	{ "DOTWIDTH", "16" },
	{ "RRDHEIGHT", "120" },
	{ "RRDWIDTH", "576" },
	{ "COLUMNDOCURL", "$CGIBINURL/hobbitcolumn.sh?%s" },
	{ "HOBBITLOGO", "Hobbit" },
	{ "MKBBLOCAL", "<B><I>Pages Hosted Locally</I></B>" },
	{ "MKBBREMOTE", "<B><I>Remote Status Display</I></B>" },
	{ "MKBBSUBLOCAL", "<B><I>Subpages Hosted Locally</I></B>" },
	{ "MKBBACKFONT", "COLOR=\"#33ebf4\" SIZE=\"-1\"" },
	{ "MKBBCOLFONT", "COLOR=\"#87a9e5\" SIZE=\"-1\"" },
	{ "MKBBROWFONT", "SIZE=\"+1\" COLOR=\"#FFFFCC\" FACE=\"Tahoma, Arial, Helvetica\"" },
	{ "MKBBTITLE", "COLOR=\"ivory\" SIZE=\"+1\"" },
	{ "BBDATEFORMAT", "%a %b %d %H:%M:%S %Y" },
	{ "BBRSSTITLE", "Hobbit Alerts" },
	{ "ACKUNTILMSG", "Next update at: %H:%M %Y-%m-%d" },
	{ "WMLMAXCHARS", "1500"	},
	{ "BBREPWARN", "97" },
	{ "BBGENREPOPTS", "--recentgifs --subpagecolumns=2" },
	{ "BBGENSNAPOPTS", "--recentgifs --subpagecolumns=2" },
	{ "BBMKBBEXT", "" },
	{ "BBHISTEXT", "" },
	{ "BBSLEEP", "300" },
	{ "MKBB2COLREPEAT", "0" },
	{ "BBHTACCESS", "" },
	{ "BBPAGEHTACCESS", "" },
	{ "BBSUBPAGEHTACCESS", "" },
	{ "BBNETSVCS", "smtp telnet ftp pop pop3 pop-3 ssh imap ssh1 ssh2 imap2 imap3 imap4 pop2 pop-2 nntp" },
	{ "HTMLCONTENTTYPE", "text/html" },
	{ "HOLIDAYFORMAT", "%d/%m" },
	{ "WEEKSTART", "1" },
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
		
		oneenv = (char *)malloc(10 + strlen(xgetenv("MACHINEDOTS")));
		sprintf(oneenv, "%s=%s", name, xgetenv("MACHINEDOTS"));
		p = oneenv; while ((p = strchr(p, '.')) != NULL) *p = ',';
		putenv(oneenv);
		result = getenv(name);
	}

	if (result == NULL) {
		for (i=0; (hobbitenv[i].name && (strcmp(hobbitenv[i].name, name) != 0)); i++) ;
		if (hobbitenv[i].name) result = expand_env(hobbitenv[i].val);
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

void loadenv(char *envfile, char *area)
{
	FILE *fd;
	strbuffer_t *inbuf;
	char *p, *oneenv;
	int n;

	MEMDEFINE(l);
	inbuf = newstrbuffer(0);

	fd = stackfopen(envfile, "r", NULL);
	if (fd) {
		while (stackfgets(inbuf, NULL)) {
			sanitize_input(inbuf, 1, 1);

			if (STRBUFLEN(inbuf) && strchr(STRBUF(inbuf), '=')) {
				/*
				 * Do the environment "area" stuff: If the input
				 * is of the form AREA/NAME=VALUE, then setup the variable
				 * only if we're called with the correct AREA setting.
				 */
				oneenv = NULL;

				p = STRBUF(inbuf) + strcspn(STRBUF(inbuf), "=/");
				if (*p == '/') {
					if (area) {
						*p = '\0';
						if (strcasecmp(STRBUF(inbuf), area) == 0) oneenv = strdup(expand_env(p+1));
					}
				}
				else oneenv = strdup(expand_env(STRBUF(inbuf)));

				if (oneenv) {
					p = strchr(oneenv, '=');
					if (*(p+1) == '"') {
						/* Move string over the first '"' */
						memmove(p+1, p+2, strlen(p+2)+1);
						/* Kill a trailing '"' */
						if (*(oneenv + strlen(oneenv) - 1) == '"') *(oneenv + strlen(oneenv) - 1) = '\0';
					}
					n = putenv(oneenv);
				}
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

	val = getenv(envname);	/* Dont use xgetenv() here! */
	if (!val) {
		val = (char *)malloc(strlen(envname) + strlen(envdefault) + 2);
		sprintf(val, "%s=%s", envname, envdefault);
		putenv(val);
		/* Dont free the string - it must be kept for the environment to work */
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

