/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for parsing the protocols.cfg file.                   */
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
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libxymon.h"

/*
 * Services we know how to handle:
 * This defines what to send to them to shut down the 
 * session nicely, and whether we want to grab the
 * banner or not.
 */
static svcinfo_t default_svcinfo[] = {
	/*           ------------- data to send ------   ---- green data ------ flags */
	/* name      databytes            length          databytes offset len        */
	{ "ftp",     "quit\r\n",          0,                  "220",	0, 0,	(TCP_GET_BANNER), 21 },
	{ "ssh",     NULL,                0,                  "SSH",	0, 0,	(TCP_GET_BANNER), 22 },
	{ "ssh1",    NULL,                0,                  "SSH",	0, 0,	(TCP_GET_BANNER), 22 },
	{ "ssh2",    NULL,                0,                  "SSH",	0, 0,	(TCP_GET_BANNER), 22 },
	{ "telnet",  NULL,                0,                  NULL,	0, 0,	(TCP_GET_BANNER|TCP_TELNET), 23 },
	{ "smtp",    "mail\r\nquit\r\n",  0,                  "220",	0, 0,	(TCP_GET_BANNER), 25 }, /* Send "MAIL" to avoid sendmail NOQUEUE logs */
	{ "pop",     "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 110 },
	{ "pop2",    "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 109 },
	{ "pop-2",   "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 109 },
	{ "pop3",    "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 110 },
	{ "pop-3",   "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 110 },
	{ "imap",    "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 143 },
	{ "imap2",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 143 },
	{ "imap3",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 220 },
	{ "imap4",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 143 },
	{ "nntp",    "quit\r\n",          0,                  "200",	0, 0,	(TCP_GET_BANNER), 119 },
	{ "ldap",    NULL,                0,                  NULL,     0, 0,	(0), 389 },
	{ "rsync",   NULL,                0,                  "@RSYNCD",0, 0,	(TCP_GET_BANNER), 873 },
	{ "bbd",     "dummy",             0,                  NULL,	0, 0,	(0), 1984 },
	{ "ftps",    "quit\r\n",          0,                  "220",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 990 },
	{ "telnets", NULL,                0,                  NULL, 	0, 0,	(TCP_GET_BANNER|TCP_TELNET|TCP_SSL), 992 },
	{ "smtps",   "mail\r\nquit\r\n",  0,                  "220",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 0 }, /* Non-standard - IANA */
	{ "pop3s",   "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 995 },
	{ "imaps",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 993 },
	{ "nntps",   "quit\r\n",          0,                  "200",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 563 },
	{ "ldaps",   NULL,                0,                  NULL,     0, 0,	(TCP_SSL), 636 },
	{ "clamd",   "PING\r\n",          0,                  "PONG",   0, 0,	(0), 3310 },
	{ "vnc",     "RFB 000.000\r\n",   0,                  "RFB ",   0, 0,   (TCP_GET_BANNER), 5900 },
	{ NULL,      NULL,                0,                  NULL,	0, 0,	(0), 0 }	/* Default behaviour: Just try a connect */
};

static svcinfo_t *svcinfo = default_svcinfo;

typedef struct svclist_t {
	struct svcinfo_t *rec;
	struct svclist_t *next;
} svclist_t;


static char *binview(unsigned char *buf, int buflen)
{
	/* Encode a string with possibly binary data into an ascii-printable form */

	static char hexchars[16] = "0123456789ABCDEF";
	static char *result = NULL;
	unsigned char *inp, *outp;
	int i;

	if (result) xfree(result);
	if (!buf) { result = strdup("[null]"); return result; }

	if (buf && (buflen == 0)) buflen = strlen(buf);
	result = (char *)malloc(4*buflen + 1);	/* Worst case: All binary */

	for (inp=buf, i=0, outp=result; (i<buflen); i++,inp++) {
		if (isprint(*inp)) {
			*outp = *inp;
			outp++;
		}
		else if (*inp == '\r') {
			*outp = '\\'; outp++;
			*outp = 'r'; outp++;
		}
		else if (*inp == '\n') {
			*outp = '\\'; outp++;
			*outp = 'n'; outp++;
		}
		else if (*inp == '\t') {
			*outp = '\\'; outp++;
			*outp = 't'; outp++;
		}
		else {
			*outp = '\\'; outp++;
			*outp = 'x'; outp++;
			*outp = hexchars[*inp / 16]; outp++;
			*outp = hexchars[*inp % 16]; outp++;
		}
	}
	*outp = '\0';

	return result;
}

char *init_tcp_services(void)
{
	static char *xymonnetsvcs = NULL;
	static void *svcflist = NULL;

	char filename[PATH_MAX];
	struct stat st;
	FILE *fd = NULL;
	strbuffer_t *inbuf;
	svclist_t *head, *tail, *first, *walk;
	char *searchstring;
	int svcnamebytes = 0;
	int svccount = 0;
	int i;

	MEMDEFINE(filename);

	filename[0] = '\0';
	if (xgetenv("XYMONHOME")) {
		sprintf(filename, "%s/etc/", xgetenv("XYMONHOME"));
	}
	strcat(filename, "protocols.cfg");

	if ((stat(filename, &st) == 0) && xymonnetsvcs) {
		/* See if we have already run and the file is unchanged - if so just pickup the result */
		if (svcflist && !stackfmodified(svcflist)) {
			return xymonnetsvcs;
		}

		/* File has changed - reload configuration. But clean up first so we don't leak memory. */
		if (svcinfo != default_svcinfo) {
			for (i=0; (svcinfo[i].svcname); i++) {
				if (svcinfo[i].svcname) xfree(svcinfo[i].svcname);
				if (svcinfo[i].sendtxt) xfree(svcinfo[i].sendtxt);
				if (svcinfo[i].exptext) xfree(svcinfo[i].exptext);
			}
			xfree(svcinfo);
			svcinfo = default_svcinfo;
		}

		xfree(xymonnetsvcs); xymonnetsvcs = NULL;
	}

	if (xgetenv("XYMONNETSVCS") == NULL) {
		putenv("XYMONNETSVCS=smtp telnet ftp pop pop3 pop-3 ssh imap ssh1 ssh2 imap2 imap3 imap4 pop2 pop-2 nntp");
	}

	fd = stackfopen(filename, "r", &svcflist);
	if (fd == NULL) {
		errprintf("Cannot open TCP service-definitions file %s - using defaults\n", filename);
		xymonnetsvcs = strdup(xgetenv("XYMONNETSVCS"));

		MEMUNDEFINE(filename);
		return xymonnetsvcs;
	}

	head = tail = first = NULL;

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		char *l, *eol;

		sanitize_input(inbuf, 1, 0);
		l = STRBUF(inbuf);

		if (*l == '[') {
			char *svcname;

			eol = strchr(l, ']'); if (eol) *eol = '\0';
			l = skipwhitespace(l+1);

			svcname = strtok(l, "|"); first = NULL;
			while (svcname) {
				svclist_t *newitem;

				svccount++;
				svcnamebytes += (strlen(svcname) + 1);

				newitem = (svclist_t *) malloc(sizeof(svclist_t));
				newitem->rec = (svcinfo_t *)calloc(1, sizeof(svcinfo_t));
				newitem->rec->svcname = strdup(svcname);
				newitem->next = NULL;

				if (first == NULL) first = newitem;

				if (head == NULL) {
					head = tail = newitem;
				}
				else {
					tail->next = newitem;
					tail = newitem;
				}

				svcname = strtok(NULL, "|");
			}
		}
		else if (strncmp(l, "send ", 5) == 0) {
			if (first) {
				getescapestring(skipwhitespace(l+4), &first->rec->sendtxt, &first->rec->sendlen);
				for (walk = first->next; (walk); walk = walk->next) {
					walk->rec->sendtxt = strdup(first->rec->sendtxt);
					walk->rec->sendlen = first->rec->sendlen;
				}
			}
		}
		else if (strncmp(l, "expect ", 7) == 0) {
			if (first) {
				getescapestring(skipwhitespace(l+6), &first->rec->exptext, &first->rec->explen);
				for (walk = first->next; (walk); walk = walk->next) {
					walk->rec->exptext = strdup(first->rec->exptext);
					walk->rec->explen = first->rec->explen;
					walk->rec->expofs = 0; /* HACK - not used right now */
				}
			}
		}
		else if (strncmp(l, "options ", 8) == 0) {
			if (first) {
				char *opt;

				first->rec->flags = 0;
				l = skipwhitespace(l+7);
				opt = strtok(l, ",");
				while (opt) {
					if      (strcmp(opt, "ssl") == 0)    first->rec->flags |= TCP_SSL;
					else if (strcmp(opt, "banner") == 0) first->rec->flags |= TCP_GET_BANNER;
					else if (strcmp(opt, "telnet") == 0) first->rec->flags |= TCP_TELNET;
					else errprintf("Unknown option: %s\n", opt);

					opt = strtok(NULL, ",");
				}
				for (walk = first->next; (walk); walk = walk->next) {
					walk->rec->flags = first->rec->flags;
				}
			}
		}
		else if (strncmp(l, "port ", 5) == 0) {
			if (first) {
				first->rec->port = atoi(skipwhitespace(l+4));
				for (walk = first->next; (walk); walk = walk->next) {
					walk->rec->port = first->rec->port;
				}
			}
		}
	}

	if (fd) stackfclose(fd);
	freestrbuffer(inbuf);

	/* Copy from the svclist to svcinfo table */
	svcinfo = (svcinfo_t *) malloc((svccount+1) * sizeof(svcinfo_t));
	for (walk=head, i=0; (walk && (i < svccount)); walk = walk->next, i++) {
		svcinfo[i].svcname = walk->rec->svcname;
		svcinfo[i].sendtxt = walk->rec->sendtxt;
		svcinfo[i].sendlen = walk->rec->sendlen;
		svcinfo[i].exptext = walk->rec->exptext;
		svcinfo[i].explen  = walk->rec->explen;
		svcinfo[i].expofs  = walk->rec->expofs;
		svcinfo[i].flags   = walk->rec->flags;
		svcinfo[i].port    = walk->rec->port;
	}
	memset(&svcinfo[svccount], 0, sizeof(svcinfo_t));

	/* This should not happen */
	if (walk) {
		errprintf("Whoa - didnt copy all services! svccount=%d, next service '%s'\n", 
			svccount, walk->rec->svcname);
	}

	/* Free the temp. svclist list */
	while (head) {
		/*
		 * Note: Don't free the strings inside the records, 
		 * as they are now owned by the svcinfo records.
		 */
		walk = head;
		head = head->next;
		xfree(walk);
	}

	searchstring = strdup(xgetenv("XYMONNETSVCS"));
	xymonnetsvcs = (char *) malloc(strlen(xgetenv("XYMONNETSVCS")) + svcnamebytes + 1);
	strcpy(xymonnetsvcs, xgetenv("XYMONNETSVCS"));
	for (i=0; (svcinfo[i].svcname); i++) {
		char *p;

		strcpy(searchstring, xgetenv("XYMONNETSVCS"));
		p = strtok(searchstring, " ");
		while (p && (strcmp(p, svcinfo[i].svcname) != 0)) p = strtok(NULL, " ");

		if (p == NULL) {
			strcat(xymonnetsvcs, " ");
			strcat(xymonnetsvcs, svcinfo[i].svcname);
		}
	}
	xfree(searchstring);

	if (debug) {
		dump_tcp_services();
		dbgprintf("XYMONNETSVCS set to : %s\n", xymonnetsvcs);
	}

	MEMUNDEFINE(filename);
	return xymonnetsvcs;
}

void dump_tcp_services(void)
{
	int i;

	dbgprintf("Service list dump\n");
	for (i=0; (svcinfo[i].svcname); i++) {
		dbgprintf(" Name      : %s\n", svcinfo[i].svcname);
		dbgprintf("   Sendtext: %s\n", binview(svcinfo[i].sendtxt, svcinfo[i].sendlen));
		dbgprintf("   Sendlen : %d\n", svcinfo[i].sendlen);
		dbgprintf("   Exp.text: %s\n", binview(svcinfo[i].exptext, svcinfo[i].explen));
		dbgprintf("   Exp.len : %d\n", svcinfo[i].explen);
		dbgprintf("   Exp.ofs : %d\n", svcinfo[i].expofs);
		dbgprintf("   Flags   : %d\n", svcinfo[i].flags);
		dbgprintf("   Port    : %d\n", svcinfo[i].port);
	}
	dbgprintf("\n");
}

int default_tcp_port(char *svcname)
{
	int svcidx;
	int result = 0;

	for (svcidx=0; (svcinfo[svcidx].svcname && (strcmp(svcname, svcinfo[svcidx].svcname) != 0)); svcidx++) ;
	if (svcinfo[svcidx].svcname) result = svcinfo[svcidx].port;

	return result;
}

svcinfo_t *find_tcp_service(char *svcname)
{
	int svcidx;

	for (svcidx=0; (svcinfo[svcidx].svcname && (strcmp(svcname, svcinfo[svcidx].svcname) != 0)); svcidx++) ;
	if (svcinfo[svcidx].svcname) 
		return &svcinfo[svcidx];
	else
		return NULL;
}

