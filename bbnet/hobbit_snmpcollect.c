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

static char rcsid[] = "$Id: hobbit_snmpcollect.c,v 1.5 2007-09-09 21:42:40 henrik Exp $";

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "libbbgen.h"

/* List of the OID's we will request */
typedef struct oid_t {
	char *oidstr;				/* the input definition of the OID */
	oid Oid[MAX_OID_LEN];			/* the internal OID representation */
	unsigned int OidLen;			/* size of the oid */
	char *devname, *dsname;
	char *result;				/* the printable result data */
	struct oid_t *next;
} oid_t;

typedef struct wantedif_t {
	enum { ID_DESCR, ID_PHYSADDR, ID_IPADDR } idtype;
	char *id, *devname;
	struct wantedif_t *next;
} wantedif_t;

typedef struct ifids_t {
	char *index;
	char *descr;
	char *physaddr;
	char *ipaddr;
	struct ifids_t *next;
} ifids_t;

/* A host and the OID's we will be polling */
typedef struct req_t {
	char *hostname;				/* Hostname used for reporting to Hobbit */
	char *hostip;				/* Hostname or IP used for testing */
	u_short portnumber;			/* SNMP daemon portnumber */
	long version;				/* SNMP version to use */
	unsigned char *community;		/* Community name used to access the SNMP daemon */
	struct snmp_session *sess;		/* SNMP session data */
	wantedif_t *wantedinterfaces;		/* List of interfaces by description or phys. addr. we want */
	ifids_t *interfacenames;		/* List of interfaces, pulled from the host */
	struct oid_t *oidhead, *oidtail, *next_oid;	/* List of the OID's we will fetch */
	struct req_t *next;
} req_t;

oid rootoid[MAX_OID_LEN];
unsigned int rootoidlen;

req_t *reqhead = NULL;
int active_requests = 0;
enum { QUERY_INTERFACES, QUERY_IPADDRS, GET_DATA } querymode = GET_DATA;

/* Tuneables */
int max_pending_requests = 30;
int retries = 0;				/* Number of retries before timeout. 0 = Net-SNMP default (5). */
long timeout = 0;				/* Number of uS until first timeout, then exponential backoff. 0 = Net-SNMP default (1 second). */


/* Must forward declare this, since callback-function and starthosts() refer to each other */
void starthosts(int resetstart);

/*
 * simple printing of returned data
 */
int print_result (int status, req_t *sp, struct snmp_pdu *pdu)
{
	char buf[1024];
	struct variable_list *vp;
	struct oid_t *owalk;
	int ix;
	ifids_t *newif;

	switch (status) {
	  case STAT_SUCCESS:
		if (pdu->errstat == SNMP_ERR_NOERROR) {
			switch (querymode) {
			  case QUERY_INTERFACES:
				newif = (ifids_t *)calloc(1, sizeof(ifids_t));

				vp = pdu->variables;
				snprint_variable(buf, sizeof(buf), vp->name, vp->name_length, vp);
				newif->index = strdup(buf);

				vp = vp->next_variable;
				snprint_variable(buf, sizeof(buf), vp->name, vp->name_length, vp);
				newif->descr = strdup(buf);

				vp = vp->next_variable;
				snprint_variable(buf, sizeof(buf), vp->name, vp->name_length, vp);
				newif->physaddr = strdup(buf);

				newif->next = sp->interfacenames;
				sp->interfacenames = newif;
				break;


			  case QUERY_IPADDRS:
				vp = pdu->variables;
				snprint_variable(buf, sizeof(buf), vp->name, vp->name_length, vp);

				newif = sp->interfacenames;
				while (newif && strcmp(newif->index, buf)) newif = newif->next;

				if (newif && (vp->name_length == (rootoidlen + 4))) {
					sprintf(buf, "%d.%d.%d.%d", 
						(int)vp->name[rootoidlen+0], (int)vp->name[rootoidlen+1],
						(int)vp->name[rootoidlen+2], (int)vp->name[rootoidlen+3]);
					newif->ipaddr = strdup(buf);
					dbgprintf("Interface %s has IP %s\n", newif->index, newif->ipaddr);
				}
				break;


			  case GET_DATA:
				owalk = sp->oidhead;
				vp = pdu->variables;
				while (vp) {
					snprint_variable(buf, sizeof(buf), vp->name, vp->name_length, vp);
					dbgprintf("%s: device %s %s\n", sp->hostip, owalk->devname, buf);
					owalk->result = strdup(buf); owalk = owalk->next;
					vp = vp->next_variable;
				}
				break;
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
	struct variable_list *vp;

	if (operation == NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE) {
		if (print_result(STAT_SUCCESS, item, pdu)) {
			req = NULL;

			switch (querymode) {
			  case QUERY_INTERFACES:
			  case QUERY_IPADDRS:
				if (pdu->errstat == SNMP_ERR_NOERROR) {
					vp = pdu->variables;

					if (debug) {
						char buf[1024];
						snprint_variable(buf, sizeof(buf), vp->name, vp->name_length, vp);
						dbgprintf("Got variable: %s\n", buf);
					}

					if ( (vp->name_length >= rootoidlen) && 
					     (memcmp(&rootoid, vp->name, rootoidlen * sizeof(oid)) == 0) ) {
						/* Still getting the right kind of data, so ask for more */
						req = snmp_pdu_create(SNMP_MSG_GETNEXT);
						while (vp) {
							snmp_add_null_var(req, vp->name, vp->name_length);
							vp = vp->next_variable;
						}
					}
				}
				break;

			  case GET_DATA:
				if (item->next_oid) {
					req = snmp_pdu_create(SNMP_MSG_GET);
					while (item->next_oid) {
						snmp_add_null_var(req, item->next_oid->Oid, item->next_oid->OidLen);
						item->next_oid = item->next_oid->next;
					}
				}
				break;
			}

			if (req) {
				if (snmp_send(item->sess, req))
					goto finish;
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

finish:
	/* Start some more hosts */
	starthosts(0);

	return 1;
}


void starthosts(int resetstart)
{
	static req_t *startpoint = NULL;
	static int haverun = 0;

	struct req_t *rwalk;

	if (resetstart) {
		startpoint = NULL;
		haverun = 0;
	}

	if (startpoint == NULL) {
		if (haverun) {
			return;
		}
		else {
			startpoint = reqhead;
			haverun = 1;
		}
	}

	/* startup as many hosts as we want to run in parallel */
	for (rwalk = startpoint; (rwalk && (active_requests <= max_pending_requests)); rwalk = rwalk->next) {
		struct snmp_session s;
		struct snmp_pdu *req;
		oid Oid[MAX_OID_LEN];
		unsigned int OidLen;

		if ((querymode != GET_DATA) && !rwalk->wantedinterfaces) continue;

		if (!rwalk->sess) {
			/* Setup the SNMP session */
			snmp_sess_init(&s);
			s.version = rwalk->version;
			if (timeout > 0) s.timeout = timeout;
			if (retries > 0) s.retries = retries;
			s.peername = rwalk->hostip;
			if (rwalk->portnumber) s.remote_port = rwalk->portnumber;
			s.community = rwalk->community;
			s.community_len = strlen((char *)rwalk->community);
			s.callback = asynch_response;
			s.callback_magic = rwalk;
			if (!(rwalk->sess = snmp_open(&s))) {
				snmp_sess_perror("snmp_open", &s);
				continue;
			}
		}

		switch (querymode) {
		  case QUERY_INTERFACES:
			req = snmp_pdu_create(SNMP_MSG_GETNEXT);
			OidLen = sizeof(Oid)/sizeof(Oid[0]);
			if (read_objid("IF-MIB::ifIndex", Oid, &OidLen)) snmp_add_null_var(req, Oid, OidLen);
			OidLen = sizeof(Oid)/sizeof(Oid[0]);
			if (read_objid("IF-MIB::ifDescr", Oid, &OidLen)) snmp_add_null_var(req, Oid, OidLen);
			OidLen = sizeof(Oid)/sizeof(Oid[0]);
			if (read_objid("IF-MIB::ifPhysAddress", Oid, &OidLen)) snmp_add_null_var(req, Oid, OidLen);
			break;

		  case QUERY_IPADDRS:
			req = snmp_pdu_create(SNMP_MSG_GETNEXT);
			OidLen = sizeof(Oid)/sizeof(Oid[0]);
			if (read_objid("IP-MIB::ipAdEntIfIndex", Oid, &OidLen)) snmp_add_null_var(req, Oid, OidLen);
			break;

		  case GET_DATA:
			/* Build the request PDU and send it */
			req = snmp_pdu_create(SNMP_MSG_GET);
			while (rwalk->next_oid) {
				snmp_add_null_var(req, rwalk->next_oid->Oid, rwalk->next_oid->OidLen);
				rwalk->next_oid = rwalk->next_oid->next;
			}
			break;
		}

		if (snmp_send(rwalk->sess, req))
			active_requests++;
		else {
			snmp_sess_perror("snmp_send", rwalk->sess);
			snmp_free_pdu(req);
		}
	}

	startpoint = rwalk;
}


void stophosts(void)
{
	struct req_t *rwalk;

	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		if (rwalk->sess) snmp_close(rwalk->sess);
	}
}


struct {
	char *oid;
	char *dsname;
} ifmibnames[] = {
	{ "IF-MIB::ifDescr", "ifDescr" },
	{ "IF-MIB::ifType", "ifType" },
	{ "IF-MIB::ifMtu", "ifMtu" },
	{ "IF-MIB::ifSpeed", "ifSpeed" },
	{ "IF-MIB::ifPhysAddress", "ifPhysAddress" },
	{ "IF-MIB::ifAdminStatus", "ifAdminStatus" },
	{ "IF-MIB::ifOperStatus", "ifOperStatus" },
	{ "IF-MIB::ifLastChange", "ifLastChange" },
	{ "IF-MIB::ifInOctets", "ifInOctets" },
	{ "IF-MIB::ifInUcastPkts", "ifInUcastPkts" },
	{ "IF-MIB::ifInNUcastPkts", "ifInNUcastPkts" },
	{ "IF-MIB::ifInDiscards", "ifInDiscards" },
	{ "IF-MIB::ifInErrors", "ifInErrors" },
	{ "IF-MIB::ifInUnknownProtos", "ifInUnknownProtos" },
	{ "IF-MIB::ifOutOctets", "ifOutOctets" },
	{ "IF-MIB::ifOutUcastPkts", "ifOutUcastPkts" },
	{ "IF-MIB::ifOutNUcastPkts", "ifOutNUcastPkts" },
	{ "IF-MIB::ifOutDiscards", "ifOutDiscards" },
	{ "IF-MIB::ifOutErrors", "ifOutErrors" },
	{ "IF-MIB::ifOutQLen", "ifOutQLen" },
	{ NULL, NULL }
};

/*
 *
 * Config file syntax
 *
 * [HOSTNAME]
 *     ip=ADDRESS
 *     port=PORTNUMBER
 *     version=VERSION
 *     community=COMMUNITY
 *     mrtg=INDEX OID1 OID2
 *     var=DSNAME OID
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

		if (strncmp(bot, "port=", 5) == 0) {
			reqitem->portnumber = atoi(bot+5);
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
			char *idx, *oid1 = NULL, *oid2 = NULL, *devname = NULL;

			idx = strtok(bot+5, " \t");
			if (idx) oid1 = strtok(NULL, " \t");
			if (oid1) oid2 = strtok(NULL, " \t");
			if (oid2) devname = strtok(NULL, "\r\n");

			if (idx && oid1 && oid2 && devname) {
				oitem = (oid_t *)calloc(1, sizeof(oid_t));
				oitem->devname = strdup(devname);
				oitem->dsname = strdup("ds1");
				oitem->oidstr = strdup(oid1);
				oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
				if (read_objid(oitem->oidstr, oitem->Oid, &oitem->OidLen)) {
					if (!reqitem->oidhead) reqitem->oidhead = oitem; else reqitem->oidtail->next = oitem;
					reqitem->oidtail = oitem;
				}
				else {
					/* Could not parse the OID definition */
					snmp_perror("read_objid");
					free(oitem->oidstr);
					free(oitem);
				}

				oitem = (oid_t *)calloc(1, sizeof(oid_t));
				oitem->devname = strdup(devname);
				oitem->dsname = strdup("ds2");
				oitem->oidstr = strdup(oid2);
				oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
				if (read_objid(oitem->oidstr, oitem->Oid, &oitem->OidLen)) {
					if (!reqitem->oidhead) reqitem->oidhead = oitem; else reqitem->oidtail->next = oitem;
					reqitem->oidtail = oitem;
				}
				else {
					/* Could not parse the OID definition */
					snmp_perror("read_objid");
					free(oitem->oidstr);
					free(oitem);
				}

			}

			reqitem->next_oid = reqitem->oidhead;
			continue;
		}

		if (strncmp(bot, "ifmib=", 6) == 0) {
			char *idx, *devname;
			int i;
			char oid[128];

			idx = strtok(bot+6, " \t");
			if (idx) devname = strtok(NULL, " \r\n");

			if ((*idx == '(') || (*idx == '[') || (*idx == '{')) {
				/* Interface-by-name or interface-by-physaddr */
				wantedif_t *newitem = (wantedif_t *)malloc(sizeof(wantedif_t));
				switch (*idx) {
				  case '(': newitem->idtype = ID_DESCR; break;
				  case '[': newitem->idtype = ID_PHYSADDR; break;
				  case '{': newitem->idtype = ID_IPADDR; break;
				}
				p = idx + strcspn(idx, "])}"); if (p) *p = '\0';
				newitem->id = strdup(idx+1);
				newitem->devname = strdup(devname);
				newitem->next = reqitem->wantedinterfaces;
				reqitem->wantedinterfaces = newitem;
			}
			else {
				/* Plain numeric interface */
				for (i=0; (ifmibnames[i].oid); i++) {
					sprintf(oid, "%s.%s", ifmibnames[i].oid, idx);

					oitem = (oid_t *)calloc(1, sizeof(oid_t));
					oitem->devname = strdup(devname);
					oitem->dsname = strdup(ifmibnames[i].dsname);
					oitem->oidstr = strdup(oid);
					oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
					if (read_objid(oitem->oidstr, oitem->Oid, &oitem->OidLen)) {
						if (!reqitem->oidhead) reqitem->oidhead = oitem; else reqitem->oidtail->next = oitem;
						reqitem->oidtail = oitem;
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
			continue;
		}

		if (strncmp(bot, "var=", 4) == 0) {
			char *dsname, *oid = NULL, *devname = NULL;

			dsname = strtok(bot+4, " \t");
			if (dsname) oid = strtok(NULL, " \t");
			if (oid) devname = strtok(NULL, " \r\n");

			if (dsname && oid) {
				oitem = (oid_t *)calloc(1, sizeof(oid_t));
				oitem->devname = strdup(devname);
				oitem->dsname = strdup(dsname);
				oitem->oidstr = strdup(oid);
				oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
				if (read_objid(oitem->oidstr, oitem->Oid, &oitem->OidLen)) {
					if (!reqitem->oidhead) reqitem->oidhead = oitem; else reqitem->oidtail->next = oitem;
					reqitem->oidtail = oitem;
				}
				else {
					/* Could not parse the OID definition */
					snmp_perror("read_objid");
					free(oitem->oidstr);
					free(oitem);
				}
			}
			reqitem->next_oid = reqitem->oidhead;
			continue;
		}
	}

	stackfclose(cfgfd);
	freestrbuffer(inbuf);
}


void communicate(void)
{
	/* loop while any active requests */
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
}


void resolveifnames(void)
{
	struct req_t *rwalk;
	ifids_t *ifwalk;
	wantedif_t *wantwalk;

	/* 
	 * To get the interface names, we walk the ifIndex table with getnext.
	 * We need the fixed part of that OID available to check when we reach
	 * the end of the table (then we'll get a variable back which has a
	 * different OID).
	 */
	rootoidlen = sizeof(rootoid)/sizeof(rootoid[0]);
	read_objid(".1.3.6.1.2.1.2.2.1.1", rootoid, &rootoidlen);
	querymode = QUERY_INTERFACES;
	starthosts(1);
	communicate();

	rootoidlen = sizeof(rootoid)/sizeof(rootoid[0]);
	read_objid(".1.3.6.1.2.1.4.20.1.2", rootoid, &rootoidlen);
	querymode = QUERY_IPADDRS;
	starthosts(1);
	communicate();

	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		if (!rwalk->wantedinterfaces || !rwalk->interfacenames) continue;

		for (wantwalk = rwalk->wantedinterfaces; (wantwalk); wantwalk = wantwalk->next) {
			switch (wantwalk->idtype) {
			  case ID_DESCR:
				for (ifwalk = rwalk->interfacenames; (ifwalk && strcmp(ifwalk->descr, wantwalk->id)); ifwalk = ifwalk->next) ;
				break;

			  case ID_PHYSADDR:
				for (ifwalk = rwalk->interfacenames; (ifwalk && strcmp(ifwalk->physaddr, wantwalk->id)); ifwalk = ifwalk->next) ;
				break;

			  case ID_IPADDR:
				for (ifwalk = rwalk->interfacenames; (ifwalk && (!ifwalk->ipaddr || strcmp(ifwalk->ipaddr, wantwalk->id))); ifwalk = ifwalk->next) ;
				break;
			}

			if (ifwalk) {
				int i;
				char oid[128];
				struct oid_t *oitem;

				for (i=0; (ifmibnames[i].oid); i++) {
					sprintf(oid, "%s.%s", ifmibnames[i].oid, ifwalk->index);

					oitem = (oid_t *)calloc(1, sizeof(oid_t));
					oitem->devname = strdup(wantwalk->devname);
					oitem->dsname = strdup(ifmibnames[i].dsname);
					oitem->oidstr = strdup(oid);
					oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
					if (read_objid(oitem->oidstr, oitem->Oid, &oitem->OidLen)) {
						if (!rwalk->oidhead) rwalk->oidhead = oitem; else rwalk->oidtail->next = oitem;
						rwalk->oidtail = oitem;
					}
					else {
						/* Could not parse the OID definition */
						snmp_perror("read_objid");
						free(oitem->oidstr);
						free(oitem);
					}
				}
				rwalk->next_oid = rwalk->oidhead;
			}
		}
	}
}


void getdata(void)
{
	querymode = GET_DATA;
	starthosts(1);
	communicate();
}


void sendresult(void)
{
	struct req_t *rwalk;
	struct oid_t *owalk;

	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		for (owalk = rwalk->oidhead; (owalk); owalk = owalk->next) {
			printf("%s interface %s: %s = %s\n", 
				rwalk->hostname, owalk->devname,
				owalk->dsname, (owalk->result ? owalk->result : "NODATA"));
		}
	}
}

int main (int argc, char **argv)
{
	int argi;
	char *configfn = "hobbit_snmpcollect.cfg";
	int cfgcheck = 0;

	for (argi = 1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[argi], "--cfgcheck") == 0) {
			cfgcheck = 1;
		}
		else if (argnmatch(argv[argi], "--timeout=")) {
			char *p = strchr(argv[argi], '=');
			timeout = 1000000*atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--retries=")) {
			char *p = strchr(argv[argi], '=');
			retries = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--concurrency=")) {
			char *p = strchr(argv[argi], '=');
			max_pending_requests = atoi(p+1);
		}
		else if (*argv[argi] != '-') {
			configfn = argv[argi];
		}
	}

	init_snmp("hobbit_snmpcollect");
	snmp_out_toggle_options("vqs");	/* Like snmpget -Ovqs */

	readconfig(configfn);
	if (cfgcheck) return 0;

	resolveifnames();

	getdata();
	stophosts();

	sendresult();

	return 0;
}

