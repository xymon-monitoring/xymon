/*----------------------------------------------------------------------------*/
/* Hobbit monitor BEA statistics tool.                                        */
/*                                                                            */
/* This is used to collect statistics from a BEA Weblogic server              */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <pcre.h>

#include "libbbgen.h"
#include "version.h"

typedef struct bea_idx_t {
        char *idx;
        struct bea_idx_t *next;
} bea_idx_t;

static bea_idx_t *bea_idxhead = NULL;
static char msgline[MAX_LINE_LEN];
static int statuscolor = COL_GREEN;

/* Set with environment or commandline options */
static char *location = "";		/* BBLOCATION value */
static int testuntagged = 0;
static int default_port = 161;
static char *default_community = "public";
static int extcmdtimeout = 30;

static void find_idxes(char *buf, char *searchstr)
{
        bea_idx_t *idxwalk;
        char *bol, *eoln, *idxval;

        /* If we've done it before, clear out the old indexes */
        while (bea_idxhead) {
                idxwalk = bea_idxhead;
                bea_idxhead = bea_idxhead->next;
                xfree(idxwalk->idx);
                xfree(idxwalk);
        }
        bea_idxhead = NULL;

        bol = buf;
        while ((bol = strstr(bol, searchstr)) != NULL) {
                idxval = NULL;
                bol++;
                eoln = strchr(bol, '\n');
                if (eoln) *eoln = '\0';
                bol = strchr(bol, '=');
                if (bol) bol = strchr(bol, '\"');
                if (bol) idxval = bol+1;
                if (bol) bol = strchr(bol+1, '\"');
                if (bol) {
                        *bol = '\0';
                        idxwalk = (bea_idx_t *)malloc(sizeof(bea_idx_t));
                        idxwalk->idx = strdup(idxval);
                        idxwalk->next = bea_idxhead;
                        bea_idxhead = idxwalk;
                        *bol = '\"';
                }
                if (eoln) *eoln = '\n';
        }
}

int wanted_host(void *host, char *netstring)
{
	char *netlocation = bbh_item(host, BBH_NET);

	return ((strlen(netstring) == 0) ||                                /* No BBLOCATION = do all */
		(netlocation && (strcmp(netlocation, netstring) == 0)) ||  /* BBLOCATION && matching NET: tag */
		(testuntagged && (netlocation == NULL)));                  /* No NET: tag for this host */
}

char *getstring(char *databuf, char *beaindex, char *key)
{
	static char result[4096];
	char keystr[4096];
	char *p, *eol;

	*result = '\0';

	sprintf(keystr, "\nBEA-WEBLOGIC-MIB::%s.\"%s\"", key, beaindex);
	p = strstr(databuf, keystr);
	if (!p) {
		/* Might be at the very beginning of the buffer (with no \n first) */
		if (strncmp(databuf, keystr+1, strlen(keystr)-1) == 0) p = databuf;
	}
	else {
		p++;
	}

	if (p) {
		eol = strchr(p, '\n');
		if (eol) *eol = '\0';
		strcpy(result, p);
		if (eol) *eol = '\n';
	}

	return result;
}

char *jrockitems[] = {
	"jrockitRuntimeIndex",
	"jrockitRuntimeParent",
	"jrockitRuntimeFreeHeap",
	"jrockitRuntimeUsedHeap",
	"jrockitRuntimeTotalHeap",
	"jrockitRuntimeFreePhysicalMemory",
	"jrockitRuntimeUsedPhysicalMemory",
	"jrockitRuntimeTotalPhysicalMemory",
	"jrockitRuntimeTotalNumberOfThreads",
	"jrockitRuntimeNumberOfDaemonThreads",
	"jrockitRuntimeTotalNurserySize",
	NULL
};

char *qitems[] = {
	"executeQueueRuntimeIndex",
	"executeQueueRuntimeName",
	"executeQueueRuntimeParent",
	"executeQueueRuntimeExecuteThreadCurrentIdleCount",
	"executeQueueRuntimePendingRequestCurrentCount",
	"executeQueueRuntimeServicedRequestTotalCount",
	NULL
};

void send_data(void *host, char *beadomain, char *databuf, char **items)
{
	bea_idx_t *idxwalk;
	strbuffer_t *msgbuf;
	char *p;
	int i;

	msgbuf = newstrbuffer(0);

        for (idxwalk = bea_idxhead; (idxwalk); idxwalk = idxwalk->next) {
		sprintf(msgline, "data %s.bea\n\n", commafy(bbh_item(host, BBH_HOSTNAME)));
		addtobuffer(msgbuf, msgline);

		if (beadomain && *beadomain) {
			sprintf(msgline, "DOMAIN:%s\n", beadomain);
			addtobuffer(msgbuf, msgline);
		}

		for (i=0; (items[i]); i++) {
			p = getstring(databuf, idxwalk->idx, items[i]);
			sprintf(msgline, "%s\n", p);
			addtobuffer(msgbuf, msgline);
		}

		sendmessage(STRBUF(msgbuf), NULL, BBTALK_TIMEOUT, NULL);
		clearstrbuffer(msgbuf);
	}

	freestrbuffer(msgbuf);
}

int main(int argc, char *argv[])
{
        void *hwalk;
	int argi;
	strbuffer_t *statusmsg, *jrockout, *qout;

	for (argi = 1; (argi < argc); argi++) {
		if ((strcmp(argv[argi], "--help") == 0)) {
			printf("beastat version %s\n\n", VERSION);
			printf("Usage:\n%s [--debug] [--no-update] [--port=SNMPPORT] [--community=SNMPCOMMUNITY]\n", 
				argv[0]);
			exit(0);
		}
		else if ((strcmp(argv[argi], "--version") == 0)) {
			printf("beastat version %s\n", VERSION);
			exit(0);
		}
		else if ((strcmp(argv[argi], "--debug") == 0)) {
			debug = 1;
		}
		else if ((strcmp(argv[argi], "--no-update") == 0)) {
			dontsendmessages = 1;
		}
		else if (argnmatch(argv[argi], "--timeout=")) {
			char *p = strchr(argv[argi], '=');
			extcmdtimeout = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--port=")) {
			char *p = strchr(argv[argi], '=');
			default_port = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--community=")) {
			char *p = strchr(argv[argi], '=');
			default_community = strdup(p+1);
		}
	}

        load_hostnames(xgetenv("BBHOSTS"), "netinclude", get_fqdn());

        if (xgetenv("BBLOCATION")) location = strdup(xgetenv("BBLOCATION"));

	init_timestamp();
	combo_start();
	statusmsg = newstrbuffer(0);
	jrockout = newstrbuffer(0);
	qout = newstrbuffer(0);

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		char *tspec = bbh_custom_item(hwalk, "bea=");
		char *snmpcommunity = default_community;
		char *beadomain = "";
		int snmpport = default_port;
		char *p;
		char pipecmd[4096];
		int jrockres, qres;

		clearstrbuffer(statusmsg);
		clearstrbuffer(jrockout);
		clearstrbuffer(qout);

		/* Check if we have a "bea" test for this host, and it is a host we want to test */
                if (!tspec || !wanted_host(hwalk, location)) continue;

		/* Parse the testspec: bea=[SNMPCOMMUNITY@]BEADOMAIN[:SNMPPORT] */
		tspec = strdup(tspec+strlen("bea="));

		p = strchr(tspec, ':');
		if (p) {
			*p = '\0';
			snmpport = atoi(p+1);
		}

		p = strchr(tspec, '@');
		if (p) {
			*p = '\0';
			snmpcommunity = strdup(tspec);
			beadomain = strdup(p+1);
		}
		else {
			beadomain = strdup(tspec);
		}

		/* Prepare for the host status */
		statuscolor = COL_GREEN;

		/* Setup the snmpwalk pipe-command for jrockit stats */
		sprintf(pipecmd, "snmpwalk -m BEA-WEBLOGIC-MIB -c %s@%s -v 1 %s:%d enterprises.140.625.302.1",
			snmpcommunity, beadomain, bbh_item(hwalk, BBH_IP), snmpport);
		jrockres = run_command(pipecmd, NULL, jrockout, 0, extcmdtimeout);
		if (jrockres == 0) {
			find_idxes(STRBUF(jrockout), "BEA-WEBLOGIC-MIB::jrockitRuntimeIndex.");
			send_data(hwalk, beadomain, STRBUF(jrockout), jrockitems);
		}
		else {
			if (statuscolor < COL_YELLOW) statuscolor = COL_YELLOW;
			sprintf(msgline, "Could not retrieve BEA jRockit statistics from %s:%d domain %s (code %d)\n",
				bbh_item(hwalk, BBH_IP), snmpport, beadomain, jrockres);
			addtobuffer(statusmsg, msgline);
		}

		/* Setup the snmpwalk pipe-command for executeQueur stats */
		sprintf(pipecmd, "snmpwalk -m BEA-WEBLOGIC-MIB -c %s@%s -v 1 %s:%d enterprises.140.625.180.1",
			snmpcommunity, beadomain, bbh_item(hwalk, BBH_IP), snmpport);
		qres = run_command(pipecmd, NULL, qout, 0, extcmdtimeout);
		if (qres == 0) {
			find_idxes(STRBUF(qout), "BEA-WEBLOGIC-MIB::executeQueueRuntimeIndex.");
			send_data(hwalk, beadomain, STRBUF(qout), qitems);
		}
		else {
			if (statuscolor < COL_YELLOW) statuscolor = COL_YELLOW;
			sprintf(msgline, "Could not retrieve BEA executeQueue statistics from %s:%d domain %s (code %d)\n",
				bbh_item(hwalk, BBH_IP), snmpport, beadomain, qres);
			addtobuffer(statusmsg, msgline);
		}

		/* FUTURE: Have the statuscolor/statusmsg be updated to check against thresholds */
		/* Right now, the "bea" status is always green */
		init_status(statuscolor);
		sprintf(msgline, "status %s.%s %s %s\n\n", commafy(bbh_item(hwalk, BBH_HOSTNAME)), "bea", colorname(statuscolor), timestamp);
		addtostatus(msgline);
		if (STRBUFLEN(statusmsg) == 0) addtobuffer(statusmsg, "All BEA monitors OK\n");
		addtostrstatus(statusmsg);
		finish_status();
	}

	combo_end();
	freestrbuffer(statusmsg);
	freestrbuffer(jrockout);
	freestrbuffer(qout);

	return 0;
}

