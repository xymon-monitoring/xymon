/*----------------------------------------------------------------------------*/
/* Hobbit                                                                     */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for parsing the bb-services file.                     */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: netservices.c,v 1.2 2005-02-21 16:36:59 henrik Exp $";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>

#include "libbbgen.h"

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


static char *binview(unsigned char *buf, int buflen)
{
	static char hexchars[16] = "0123456789ABCDEF";
	static char result[MAX_LINE_LEN];
	unsigned char *inp, *outp;
	int i;

	if (buf && (buflen == 0)) buflen = strlen(buf);
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
	static char *bbnetsvcs = NULL;

	char filename[PATH_MAX];
	FILE *fd = NULL;
	char buf[MAX_LINE_LEN];
	svclist_t *head = NULL;
	svclist_t *item = NULL;
	svclist_t *first = NULL;
	svclist_t *walk;
	char *searchstring;
	int svcnamebytes = 0;
	int svccount = 1;
	int i;

	if (bbnetsvcs) return bbnetsvcs;	/* Has already run, so just pickup the result */

	if (xgetenv("BBNETSVCS") == NULL) {
		putenv("BBNETSVCS=smtp telnet ftp pop pop3 pop-3 ssh imap ssh1 ssh2 imap2 imap3 imap4 pop2 pop-2 nntp");
	}

	filename[0] = '\0';
	if (xgetenv("BBHOME")) {
		sprintf(filename, "%s/etc/", xgetenv("BBHOME"));
	}
	strcat(filename, "bb-services");

	fd = fopen(filename, "r");
	if (fd == NULL) {
		errprintf("Cannot open TCP service-definitions file %s - using defaults\n", filename);
		return strdup(xgetenv("BBNETSVCS"));
	}

	head = (svclist_t *)malloc(sizeof(svclist_t));
	head->rec = (svcinfo_t *)calloc(1, sizeof(svcinfo_t));
	head->next = NULL;

	while (fd && fgets(buf, sizeof(buf), fd)) {
		char *l, *eol;

		l = strchr(buf, '\n'); if (l) *l = '\0';
		l = skipwhitespace(buf);

		if (*l == '[') {
			char *svcname;

			eol = strchr(l, ']'); if (eol) *eol = '\0';
			l = skipwhitespace(l+1);
			svcname = strtok(l, "|");
			first = NULL;
			while (svcname) {
				item = (svclist_t *) malloc(sizeof(svclist_t));
				item->rec = (svcinfo_t *)calloc(1, sizeof(svcinfo_t));
				item->rec->svcname = strdup(svcname);
				svcnamebytes += (strlen(svcname) + 1);
				item->next = head;
				head = item;
				svcname = strtok(NULL, "|");
				if (first == NULL) first = item;
				svccount++;
			}
		}
		else if (strncmp(l, "send ", 5) == 0) {
			if (item) {
				getescapestring(skipwhitespace(l+4), &item->rec->sendtxt, &item->rec->sendlen);
				for (walk = item; (walk != first); walk = walk->next) {
					walk->next->rec->sendtxt = item->rec->sendtxt;
					walk->next->rec->sendlen = item->rec->sendlen;
				}
			}
		}
		else if (strncmp(l, "expect ", 7) == 0) {
			if (item) {
				getescapestring(skipwhitespace(l+7), &item->rec->exptext, &item->rec->explen);
				for (walk = item; (walk != first); walk = walk->next) {
					walk->next->rec->exptext = item->rec->exptext;
					walk->next->rec->explen = item->rec->explen;
					walk->next->rec->expofs = 0; /* HACK - not used right now */
				}
			}
		}
		else if (strncmp(l, "options ", 8) == 0) {
			if (item) {
				char *opt;

				item->rec->flags = 0;
				l = skipwhitespace(l+7);
				opt = strtok(l, ",");
				while (opt) {
					if      (strcmp(opt, "ssl") == 0)    item->rec->flags += TCP_SSL;
					else if (strcmp(opt, "banner") == 0) item->rec->flags += TCP_GET_BANNER;
					else if (strcmp(opt, "telnet") == 0) item->rec->flags += TCP_TELNET;
					else errprintf("Unknown option: %s\n", opt);

					opt = strtok(NULL, ",");
				}
				for (walk = item; (walk != first); walk = walk->next) {
					walk->next->rec->flags = item->rec->flags;
				}
			}
		}
		else if (strncmp(l, "port ", 5) == 0) {
			if (item) {
				item->rec->port = atoi(skipwhitespace(l+4));
				for (walk = item; (walk != first); walk = walk->next) {
					walk->next->rec->port = item->rec->port;
				}
			}
		}
	}

	if (fd) fclose(fd);

	svcinfo = (svcinfo_t *) malloc(svccount * sizeof(svcinfo_t));
	for (walk=head, i=0; (walk); walk = walk->next, i++) {
		svcinfo[i].svcname = walk->rec->svcname;
		svcinfo[i].sendtxt = walk->rec->sendtxt;
		svcinfo[i].sendlen = walk->rec->sendlen;
		svcinfo[i].exptext = walk->rec->exptext;
		svcinfo[i].explen  = walk->rec->explen;
		svcinfo[i].expofs  = walk->rec->expofs;
		svcinfo[i].flags   = walk->rec->flags;
		svcinfo[i].port    = walk->rec->port;
	}

	searchstring = strdup(xgetenv("BBNETSVCS"));
	bbnetsvcs = (char *) malloc(strlen(xgetenv("BBNETSVCS")) + svcnamebytes + 1);
	strcpy(bbnetsvcs, xgetenv("BBNETSVCS"));
	for (i=0; (svcinfo[i].svcname); i++) {
		char *p;

		strcpy(searchstring, xgetenv("BBNETSVCS"));
		p = strtok(searchstring, " ");
		while (p && (strcmp(p, svcinfo[i].svcname) != 0)) p = strtok(NULL, " ");

		if (p == NULL) {
			strcat(bbnetsvcs, " ");
			strcat(bbnetsvcs, svcinfo[i].svcname);
		}
	}
	xfree(searchstring);

	if (debug) {
		dump_tcp_services();
		printf("BBNETSVCS set to : %s\n", bbnetsvcs);
	}

	return bbnetsvcs;
}

void dump_tcp_services(void)
{
	int i;

	printf("Service list dump\n");
	for (i=0; (svcinfo[i].svcname); i++) {
		printf(" Name      : %s\n", svcinfo[i].svcname);
		printf("   Sendtext: %s\n", binview(svcinfo[i].sendtxt, svcinfo[i].sendlen));
		printf("   Sendlen : %d\n", svcinfo[i].sendlen);
		printf("   Exp.text: %s\n", binview(svcinfo[i].exptext, svcinfo[i].explen));
		printf("   Exp.len : %d\n", svcinfo[i].explen);
		printf("   Exp.ofs : %d\n", svcinfo[i].expofs);
		printf("   Flags   : %d\n", svcinfo[i].flags);
		printf("   Port    : %d\n", svcinfo[i].port);
	}
	printf("\n");
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

