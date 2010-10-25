/*----------------------------------------------------------------------------*/
/* Xymon monitor SNMP data collection tool                                    */
/*                                                                            */
/* Copyright (C) 2007-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* Inspired by the asyncapp.c file from the "NET-SNMP demo", available from   */
/* the Net-SNMP website. This file carries the attribution                    */
/*         "Niels Baggesen (Niels.Baggesen@uni-c.dk), 1999."                  */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <limits.h>

#include "libbbgen.h"

/* -----------------  struct's used for the host/requests we need to do ---------------- */
/* List of the OID's we will request */
typedef struct oid_t {
	mibdef_t *mib;				/* pointer to the mib definition for mibs */
	oid Oid[MAX_OID_LEN];			/* the internal OID representation */
	unsigned int OidLen;			/* size of the oid */
	char *devname;				/* Users' chosen device name. May be a key (e.g "eth0") */
	oidds_t *oiddef;			/* Points to the oidds_t definition */
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
	char *hostname;				/* Hostname used for reporting to Xymon */
	char *hostip[10];			/* Hostname(s) or IP(s) used for testing. Max 10 IP's */
	int hostipidx;				/* Index into hostip[] for the active IP we use */
	long version;				/* SNMP version to use */
	unsigned char *community;		/* Community name used to access the SNMP daemon (v1, v2c) */
	unsigned char *username;		/* Username used to access the SNMP daemon (v3) */
	unsigned char *passphrase;		/* Passphrase used to access the SNMP daemon (v3) */
	enum { 
		SNMP_V3AUTH_MD5, 
		SNMP_V3AUTH_SHA1 
	} authmethod;				/* Authentication method (v3) */
	int setnumber;				/* Per-host setnumber used while building requests */
	struct snmp_session *sess;		/* SNMP session data */

	keyrecord_t *keyrecords, *currentkey;	/* For keyed requests: Key records */

	oid_t *oidhead, *oidtail;		/* List of the OID's we will fetch */
	oid_t *curr_oid, *next_oid;		/* Current- and next-OID pointers while fetching data */
	struct req_t *next;
} req_t;


/* Global variables */
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
	int len;
	keyrecord_t *kwalk;
	int keyoidlen;
	oid_t *owalk;
	int done;

	switch (status) {
	  case STAT_SUCCESS:
		if (pdu->errstat == SNMP_ERR_NOERROR) {
			unsigned char *valstr = NULL, *oidstr = NULL;
			int valsz = 0, oidsz = 0;

			okcount++;

			switch (dataoperation) {
			  case GET_KEYS:
				/* 
				 * Find the keyrecord currently processed for this request, and
				 * look through the unresolved keys to see if we have a match.
				 * If we do, determine the index for data retrieval.
				 */
				vp = pdu->variables;
				len = 0; sprint_realloc_value(&valstr, &valsz, &len, 1, vp->name, vp->name_length, vp);
				len = 0; sprint_realloc_objid(&oidstr, &oidsz, &len, 1, vp->name, vp->name_length);
				dbgprintf("Got key-oid '%s' = '%s'\n", oidstr, valstr);
				for (kwalk = req->currentkey, done = 0; (kwalk && !done); kwalk = kwalk->next) {
					/* Skip records where we have the result already, or that are not keyed */
					if (kwalk->indexoid || (kwalk->indexmethod != req->currentkey->indexmethod)) {
						continue;
					}

					keyoidlen = strlen(req->currentkey->indexmethod->keyoid);

					switch (kwalk->indexmethod->idxtype) {
					  case MIB_INDEX_IN_OID:
						/* Does the key match the value we just got? */
						if (*kwalk->key == '*') {
							/* Match all. Add an extra key-record at the end. */
							keyrecord_t *newkey;

							newkey = (keyrecord_t *)calloc(1, sizeof(keyrecord_t));
							memcpy(newkey, kwalk, sizeof(keyrecord_t));
							newkey->indexoid = strdup(oidstr + keyoidlen + 1);
							newkey->key = valstr; valstr = NULL;
							newkey->next = kwalk->next;
							kwalk->next = newkey;
							done = 1;
						}
						else if (strcmp(valstr, kwalk->key) == 0) {
							/* Grab the index part of the OID */
							kwalk->indexoid = strdup(oidstr + keyoidlen + 1);
							done = 1;
						}
						break;

					  case MIB_INDEX_IN_VALUE:
						/* Does the key match the index-part of the result OID? */
						if (*kwalk->key == '*') {
							/* Match all. Add an extra key-record at the end. */
							keyrecord_t *newkey;

							newkey = (keyrecord_t *)calloc(1, sizeof(keyrecord_t));
							memcpy(newkey, kwalk, sizeof(keyrecord_t));
							newkey->indexoid = valstr; valstr = NULL;
							newkey->key = strdup(oidstr + keyoidlen + 1);
							newkey->next = kwalk->next;
							kwalk->next = newkey;
							done = 1;
						}
						else if ((*(oidstr+keyoidlen) == '.') && (strcmp(oidstr+keyoidlen+1, kwalk->key)) == 0) {
							/* 
							 * Grab the index which is the value. 
							 * Avoid a strdup by grabbing the valstr pointer.
							 */
							kwalk->indexoid = valstr; valstr = NULL; valsz = 0;
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
					valsz = len = 0;
					sprint_realloc_value((unsigned char **)&owalk->result, &valsz, &len, 1, 
							     vp->name, vp->name_length, vp);
					owalk = owalk->next; vp = vp->next_variable;
				}
				break;
			}

			if (valstr) xfree(valstr);
			if (oidstr) xfree(oidstr);
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
		int okoid = 1;

		if (dataoperation == GET_KEYS) {
			/* 
			 * We're doing GETNEXT's when retrieving keys, so we will get a response
			 * which has nothing really to do with the data we're looking for. In that
			 * case, we should NOT process data from this response.
			 */
			struct variable_list *vp = pdu->variables;

			okoid = ((vp->name_length >= req->currentkey->indexmethod->rootoidlen) && 
			         (memcmp(req->currentkey->indexmethod->rootoid, vp->name, req->currentkey->indexmethod->rootoidlen * sizeof(oid)) == 0));
		}

		switch (pdu->errstat) {
		  case SNMP_ERR_NOERROR:
			/* Pick up the results, but only if the OID is valid */
			if (okoid) print_result(STAT_SUCCESS, req, pdu);
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
				     (memcmp(req->currentkey->indexmethod->rootoid, vp->name, req->currentkey->indexmethod->rootoidlen * sizeof(oid)) == 0) ) {
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

		/* 
		 * snmp_session has a "remote_port" field, but it does not work.
		 * Instead, the peername should include a port number (IP:PORT)
		 * if (req->portnumber) s.remote_port = req->portnumber;
		 */
		s.peername = req->hostip[req->hostipidx];

		/* Set the SNMP version and authentication token(s) */
		s.version = req->version;
		switch (s.version) {
		  case SNMP_VERSION_1:
		  case SNMP_VERSION_2c:
			s.community = req->community;
			s.community_len = strlen((char *)req->community);
			break;

		  case SNMP_VERSION_3:
			/* set the SNMPv3 user name */
			s.securityName = strdup(req->username);
			s.securityNameLen = strlen(s.securityName);

			/* set the security level to authenticated, but not encrypted */
			s.securityLevel = SNMP_SEC_LEVEL_AUTHNOPRIV;

			/* set the authentication method */
			switch (req->authmethod) {
			  case SNMP_V3AUTH_MD5:
				s.securityAuthProto = usmHMACMD5AuthProtocol;
				s.securityAuthProtoLen = sizeof(usmHMACMD5AuthProtocol)/sizeof(oid);
				s.securityAuthKeyLen = USM_AUTH_KU_LEN;
				break;

			  case SNMP_V3AUTH_SHA1:
				s.securityAuthProto = usmHMACSHA1AuthProtocol;
				s.securityAuthProtoLen = sizeof(usmHMACSHA1AuthProtocol)/sizeof(oid);
				s.securityAuthKeyLen = USM_AUTH_KU_LEN;
				break;
			}

			/*
			 * set the authentication key to a hashed version of our
			 * passphrase (which must be at least 8 characters long).
			 */
			if (generate_Ku(s.securityAuthProto, s.securityAuthProtoLen,
					(u_char *)req->passphrase, strlen(req->passphrase),
					s.securityAuthKey, &s.securityAuthKeyLen) != SNMPERR_SUCCESS) {
				errprintf("Failed to generate Ku from authentication pass phrase for host %s\n",
					  req->hostname);
				snmp_perror("generate_Ku");
				return;
			}
			break;
		}

		/* Set timeouts and retries */
		if (timeout > 0) s.timeout = timeout;
		if (retries > 0) s.retries = retries;

		/* Setup the callback */
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
		if (rwalk->sess) {
			snmp_close(rwalk->sess);
		}
	}
}


/* 
 * This routine loads MIB files, and computes the key-OID values.
 * We defer this until the mib-definition is actually being referred to 
 * in snmphosts.cfg, because lots of MIB's are not being used
 * (and probably do not exist on the host where we're running) and
 * to avoid spending a lot of time to load MIB's that are not used.
 */
void setupmib(mibdef_t *mib, int verbose)
{
	mibidx_t *iwalk;
	int sz, len;

	if (mib->loadstatus != MIB_STATUS_NOTLOADED) return;

	if (mib->mibfn && (read_mib(mib->mibfn) == NULL)) {
		mib->loadstatus = MIB_STATUS_LOADFAILED;
		if (verbose) {
			errprintf("Failed to read MIB file %s\n", mib->mibfn);
			snmp_perror("read_objid");
		}
	}

	for (iwalk = mib->idxlist; (iwalk); iwalk = iwalk->next) {
		iwalk->rootoid = calloc(MAX_OID_LEN, sizeof(oid));
		iwalk->rootoidlen = MAX_OID_LEN;
		if (read_objid(iwalk->keyoid, iwalk->rootoid, &iwalk->rootoidlen)) {
			/* Re-use the iwalk->keyoid buffer */
			sz = strlen(iwalk->keyoid) + 1; len = 0;
			sprint_realloc_objid((unsigned char **)&iwalk->keyoid, &sz, &len, 1, iwalk->rootoid, iwalk->rootoidlen);
		}
		else {
			mib->loadstatus = MIB_STATUS_LOADFAILED;
			if (verbose) {
				errprintf("Cannot determine OID for %s\n", iwalk->keyoid);
				snmp_perror("read_objid");
			}
		}
	}

	mib->loadstatus = MIB_STATUS_LOADED;
}

/*
 *
 * Config file syntax
 *
 * [HOSTNAME]
 *     ip=ADDRESS[:PORT]
 *     version=VERSION
 *     community=COMMUNITY
 *     username=USERNAME
 *     passphrase=PASSPHRASE
 *     authmethod=[MD5|SHA1]
 *     mibname1[=index]
 *     mibname2[=index]
 *     mibname3[=index]
 *
 */

static oid_t *make_oitem(mibdef_t *mib, char *devname, oidds_t *oiddef, char *oidstr, struct req_t *reqitem)
{
	oid_t *oitem = (oid_t *)calloc(1, sizeof(oid_t));

	oitem->mib = mib;
	oitem->devname = strdup(devname);
	oitem->setnumber = reqitem->setnumber;
	oitem->oiddef = oiddef;
	oitem->OidLen = sizeof(oitem->Oid)/sizeof(oitem->Oid[0]);
	if (read_objid(oidstr, oitem->Oid, &oitem->OidLen)) {
		if (!reqitem->oidhead) reqitem->oidhead = oitem; else reqitem->oidtail->next = oitem;
		reqitem->oidtail = oitem;
	}
	else {
		/* Could not parse the OID definition */
		errprintf("Cannot determine OID for %s\n", oidstr);
		snmp_perror("read_objid");
		xfree(oitem->devname);
		xfree(oitem);
	}

	return oitem;
}


void readconfig(char *cfgfn, int verbose)
{
	static void *cfgfiles = NULL;
	FILE *cfgfd;
	strbuffer_t *inbuf;

	struct req_t *reqitem = NULL;
	int bbsleep = atoi(xgetenv("BBSLEEP"));

	mibdef_t *mib;

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
			reqitem->authmethod = SNMP_V3AUTH_MD5;
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

		if (strncmp(bot, "username=", 9) == 0) {
			reqitem->username = strdup(bot+9);
			continue;
		}

		if (strncmp(bot, "passphrase=", 10) == 0) {
			reqitem->passphrase = strdup(bot+10);
			continue;
		}

		if (strncmp(bot, "authmethod=", 10) == 0) {
			if (strcasecmp(bot+10, "md5") == 0)
				reqitem->authmethod = SNMP_V3AUTH_MD5;
			else if (strcasecmp(bot+10, "sha1") == 0)
				reqitem->authmethod = SNMP_V3AUTH_SHA1;
			else
				errprintf("Unknown SNMPv3 authentication method '%s'\n", bot+10);

			continue;
		}

		/* Custom mibs */
		p = bot + strcspn(bot, "= \t\r\n"); savech = *p; *p = '\0';
		mib = find_mib(bot);
		*p = savech; 
		p += strspn(p, "= \t");
		mibidx = p;
		if (mib) {
			int i;
			mibidx_t *iwalk = NULL;
			char *oid, *oidbuf;
			char *devname;
			oidset_t *swalk;

			setupmib(mib, verbose);
			if (mib->loadstatus != MIB_STATUS_LOADED) continue;	/* Cannot use this MIB */

			/* See if this is an entry where we must determine the index ourselves */
			if (*mibidx) {
				for (iwalk = mib->idxlist; (iwalk && (*mibidx != iwalk->marker)); iwalk = iwalk->next) ;
			}

			if ((*mibidx == '*') && !iwalk) {
				errprintf("Cannot do wildcard matching without an index (host %s, mib %s)\n",
					  reqitem->hostname, mib->mibname);
				continue;
			}

			if (!iwalk) {
				/* No key lookup */
				swalk = mib->oidlisthead;
				while (swalk) {
					reqitem->setnumber++;

					for (i=0; (i <= swalk->oidcount); i++) {
						if (*mibidx) {
							oid = oidbuf = (char *)malloc(strlen(swalk->oids[i].oid) + strlen(mibidx) + 2);
							sprintf(oidbuf, "%s.%s", swalk->oids[i].oid, mibidx);
							devname = mibidx;
						}
						else {
							oid = swalk->oids[i].oid;
							oidbuf = NULL;
							devname = "-";
						}

						make_oitem(mib, devname, &swalk->oids[i], oid, reqitem);
						if (oidbuf) xfree(oidbuf);
					}

					swalk = swalk->next;
				}

				reqitem->next_oid = reqitem->oidhead;
			}
			else {
				/* Add a key-record so we can try to locate the index */
				keyrecord_t *newitem = (keyrecord_t *)calloc(1, sizeof(keyrecord_t));
				char endmarks[6];

				mibidx++;	/* Skip the key-marker */
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
			errprintf("Unknown MIB (not in snmpmibs.cfg): '%s'\n", bot);
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
	char *oid;

	/* Fetch the key data, and determine the indices we want to use */
	dataoperation = GET_KEYS;
	starthosts(1);
	communicate();

	/* Generate new requests for the datasets we now know the indices of */
	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		if (!rwalk->keyrecords) continue;

		for (kwalk = rwalk->keyrecords; (kwalk); kwalk = kwalk->next) {
			if (!kwalk->indexoid) {
				/* Dont report failed lookups for the pseudo match-all key record */
				if (*kwalk->key != '*') {
					/* We failed to determine the index */
					errprintf("Could not determine index for host=%s mib=%s key=%s\n",
						  rwalk->hostname, kwalk->mib->mibname, kwalk->key);
				}
				continue;
			}

			swalk = kwalk->mib->oidlisthead;
			while (swalk) {

				rwalk->setnumber++;

				for (i=0; (i <= swalk->oidcount); i++) {
					oid = (char *)malloc(strlen(swalk->oids[i].oid) + strlen(kwalk->indexoid) + 2);
					sprintf(oid, "%s.%s", swalk->oids[i].oid, kwalk->indexoid);
					make_oitem(kwalk->mib, kwalk->key, &swalk->oids[i], oid, rwalk);
					xfree(oid);
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
	char msgline[1024];
	char *currdev, *currhost;
	mibdef_t *mib;
	strbuffer_t *clientmsg = newstrbuffer(0);
	int havemsg = 0;
	int itemcount = 0;

	currhost = "";
	for (rwalk = reqhead; (rwalk); rwalk = rwalk->next) {
		if (strcmp(rwalk->hostname, currhost) != 0) {
			/* Flush buffer */
			if (havemsg) {
				sprintf(msgline, "\n.<!-- linecount=%d -->\n", itemcount);
				addtobuffer(clientmsg, msgline);
				sendmessage(STRBUF(clientmsg), NULL, BBTALK_TIMEOUT, NULL);
			}
			clearstrbuffer(clientmsg);
			havemsg = 0;
			itemcount = 0;

			sprintf(msgline, "client/snmpcollect %s.snmpcollect snmp\n\n", rwalk->hostname);
			addtobuffer(clientmsg, msgline);
		}

		currdev = "";

		for (mib = first_mib(); (mib); mib = next_mib()) {
			clearstrbuffer(mib->resultbuf);
			mib->haveresult = 0;

			sprintf(msgline, "\n[%s]\nInterval=%d\nActiveIP=%s\n\n",
				mib->mibname,
				atoi(xgetenv("BBSLEEP")),
				rwalk->hostip[rwalk->hostipidx]);
			addtobuffer(mib->resultbuf, msgline);
		}

		for (owalk = rwalk->oidhead; (owalk); owalk = owalk->next) {
			if (strcmp(currdev, owalk->devname)) {
				currdev = owalk->devname;	/* OK, because ->devname is permanent */

				if (*owalk->devname && (*owalk->devname != '-') ) {
					addtobuffer(owalk->mib->resultbuf, "\n<");
					addtobuffer(owalk->mib->resultbuf, owalk->devname);
					addtobuffer(owalk->mib->resultbuf, ">\n");
					itemcount++;
				}
			}

			addtobuffer(owalk->mib->resultbuf, "\t");
			addtobuffer(owalk->mib->resultbuf, owalk->oiddef->dsname);
			addtobuffer(owalk->mib->resultbuf, " = ");
			if (owalk->result) {
				int ival;
				unsigned int uval;

				switch (owalk->oiddef->conversion) {
				  case OID_CONVERT_U32:
					ival = atoi(owalk->result);
					memcpy(&uval, &ival, sizeof(uval));
					sprintf(msgline, "%u", uval);
					addtobuffer(owalk->mib->resultbuf, msgline);
					break;

				  default:
					addtobuffer(owalk->mib->resultbuf, owalk->result);
					break;
				}
			}
			else
				addtobuffer(owalk->mib->resultbuf, "NODATA");
			addtobuffer(owalk->mib->resultbuf, "\n");
			owalk->mib->haveresult = 1;
		}

		for (mib = first_mib(); (mib); mib = next_mib()) {
			if (mib->haveresult) {
				addtostrbuffer(clientmsg, mib->resultbuf);
				havemsg = 1;
			}
		}
	}

	if (havemsg) {
		sendmessage(STRBUF(clientmsg), NULL, BBTALK_TIMEOUT, NULL);
	}
	
	freestrbuffer(clientmsg);
}

void egoresult(int color, char *egocolumn)
{
	char msgline[1024];
	char *timestamps = NULL;

	init_timestamp();

	combo_start();
	init_status(color);
	sprintf(msgline, "status %s.%s %s snmpcollect %s\n\n", 
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
	if (timestamps) {
		addtostatus(timestamps);
		xfree(timestamps);
	}

	finish_status();
	combo_end();

}


int main (int argc, char **argv)
{
	int argi;
	char *configfn = NULL;
	int cfgcheck = 0;
	int mibcheck = 0;

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
		else if (strcmp(argv[argi], "--mibcheck") == 0) {
			mibcheck = 1;
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

	add_timestamp("xymon-snmpcollect startup");

	netsnmp_register_loghandler(NETSNMP_LOGHANDLER_STDERR, 7);
	init_snmp("xymon-snmpcollect");
	snmp_mib_toggle_options("e");	/* Like -Pe: Dont show MIB parsing errors */
	snmp_out_toggle_options("qn");	/* Like -Oqn: OID's printed as numbers, values printed without type */

	readmibs(NULL, mibcheck);

	if (configfn == NULL) {
		configfn = (char *)malloc(PATH_MAX);
		sprintf(configfn, "%s/etc/snmphosts.cfg", xgetenv("BBHOME"));
	}
	readconfig(configfn, mibcheck);
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

	xfree(configfn);

	return 0;
}

