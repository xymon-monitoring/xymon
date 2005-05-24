/*----------------------------------------------------------------------------*/
/* Hobbit monitor BEA statistics tool.                                        */
/*                                                                            */
/* This is used to collect statistics from a BEA Weblogic server              */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: beastat.c,v 1.1 2005-05-24 15:43:02 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>

#include "libbbgen.h"

#define DEFAULT_SNMP_PORT 1161
#define DEFAULT_SNMP_COMMUNITY "public"

char statusmsg[MAXMSG];
char msgline[MAXMSG];
int statuscolor = COL_GREEN;

typedef struct bea_idx_t {
        char *idx;
        struct bea_idx_t *next;
} bea_idx_t;

static bea_idx_t *bea_idxhead = NULL;

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

char *location = "";		/* BBLOCATION value */
int testuntagged = 0;

int wanted_host(namelist_t *host, char *netstring)
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

void send_data(namelist_t *host, char *beadomain, char *databuf, char **items)
{
	bea_idx_t *idxwalk;
	char *msgbuf = NULL;
	int msglen = 0;
	char *p;
	int i;

        for (idxwalk = bea_idxhead; (idxwalk); idxwalk = idxwalk->next) {
		sprintf(msgline, "data %s.bea\n\n", commafy(bbh_item(host, BBH_HOSTNAME)));
		addtobuffer(&msgbuf, &msglen, msgline);

		if (beadomain && *beadomain) {
			sprintf(msgline, "DOMAIN:%s\n", beadomain);
			addtobuffer(&msgbuf, &msglen, msgline);
		}

		for (i=0; (items[i]); i++) {
			p = getstring(databuf, idxwalk->idx, items[i]);
			sprintf(msgline, "%s\n", p);
			addtobuffer(&msgbuf, &msglen, msgline);
		}

		sendmessage(msgbuf, NULL, NULL, NULL, 0, BBTALK_TIMEOUT);
		xfree(msgbuf); msgbuf = NULL; msglen = 0;
	}
}

int main(int argc, char *argv[])
{
        namelist_t *hosts, *hwalk;

	/* FIXME */
	dontsendmessages = 1;
	debug = 1;

        hosts = load_hostnames(xgetenv("BBHOSTS"), "netinclude", get_fqdn());
        if (hosts == NULL) {
                errprintf("Cannot load bb-hosts\n");
                return 1;
        }

	init_timestamp();

        if (xgetenv("BBLOCATION")) location = strdup(xgetenv("BBLOCATION"));

	combo_start();

	for (hwalk = hosts; (hwalk); hwalk = hwalk->next) {
		char *tspec = bbh_custom_item(hwalk, "bea=");
		char *snmpcommunity = DEFAULT_SNMP_COMMUNITY;
		char *beadomain = "";
		int snmpport = DEFAULT_SNMP_PORT;
		char *p;
		char pipecmd[4096];
		char *jrockout, *qout;
		int jrockres, qres, jrockoutbytes, qoutbytes;

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
		*statusmsg = '\0';
		statuscolor = COL_GREEN;

		/* Setup the snmpwalk pipe-command for jrockit stats */
		sprintf(pipecmd, "snmpwalk -m BEA-WEBLOGIC-MIB -c %s@%s -v 1 %s:%d enterprises.140.625.302.1",
			snmpcommunity, beadomain, bbh_item(hwalk, BBH_IP), snmpport);
		jrockres = run_command(pipecmd, NULL, &jrockout, &jrockoutbytes, 0);
		if (jrockres == 0) {
			find_idxes(jrockout, "BEA-WEBLOGIC-MIB::jrockitRuntimeIndex.");
			send_data(hwalk, beadomain, jrockout, jrockitems);
		}
		else {
			if (statuscolor < COL_YELLOW) statuscolor = COL_YELLOW;
			sprintf(msgline, "Could not retrieve BEA jRockit statistics from %s:%d domain %s (code %d)\n",
				bbh_item(hwalk, BBH_IP), snmpport, beadomain, jrockres);
			strcat(statusmsg, msgline);
		}

		/* Setup the snmpwalk pipe-command for executeQueur stats */
		sprintf(pipecmd, "snmpwalk -m BEA-WEBLOGIC-MIB -c %s@%s -v 1 %s:%d enterprises.140.625.180.1",
			snmpcommunity, beadomain, bbh_item(hwalk, BBH_IP), snmpport);
		qres = run_command(pipecmd, NULL, &qout, &qoutbytes, 0);
		if (qres == 0) {
			find_idxes(qout, "BEA-WEBLOGIC-MIB::executeQueueRuntimeIndex.");
			send_data(hwalk, beadomain, qout, qitems);
		}
		else {
			if (statuscolor < COL_YELLOW) statuscolor = COL_YELLOW;
			sprintf(msgline, "Could not retrieve BEA executeQueue statistics from %s:%d domain %s (code %d)\n",
				bbh_item(hwalk, BBH_IP), snmpport, beadomain, qres);
			strcat(statusmsg, msgline);
		}

		init_status(statuscolor);
		sprintf(msgline, "status %s.%s %s %s\n\n", commafy(bbh_item(hwalk, BBH_HOSTNAME)), "bea", colorname(statuscolor), timestamp);
		addtostatus(msgline);
		if (*statusmsg == '\0') sprintf(statusmsg, "All BEA monitors OK\n");
		addtostatus(statusmsg);
		finish_status();
	}

	combo_end();

	return 0;
}

