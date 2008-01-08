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

static char rcsid[] = "$Id: hobbit_snmpcollect.c,v 1.31 2008-01-08 11:58:37 henrik Exp $";

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "libbbgen.h"

/* -------------------  struct's used for the MIB definition  ------------------------ */
/* This holds one OID and our corresponding short-name */
typedef struct oidds_t {
	char *dsname;		/* Our short-name for the data item in the mib definition */
	char *oid;		/* The OID for the data in the mib definition */
} oidds_t;

/* This holds a list of OID's and their shortnames */
typedef struct oidset_t {
	int oidsz, oidcount;
	oidds_t *oids;
	struct oidset_t *next;
} oidset_t;

/* This describes the ways we can index into this MIB */
enum mibidxtype_t { 
	MIB_INDEX_IN_OID, 	/*
				 * The index is part of the key-table OID's; by scanning the key-table
				 * values we find the one matching the wanted key, and can extract the
				 * index from the matching rows' OID.
				 * E.g. interfaces table looking for ifDescr or ifPhysAddress, or
				 * interfaces table looking for the extension-object ifName.
				 *   IF-MIB::ifDescr.1 = STRING: lo
				 *   IF-MIB::ifDescr.2 = STRING: eth1
				 *   IF-MIB::ifPhysAddress.1 = STRING:
				 *   IF-MIB::ifPhysAddress.2 = STRING: 0:e:a6:ce:cf:7f
				 *   IF-MIB::ifName.1 = STRING: lo
				 *   IF-MIB::ifName.2 = STRING: eth1
				 * The key table has an entry with the value = key-value. The index
				 * is then the part of the key-OID beyond the base key table OID.
				 */

	MIB_INDEX_IN_VALUE 	/*
				 * Index can be found by adding the key value as a part-OID to the
				 * base OID of the key (e.g. interfaces by IP-address).
				 *   IP-MIB::ipAdEntIfIndex.127.0.0.1 = INTEGER: 1
				 *   IP-MIB::ipAdEntIfIndex.172.16.10.100 = INTEGER: 3
				 */
};

typedef struct mibidx_t {
	char marker;				/* Marker character for key OID */
	enum mibidxtype_t idxtype;		/* How to interpret the key */
	char *keyoid;				/* Key OID */
	oid rootoid[MAX_OID_LEN];		/* Binary representation of keyoid */
	unsigned int rootoidlen;		/* Length of binary keyoid */
	struct mibidx_t *next;
} mibidx_t;

typedef struct mibdef_t {
	char *mibname;				/* MIB definition name */
	oidset_t *oidlisthead, *oidlisttail;	/* The list of OID's in the MIB set */
	mibidx_t *idxlist;			/* List of the possible indices used for the MIB */
	int haveresult;				/* Used while building result messages */
	strbuffer_t *resultbuf;			/* Used while building result messages */
} mibdef_t;


/* -----------------  struct's used for the host/requests we need to do ---------------- */
/* List of the OID's we will request */
typedef struct oid_t {
	mibdef_t *mib;				/* pointer to the mib definition for mibs */
	oid Oid[MAX_OID_LEN];			/* the internal OID representation */
	unsigned int OidLen;			/* size of the oid */
	char *devname;				/* Users' chosen device name. May be a key (e.g "eth0") */
	char *dsname;				/* Points to the dsname in the oidds_t definition */
	int  setnumber;				/* All vars fetched in one PDU's have the same setnumber */
	char *result;				/* the printable result data */
	struct oid_t *next;
} oid_t;

/* Used for requests where we must determine the appropriate table index first */
typedef struct keyrecord_t {
	mibdef_t *mib;				/* Pointer to the mib definition */
	mibidx_t *indexmethod;			/* Pointer to the mib index definition */
	char *key;				/* The user-provided key we must find */
	char *indexoid;				/* Result: Index part of the OID */
	struct keyrecord_t *next;
} keyrecord_t;

/* A host and the OID's we will be polling */
typedef struct req_t {
	char *hostname;				/* Hostname used for reporting to Hobbit */
	char *hostip[10];			/* Hostname(s) or IP(s) used for testing. Max 10 IP's */
	int hostipidx;				/* Index into hostip[] for the active IP we use */
	u_short portnumber;			/* SNMP daemon portnumber */
	long version;				/* SNMP version to use */
	unsigned char *community;		/* Community name used to access the SNMP daemon */
	int setnumber;				/* Per-host setnumber used while building requests */
	struct snmp_session *sess;		/* SNMP session data */

	keyrecord_t *keyrecords, *currentkey;	/* For keyed requests: Key records */

	oid_t *oidhead, *oidtail;		/* List of the OID's we will fetch */
	oid_t *curr_oid, *next_oid;		/* Current- and next-OID pointers while fetching data */
	struct req_t *next;
} req_t;


/* Global variables */
RbtHandle mibdefs;				/* Holds the list of MIB definitions */
req_t *reqhead = NULL;				/* Holds the list of requests */
int active_requests = 0;			/* Number of active SNMP requests in flight */

/* dataoperation tracks what we are currently doing */
enum { 
	GET_KEYS,	/* Scan for the keys */
	GET_DATA 	/* Fetch the actual data */
} dataoperation;

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
	currentset = item->next_oid->setnumber;
	while (item->next_oid && (currentset == item->next_oid->setnumber)) {
		varcount++;
		snmp_add_null_var(req, item->next_oid->Oid, item->next_oid->OidLen);
		item->next_oid = item->next_oid->next;
	}

	return req;
}


/*
 * Store data received in response PDU
 */
int print_result (int status, req_t *req, struct snmp_pdu *pdu)
{
	struct variable_list *vp;
	char valstr[1024];
	char oidstr[1024];
	keyrecord_t *kwalk;
	int keyoidlen;
	oid_t *owalk;
	int done;

	switch (status) {
	  case STAT_SUCCESS:
		if (pdu->errstat == SNMP_ERR_NOERROR) {
			okcount++;

			switch (dataoperation) {
			  case GET_KEYS:
				/* 
				 * Find the keyrecord currently processed for this request, and
				 * look through the unresolved keys to see if we have a match.
				 * If we do, determine the index for data retrieval.
				 */
				vp = pdu->variables;
				snprint_value(valstr, sizeof(valstr), vp->name, vp->name_length, vp);
				snprint_objid(oidstr, sizeof(oidstr), vp->name, vp->name_length);
				for (kwalk = req->currentkey, done = 0; (kwalk && !done); kwalk = kwalk->next) {
					/* Skip records where we have the result already, or that are not keyed */
					if (kwalk->indexoid || (kwalk->indexmethod != req->currentkey->indexmethod)) {
						continue;
					}

					keyoidlen = strlen(req->currentkey->indexmethod->keyoid);

					switch (kwalk->indexmethod->idxtype) {
					  case MIB_INDEX_IN_OID:
						/* Does the key match the value we just got? */
						if (strcmp(valstr, kwalk->key) == 0) {
							/* Grab the index part of the OID */
							kwalk->indexoid = strdup(oidstr + keyoidlen + 1);
							done = 1;
						}
						break;

					  case MIB_INDEX_IN_VALUE:
						/* Does the key match the index-part of the result OID? */
						if ((*(oidstr+keyoidlen) == '.') && (strcmp(oidstr+keyoidlen+1, kwalk->key)) == 0) {
							/* Grab the index which is the value */
							kwalk->indexoid = strdup(valstr);
							done = 1;
						}
						break;
					}
				}
				break;

			  case GET_DATA:
				owalk = req->curr_oid;
				vp = pdu->variables;
				while (vp) {
					snprint_value(valstr, sizeof(valstr), vp->name, vp->name_length, vp);
					owalk->result = strdup(valstr); owalk = owalk->next;
					vp = vp->next_variable;
				}
				break;
			}
		}
		else {
			errorcount++;
			errprintf("ERROR %s: %s\n", req->hostip[req->hostipidx], snmp_errstring(pdu->errstat));
		}
		return 1;

	  case STAT_TIMEOUT:
		timeoutcount++;
		dbgprintf("%s: Timeout\n", req->hostip);
		if (req->hostip[req->hostipidx+1]) {
			req->hostipidx++;
			startonehost(req, 1);
		}
		return 0;

	  case STAT_ERROR:
		errorcount++;
		snmp_sess_perror(req->hostip[req->hostipidx], req->sess);
		return 0;
	}

	return 0;
}


/*
 * response handler
 */
int asynch_response(int operation, struct snmp_session *sp, int reqid, struct snmp_pdu *pdu, void *magic)
{
	struct req_t *req = (struct req_t *)magic;

	if (operation == NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE) {
		struct snmp_pdu *snmpreq = NULL;

		switch (pdu->errstat) {
		  case SNMP_ERR_NOERROR:
			/* Pick up the results */
			print_result(STAT_SUCCESS, req, pdu);
			break;

		  case SNMP_ERR_NOSUCHNAME:
			dbgprintf("Host %s item %s: No such name\n", req->hostname, req->curr_oid->devname);
			if (req->hostip[req->hostipidx+1]) {
				req->hostipidx++;
				startonehost(req, 1);
			}
			break;

		  case SNMP_ERR_TOOBIG:
			toobigcount++;
			errprintf("Host %s item %s: Response too big\n", req->hostname, req->curr_oid->devname);
			break;

		  default:
			errorcount++;
			errprintf("Host %s item %s: SNMP error %d\n",  req->hostname, req->curr_oid->devname, pdu->errstat);
			break;
		}

		/* Now see if we should send another request */
		switch (dataoperation) {
		  case GET_KEYS:
			/*
			 * While fetching keys, walk the current key-table until we reach the end of the table.
			 * When we reach the end of one key-table, start with the next.
			 * FIXME: Could optimize so we dont fetch the whole table, but only those rows we need.
			 */
			if (pdu->errstat == SNMP_ERR_NOERROR) {
				struct variable_list *vp = pdu->variables;

				if ( (vp->name_length >= req->currentkey->indexmethod->rootoidlen) && 
				     (memcmp(&req->currentkey->indexmethod->rootoid, vp->name, req->currentkey->indexmethod->rootoidlen * sizeof(oid)) == 0) ) {
					/* Still more data in the current key table, get the next row */
					snmpreq = snmp_pdu_create(SNMP_MSG_GETNEXT);
					pducount++;
					/* Probably only one variable to fetch, but never mind ... */
					while (vp) {
						varcount++;
						snmp_add_null_var(snmpreq, vp->name, vp->name_length);
						vp = vp->next_variable;
					}
				}
				else {
					/* End of current key table. If more keys to be found, start the next table. */
					do { 
						req->currentkey = req->currentkey->next;
					} while (req->currentkey && req->currentkey->indexoid);

					if (req->currentkey) {
						snmpreq = snmp_pdu_create(SNMP_MSG_GETNEXT);
						pducount++;
						snmp_add_null_var(snmpreq, 
								  req->currentkey->indexmethod->rootoid, 
								  req->currentkey->indexmethod->rootoidlen);
					}
				}
			}
			break;

		  case GET_DATA:
			/* Generate a request for the next dataset, if any */
			if (req->next_oid) {
				snmpreq = generate_datarequest(req);
			}
			else {
				dbgprintf("No more oids left\n");
			}
			break;
		}

		/* Send the request we just made */
		if (snmpreq) {
			if (snmp_send(req->sess, snmpreq))
				goto finish;
			else {
				snmp_sess_perror("snmp_send", req->sess);
				snmp_free_pdu(snmpreq);
			}
		}
	}
	else {
		dbgprintf("operation not succesful: %d\n", operation);
		print_result(STAT_TIMEOUT, req, pdu);
	}

	/* 
	 * Something went wrong (or end of variables).
	 * This host not active any more
	 */
	dbgprintf("Finished host %s\n", req->hostname);
	active_requests--;

finish:
	/* Start some more hosts */
	starthosts(0);

	return 1;
}


void startonehost(struct req_t *req, int ipchange)
{
	struct snmp_session s;
	struct snmp_pdu *snmpreq = NULL;

	if ((dataoperation < GET_DATA) && !req->currentkey) return;

	/* Are we retrying a cluster with a new IP? Then drop the current session */
	if (req->sess && ipchange) {
		/*
		 * Apparently, we cannot close a session while in a callback.
		 * So leave this for now - it will leak some memory, but
		 * this is not a problem as long as we only run once.
		 */
		/* snmp_close(req->sess); */
		req->sess = NULL;
	}

	/* Setup the SNMP session */
	if (!req->sess) {
		snmp_sess_init(&s);
		s.version = req->version;
		if (timeout > 0) s.timeout = timeout;
		if (retries > 0) s.retries = retries;
		s.peername = req->hostip[req->hostipidx];
		if (req->portnumber) s.remote_port = req->portnumber;
		s.community = req->community;
		s.community_len = strlen((char *)req->community);
		s.callback = asynch_response;
		s.callback_magic = req;
		if (!(req->sess = snmp_open(&s))) {
			snmp_sess_perror("snmp_open", &s);
			return;
		}
	}

	switch (dataoperation) {
	  case GET_KEYS:
		snmpreq = snmp_pdu_create(SNMP_MSG_GETNEXT);
		pducount++;
		snmp_add_null_var(snmpreq, req->currentkey->indexmethod->rootoid, req->currentkey->indexmethod->rootoidlen);
		break;

	  case GET_DATA:
		/* Build the request PDU and send it */
		snmpreq = generate_datarequest(req);
		break;
	}

	if (!snmpreq) return;

	if (snmp_send(req->sess, snmpreq))
		active_requests++;
	else {
		errorcount++;
		snmp_sess_perror("snmp_send", req->sess);
		snmp_free_pdu(snmpreq);
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
	char oidstr[1024];

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

		if (mib && ((strncmp(bot, "keyidx", 6) == 0) || (strncmp(bot, "validx", 6) == 0))) {
			/* 
			 * Define an index. Looks like:
			 *    keyidx (IF-MIB::ifDescr)
			 *    validx [IP-MIB::ipAdEntIfIndex]
			 */
			char endmarks[6];
			mibidx_t *newidx = (mibidx_t *)calloc(1, sizeof(mibidx_t));

			p = bot + 6; p += strspn(p, " \t");
			newidx->marker = *p; p++;
			newidx->idxtype = (strncmp(bot, "keyidx", 6) == 0) ? MIB_INDEX_IN_OID : MIB_INDEX_IN_VALUE;
			newidx->keyoid = p;
			sprintf(endmarks, "%s%c", ")]}>", newidx->marker);
			p = newidx->keyoid + strcspn(newidx->keyoid, endmarks); *p = '\0';
			newidx->rootoidlen = sizeof(newidx->rootoid) / sizeof(newidx->rootoid[0]);
			if (read_objid(newidx->keyoid, newidx->rootoid, &newidx->rootoidlen)) {
				snprint_objid(oidstr, sizeof(oidstr), newidx->rootoid, newidx->rootoidlen);
				newidx->keyoid = strdup(oidstr);
				newidx->next = mib->idxlist;
				mib->idxlist = newidx;
			}
			else {
				xfree(newidx);
			}

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

				mib->oidlisttail->oids[mib->oidlisttail->oidcount].dsname = strdup(name);
				mib->oidlisttail->oids[mib->oidlisttail->oidcount].oid = strdup(oid);
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
 *     mibname1[=index]
 *     mibname2[=index]
 *     mibname3[=index]
 *
 */

static oid_t *make_oitem(mibdef_t *mib, char *devname, char *dsname, char *oidstr, struct req_t *reqitem)
{
	oid_t *oitem = (oid_t *)calloc(1, sizeof(oid_t));

	oitem->mib = mib;
	oitem->devname = strdup(devname);
	oitem->setnumber = reqitem->setnumber;
	oitem->dsname = dsname;
	oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
	if (read_objid(oidstr, oitem->Oid, &oitem->OidLen)) {
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
		if ((*bot == '\0') || (*bot == '#')) continue;

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

		/* Custom mibs */
		p = bot + strcspn(bot, "= \t\r\n"); savech = *p; *p = '\0';
		mibhandle = rbtFind(mibdefs, bot);
		*p = savech; mibidx = p + strspn(p, "= \t\r\n");
		p = mibidx + strcspn(mibidx, "\r\n\t "); *p = '\0';
		if (mibhandle != rbtEnd(mibdefs)) {
			int i;
			mibdef_t *mib;
			mibidx_t *iwalk = NULL;
			char *oid, oidbuf[1024];
			char *devname;
			oidset_t *swalk;

			mib = (mibdef_t *)gettreeitem(mibdefs, mibhandle);

			/* See if this is an entry where we must determine the index ourselves */
			if (*mibidx) {
				for (iwalk = mib->idxlist; (iwalk && (*mibidx != iwalk->marker)); iwalk = iwalk->next) ;
			}

			if (!iwalk) {
				/* No key lookup */
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

						make_oitem(mib, devname, swalk->oids[i].dsname, oid, reqitem);
					}

					swalk = swalk->next;
				}

				reqitem->next_oid = reqitem->oidhead;
			}
			else {
				/* Add a key-record so we can try to locate the index */
				keyrecord_t *newitem = (keyrecord_t *)calloc(1, sizeof(keyrecord_t));
				char endmarks[6];

				mibidx++;
				sprintf(endmarks, "%s%c", ")]}>", iwalk->marker);
				p = mibidx + strcspn(mibidx, endmarks); *p = '\0';
				newitem->key = strdup(mibidx);
				newitem->indexmethod = iwalk;
				newitem->mib = mib;
				newitem->next = reqitem->keyrecords;
				reqitem->currentkey = reqitem->keyrecords = newitem;
			}

			continue;
		}
		else {
			errprintf("Unknown MIB (not in hobbit-snmpmibs.cfg): '%s'\n", bot);
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


void resolvekeys(void)
{
	req_t *rwalk;
	keyrecord_t *kwalk;
	oidset_t *swalk;
	int i;
	char oid[1024];

	/* Fetch the key data, and determine the indices we want to use */
	dataoperation = GET_KEYS;
	starthosts(1);
	communicate();

	/* Generate new requests for the datasets we now know the indices of */
	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		if (!rwalk->keyrecords) continue;

		for (kwalk = rwalk->keyrecords; (kwalk); kwalk = kwalk->next) {
			if (!kwalk->indexoid) {
				/* We failed to determine the index */
				errprintf("Could not determine index for host=%s mib=%s key=%s\n",
					  rwalk->hostname, kwalk->mib->mibname, kwalk->key);
				continue;
			}

			swalk = kwalk->mib->oidlisthead;
			while (swalk) {

				rwalk->setnumber++;

				for (i=0; (i <= swalk->oidcount); i++) {
					sprintf(oid, "%s.%s", swalk->oids[i].oid, kwalk->indexoid);
					make_oitem(kwalk->mib, kwalk->key, swalk->oids[i].dsname, oid, rwalk);
				}

				swalk = swalk->next;
			}

			rwalk->next_oid = rwalk->oidhead;
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
				strcpy(currdev, owalk->devname);

				if (*owalk->devname && (*owalk->devname != '-') ) {
					sprintf(msgline, "\n[%s]\n", owalk->devname);
					addtobuffer(owalk->mib->resultbuf, msgline);
				}
			}

			sprintf(msgline, "\t%s = %s\n", 
				owalk->dsname, (owalk->result ? owalk->result : "NODATA"));
			addtobuffer(owalk->mib->resultbuf, msgline);
			owalk->mib->haveresult = 1;
		}

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
	snmp_out_toggle_options("qn");	/* Like snmpget -Oqn: OID's printed as numbers, values printed without type */

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

	resolvekeys();
	add_timestamp("Keys lookup complete");

	getdata();
	stophosts();
	add_timestamp("Data retrieved");

	sendresult();
	add_timestamp("Results transmitted");

	if (reportcolumn) egoresult(COL_GREEN, reportcolumn);

	return 0;
}

