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

typedef struct netdialogalias_t {
	char *name;
	netdialog_t *rec;
} netdialogalias_t;

static void *netdialogs = NULL;
static void *netdialogaliases = NULL;
char *silentdialog[] = { "CLOSE", NULL };

void load_protocols(char *fn)
{
	static void *cfgfilelist = NULL;
	char *configfn;
	FILE *fd;
	strbuffer_t *l;
	netdialog_t *rec = NULL;
	int dialogsz = 0;
	xtreePos_t handle;

	if (!fn) {
		configfn = (char *)malloc(strlen(xgetenv("XYMONHOME")) + strlen("/etc/protocols2.cfg") + 1);
		sprintf(configfn, "%s/etc/protocols2.cfg", xgetenv("XYMONHOME"));
	}
	else
		configfn = strdup(fn);

	if (cfgfilelist && !stackfmodified(cfgfilelist)) {
		dbgprintf("protocols2.cfg unchanged, skipping reload\n");
		xfree(configfn);
		return;
	}

	fd = stackfopen(configfn, "r", &cfgfilelist);
	if (!fd) {
		errprintf("Cannot open protocols2.cfg file %s\n", configfn);
		xfree(configfn);
		return;
	}

	xfree(configfn);

	/* Wipe out the current configuration */
	if (netdialogs) {
		handle = xtreeFirst(netdialogs);
		while (handle != xtreeEnd(netdialogs)) {
			int i;
			netdialog_t *rec = xtreeData(netdialogs, handle);
			handle = xtreeNext(netdialogs, handle);

			if (rec->name) xfree(rec->name);
			if (rec->dialog && (rec->dialog != silentdialog)) {
				for (i=0; (rec->dialog[i]); i++) xfree(rec->dialog[i]);
				xfree(rec->dialog);
			}
			xfree(rec);
		}
		xtreeDestroy(netdialogs);
		netdialogs = NULL;
	}

	if (netdialogaliases) {
		handle = xtreeFirst(netdialogaliases);
		while (handle != xtreeEnd(netdialogaliases)) {
			netdialogalias_t *rec = xtreeData(netdialogaliases, handle);
			handle = xtreeNext(netdialogaliases, handle);

			if (rec->name) xfree(rec->name);
			xfree(rec);
		}
		xtreeDestroy(netdialogaliases);
		netdialogaliases = NULL;
	}

	netdialogs = xtreeNew(strcmp);
	netdialogaliases = xtreeNew(strcmp);
	l = newstrbuffer(0);
	while (stackfgets(l, NULL)) {
		char *p;

		sanitize_input(l, 1, 0);
		if (STRBUFLEN(l) == 0) continue;

		if (*STRBUF(l) == '[') {
			char *nam, *sptr;

			rec = (netdialog_t *)calloc(1, sizeof(netdialog_t));
			nam = strtok_r(STRBUF(l)+1, "|]", &sptr);
			rec->name = strdup(nam);
			xtreeAdd(netdialogs, rec->name, rec);

			nam = strtok_r(NULL, "|]", &sptr);
			while (nam) {
				netdialogalias_t *arec = (netdialogalias_t *)calloc(1, sizeof(netdialogalias_t));

				arec->name = strdup(nam);
				arec->rec = rec;
				xtreeAdd(netdialogaliases, arec->name, arec);

				nam = strtok_r(NULL, "|]", &sptr);
			}

			rec->portnumber = conn_lookup_portnumber(rec->name, 0);
			dialogsz = 0;
		}
		else if (strncasecmp(STRBUF(l), "port ", 5) == 0) {
			rec->portnumber = atoi(STRBUF(l)+5);
		}
		else if (strncasecmp(STRBUF(l), "options ", 8) == 0) {
			char *tok, *savptr;

			tok = strtok_r(STRBUF(l), " \t", &savptr);
			tok = strtok_r(NULL, ",", &savptr);
			while (tok) {
				if (strcasecmp(tok, "telnet") == 0) rec->option_telnet = 1;
				if (strcasecmp(tok, "ntp") == 0) rec->option_ntp = 1;
				if (strcasecmp(tok, "dns") == 0) rec->option_dns = 1;
				if (strcasecmp(tok, "ssl") == 0) rec->option_ssl = 1;
				if (strcasecmp(tok, "starttls") == 0) rec->option_starttls = 1;
				if (strcasecmp(tok, "udp") == 0) rec->option_udp = 1;
				if (strcasecmp(tok, "external") == 0) rec->option_external = 1;
				tok = strtok_r(NULL, ",", &savptr);
			}
		}
		else if ( (strncasecmp(STRBUF(l), "send:", 5) == 0) ||
			  (strncasecmp(STRBUF(l), "expect:", 7) == 0) ||
			  (strncasecmp(STRBUF(l), "read", 4) == 0) ||
			  (strncasecmp(STRBUF(l), "close", 5) == 0) ||
			  (strncasecmp(STRBUF(l), "starttls", 8) == 0) ) {
			dialogsz++;
			rec->dialog = (char **)realloc(rec->dialog, (dialogsz+1)*sizeof(char *));
			getescapestring(STRBUF(l), (unsigned char **)&(rec->dialog[dialogsz-1]), NULL);
			rec->dialog[dialogsz] = NULL;

			if (strncasecmp(STRBUF(l), "starttls", 8) == 0) rec->option_starttls = 1;
		}
	}

	/* Make sure all protocols have a dialog - minimum the silent dialog */
	for (handle = xtreeFirst(netdialogs); (handle != xtreeEnd(netdialogs)); handle = xtreeNext(netdialogs, handle)) {
		netdialog_t *rec = xtreeData(netdialogs, handle);
		if (rec->dialog == NULL) rec->dialog = silentdialog;
	}

	freestrbuffer(l);
	stackfclose(fd);
}

char *init_net_services(void)
{
	xtreePos_t handle;
	strbuffer_t *resbuf = newstrbuffer(0);
	char *result;

	load_protocols(NULL);

	for (handle = xtreeFirst(netdialogs); (handle != xtreeEnd(netdialogs)); handle = xtreeNext(netdialogs, handle)) {
		netdialog_t *rec = xtreeData(netdialogs, handle);
		if (STRBUFLEN(resbuf) > 0) addtobuffer(resbuf, " ");
		addtobuffer(resbuf, rec->name);
	}

	for (handle = xtreeFirst(netdialogaliases); (handle != xtreeEnd(netdialogaliases)); handle = xtreeNext(netdialogaliases, handle)) {
		netdialogalias_t *arec = xtreeData(netdialogaliases, handle);

		if (STRBUFLEN(resbuf) > 0) addtobuffer(resbuf, " ");
		addtobuffer(resbuf, arec->rec->name);
	}

	result = grabstrbuffer(resbuf);
	return result;
}

netdialog_t *find_net_service(char *servicename)
{
	netdialog_t *rec = NULL;
	xtreePos_t handle;

	handle = xtreeFind(netdialogs, servicename);
	if (handle != xtreeEnd(netdialogs)) {
		rec = xtreeData(netdialogs, handle);
	}
	else {
		handle = xtreeFind(netdialogaliases, servicename);
		if (handle != xtreeEnd(netdialogaliases)) {
			netdialogalias_t *arec = xtreeData(netdialogaliases, handle);
			rec = arec->rec;
		}
	}

	return rec;
}

