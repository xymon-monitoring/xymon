/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains miscellaneous routines.                                        */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include "config.h"

#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>         /* Someday I'll move to GNU Autoconf for this ... */
#endif
#include <fcntl.h>
#include <sys/statvfs.h>

#include "libxymon.h"
#include "version.h"

enum ostype_t get_ostype(char *osname)
{
	char *nam;
	enum ostype_t result = OS_UNKNOWN;
	int n;

	if (!osname || (*osname == '\0')) return OS_UNKNOWN;

	n = strspn(osname, "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-/_");
	nam = (char *)malloc(n+1);
	strncpy(nam, osname, n);
	*(nam+n) = '\0';

	if      (strcasecmp(nam, "solaris") == 0)     result = OS_SOLARIS;
	else if (strcasecmp(nam, "sunos") == 0)       result = OS_SOLARIS;
	else if (strcasecmp(nam, "hpux") == 0)        result = OS_HPUX;
	else if (strcasecmp(nam, "hp-ux") == 0)       result = OS_HPUX;
	else if (strcasecmp(nam, "aix") == 0)         result = OS_AIX;
	else if (strcasecmp(nam, "osf") == 0)         result = OS_OSF;
	else if (strcasecmp(nam, "osf1") == 0)        result = OS_OSF;
	else if (strcasecmp(nam, "win32") == 0)       result = OS_WIN32;
	else if (strcasecmp(nam, "hmdc") == 0)        result = OS_WIN32_HMDC;
	else if (strcasecmp(nam, "bbwin") == 0)       result = OS_WIN32_BBWIN;
	else if (strcasecmp(nam, "powershell") == 0)  result = OS_WIN_POWERSHELL;
	else if (strcasecmp(nam, "freebsd") == 0)     result = OS_FREEBSD;
	else if (strcasecmp(nam, "netbsd") == 0)      result = OS_NETBSD;
	else if (strcasecmp(nam, "openbsd") == 0)     result = OS_OPENBSD;
	else if (strcasecmp(nam, "debian3") == 0)     result = OS_LINUX22;
	else if (strcasecmp(nam, "linux22") == 0)     result = OS_LINUX22;
	else if (strcasecmp(nam, "linux") == 0)       result = OS_LINUX;
	else if (strcasecmp(nam, "redhat") == 0)      result = OS_LINUX;
	else if (strcasecmp(nam, "debian") == 0)      result = OS_LINUX;
	else if (strcasecmp(nam, "suse") == 0)        result = OS_LINUX;
	else if (strcasecmp(nam, "mandrake") == 0)    result = OS_LINUX;
	else if (strcasecmp(nam, "redhatAS") == 0)    result = OS_LINUX;
	else if (strcasecmp(nam, "redhatES") == 0)    result = OS_RHEL3;
	else if (strcasecmp(nam, "rhel3") == 0)       result = OS_RHEL3;
	else if (strcasecmp(nam, "snmp") == 0)        result = OS_SNMP;
	else if (strcasecmp(nam, "snmpnetstat") == 0) result = OS_SNMP;
	else if (strncasecmp(nam, "irix", 4) == 0)    result = OS_IRIX;
	else if (strcasecmp(nam, "macosx") == 0)      result = OS_DARWIN;
	else if (strcasecmp(nam, "darwin") == 0)      result = OS_DARWIN;
	else if (strcasecmp(nam, "sco_sv") == 0)      result = OS_SCO_SV;
	else if (strcasecmp(nam, "unixware") == 0)    result = OS_SCO_SV;
	else if (strcasecmp(nam, "netware_snmp") == 0) result = OS_NETWARE_SNMP;
	else if (strcasecmp(nam, "zvm") == 0)         result = OS_ZVM;
	else if (strcasecmp(nam, "zvse") == 0)        result = OS_ZVSE;
	else if (strcasecmp(nam, "zos") == 0)         result = OS_ZOS;
	else if (strcasecmp(nam, "snmpcollect") == 0) result = OS_SNMPCOLLECT;
	else if (strcasecmp(nam, "mqcollect") == 0)    result = OS_MQCOLLECT;
	else if (strcasecmp(nam, "gnu/kfreebsd") == 0) result = OS_GNUKFREEBSD;

	if (result == OS_UNKNOWN) dbgprintf("Unknown OS: '%s'\n", osname);

	xfree(nam);
	return result;
}

char *osname(enum ostype_t os)
{
	switch (os) {
		case OS_SOLARIS: return "solaris";
		case OS_HPUX: return "hpux";
		case OS_AIX: return "aix";
		case OS_OSF: return "osf";
		case OS_WIN32: return "win32";
		case OS_WIN32_HMDC: return "hmdc";
		case OS_WIN32_BBWIN: return "bbwin";
		case OS_WIN_POWERSHELL: return "powershell";
		case OS_FREEBSD: return "freebsd";
		case OS_NETBSD: return "netbsd";
		case OS_OPENBSD: return "openbsd";
		case OS_LINUX22: return "linux22";
		case OS_LINUX: return "linux";
		case OS_RHEL3: return "rhel3";
		case OS_SNMP: return "snmp";
		case OS_IRIX: return "irix";
		case OS_DARWIN: return "darwin";
	        case OS_SCO_SV: return "sco_sv";
	        case OS_NETWARE_SNMP: return "netware_snmp";
		case OS_ZVM: return "zvm";
		case OS_ZVSE: return "zvse";
		case OS_ZOS: return "zos";
		case OS_SNMPCOLLECT: return "snmpcollect";
		case OS_MQCOLLECT: return "mqcollect";
		case OS_GNUKFREEBSD: return "gnu/kfreebsd";
		case OS_UNKNOWN: return "unknown";
	}

	return "unknown";
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

char *commafy(char *hostname)
{
	static char *s = NULL;
	char *p;

	if (s == NULL) {
		s = strdup(hostname);
	}
	else if (strlen(hostname) > strlen(s)) {
		xfree(s);
		s = strdup(hostname);
	}
	else {
		strcpy(s, hostname);
	}

	for (p = strchr(s, '.'); (p); p = strchr(s, '.')) *p = ',';
	return s;
}

void uncommafy(char *hostname)
{
	char *p;

	p = hostname; while ((p = strchr(p, ',')) != NULL) *p = '.';
}



char *skipword(char *l)
{
	return l + strcspn(l, " \t");
}


char *skipwhitespace(char *l)
{
	return l + strspn(l, " \t");
}


int argnmatch(char *arg, char *match)
{
	return (strncmp(arg, match, strlen(match)) == 0);
}


char *msg_data(char *msg)
{
	/* Find the start position of the data following the "status host.test " message */
	char *result;

	if (!msg || (*msg == '\0')) return msg;

	result = strchr(msg, '.');              /* Hits the '.' in "host.test" */
	if (!result) {
		dbgprintf("Msg was not what I expected: '%s'\n", msg);
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
	else if (n == strlen(whereat)) {
		/* Last token */
		result = whereat;
		whereat += n;
	}
	else {
		/* Mid-string token - null-teminate the token */
		*(whereat + n) = '\0';
		result = whereat;

		/* Move past this token and the delimiter */
		whereat += (n+1);
	}

	return result;
}


char *wstok(char *s)
{
	/*
	 * This works like strtok(s, " \t"), but can handle quoted fields.
	 */

	static char *source = NULL;
	static char *whereat = NULL;
	int n;
	char *result;

	if ((source == NULL) && (s == NULL)) return NULL;
	if (s) source = whereat = s + strspn(s, " \t");		/* First call */

	if (*whereat == '\0') {
		/* End of string ... clear local state and return NULL */
		source = whereat = NULL;
		return NULL;
	}

	n = 0;
	do {
		n += strcspn(whereat+n, " \t\"");
		if (*(whereat+n) == '"') {
			char *p = strchr(whereat+n+1, '"');
			if (!p) n = strlen(whereat);
			else n = (p - whereat) + 1;
		}
	} while (*(whereat+n) && (*(whereat+n) != ' ') && (*(whereat+n) != '\t'));

	if (n == strlen(whereat)) {
		/* Last token */
		result = whereat;
		whereat += n;
	}
	else {
		/* Mid-string token - null-teminate the token */
		*(whereat + n) = '\0';
		result = whereat;

		/* Move past this token and the delimiter */
		whereat += (n+1);
		whereat += strspn(whereat, " \t");
	}

	/* Strip leading/trailing quote */
	{
		char *p;

		if (*result == '"') result++;
		p = result + strlen(result) - 1;
		if (*p == '"') *p = '\0';
	}

	return result;
}


void sanitize_input(strbuffer_t *l, int stripcomment, int unescape)
{
	int i;

	/*
	 * This routine sanitizes an input line, stripping off leading/trailing whitespace.
	 * If requested, it also strips comments.
	 * If requested, it also un-escapes \-escaped charactes.
	 */

	/* Kill comments */
	if (stripcomment || unescape) {
		char *p, *commentstart = NULL;
		char *noquotemarkers = (unescape ? "\"'#\\" : "\"'#");
		char *inquotemarkers = (unescape ? "\"'\\" : "\"'");
		int inquote = 0;

		p = STRBUF(l) + strcspn(STRBUF(l), noquotemarkers);
		while (*p && (commentstart == NULL)) {
			switch (*p) {
			  case '\\':
				if (inquote)
					p += 2+strcspn(p+2, inquotemarkers);
				else
					p += 2+strcspn(p+2, noquotemarkers);
				break;

			  case '"': 
			  case '\'':
				inquote = (1 - inquote);
				if (inquote)
					p += 1+strcspn(p+1, inquotemarkers);
				else
					p += 1+strcspn(p+1, noquotemarkers);
			  	break;

			  case '#':
				if (!inquote) commentstart = p;
				break;
			}
		}

		if (commentstart) strbufferchop(l, STRBUFLEN(l) - (commentstart - STRBUF(l)));
	}

	/* Kill a trailing CR/NL */
	i = strcspn(STRBUF(l), "\r\n");
	if (i != STRBUFLEN(l)) strbufferchop(l, STRBUFLEN(l)-i);

	/* Kill trailing whitespace */
	i = STRBUFLEN(l);
	while ((i > 0) && isspace((int)(*(STRBUF(l)+i-1)))) i--;
	if (i != STRBUFLEN(l)) strbufferchop(l, STRBUFLEN(l)-i);

	/* Kill leading whitespace */
	i = strspn(STRBUF(l), " \t");
	if (i > 0) {
		memmove(STRBUF(l), STRBUF(l)+i, STRBUFLEN(l)-i);
		strbufferchop(l, i);
	}

	if (unescape) {
		char *p;

		p = STRBUF(l) + strcspn(STRBUF(l), "\\");
		while (*p) {
			memmove(p, p+1, STRBUFLEN(l)-(p-STRBUF(l)));
			strbufferchop(l, 1);
			p = p + 1 + strcspn(p+1, "\\");
		}
	}
}


unsigned int IPtou32(int ip1, int ip2, int ip3, int ip4)
{
	return ((ip1 << 24) | (ip2 << 16) | (ip3 << 8) | (ip4));
}

char *u32toIP(unsigned int ip32)
{
	int ip1, ip2, ip3, ip4;
	static char *result = NULL;

	if (result == NULL) result = (char *)malloc(16);

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
	return (strcmp(xgetenv("FQDN"), "TRUE") == 0);
}

int generate_static(void)
{
	getenv_default("XYMONLOGSTATUS", "STATIC", NULL);
	return (strcmp(xgetenv("XYMONLOGSTATUS"), "STATIC") == 0);
}


void do_extensions(FILE *output, char *extenv, char *family)
{
	/*
	 * Extension scripts. These are ad-hoc, and implemented as a
	 * simple pipe. So we do a fork here ...
	 */

	char *exts, *p;
	FILE *inpipe;
	char extfn[PATH_MAX];
	strbuffer_t *inbuf;

	p = xgetenv(extenv);
	if (p == NULL) {
		/* No extension */
		return;
	}

	MEMDEFINE(extfn);

	exts = strdup(p);
	p = strtok(exts, "\t ");
	inbuf = newstrbuffer(0);

	while (p) {
		/* Dont redo the eventlog or acklog things */
		if ((strcmp(p, "eventlog.sh") != 0) &&
		    (strcmp(p, "acklog.sh") != 0)) {
			sprintf(extfn, "%s/ext/%s/%s", xgetenv("XYMONHOME"), family, p);
			inpipe = popen(extfn, "r");
			if (inpipe) {
				initfgets(inpipe);
				while (unlimfgets(inbuf, inpipe)) fputs(STRBUF(inbuf), output);
				pclose(inpipe);
				freestrbuffer(inbuf);
			}
		}
		p = strtok(NULL, "\t ");
	}

	xfree(exts);

	MEMUNDEFINE(extfn);
	MEMUNDEFINE(buf);
}

static void clean_cmdarg(char *l)
{
	/*
	 * This routine sanitizes command-line argument, stripping off whitespace,
	 * removing comments and un-escaping \-escapes and quotes.
	 */
	char *p, *outp;
	int inquote, inhyphen;

	/* Remove quotes, comments and leading whitespace */
	p = l + strspn(l, " \t"); outp = l; inquote = inhyphen = 0;
	while (*p) {
		if (*p == '\\') {
			*outp = *(p+1);
			outp++; p += 2;
		}
		else if (*p == '"') {
			inquote = (1 - inquote);
			p++;
		}
		else if (*p == '\'') {
			inhyphen = (1 - inhyphen);
			p++;
		}
		else if ((*p == '#') && !inquote && !inhyphen) {
			*p = '\0';
		}
		else {
			if (outp != p) *outp = *p;
			outp++; p++;
		}
	}

	/* Remove trailing whitespace */
	while ((outp > l) && (isspace((int) *(outp-1)))) outp--;
	*outp = '\0';
}

char **setup_commandargs(char *cmdline, char **cmd)
{
	/*
	 * Good grief - argument parsing is complex!
	 *
	 * This routine takes a command-line, picks out any environment settings
	 * that are in the command line, and splits up the remainder into the
	 * actual command to run, and the arguments.
	 *
	 * It handles quotes, hyphens and escapes.
	 */

	char **cmdargs;
	char *cmdcp, *barg, *earg, *eqchar, *envsetting;
	int argi, argsz;
	int argdone, inquote, inhyphen;
	char savech;

	argsz = 1; cmdargs = (char **) malloc((1+argsz)*sizeof(char *)); argi = 0;
	cmdcp = strdup(expand_env(cmdline));

	/* Kill a trailing CR/NL */
	barg = cmdcp + strcspn(cmdcp, "\r\n"); *barg = '\0';

	barg = cmdcp;
	do {
		earg = barg; argdone = 0; inquote = inhyphen = 0;
		while (*earg && !argdone) {
			if (!inquote && !inhyphen) {
				argdone = isspace((int)*earg);
			}

			if ((*earg == '"') && !inhyphen) inquote = (1 - inquote);
			if ((*earg == '\'') && !inquote) inhyphen = (1 - inhyphen);
			if (!argdone) earg++;
		}
		savech = *earg;
		*earg = '\0';

		clean_cmdarg(barg);
		eqchar = strchr(barg, '=');
		if (eqchar && (eqchar == (barg + strspn(barg, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")))) {
			/* It's an environment definition */
			dbgprintf("Setting environment: %s\n", barg);
			envsetting = strdup(barg);
			putenv(envsetting);
		}
		else {
			if (argi == argsz) {
				argsz++; cmdargs = (char **) realloc(cmdargs, (1+argsz)*sizeof(char *));
			}
			cmdargs[argi++] = strdup(barg);
		}

		*earg = savech;
		barg = earg + strspn(earg, " \t\n");
	} while (*barg);
	cmdargs[argi] = NULL;

	xfree(cmdcp);

	*cmd = cmdargs[0];
	return cmdargs;
}

long long str2ll(char *s, char **errptr)
{
#ifdef HAVE_STRTOLL
	return strtoll(s, errptr, 10);
#else
	long long result = 0;
	int negative = 0;
	char *inp;

	inp = s + strspn(s, " \t");
	if (*inp == '-') { negative = 1; inp++; }
	while (isdigit((int)*inp)) { 
		result = 10*result + (*inp - '0'); 
		inp++;
	}

	if (errptr && (*inp != '\0') && (!isspace((int)*inp))) *errptr = inp;

	if (negative) result = -result;

	return result;
#endif
}
int checkalert(char *alertlist, char *testname)
{
	char *alist, *aname;
	int result;

	if (!alertlist) return 0;

	alist = (char *) malloc(strlen(alertlist) + 3);
	sprintf(alist, ",%s,", alertlist);
	aname = (char *) malloc(strlen(testname) + 3);
	sprintf(aname, ",%s,", testname);

	result = (strstr(alist, aname) != NULL);

	xfree(aname); xfree(alist);
	return result;
}

char *nextcolumn(char *s)
{
	static char *ofs = NULL;
	char *result;

	if (s) ofs = s + strspn(s, " \t");
	if (!s && !ofs) return NULL;

	result = ofs;
	ofs += strcspn(ofs, " \t");
	if (*ofs) { *ofs = '\0'; ofs += 1 + strspn(ofs+1, " \t"); } else ofs = NULL;

	return result;
}

int selectcolumn(char *heading, char *wanted)
{
	char *hdr;
	int result = 0;

	hdr = nextcolumn(heading);
	while (hdr && strcasecmp(hdr, wanted)) {
		result++;
		hdr = nextcolumn(NULL);
	}

	if (hdr) return result; else return -1;
}

char *getcolumn(char *s, int wanted)
{
	char *result;
	int i;

	for (i=0, result=nextcolumn(s); (i < wanted); i++, result = nextcolumn(NULL));

	return result;
}


int chkfreespace(char *path, int minblks, int mininodes)
{
	/* Check there is least 'minblks' % free space on filesystem 'path' */
	struct statvfs fs;
	int n;
	int avlblk, avlnod;

	n = statvfs(path, &fs);
	if (n == -1) {
		errprintf("Cannot stat filesystem %s: %s", path, strerror(errno));
		return 0;
	}

	/* Not all filesystems report i-node data, so play it safe */
	avlblk = ((fs.f_bavail > 0) && (fs.f_blocks > 0)) ? fs.f_bavail / (fs.f_blocks / 100) : 100;
	avlnod = ((fs.f_favail > 0) && (fs.f_files > 0))   ? fs.f_favail / (fs.f_files / 100)  : 100;

	if ((avlblk >= minblks) && (avlnod >= mininodes)) return 0;

	return 1;
}

