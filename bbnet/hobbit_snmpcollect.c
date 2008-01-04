/*----------------------------------------------------------------------------*/
/* Hobbit monitor SNMP data collection tool                                   */
/*                                                                            */
/* Copyright (C) 2007-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* Inspired by the asyncapp.c file from the "NET-SNMP demo", available from   */
/* the Net-SNMP website. This file carries the attribution                    */
/*         "Niels Baggesen (Niels.Baggesen@uni-c.dk), 1999."                  */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit_snmpcollect.c,v 1.20 2008-01-04 22:26:40 henrik Exp $";

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "libbbgen.h"

/* List of the OID's we will request */
enum querytype_t { QUERY_IFMIB, QUERY_IFMIB_X, QUERY_MRTG, QUERY_VAR, QUERY_SNMPMIB, QUERY_ICMPMIB };
typedef struct oid_t {
	char *oidstr;				/* the input definition of the OID */
	enum querytype_t querytype;
	oid Oid[MAX_OID_LEN];			/* the internal OID representation */
	unsigned int OidLen;			/* size of the oid */
	char *devname, *dsname;
	int  requestset;			/* Used to group vars into SNMP PDU's */
	char *result;				/* the printable result data */
	struct oid_t *next;
} oid_t;

typedef struct wantedif_t {
	enum { ID_DESCR, ID_PHYSADDR, ID_IPADDR, ID_NAME } idtype;
	char *id, *devname;
	int requestset;
	struct wantedif_t *next;
} wantedif_t;

typedef struct ifids_t {
	char *index;
	char *descr;
	char *name;
	char *physaddr;
	char *ipaddr;
	struct ifids_t *next;
} ifids_t;

/* A host and the OID's we will be polling */
typedef struct req_t {
	char *hostname;				/* Hostname used for reporting to Hobbit */
	char *hostip[10];			/* Hostname or IP used for testing */
	int hostipidx;
	u_short portnumber;			/* SNMP daemon portnumber */
	long version;				/* SNMP version to use */
	unsigned char *community;		/* Community name used to access the SNMP daemon */
	struct snmp_session *sess;		/* SNMP session data */
	wantedif_t *wantedinterfaces;		/* List of interfaces by description or phys. addr. we want */
	ifids_t *interfacenames;		/* List of interfaces, pulled from the host */
	struct oid_t *oidhead, *oidtail;	/* List of the OID's we will fetch */
	struct oid_t *curr_oid, *next_oid;	/* Current- and next-OID pointers while fetching data */
	struct req_t *next;
} req_t;

oid rootoid[MAX_OID_LEN];
unsigned int rootoidlen;

req_t *reqhead = NULL;
int active_requests = 0;
/* dataoperation tracks what we are currently doing */
enum { 	SCAN_INTERFACES, 	/* Scan what descriptions (IF-MIB::ifDescr) or MAC address 
				   (IF-MIB::ifPhysAddress) have been given to each interface */
	SCAN_IPADDRS, 		/* Scan what IP's (IP-MIB::ipAdEntIfIndex) have been given to each interface */
	SCAN_IFNAMES, 		/* Scan what names (IF-MIB::ifName) have been given to each interface */
	GET_DATA 		/* Fetch the actual data */
} dataoperation = GET_DATA;

/* Tuneables */
int max_pending_requests = 30;
int retries = 0;				/* Number of retries before timeout. 0 = Net-SNMP default (5). */
long timeout = 0;				/* Number of uS until first timeout, then exponential backoff. 0 = Net-SNMP default (1 second). */

/* Statistics */
char *reportcolumn = NULL;
int varcount = 0;
int pducount = 0;
int okcount = 0;
int toobigcount = 0;
int timeoutcount = 0;
int errorcount = 0;
struct timeval starttv, endtv;

typedef struct oidds_t {
	char *oid;
	char *dsname;
} oidds_t;

oidds_t ifmibnames[] = {
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

oidds_t ifXmibnames[] = {
	/* These are extension variables, they may not exist */
	{ "IF-MIB::ifName", "ifName" },
	{ "IF-MIB::ifInMulticastPkts", "ifInMulticastPkts" },
	{ "IF-MIB::ifInBroadcastPkts", "ifInBroadcastPkts" },
	{ "IF-MIB::ifOutMulticastPkts", "ifOutMulticastPkts" },
	{ "IF-MIB::ifOutBroadcastPkts", "ifOutBroadcastPkts" },
	{ "IF-MIB::ifHCInOctets", "ifHCInOctets" },
	{ "IF-MIB::ifHCInUcastPkts", "ifHCInUcastPkts" },
	{ "IF-MIB::ifHCInMulticastPkts", "ifHCInMulticastPkts" },
	{ "IF-MIB::ifHCInBroadcastPkts", "ifHCInBroadcastPkts" },
	{ "IF-MIB::ifHCOutOctets", "ifHCOutOctets" },
	{ "IF-MIB::ifHCOutUcastPkts", "ifHCOutUcastPkts" },
	{ "IF-MIB::ifHCOutMulticastPkts", "ifHCOutMulticastPkts" },
	{ "IF-MIB::ifHCOutBroadcastPkts", "ifHCOutBroadcastPkts" },
	{ "IF-MIB::ifLinkUpDownTrapEnable", "ifLinkUpDownTrapEnable" },
	{ "IF-MIB::ifHighSpeed", "ifHighSpeed" },
	{ "IF-MIB::ifPromiscuousMode", "ifPromiscuousMode" },
	{ "IF-MIB::ifConnectorPresent", "ifConnectorPresent" },
	{ "IF-MIB::ifAlias", "ifAlias" },
	{ "IF-MIB::ifCounterDiscontinuityTime", "ifCounterDiscontinuityTime" },
	{ NULL, NULL }
};

oidds_t snmpmibnames[] = {
	{ "SNMPv2-MIB::snmpInPkts.0", "snmpInPkts" },
	{ "SNMPv2-MIB::snmpOutPkts.0", "snmpOutPkts" },
	{ "SNMPv2-MIB::snmpInBadVersions.0", "snmpInBadVersions" },
	{ "SNMPv2-MIB::snmpInBadCommunityNames.0", "snmpInBadCommunityNames" },
	{ "SNMPv2-MIB::snmpInBadCommunityUses.0", "snmpInBadcommunityUses" },
	{ "SNMPv2-MIB::snmpInASNParseErrs.0", "snmpInASMParseErrs" },
	{ "SNMPv2-MIB::snmpInTooBigs.0", "snmpInTooBigs" },
	{ "SNMPv2-MIB::snmpInNoSuchNames.0", "snmpInNoSuchNames" },
	{ "SNMPv2-MIB::snmpInBadValues.0", "snmpInBadValues" },
	{ "SNMPv2-MIB::snmpInReadOnlys.0", "snmpInReadOnlys" },
	{ "SNMPv2-MIB::snmpInGenErrs.0", "snmpInGenErrs" },
	{ "SNMPv2-MIB::snmpInTotalReqVars.0", "snmpInTotalReqVars" },
	{ "SNMPv2-MIB::snmpInTotalSetVars.0", "snmpInTotalSetVars" },
	{ "SNMPv2-MIB::snmpInGetRequests.0", "snmpInGetRequests" },
	{ "SNMPv2-MIB::snmpInGetNexts.0", "snmpInGetNexts" },
	{ "SNMPv2-MIB::snmpInSetRequests.0", "snmpInSetRequests" },
	{ "SNMPv2-MIB::snmpInGetResponses.0", "snmpInGetResponses" },
	{ "SNMPv2-MIB::snmpInTraps.0", "snmpInTraps" },
	{ "SNMPv2-MIB::snmpOutTooBigs.0", "snmpOutTooBigs" },
	{ "SNMPv2-MIB::snmpOutNoSuchNames.0", "snmpOutNoSuchNames" },
	{ "SNMPv2-MIB::snmpOutBadValues.0", "snmpOutBadValues" },
	{ "SNMPv2-MIB::snmpOutGenErrs.0", "snmpOutGenErrs" },
	{ "SNMPv2-MIB::snmpOutGetRequests.0", "snmpOutGetRequests" },
	{ "SNMPv2-MIB::snmpOutGetNexts.0", "snmpOutGetNexts" },
	{ "SNMPv2-MIB::snmpOutSetRequests.0", "snmpOutSetRequests" },
	{ "SNMPv2-MIB::snmpOutGetResponses.0", "snmpOutGetResponses" },
	{ "SNMPv2-MIB::snmpOutTraps.0", "snmpOutTraps" },
	{ "SNMPv2-MIB::snmpSilentDrops.0", "snmpSilentDrops" },
	{ "SNMPv2-MIB::snmpProxyDrops.0", "snmpProxyDrops" },
	{ NULL, NULL }
};

oidds_t icmpmibnames[] = {
	{ "IP-MIB::icmpInMsgs.0", "icmpInMsgs" },
	{ "IP-MIB::icmpInErrors.0", "icmpInErrors" },
	{ "IP-MIB::icmpInDestUnreachs.0", "icmpInDestUnreachs" },
	{ "IP-MIB::icmpInTimeExcds.0", "icmpInTimeExcds" },
	{ "IP-MIB::icmpInParmProbs.0", "icmpInParmProbs" },
	{ "IP-MIB::icmpInSrcQuenchs.0", "icmpInSrcQuenchs" },
	{ "IP-MIB::icmpInRedirects.0", "icmpInRedirects" },
	{ "IP-MIB::icmpInEchos.0", "icmpInEchos" },
	{ "IP-MIB::icmpInEchoReps.0", "icmpInEchoReps" },
	{ "IP-MIB::icmpInTimestamps.0", "icmpInTimestamps" },
	{ "IP-MIB::icmpInTimestampReps.0", "icmpInTimestampReps" },
	{ "IP-MIB::icmpInAddrMasks.0", "icmpInAddrMasks" },
	{ "IP-MIB::icmpInAddrMaskReps.0", "icmpInAddrMaskReps" },
	{ "IP-MIB::icmpOutMsgs.0", "icmpOutMsgs" },
	{ "IP-MIB::icmpOutErrors.0", "icmpOutErrors" },
	{ "IP-MIB::icmpOutDestUnreachs.0", "icmpOutDestUnreachs" },
	{ "IP-MIB::icmpOutTimeExcds.0", "icmpOutTimeExcds" },
	{ "IP-MIB::icmpOutParmProbs.0", "icmpOutParmProbs" },
	{ "IP-MIB::icmpOutSrcQuenchs.0", "icmpOutSrcQuenchs" },
	{ "IP-MIB::icmpOutRedirects.0", "icmpOutRedirects" },
	{ "IP-MIB::icmpOutEchos.0", "icmpOutEchos" },
	{ "IP-MIB::icmpOutEchoReps.0", "icmpOutEchoReps" },
	{ "IP-MIB::icmpOutTimestamps.0", "icmpOutTimestamps" },
	{ "IP-MIB::icmpOutTimestampReps.0", "icmpOutTimestampReps" },
	{ "IP-MIB::icmpOutAddrMasks.0", "icmpOutAddrMasks" },
	{ "IP-MIB::icmpOutAddrMaskReps.0", "icmpOutAddrMaskReps" },
	{ NULL, NULL }
};

/* Must forward declare these */
void startonehost(struct req_t *r, int ipchange);
void starthosts(int resetstart);

struct snmp_pdu *generate_datarequest(req_t *item)
{
	struct snmp_pdu *req;
	int currentset;

	if (!item->next_oid) return NULL;

	req = snmp_pdu_create(SNMP_MSG_GET);
	pducount++;
	item->curr_oid = item->next_oid;
	currentset = item->next_oid->requestset;
	while (item->next_oid && (currentset == item->next_oid->requestset)) {
		varcount++;
		snmp_add_null_var(req, item->next_oid->Oid, item->next_oid->OidLen);
		item->next_oid = item->next_oid->next;
	}

	return req;
}


/*
 * simple printing of returned data
 */
int print_result (int status, req_t *sp, struct snmp_pdu *pdu)
{
	char buf[1024];
	char ifid[1024];
	struct variable_list *vp;
	struct oid_t *owalk;
	ifids_t *newif;

	switch (status) {
	  case STAT_SUCCESS:
		if (pdu->errstat == SNMP_ERR_NOERROR) {
			okcount++;

			switch (dataoperation) {
			  case SCAN_INTERFACES:
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


			  case SCAN_IFNAMES:
				/* Value returned is the name. The last byte of the OID is the interface index */
				vp = pdu->variables;
				snprint_variable(buf, sizeof(buf), vp->name, vp->name_length, vp);
				snprintf(ifid, sizeof(ifid), "%d", (int)vp->name[vp->name_length-1]);

				newif = sp->interfacenames;
				while (newif && strcmp(newif->index, ifid)) newif = newif->next;
				if (newif) newif->name = strdup(buf);
				break;


			  case SCAN_IPADDRS:
				/* value returned is the interface index. The last 4 bytes of the OID is the IP */
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
				owalk = sp->curr_oid;
				vp = pdu->variables;
				while (vp) {
					snprint_variable(buf, sizeof(buf), vp->name, vp->name_length, vp);
					dbgprintf("%s: device %s %s\n", sp->hostip[sp->hostipidx], owalk->devname, buf);
					owalk->result = strdup(buf); owalk = owalk->next;
					vp = vp->next_variable;
				}
				break;
			}
		}
		else {
			errorcount++;
			errprintf("ERROR %s: %s\n", sp->hostip[sp->hostipidx], snmp_errstring(pdu->errstat));
		}
		return 1;

	  case STAT_TIMEOUT:
		timeoutcount++;
		dbgprintf("%s: Timeout\n", sp->hostip);
		if (sp->hostip[sp->hostipidx+1]) {
			sp->hostipidx++;
			startonehost(sp, 1);
		}
		return 0;

	  case STAT_ERROR:
		errorcount++;
		snmp_sess_perror(sp->hostip[sp->hostipidx], sp->sess);
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

	if (operation == NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE) {
		struct snmp_pdu *req = NULL;

		switch (pdu->errstat) {
		  case SNMP_ERR_NOERROR:
			/* Pick up the results */
			print_result(STAT_SUCCESS, item, pdu);
			break;

		  case SNMP_ERR_NOSUCHNAME:
			dbgprintf("Host %s item %s: No such name\n", item->hostname, item->curr_oid->devname);
			if (item->hostip[item->hostipidx+1]) {
				item->hostipidx++;
				startonehost(item, 1);
			}
			break;

		  case SNMP_ERR_TOOBIG:
			toobigcount++;
			errprintf("Host %s item %s: Response too big\n", item->hostname, item->curr_oid->devname);
			break;

		  default:
			errorcount++;
			errprintf("Host %s item %s: SNMP error %d\n",  item->hostname, item->curr_oid->devname, pdu->errstat);
			break;
		}

		/* Now see if we should send another request */
		switch (dataoperation) {
		  case SCAN_INTERFACES:
		  case SCAN_IFNAMES:
		  case SCAN_IPADDRS:
			if (pdu->errstat == SNMP_ERR_NOERROR) {
				struct variable_list *vp = pdu->variables;

				if (debug) {
					char buf[1024];
					snprint_variable(buf, sizeof(buf), vp->name, vp->name_length, vp);
					dbgprintf("Got variable: %s\n", buf);
				}

				if ( (vp->name_length >= rootoidlen) && 
				     (memcmp(&rootoid, vp->name, rootoidlen * sizeof(oid)) == 0) ) {
					/* Still getting the right kind of data, so ask for more */
					req = snmp_pdu_create(SNMP_MSG_GETNEXT);
					pducount++;
					while (vp) {
						varcount++;
						snmp_add_null_var(req, vp->name, vp->name_length);
						vp = vp->next_variable;
					}
				}
			}
			break;

		  case GET_DATA:
			if (item->next_oid) {
				req = generate_datarequest(item);
			}
			else {
				dbgprintf("No more oids left\n");
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
	else {
		dbgprintf("operation not succesful: %d\n", operation);
		print_result(STAT_TIMEOUT, item, pdu);
	}

	/* something went wrong (or end of variables) 
	 * this host not active any more
	 */
	dbgprintf("Finished host %s\n", item->hostname);
	active_requests--;

finish:
	/* Start some more hosts */
	starthosts(0);

	return 1;
}


void startonehost(struct req_t *r, int ipchange)
{
	struct snmp_session s;
	struct snmp_pdu *req = NULL;
	oid Oid[MAX_OID_LEN];
	unsigned int OidLen;

	if ((dataoperation < GET_DATA) && !r->wantedinterfaces) return;

	/* Are we retrying a cluster with a new IP? Then drop the current session */
	if (r->sess && ipchange) {
		/*
		 * Apparently, we cannot close a session while in a callback.
		 * So leave this for now - it will leak some memory, but
		 * this is not a problem as long as we only run once.
		 */
		/* snmp_close(r->sess); */
		r->sess = NULL;
	}

	/* Setup the SNMP session */
	if (!r->sess) {
		snmp_sess_init(&s);
		s.version = r->version;
		if (timeout > 0) s.timeout = timeout;
		if (retries > 0) s.retries = retries;
		s.peername = r->hostip[r->hostipidx];
		if (r->portnumber) s.remote_port = r->portnumber;
		s.community = r->community;
		s.community_len = strlen((char *)r->community);
		s.callback = asynch_response;
		s.callback_magic = r;
		if (!(r->sess = snmp_open(&s))) {
			snmp_sess_perror("snmp_open", &s);
			return;
		}
	}

	switch (dataoperation) {
	  case SCAN_INTERFACES:
		req = snmp_pdu_create(SNMP_MSG_GETNEXT);
		pducount++;
		OidLen = sizeof(Oid)/sizeof(Oid[0]);
		if (read_objid("IF-MIB::ifIndex", Oid, &OidLen)) {
			varcount++;
			snmp_add_null_var(req, Oid, OidLen);
		}
		OidLen = sizeof(Oid)/sizeof(Oid[0]);
		if (read_objid("IF-MIB::ifDescr", Oid, &OidLen)) {
			varcount++;
			snmp_add_null_var(req, Oid, OidLen);
		}
		OidLen = sizeof(Oid)/sizeof(Oid[0]);
		if (read_objid("IF-MIB::ifPhysAddress", Oid, &OidLen)) {
			varcount++;
			snmp_add_null_var(req, Oid, OidLen);
		}
		break;

	  case SCAN_IFNAMES:
		req = snmp_pdu_create(SNMP_MSG_GETNEXT);
		pducount++;
		OidLen = sizeof(Oid)/sizeof(Oid[0]);
		if (read_objid("IF-MIB::ifName", Oid, &OidLen)) {
			varcount++;
			snmp_add_null_var(req, Oid, OidLen);
		}
		break;

	  case SCAN_IPADDRS:
		req = snmp_pdu_create(SNMP_MSG_GETNEXT);
		pducount++;
		OidLen = sizeof(Oid)/sizeof(Oid[0]);
		if (read_objid("IP-MIB::ipAdEntIfIndex", Oid, &OidLen)) {
			varcount++;
			snmp_add_null_var(req, Oid, OidLen);
		}
		break;

	  case GET_DATA:
		/* Build the request PDU and send it */
		req = generate_datarequest(r);
		break;
	}

	if (!req) return;

	if (snmp_send(r->sess, req))
		active_requests++;
	else {
		errorcount++;
		snmp_sess_perror("snmp_send", r->sess);
		snmp_free_pdu(req);
	}
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
		startonehost(rwalk, 0);
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

static oid_t *make_oitem(enum querytype_t qtype, char *devname, int setnumber, char *dsname, char *oidstr, struct req_t *reqitem)
{
	oid_t *oitem = (oid_t *)calloc(1, sizeof(oid_t));

	oitem->querytype = qtype;
	oitem->devname = strdup(devname);
	oitem->requestset = setnumber;
	oitem->dsname = dsname;
	oitem->oidstr = strdup(oidstr);
	oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
	if (read_objid(oitem->oidstr, oitem->Oid, &oitem->OidLen)) {
		if (!reqitem->oidhead) reqitem->oidhead = oitem; else reqitem->oidtail->next = oitem;
		reqitem->oidtail = oitem;
	}
	else {
		/* Could not parse the OID definition */
		snmp_perror("read_objid");
		xfree(oitem->oidstr);
		xfree(oitem);
	}

	return oitem;
}


void readconfig(char *cfgfn)
{
	static void *cfgfiles = NULL;
	FILE *cfgfd;
	strbuffer_t *inbuf;

	struct req_t *reqitem = NULL;
	int setnumber = 0;
	int bbsleep = atoi(xgetenv("BBSLEEP"));

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
			char *intvl = strchr(bot, '/');

			/*
			 * See if we're running a non-standard interval.
			 * If yes, then process only the records that match
			 * this BBSLEEP setting.
			 */
			if (bbsleep != 300) {
				/* Non-default interval. Skip the host if it HASN'T got an interval setting */
				if (!intvl) continue;

				/* Also skip the hosts that have an interval different from the current */
				*intvl = '\0';	/* Clip the interval from the hostname */
				if (atoi(intvl+1) != bbsleep) continue;
			}
			else {
				/* Default interval. Skip the host if it HAS an interval setting */
				if (intvl) continue;
			}

			reqitem = (req_t *)calloc(1, sizeof(req_t));
			setnumber = 0;

			p = strchr(bot, ']'); if (p) *p = '\0';
			reqitem->hostname = strdup(bot + 1);
			if (p) *p = ']';

			reqitem->hostip[0] = reqitem->hostname;
			reqitem->version = SNMP_VERSION_1;
			reqitem->next = reqhead;
			reqhead = reqitem;

			continue;
		}

		/* If we have nowhere to put the data, then skip further processing */
		if (!reqitem) continue;

		if (strncmp(bot, "ip=", 3) == 0) {
			char *nextip = strtok(strdup(bot+3), ",");
			int i = 0;

			do {
				reqitem->hostip[i++] = nextip;
				nextip = strtok(NULL, ",");
			} while (nextip);
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

			setnumber++;

			idx = strtok(bot+5, " \t");
			if (idx) oid1 = strtok(NULL, " \t");
			if (oid1) oid2 = strtok(NULL, " \t");
			if (oid2) devname = strtok(NULL, "\r\n");

			if (idx && oid1 && oid2 && devname) {
				make_oitem(QUERY_MRTG, devname, setnumber, "ds1", oid1, reqitem);
				make_oitem(QUERY_MRTG, devname, setnumber, "ds2", oid2, reqitem);
			}

			reqitem->next_oid = reqitem->oidhead;
			continue;
		}

		if (strncmp(bot, "snmpmib", 7) == 0) {
			int i;

			for (i=0; (snmpmibnames[i].oid); i++) {
				make_oitem(QUERY_SNMPMIB, "-", 0, snmpmibnames[i].dsname, snmpmibnames[i].oid, reqitem);
			}

			reqitem->next_oid = reqitem->oidhead;
			continue;
		}

		if (strncmp(bot, "icmpmib", 7) == 0) {
			int i;

			for (i=0; (icmpmibnames[i].oid); i++) {
				make_oitem(QUERY_ICMPMIB, "-", 0, icmpmibnames[i].dsname, icmpmibnames[i].oid, reqitem);
			}

			reqitem->next_oid = reqitem->oidhead;
			continue;
		}

		if (strncmp(bot, "ifmib=", 6) == 0) {
			char *idx, *devname = NULL;
			int i;
			char oid[128];

			setnumber++;

			idx = strtok(bot+6, " \t");
			if (idx) devname = strtok(NULL, " \r\n");
			if (!devname) devname = idx;

			if ((*idx == '(') || (*idx == '[') || (*idx == '{') || (*idx == '<')) {
				/* Interface-by-name or interface-by-physaddr */
				wantedif_t *newitem = (wantedif_t *)malloc(sizeof(wantedif_t));
				switch (*idx) {
				  case '(': newitem->idtype = ID_DESCR; break;
				  case '[': newitem->idtype = ID_PHYSADDR; break;
				  case '{': newitem->idtype = ID_IPADDR; break;
				  case '<': newitem->idtype = ID_NAME; break;
				}
				p = idx + strcspn(idx, "])}>"); if (p) *p = '\0';
				newitem->id = strdup(idx+1);
				newitem->devname = strdup(devname);
				newitem->requestset = setnumber;
				newitem->next = reqitem->wantedinterfaces;
				reqitem->wantedinterfaces = newitem;
			}
			else {
				/* Plain numeric interface */
				char *devnamecopy = strdup(devname);

				for (i=0; (ifmibnames[i].oid); i++) {
					sprintf(oid, "%s.%s", ifmibnames[i].oid, idx);
					make_oitem(QUERY_IFMIB, devnamecopy, setnumber, ifmibnames[i].dsname, oid, reqitem);
				}

				setnumber++;
				for (i=0; (ifXmibnames[i].oid); i++) {
					sprintf(oid, "%s.%s", ifXmibnames[i].oid, idx);
					make_oitem(QUERY_IFMIB_X, devnamecopy, setnumber, ifXmibnames[i].dsname, oid, reqitem);
				}

				reqitem->next_oid = reqitem->oidhead;
			}
			continue;
		}

		if (strncmp(bot, "var=", 4) == 0) {
			char *dsname, *oid = NULL, *devname = NULL;

			setnumber++;

			dsname = strtok(bot+4, " \t");
			if (dsname) oid = strtok(NULL, " \t");
			if (oid) devname = strtok(NULL, " \r\n");

			if (dsname && oid) {
				make_oitem(QUERY_VAR, (devname ? devname : "-"), setnumber, dsname, oid, reqitem);
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
	dataoperation = SCAN_INTERFACES;
	starthosts(1);
	communicate();

	rootoidlen = sizeof(rootoid)/sizeof(rootoid[0]);
	read_objid(".1.3.6.1.2.1.4.20.1.2", rootoid, &rootoidlen);
	dataoperation = SCAN_IPADDRS;
	starthosts(1);
	communicate();

	rootoidlen = sizeof(rootoid)/sizeof(rootoid[0]);
	read_objid(".1.3.6.1.2.1.31.1.1.1.1", rootoid, &rootoidlen);
	dataoperation = SCAN_IFNAMES;
	starthosts(1);
	communicate();

	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		if (!rwalk->wantedinterfaces || !rwalk->interfacenames) continue;

		for (wantwalk = rwalk->wantedinterfaces; (wantwalk); wantwalk = wantwalk->next) {
			ifwalk = NULL;

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

			  case ID_NAME:
				for (ifwalk = rwalk->interfacenames; (ifwalk && (!ifwalk->name || strcmp(ifwalk->name, wantwalk->id))); ifwalk = ifwalk->next) ;
				break;
			}

			if (ifwalk) {
				int i;
				char oid[128];
				char *devnamecopy = strdup(wantwalk->devname);

				for (i=0; (ifmibnames[i].oid); i++) {
					sprintf(oid, "%s.%s", ifmibnames[i].oid, ifwalk->index);

					make_oitem(QUERY_IFMIB, devnamecopy, 
						   wantwalk->requestset, 
						   ifmibnames[i].dsname, oid, rwalk);
				}

				for (i=0; (ifXmibnames[i].oid); i++) {
					sprintf(oid, "%s.%s", ifXmibnames[i].oid, ifwalk->index);
					make_oitem(QUERY_IFMIB_X, devnamecopy, 
						   -wantwalk->requestset, /* Hack! To get a unique request set number */
						   ifXmibnames[i].dsname, oid, rwalk);
				}
				rwalk->next_oid = rwalk->oidhead;
			}
		}
	}
}


void getdata(void)
{
	dataoperation = GET_DATA;
	starthosts(1);
	communicate();
}


void sendresult(void)
{
	struct req_t *rwalk;
	struct oid_t *owalk;
	char msgline[4096];
	char currdev[1024];
	strbuffer_t *ifmibdata = newstrbuffer(0);
	strbuffer_t *snmpmibdata = newstrbuffer(0);
	strbuffer_t *icmpmibdata = newstrbuffer(0);
	int activestatus = 0;
	int haveifmibdata = 0;
	int havesnmpmibdata = 0;
	int haveicmpmibdata = 0;

	init_timestamp();

	*currdev = '\0';
	combo_start();

	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		clearstrbuffer(ifmibdata);
		clearstrbuffer(snmpmibdata);
		clearstrbuffer(icmpmibdata);

		sprintf(msgline, "status+%d %s.ifmib green %s\n", 
			2*atoi(xgetenv("BBSLEEP")), rwalk->hostname, timestamp);
		addtobuffer(ifmibdata, msgline);
		sprintf(msgline, "status+%d %s.snmpmib green %s\n", 
			2*atoi(xgetenv("BBSLEEP")), rwalk->hostname, timestamp);
		addtobuffer(snmpmibdata, msgline);
		sprintf(msgline, "status+%d %s.icmpmib green %s\n", 
			2*atoi(xgetenv("BBSLEEP")), rwalk->hostname, timestamp);
		addtobuffer(icmpmibdata, msgline);

		sprintf(msgline, "Interval=%d\n", atoi(xgetenv("BBSLEEP")));
		addtobuffer(ifmibdata, msgline);
		addtobuffer(snmpmibdata, msgline);
		addtobuffer(icmpmibdata, msgline);

		sprintf(msgline, "ActiveIP=%s\n", rwalk->hostip[rwalk->hostipidx]);
		addtobuffer(ifmibdata, msgline);
		addtobuffer(snmpmibdata, msgline);
		addtobuffer(icmpmibdata, msgline);


		for (owalk = rwalk->oidhead; (owalk); owalk = owalk->next) {
			if (strcmp(currdev, owalk->devname)) {
				if (activestatus) {
					finish_status();
					activestatus = 0;
				}

				strcpy(currdev, owalk->devname);

				switch (owalk->querytype) {
				  case QUERY_IFMIB:
				  case QUERY_IFMIB_X:
					haveifmibdata = 1;
					sprintf(msgline, "\n[%s]\n", owalk->devname);
					addtobuffer(ifmibdata, msgline);
					break;

				  case QUERY_MRTG:
					init_status(COL_GREEN); activestatus = 1;
					sprintf(msgline, "status %s.mrtg green\n", rwalk->hostname);
					addtostatus(msgline);
					sprintf(msgline, "\n[%s]\n", owalk->devname);
					addtostatus(msgline);
					break;

				  case QUERY_VAR:
					init_status(COL_GREEN); activestatus = 1;
					sprintf(msgline, "status %s.snmpvar green\n", rwalk->hostname);
					addtostatus(msgline);
					sprintf(msgline, "\n[%s]\n", owalk->devname);
					addtostatus(msgline);
					break;

				  default:
					break;
				}
			}

			sprintf(msgline, "\t%s = %s\n", 
				owalk->dsname, (owalk->result ? owalk->result : "NODATA"));

			switch (owalk->querytype) {
			  case QUERY_IFMIB:
			  case QUERY_IFMIB_X:
				addtobuffer(ifmibdata, msgline);
				break;

			  case QUERY_MRTG:
			  case QUERY_VAR:
				addtostatus(msgline);
				break;

			  case QUERY_SNMPMIB:
				havesnmpmibdata = 1;
				addtobuffer(snmpmibdata, msgline);
				break;

			  case QUERY_ICMPMIB:
				addtobuffer(icmpmibdata, msgline);
				haveicmpmibdata = 1;
				break;
			}
		}

		if (activestatus) finish_status();

		if (haveifmibdata) {
			init_status(COL_GREEN);
			addtostrstatus(ifmibdata);
			finish_status();
		}
		if (havesnmpmibdata) {
			init_status(COL_GREEN);
			addtostrstatus(snmpmibdata);
			finish_status();
		}
		if (haveicmpmibdata) {
			init_status(COL_GREEN);
			addtostrstatus(icmpmibdata);
			finish_status();
		}
	}

	combo_end();
	freestrbuffer(ifmibdata);
	freestrbuffer(snmpmibdata);
	freestrbuffer(icmpmibdata);
}

void egoresult(int color, char *egocolumn)
{
	char msgline[1024];
	char *timestamps = NULL;

	combo_start();
	init_status(color);
	sprintf(msgline, "status %s.%s %s %s\n\n", 
		xgetenv("MACHINE"), egocolumn, colorname(color), timestamp);
	addtostatus(msgline);

	sprintf(msgline, "Variables  : %d\n", varcount);
	addtostatus(msgline);
	sprintf(msgline, "PDUs       : %d\n", pducount);
	addtostatus(msgline);
	sprintf(msgline, "Responses  : %d\n", okcount);
	addtostatus(msgline);
	sprintf(msgline, "Timeouts   : %d\n", timeoutcount);
	addtostatus(msgline);
	sprintf(msgline, "Too big    : %d\n", toobigcount);
	addtostatus(msgline);
	sprintf(msgline, "Errors     : %d\n", errorcount);
	addtostatus(msgline);

	show_timestamps(&timestamps);
	if (timestamps) addtostatus(timestamps);

	finish_status();
	combo_end();

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
		else if (strcmp(argv[argi], "--no-update") == 0) {
			dontsendmessages = 1;
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
		else if (argnmatch(argv[argi], "--report=")) {
			char *p = strchr(argv[argi], '=');
			reportcolumn = strdup(p+1);
			timing = 1;
		}
		else if (*argv[argi] != '-') {
			configfn = argv[argi];
		}
	}

	add_timestamp("hobbit_snmpcollect startup");

	netsnmp_register_loghandler(NETSNMP_LOGHANDLER_STDERR, 7);
	init_snmp("hobbit_snmpcollect");
	snmp_out_toggle_options("vqs");	/* Like snmpget -Ovqs */

	readconfig(configfn);
	if (cfgcheck) return 0;
	add_timestamp("Configuration loaded");

	resolveifnames();
	add_timestamp("Interface names detected");

	getdata();
	stophosts();
	add_timestamp("Data retrieved");

	sendresult();
	add_timestamp("Results transmitted");

	if (reportcolumn) egoresult(COL_GREEN, reportcolumn);

	return 0;
}

