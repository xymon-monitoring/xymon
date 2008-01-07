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

static char rcsid[] = "$Id: hobbit_snmpcollect.c,v 1.28 2008-01-07 11:17:55 henrik Exp $";

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "libbbgen.h"

/* -------------------  struct's used for the MIB definition  ------------------------ */
/* This holds one OID and our corresponding short-name */
typedef struct oidds_t {
	char *oid;
	char *dsname;
} oidds_t;

/* This holds a list of OID's and their shortnames */
typedef struct oidset_t {
	int oidsz, oidcount;
	oidds_t *oids;
	struct oidset_t *next;
} oidset_t;

typedef struct mibdef_t {
	char *mibname;
	oidset_t *oidlisthead, *oidlisttail;
	strbuffer_t *resultbuf;
	int haveresult;
} mibdef_t;

RbtHandle mibdefs;


/* -----------------  struct's used for the host/requests we need to do ---------------- */
enum querytype_t { QUERY_VAR, QUERY_MRTG, QUERY_MIB };

/* List of the OID's we will request */
typedef struct oid_t {
	char *oidstr;				/* the input definition of the OID */
	enum querytype_t querytype;
	mibdef_t *mib;				/* pointer to the mib definition for custom mibs */
	oid Oid[MAX_OID_LEN];			/* the internal OID representation */
	unsigned int OidLen;			/* size of the oid */
	char *devname, *dsname;
	int  requestset;			/* Used to group vars into SNMP PDU's */
	char *result;				/* the printable result data */
	struct oid_t *next;
} oid_t;

typedef struct wantedif_t {
	enum { ID_DESCR, ID_PHYSADDR, ID_IPADDR, ID_NAME } idtype;
	char *id;
	char *devname;
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
	char *hostip[10];			/* Hostname(s) or IP(s) used for testing. Max 10 IP's */
	int hostipidx;
	u_short portnumber;			/* SNMP daemon portnumber */
	long version;				/* SNMP version to use */
	unsigned char *community;		/* Community name used to access the SNMP daemon */
	int setnumber;
	struct snmp_session *sess;		/* SNMP session data */
	wantedif_t *wantedinterfaces;		/* List of interfaces by description or phys. addr. we want */
	ifids_t *interfacenames;		/* List of interfaces, pulled from the host */
	struct oid_t *oidhead, *oidtail;	/* List of the OID's we will fetch */
	struct oid_t *curr_oid, *next_oid;	/* Current- and next-OID pointers while fetching data */
	struct req_t *next;
} req_t;

req_t *reqhead = NULL;

int active_requests = 0;
oid rootoid[MAX_OID_LEN];
unsigned int rootoidlen;

/* dataoperation tracks what we are currently doing */
enum { 	SCAN_INTERFACES, 	/* Scan what descriptions (IF-MIB::ifDescr) or MAC address 
				   (IF-MIB::ifPhysAddress) have been given to each interface */
	SCAN_IPADDRS, 		/* Scan what IP's (IP-MIB::ipAdEntIfIndex) have been given to each interface */
	SCAN_IFNAMES, 		/* Scan what names (IF-MIB::ifName) have been given to each interface */
	GET_DATA 		/* Fetch the actual data */
} dataoperation = GET_DATA;


/* Tuneables */
int max_pending_requests = 30;
int retries = 0;	/* Number of retries before timeout. 0 = Net-SNMP default (5). */
long timeout = 0;	/* Number of uS until first timeout, then exponential backoff. 0 = Net-SNMP default (1 second). */

/* Statistics */
char *reportcolumn = NULL;
int varcount = 0;
int pducount = 0;
int okcount = 0;
int toobigcount = 0;
int timeoutcount = 0;
int errorcount = 0;
struct timeval starttv, endtv;



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

	/* 
	 * Something went wrong (or end of variables).
	 * This host not active any more
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


void readmibs(char *cfgfn)
{
	static void *cfgfiles = NULL;
	FILE *cfgfd;
	strbuffer_t *inbuf;
	mibdef_t *mib = NULL;

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

		if ((*bot == '\0') || (*bot == '#')) continue;

		if (*bot == '[') {
			char *mibname;
			
			mibname = bot+1;
			p = strchr(mibname, ']'); if (p) *p = '\0';

			mib = (mibdef_t *)calloc(1, sizeof(mibdef_t));
			mib->mibname = strdup(mibname);
			mib->oidlisthead = mib->oidlisttail = (oidset_t *)calloc(1, sizeof(oidset_t));
			mib->oidlisttail->oidsz = 10;
			mib->oidlisttail->oidcount = -1;
			mib->oidlisttail->oids = (oidds_t *)malloc(mib->oidlisttail->oidsz*sizeof(oidds_t));
			mib->resultbuf = newstrbuffer(0);
			rbtInsert(mibdefs, mib->mibname, mib);

			continue;
		}

		if (mib && (strncmp(bot, "extra", 5) == 0)) {
			/* Add an extra set of MIB objects to retrieve separately */
			mib->oidlisttail->next = (oidset_t *)calloc(1, sizeof(oidset_t));
			mib->oidlisttail = mib->oidlisttail->next;
			mib->oidlisttail->oidsz = 10;
			mib->oidlisttail->oidcount = -1;
			mib->oidlisttail->oids = (oidds_t *)malloc(mib->oidlisttail->oidsz*sizeof(oidds_t));

			continue;
		}

		if (mib) {
			/* icmpInMsgs = IP-MIB::icmpInMsgs.0 */
			char oid[1024], name[1024];

			if (sscanf(bot, "%s = %s", name, oid) == 2) {
				mib->oidlisttail->oidcount++;

				if (mib->oidlisttail->oidcount == mib->oidlisttail->oidsz) {
					mib->oidlisttail->oidsz += 10;
					mib->oidlisttail->oids = (oidds_t *)realloc(mib->oidlisttail->oids, mib->oidlisttail->oidsz*sizeof(oidds_t));
				}

				mib->oidlisttail->oids[mib->oidlisttail->oidcount].oid = strdup(oid);
				mib->oidlisttail->oids[mib->oidlisttail->oidcount].dsname = strdup(name);
			}

			continue;
		}

		errprintf("Unknown MIB definition line: '%s'\n", bot);
	}

	stackfclose(cfgfd);
	freestrbuffer(inbuf);

	if (debug) {
		RbtIterator handle;

		for (handle = rbtBegin(mibdefs); (handle != rbtEnd(mibdefs)); handle = rbtNext(mibdefs, handle)) {
			mibdef_t *mib = (mibdef_t *)gettreeitem(mibdefs, handle);
			oidset_t *swalk;
			int i;

			dbgprintf("[%s]\n", mib->mibname);
			for (swalk = mib->oidlisthead; (swalk); swalk = swalk->next) {
				dbgprintf("\t*** OID set, %d entries ***\n", swalk->oidcount);
				for (i=0; (i <= swalk->oidcount); i++) {
					dbgprintf("\t\t%s = %s\n", swalk->oids[i].dsname, swalk->oids[i].oid);
				}
			}
		}
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

static oid_t *make_oitem(enum querytype_t qtype, mibdef_t *mib,
			 char *devname,
			 char *dsname, char *oidstr, 
			 struct req_t *reqitem)
{
	oid_t *oitem = (oid_t *)calloc(1, sizeof(oid_t));

	/* Note: Caller must ensure dsname and oidstr are long-lived (static or strdup'ed) */

	oitem->querytype = qtype;
	oitem->mib = mib;
	oitem->devname = strdup(devname);
	oitem->requestset = reqitem->setnumber;
	oitem->dsname = dsname;
	oitem->oidstr = oidstr;
	oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
	if (read_objid(oitem->oidstr, oitem->Oid, &oitem->OidLen)) {
		if (!reqitem->oidhead) reqitem->oidhead = oitem; else reqitem->oidtail->next = oitem;
		reqitem->oidtail = oitem;
	}
	else {
		/* Could not parse the OID definition */
		snmp_perror("read_objid");
		xfree(oitem->devname);
		xfree(oitem);
	}

	return oitem;
}


static void add_ifmib_request(char *idx, char *devname, struct req_t *reqitem)
{
	RbtIterator mibhandle;
	mibdef_t *mib;
	oidset_t *swalk;
	int i;
	char oid[128];

	mibhandle = rbtFind(mibdefs, "ifmib");
	mib = (mibdef_t *)gettreeitem(mibdefs, mibhandle);

	swalk = mib->oidlisthead;
	while (swalk) {
		reqitem->setnumber++;

		for (i=0; (i <= swalk->oidcount); i++) {
			sprintf(oid, "%s.%s", swalk->oids[i].oid, idx);
			make_oitem(QUERY_MIB, mib, devname,
				   swalk->oids[i].dsname, 
				   strdup(oid), 
				   reqitem);
		}

		swalk = swalk->next;
	}
}


void readconfig(char *cfgfn)
{
	static void *cfgfiles = NULL;
	FILE *cfgfd;
	strbuffer_t *inbuf;

	struct req_t *reqitem = NULL;
	int bbsleep = atoi(xgetenv("BBSLEEP"));

	RbtIterator mibhandle;

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
		char *bot, *p, *mibidx;
		char savech;

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

		if (strncmp(bot, "var=", 4) == 0) {
			char *dsname, *oid = NULL, *devname = NULL;

			reqitem->setnumber++;

			dsname = strtok(bot+4, " \t");
			if (dsname) oid = strtok(NULL, " \t");
			if (oid) devname = strtok(NULL, " \r\n");

			if (dsname && oid) {
				make_oitem(QUERY_VAR, NULL, (devname ? devname : "-"), strdup(dsname), strdup(oid), reqitem);
			}
			reqitem->next_oid = reqitem->oidhead;
			continue;
		}

		if (strncmp(bot, "mrtg=", 5) == 0) {
			char *idx, *oid1 = NULL, *oid2 = NULL, *devname = NULL;

			reqitem->setnumber++;

			idx = strtok(bot+5, " \t");
			if (idx) oid1 = strtok(NULL, " \t");
			if (oid1) oid2 = strtok(NULL, " \t");
			if (oid2) devname = strtok(NULL, "\r\n");

			if (idx && oid1 && oid2 && devname) {
				make_oitem(QUERY_MRTG, NULL, (devname ? devname : "-"), "ds1", strdup(oid1), reqitem);
				make_oitem(QUERY_MRTG, NULL, (devname ? devname : "-"), "ds2", strdup(oid2), reqitem);
			}

			reqitem->next_oid = reqitem->oidhead;
			continue;
		}

		if (strncmp(bot, "ifmib=", 6) == 0) {
			char *idx, *devname = NULL;

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

				/* If we're using the default devname, make sure to skip the marker */
				if (devname == idx) devname++;

				newitem->id = strdup(idx+1);
				newitem->devname = strdup(devname);
				newitem->next = reqitem->wantedinterfaces;
				reqitem->wantedinterfaces = newitem;
			}
			else {
				/* Plain numeric interface */
				add_ifmib_request(idx, devname, reqitem);
				reqitem->next_oid = reqitem->oidhead;
			}
			continue;
		}

		/* Custom mibs */
		p = bot + strcspn(bot, "= \t\r\n"); savech = *p; *p = '\0';
		mibhandle = rbtFind(mibdefs, bot);
		*p = savech; mibidx = p + strspn(p, "= \t\r\n");
		p = mibidx + strcspn(mibidx, "\r\n\t "); *p = '\0';
		if (mibhandle != rbtEnd(mibdefs)) {
			int i;
			mibdef_t *mib;
			char *oid, oidbuf[1024];
			char *devname;
			oidset_t *swalk;

			mib = (mibdef_t *)gettreeitem(mibdefs, mibhandle);

			swalk = mib->oidlisthead;
			while (swalk) {
				reqitem->setnumber++;

				for (i=0; (i <= swalk->oidcount); i++) {
					if (*mibidx) {
						sprintf(oidbuf, "%s.%s", swalk->oids[i].oid, mibidx);
						oid = oidbuf;
						devname = mibidx;
					}
					else {
						oid = swalk->oids[i].oid;
						devname = "-";
					}

					make_oitem(QUERY_MIB, mib, devname,
						   swalk->oids[i].dsname, 
						   strdup(oid),
						   reqitem);
				}

				swalk = swalk->next;
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
				add_ifmib_request(ifwalk->index, wantwalk->devname, rwalk);
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
	int activestatus = 0;
	RbtIterator handle;
	mibdef_t *mib;

	init_timestamp();

	combo_start();

	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		*currdev = '\0';

		for (handle = rbtBegin(mibdefs); (handle != rbtEnd(mibdefs)); handle = rbtNext(mibdefs, handle)) {
			mib = (mibdef_t *)gettreeitem(mibdefs, handle);

			clearstrbuffer(mib->resultbuf);
			mib->haveresult = 0;

			sprintf(msgline, "status+%d %s.%s green %s\n", 
				2*atoi(xgetenv("BBSLEEP")), rwalk->hostname, mib->mibname, timestamp);
			addtobuffer(mib->resultbuf, msgline);
			sprintf(msgline, "Interval=%d\n", atoi(xgetenv("BBSLEEP")));
			addtobuffer(mib->resultbuf, msgline);
			sprintf(msgline, "ActiveIP=%s\n", rwalk->hostip[rwalk->hostipidx]);
			addtobuffer(mib->resultbuf, msgline);
		}

		for (owalk = rwalk->oidhead; (owalk); owalk = owalk->next) {
			if (strcmp(currdev, owalk->devname)) {
				if (activestatus) {
					finish_status();
					activestatus = 0;
				}

				strcpy(currdev, owalk->devname);

				switch (owalk->querytype) {
				  case QUERY_VAR:
					init_status(COL_GREEN); activestatus = 1;
					sprintf(msgline, "status %s.snmpvar green\n", rwalk->hostname);
					addtostatus(msgline);
					sprintf(msgline, "\n[%s]\n", owalk->devname);
					addtostatus(msgline);
					break;

				  case QUERY_MRTG:
					init_status(COL_GREEN); activestatus = 1;
					sprintf(msgline, "status %s.mrtg green\n", rwalk->hostname);
					addtostatus(msgline);
					sprintf(msgline, "\n[%s]\n", owalk->devname);
					addtostatus(msgline);
					break;

				  case QUERY_MIB:
					if (*owalk->devname && (*owalk->devname != '-') ) {
						sprintf(msgline, "\n[%s]\n", owalk->devname);
						addtobuffer(owalk->mib->resultbuf, msgline);
					}
					break;
				}
			}

			sprintf(msgline, "\t%s = %s\n", 
				owalk->dsname, (owalk->result ? owalk->result : "NODATA"));

			switch (owalk->querytype) {
			  case QUERY_VAR:
			  case QUERY_MRTG:
				addtostatus(msgline);
				break;

			  case QUERY_MIB:
				owalk->mib->haveresult = 1;
				addtobuffer(owalk->mib->resultbuf, msgline);
				break;
			}
		}

		if (activestatus) finish_status();

		for (handle = rbtBegin(mibdefs); (handle != rbtEnd(mibdefs)); handle = rbtNext(mibdefs, handle)) {
			mib = (mibdef_t *)gettreeitem(mibdefs, handle);

			if (mib->haveresult) {
				init_status(COL_GREEN);
				addtostrstatus(mib->resultbuf);
				finish_status();
			}
		}
	}

	combo_end();
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
	char *mibfn = NULL;
	char *configfn = NULL;
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
			configfn = strdup(argv[argi]);
		}
	}

	add_timestamp("hobbit_snmpcollect startup");

	netsnmp_register_loghandler(NETSNMP_LOGHANDLER_STDERR, 7);
	init_snmp("hobbit_snmpcollect");
	snmp_out_toggle_options("vqs");	/* Like snmpget -Ovqs */

	if (mibfn == NULL) {
		mibfn = (char *)malloc(PATH_MAX);
		sprintf(mibfn, "%s/etc/hobbit-snmpmibs.cfg", xgetenv("BBHOME"));
	}
	mibdefs = rbtNew(name_compare);
	readmibs(mibfn);

	if (configfn == NULL) {
		configfn = (char *)malloc(PATH_MAX);
		sprintf(configfn, "%s/etc/hobbit-snmphosts.cfg", xgetenv("BBHOME"));
	}
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

