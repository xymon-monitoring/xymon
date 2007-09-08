/*----------------------------------------------------------------------------*/
/* Hobbit monitor SNMP data collection tool                                   */
/*                                                                            */
/* Copyright (C) 2007 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* Inspired by the asyncapp.c file from the "NET-SNMP demo", available from   */
/* the Net-SNMP website. This file carries the attribution                    */
/*         "Niels Baggesen (Niels.Baggesen@uni-c.dk), 1999."                  */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit_snmpcollect.c,v 1.1 2007-09-08 12:07:49 henrik Exp $";

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "libbbgen.h"

/* List of the OID's we will request */
typedef struct oid_t {
	char *oidstr;				/* the input definition of the OID */
	oid Oid[MAX_OID_LEN];			/* the internal OID representation */
	unsigned int OidLen;			/* size of the oid */
	int index;
	char *dsname;
	char *result;				/* the printable result data */
	struct oid_t *next;
} oid_t;

/* A host and the OID's we will be polling */
typedef struct req_t {
	char *hostname;				/* Hostname used for reporting to Hobbit */
	char *hostip;				/* Hostname or IP used for testing */
	long version;				/* SNMP version to use */
	unsigned char *community;		/* Community name used to access the SNMP daemon */
	struct snmp_session *sess;		/* SNMP session data */
	struct oid_t *oidhead, *next_oid;	/* List of the OID's we will getch */
	struct req_t *next;
} req_t;

req_t *reqhead = NULL;
int active_requests = 0;


/*
 * simple printing of returned data
 */
int print_result (int status, req_t *sp, struct snmp_pdu *pdu)
{
	char buf[1024];
	struct variable_list *vp;
	struct oid_t *owalk;
	int ix;

	switch (status) {
	  case STAT_SUCCESS:
		vp = pdu->variables;
		owalk = sp->oidhead;
		if (pdu->errstat == SNMP_ERR_NOERROR) {
			while (vp) {
				snprint_variable(buf, sizeof(buf), vp->name, vp->name_length, vp);
				owalk->result = strdup(buf);
				dbgprintf("%s: index %d %s\n", sp->hostip, owalk->index, buf);
				vp = vp->next_variable; owalk = owalk->next;
			}
		}
		else {
			for (ix = 1; vp && ix != pdu->errindex; vp = vp->next_variable, ix++) ;

			if (vp) 
				snprint_objid(buf, sizeof(buf), vp->name, vp->name_length);
			else 
				strcpy(buf, "(none)");

			dbgprintf("%s: %s: %s\n",
				sp->hostip, buf, snmp_errstring(pdu->errstat));
		}
		return 1;

	  case STAT_TIMEOUT:
		dbgprintf("%s: Timeout\n", sp->hostip);
		return 0;

	  case STAT_ERROR:
		snmp_sess_perror(sp->hostip, sp->sess);
		return 0;
	}

	return 0;
}


/*
 * response handler
 */
int asynch_response(int operation, struct snmp_session *sp, int reqid,
		struct snmp_pdu *pdu, void *magic)
{
	struct req_t *item = (struct req_t *)magic;
	struct snmp_pdu *req;

	if (operation == NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE) {
		if (print_result(STAT_SUCCESS, item, pdu)) {
			if (item->next_oid) {
				req = snmp_pdu_create(SNMP_MSG_GET);
				while (item->next_oid) {
					snmp_add_null_var(req, item->next_oid->Oid, item->next_oid->OidLen);
					item->next_oid = item->next_oid->next;
				}
				if (snmp_send(item->sess, req))
					return 1;
				else {
					snmp_sess_perror("snmp_send", item->sess);
					snmp_free_pdu(req);
				}
			}
		}
	}
	else
		print_result(STAT_TIMEOUT, item, pdu);

	/* something went wrong (or end of variables) 
	 * this host not active any more
	 */
	active_requests--;
	return 1;
}

void asynchronous(void)
{
	struct req_t *rwalk;

	/* startup all hosts */
	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		struct snmp_session s;
		struct snmp_pdu *req;

		/* Setup the SNMP session */
		snmp_sess_init(&s);
		s.version = rwalk->version;
		s.peername = rwalk->hostip;
		s.community = rwalk->community;
		s.community_len = strlen((char *)rwalk->community);
		s.callback = asynch_response;
		s.callback_magic = rwalk;
		if (!(rwalk->sess = snmp_open(&s))) {
			snmp_sess_perror("snmp_open", &s);
			continue;
		}

		/* Build the request PDU and send it */
		req = snmp_pdu_create(SNMP_MSG_GET);
		while (rwalk->next_oid) {
			snmp_add_null_var(req, rwalk->next_oid->Oid, rwalk->next_oid->OidLen);
			rwalk->next_oid = rwalk->next_oid->next;
		}

		if (snmp_send(rwalk->sess, req))
			active_requests++;
		else {
			snmp_sess_perror("snmp_send", rwalk->sess);
			snmp_free_pdu(req);
		}
	}

	/* loop while any active hosts */
	while (active_requests) {
		int fds = 0, block = 1;
		fd_set fdset;
		struct timeval timeout;

		FD_ZERO(&fdset);
		snmp_select_info(&fds, &fdset, &timeout, &block);
		fds = select(fds, &fdset, NULL, NULL, block ? NULL : &timeout);
		if (fds < 0) {
			perror("select failed");
			exit(1);
		}
		if (fds)
			snmp_read(&fdset);
		else
			snmp_timeout();
	}

	/* cleanup */
	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		if (rwalk->sess) snmp_close(rwalk->sess);
	}
}


/*
 *
 * Config file syntax
 *
 * [HOSTNAME]
 *     ip=ADDRESS
 *     version=VERSION
 *     community=COMMUNITY
 *     mrtg=index OID1 OID2
 *
 */
void readconfig(char *cfgfn)
{
	static void *cfgfiles = NULL;
	FILE *cfgfd;
	strbuffer_t *inbuf;

	struct req_t *reqitem = NULL;
	struct oid_t *oitem;

	/* Check if config was modified */
	if (cfgfiles) {
		if (!stackfmodified(cfgfiles)) {
			dbgprintf("No files changed, skipping reload\n");
			return;
		}
		else {
			stackfclist(&cfgfiles);
			cfgfiles = NULL;
		}
	}

	cfgfd = stackfopen(cfgfn, "r", &cfgfiles);
	if (cfgfd == NULL) {
		errprintf("Cannot open configuration files %s\n", cfgfn);
		return;
	}

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		char *bot, *p;

		sanitize_input(inbuf, 0, 0);
		bot = STRBUF(inbuf) + strspn(STRBUF(inbuf), " \t");

		if (*bot == '[') {
			reqitem = (req_t *)calloc(1, sizeof(req_t));

			p = strchr(bot, ']'); if (p) *p = '\0';
			reqitem->hostname = strdup(bot + 1);
			if (p) *p = ']';

			reqitem->hostip = reqitem->hostname;
			reqitem->version = SNMP_VERSION_1;
			reqitem->next = reqhead;
			reqhead = reqitem;

			continue;
		}

		/* If we have nowhere to put the data, then skip further processing */
		if (!reqitem) continue;

		if (strncmp(bot, "ip=", 3) == 0) {
			reqitem->hostip = strdup(bot+3);
			continue;
		}

		if (strncmp(bot, "version=", 8) == 0) {
			switch (*(bot+8)) {
			  case '1': reqitem->version = SNMP_VERSION_1; break;
			  case '2': reqitem->version = SNMP_VERSION_2c; break;
			  case '3': reqitem->version = SNMP_VERSION_3; break;
			}
			continue;
		}

		if (strncmp(bot, "community=", 10) == 0) {
			reqitem->community = strdup(bot+10);
			continue;
		}

		if (strncmp(bot, "mrtg=", 5) == 0) {
			char *idx, *oid1 = NULL, *oid2 = NULL;

			idx = strtok(bot+5, " \t");
			if (idx) oid1 = strtok(NULL, " \t");
			if (oid1) oid2 = strtok(NULL, " \t");

			if (idx && oid1 && oid2) {
				oitem = (oid_t *)calloc(1, sizeof(oid_t));
				oitem->index = atoi(idx);
				oitem->dsname = strdup("ds1");
				oitem->oidstr = strdup(oid1);
				oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
				if (read_objid(oitem->oidstr, oitem->Oid, &oitem->OidLen)) {
					oitem->next = reqitem->oidhead;
					reqitem->oidhead = oitem;
				}
				else {
					/* Could not parse the OID definition */
					snmp_perror("read_objid");
					free(oitem->oidstr);
					free(oitem);
				}

				oitem = (oid_t *)calloc(1, sizeof(oid_t));
				oitem->index = atoi(idx);
				oitem->dsname = strdup("ds2");
				oitem->oidstr = strdup(oid2);
				oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
				if (read_objid(oitem->oidstr, oitem->Oid, &oitem->OidLen)) {
					oitem->next = reqitem->oidhead;
					reqitem->oidhead = oitem;
				}
				else {
					/* Could not parse the OID definition */
					snmp_perror("read_objid");
					free(oitem->oidstr);
					free(oitem);
				}

			}

			reqitem->next_oid = reqitem->oidhead;
		}
	}

	stackfclose(cfgfd);
	freestrbuffer(inbuf);
}

void sendresult(void)
{
	struct req_t *rwalk;
	struct oid_t *owalk;

	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		for (owalk = rwalk->oidhead; (owalk); owalk = owalk->next) {
			printf("%s index %d : %s %s = %s\n", 
				rwalk->hostname, owalk->index, owalk->oidstr, owalk->dsname, owalk->result);
		}
	}
}

int main (int argc, char **argv)
{
	init_snmp("hobbit_snmpcollect");
	snmp_out_toggle_options("vqs");	/* Like snmpget -Ovqs */

	readconfig("snmpcollect.cfg");
	asynchronous();
	sendresult();

	return 0;
}

