/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains miscellaneous routines.                                        */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: misc.c,v 1.13 2004-11-20 22:28:27 henrik Exp $";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>

#include "errormsg.h"
#include "misc.h"
#include "stackio.h"
#include "version.h"

enum ostype_t get_ostype(char *osname)
{
	char savech;
	enum ostype_t result = OS_UNKNOWN;

	int n = strspn(osname, "abcdefghijklmnopqrstuvwxyz0123456789");
	savech = *(osname+n); *(osname+n) = '\0';

	if      (strcmp(osname, "solaris") == 0)     result = OS_SOLARIS;
	else if (strcmp(osname, "hpux") == 0)        result = OS_HPUX;
	else if (strcmp(osname, "aix") == 0)         result = OS_AIX;
	else if (strcmp(osname, "osf") == 0)         result = OS_OSF;
	else if (strcmp(osname, "sco") == 0)         result = OS_SCO;
	else if (strcmp(osname, "win32") == 0)       result = OS_WIN32;
	else if (strcmp(osname, "freebsd") == 0)     result = OS_FREEBSD;
	else if (strcmp(osname, "redhat") == 0)      result = OS_REDHAT;
	else if (strcmp(osname, "debian3") == 0)     result = OS_DEBIAN3;
	else if (strcmp(osname, "debian") == 0)      result = OS_DEBIAN;
	else if (strcmp(osname, "linux") == 0)       result = OS_LINUX;
	else if (strcmp(osname, "snmp") == 0)        result = OS_SNMP;
	else if (strcmp(osname, "snmpnetstat") == 0) result = OS_SNMP;

	if (result == OS_UNKNOWN) dprintf("Unknown OS: '%s'\n", osname);

	*(osname+n) = savech;
	return result;
}

int hexvalue(unsigned char c)
{
	switch (c) {
	  case '0': return 0;
	  case '1': return 1;
	  case '2': return 2;
	  case '3': return 3;
	  case '4': return 4;
	  case '5': return 5;
	  case '6': return 6;
	  case '7': return 7;
	  case '8': return 8;
	  case '9': return 9;
	  case 'a': return 10;
	  case 'A': return 10;
	  case 'b': return 11;
	  case 'B': return 11;
	  case 'c': return 12;
	  case 'C': return 12;
	  case 'd': return 13;
	  case 'D': return 13;
	  case 'e': return 14;
	  case 'E': return 14;
	  case 'f': return 15;
	  case 'F': return 15;
	}

	return -1;
}

void envcheck(char *envvars[])
{
	int i;
	int ok = 1;

	for (i = 0; (envvars[i]); i++) {
		if (getenv(envvars[i]) == NULL) {
			errprintf("Environment variable %s not defined\n", envvars[i]);
			ok = 0;
		}
	}

	if (!ok) {
		errprintf("Aborting\n");
		exit (1);
	}
}

void loadenv(char *envfile)
{
	FILE *fd;
	char l[32768];
	char *p, *oneenv;
	int n;

	fd = stackfopen(envfile, "r");
	if (fd) {
		while (stackfgets(l, sizeof(l), "include", NULL)) {
			/* Kill the newline ... */
			p = strchr(l, '\n'); if (p) *p = '\0';

			/* ... and any comments ... */
			p = l + strlen(l) - 1;
			while ((p > l) && (*p != '"') && (*p != '#')) p--;
			if (*p == '#') *p = '\0'; /* Kill all comments */

			/* ... and trailing whitespace */
			p = l + strlen(l) -1; while (isspace((int)*p) && (p >= l)) p--;
			*(p+1) = '\0';

			p = l + strspn(l, " \t");
			if ((*p) && strchr(p, '=')) {
				oneenv = strdup(p);
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
		stackfclose(fd);

		/* Always provide the BBGENDREL variable */
		if (getenv("BBGENDREL") == NULL) {
			sprintf(l, "BBGENDREL=%s", VERSION);
			oneenv = strdup(l);
			putenv(oneenv);
		}
	}
	else {
		errprintf("Cannot open env file %s - %s\n", envfile, strerror(errno));
	}
}

char *getenv_default(char *envname, char *envdefault, char **buf)
{
	static char *val;

	val = getenv(envname);
	if (!val) {
		val = (char *)malloc(strlen(envname) + strlen(envdefault) + 2);
		sprintf(val, "%s=%s", envname, envdefault);
		putenv(val);
		/* Dont free the string - it must be kept for the environment to work */
		val = getenv(envname);
	}

	if (buf) *buf = val;
	return val;
}


char *commafy(char *hostname)
{
	static char *s = NULL;
	char *p;

	if (s == NULL) {
		s = strdup(hostname);
	}
	else if (strlen(hostname) > strlen(s)) {
		free(s);
		s = strdup(hostname);
	}
	else {
		strcpy(s, hostname);
	}

	for (p = strchr(s, '.'); (p); p = strchr(s, '.')) *p = ',';
	return s;
}


char *skipword(char *l)
{
	char *p;

	for (p=l; (*p && (!isspace((int)*p))); p++) ;
	return p;
}


char *skipwhitespace(char *l)
{
	char *p;

	for (p=l; (*p && (isspace((int)*p))); p++) ;
	return p;
}


int argnmatch(char *arg, char *match)
{
	return (strncmp(arg, match, strlen(match)) == 0);
}


void addtobuffer(char **buf, int *bufsz, char *newtext)
{
	if (*buf == NULL) {
		*bufsz = strlen(newtext) + 4096;
		*buf = (char *) malloc(*bufsz);
		**buf = '\0';
	}
	else if ((strlen(*buf) + strlen(newtext) + 1) > *bufsz) {
		*bufsz += strlen(newtext) + 4096;
		*buf = (char *) realloc(*buf, *bufsz);
	}

	strcat(*buf, newtext);
}


char *msg_data(char *msg)
{
	/* Find the start position of the data following the "status host.test " message */
	char *result;

	result = strchr(msg, '.');              /* Hits the '.' in "host.test" */
	if (!result) {
		dprintf("Msg was not what I expected: '%s'\n", msg);
		return msg;
	}

	result += strcspn(result, " \t\n");     /* Skip anything until we see a space, TAB or NL */
	result += strspn(result, " \t");        /* Skip all whitespace */

	return result;
}

char *gettok(char *s, char *delims)
{
	/*
	 * This works like strtok(), but can handle empty fields.
	 */

	static char *source = NULL;
	static char *whereat = NULL;
	int n;
	char *result;

	if ((delims == NULL) || (*delims == '\0')) return NULL;	/* Sanity check */
	if ((source == NULL) && (s == NULL)) return NULL;	/* Programmer goofed and called us first time with NULL */

	if (s) source = whereat = s;				/* First call */

	if (*whereat == '\0') {
		/* End of string ... clear local state and return NULL */
		source = whereat = NULL;
		return NULL;
	}

	n = strcspn(whereat, delims);
	if (n == 0) {
		/* An empty token */
		whereat++;
		result = "";
	}
	else {
		/* Null-teminate the token */
		*(whereat + n) = '\0';
		result = whereat;

		/* Move past this token and the delimiter */
		whereat += (n+1);
	}

	return result;
}

char *getword(char *buf)
{
	static char *startpos = NULL;
	int n, insideword;
	char savech;
	char *beginquote = NULL, *endquote = NULL;
	char *endpos, *result;

	if (startpos == NULL) {
		if (buf == NULL) return NULL;
		startpos = buf;
	}

	/* Skip leading whitespace */
	n = strspn(startpos, " \t\r\n");
	startpos += n;
	if (*startpos == '\0') return NULL;
	result = startpos;

	/* How long is the next word ? */
	insideword = 1;
	while (insideword) {
		n = strcspn(startpos, " \t\r\n");
		endpos = startpos + n;
		savech = *endpos;
		*endpos = '\0';
		beginquote = strchr(startpos, '"');
		*endpos = savech;
		if (beginquote) {
			memmove(beginquote, beginquote+1, strlen(beginquote));
			endquote = strchr(beginquote, '"');
			if (endquote) memmove(endquote, endquote+1, strlen(endquote));
			endpos = endquote;
			startpos = endquote+1;
			insideword = (strcspn(startpos, " \t\r\n") > 0);
		}
	}
	*endpos = '\0';

	return result;
}

unsigned int IPtou32(int ip1, int ip2, int ip3, int ip4)
{
	return ((ip1 << 24) | (ip2 << 16) | (ip3 << 8) | (ip4));
}

char *u32toIP(unsigned int ip32)
{
	int ip1, ip2, ip3, ip4;
	static char result[16];

	ip1 = ((ip32 >> 24) & 0xFF);
	ip2 = ((ip32 >> 16) & 0xFF);
	ip3 = ((ip32 >> 8) & 0xFF);
	ip4 = (ip32 & 0xFF);

	sprintf(result, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
	return result;
}

const char *textornull(const char *text)
{
	return (text ? text : "(NULL)");
}


int get_fqdn(void)
{
	/* Get FQDN setting */
	getenv_default("FQDN", "TRUE", NULL);
	return (strcmp(getenv("FQDN"), "TRUE") == 0);
}

int generate_static(void)
{
	getenv_default("BBLOGSTATUS", "STATIC", NULL);
	return (strcmp(getenv("BBLOGSTATUS"), "STATIC") == 0);
}


int run_command(char *cmd, char *errortext, char **banner, int *bannerbytes, int showcmd)
{
	FILE	*cmdpipe;
	char	l[1024];
	int	result;
	int	piperes;
	int	bannersize = 0;

	result = 0;
	if (banner) { 
		bannersize = 4096;
		*banner = (char *) malloc(bannersize); 
		**banner = '\0';
		if (showcmd) sprintf(*banner, "Command: %s\n\n", cmd); 
	}
	cmdpipe = popen(cmd, "r");
	if (cmdpipe == NULL) {
		errprintf("Could not open pipe for command %s\n", cmd);
		if (banner) strcat(*banner, "popen() failed to run command\n");
		return -1;
	}

	while (fgets(l, sizeof(l), cmdpipe)) {
		if (banner) {
			if ((strlen(l) + strlen(*banner)) > bannersize) {
				bannersize += strlen(l) + 4096;
				*banner = (char *) realloc(*banner, bannersize);
			}
			strcat(*banner, l);
		}
		if (errortext && (strstr(l, errortext) != NULL)) result = 1;
	}
	piperes = pclose(cmdpipe);
	if (!WIFEXITED(piperes) || (WEXITSTATUS(piperes) != 0)) {
		/* Something failed */
		result = 1;
	}

	if (bannerbytes) *bannerbytes = strlen(*banner);
	return result;
}


void do_bbext(FILE *output, char *extenv, char *family)
{
	/*
	 * Extension scripts. These are ad-hoc, and implemented as a
	 * simple pipe. So we do a fork here ...
	 */

	char *bbexts, *p;
	FILE *inpipe;
	char extfn[PATH_MAX];
	char buf[4096];

	p = getenv(extenv);
	if (p == NULL) {
		/* No extension */
		return;
	}

	bbexts = strdup(p);
	p = strtok(bbexts, "\t ");

	while (p) {
		/* Dont redo the eventlog or acklog things */
		if ((strcmp(p, "eventlog.sh") != 0) &&
		    (strcmp(p, "acklog.sh") != 0)) {
			sprintf(extfn, "%s/ext/%s/%s", getenv("BBHOME"), family, p);
			inpipe = popen(extfn, "r");
			if (inpipe) {
				while (fgets(buf, sizeof(buf), inpipe)) 
					fputs(buf, output);
				pclose(inpipe);
			}
		}
		p = strtok(NULL, "\t ");
	}

	free(bbexts);
}

