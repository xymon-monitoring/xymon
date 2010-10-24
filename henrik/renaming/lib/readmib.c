/*----------------------------------------------------------------------------*/
/* Xymon monitor SNMP data collection tool                                    */
/*                                                                            */
/* Copyright (C) 2007-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <string.h>
#include <stdlib.h>

#include "libbbgen.h"

static RbtHandle mibdefs;				/* Holds the list of MIB definitions */
static RbtIterator nexthandle;

int readmibs(char *cfgfn, int verbose)
{
	static char *fn = NULL;
	static void *cfgfiles = NULL;
	FILE *cfgfd;
	strbuffer_t *inbuf;
	mibdef_t *mib = NULL;

	/* Check if config was modified */
	if (cfgfiles) {
		if (!stackfmodified(cfgfiles)) {
			dbgprintf("No files changed, skipping reload\n");
			return 0;
		}
		else {
			RbtIterator handle;

			errprintf("Re-loading MIBs\n");

			/* Clear list of config files */
			stackfclist(&cfgfiles);
			cfgfiles = NULL;

			/* Drop the current data */
			for (handle = rbtBegin(mibdefs); (handle != rbtEnd(mibdefs)); handle = rbtNext(mibdefs, handle)) {
				mibdef_t *mib = (mibdef_t *)gettreeitem(mibdefs, handle);
				oidset_t *swalk, *szombie;
				mibidx_t *iwalk, *izombie;
				int i;

				swalk = mib->oidlisthead;
				while (swalk) {
					szombie = swalk;
					swalk = swalk->next;

					for (i=0; (i <= szombie->oidcount); i++) {
						xfree(szombie->oids[i].dsname);
						xfree(szombie->oids[i].oid);
					}
					xfree(szombie->oids);
					xfree(szombie);
				}

				iwalk = mib->idxlist;
				while (iwalk) {
					izombie = iwalk;
					iwalk = iwalk->next;

					if (izombie->keyoid) xfree(izombie->keyoid);
					if (izombie->rootoid) xfree(izombie->rootoid);
					xfree(izombie);
				}

				if (mib->mibfn) xfree(mib->mibfn);
				if (mib->mibname) xfree(mib->mibname);
				freestrbuffer(mib->resultbuf);
				xfree(mib);
			}

			rbtDelete(mibdefs);
		}
	}

	mibdefs = rbtNew(name_compare);
	nexthandle = rbtEnd(mibdefs);

	if (fn) xfree(fn);
	fn = cfgfn;
	if (!fn) {
		fn = (char *)malloc(strlen(xgetenv("BBHOME")) + strlen("/etc/snmpmibs.cfg") + 1);
		sprintf(fn, "%s/etc/snmpmibs.cfg", xgetenv("BBHOME"));
	}

	cfgfd = stackfopen(fn, "r", &cfgfiles);
	if (cfgfd == NULL) {
		errprintf("Cannot open configuration files %s\n", fn);
		return 0;
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
			mib->tabular = 0;
			rbtInsert(mibdefs, mib->mibname, mib);

			continue;
		}

		if (mib && (strncmp(bot, "mibfile", 7) == 0)) {
			p = bot + 7; p += strspn(p, " \t");
			mib->mibfn = strdup(p);

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

		if (mib && (strncmp(bot, "table", 5) == 0)) {
			mib->tabular = 1;
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
			newidx->keyoid = strdup(p);
			sprintf(endmarks, "%s%c", ")]}>", newidx->marker);
			p = newidx->keyoid + strcspn(newidx->keyoid, endmarks); *p = '\0';
			newidx->next = mib->idxlist;
			mib->idxlist = newidx;
			mib->tabular = 1;

			continue;
		}

		if (mib) {
			/* icmpInMsgs = IP-MIB::icmpInMsgs.0 [/u32] [/rrd:TYPE] */
			char *tok, *name, *oid = NULL;

			name = strtok(bot, " \t");
			if (name) tok = strtok(NULL, " \t");
			if (tok && (*tok == '=')) oid = strtok(NULL, " \t"); else oid = tok;

			if (name && oid) {
				mib->oidlisttail->oidcount++;

				if (mib->oidlisttail->oidcount == mib->oidlisttail->oidsz) {
					mib->oidlisttail->oidsz += 10;
					mib->oidlisttail->oids = (oidds_t *)realloc(mib->oidlisttail->oids, mib->oidlisttail->oidsz*sizeof(oidds_t));
				}

				mib->oidlisttail->oids[mib->oidlisttail->oidcount].dsname = strdup(name);
				mib->oidlisttail->oids[mib->oidlisttail->oidcount].oid = strdup(oid);
				mib->oidlisttail->oids[mib->oidlisttail->oidcount].conversion = OID_CONVERT_NONE;
				mib->oidlisttail->oids[mib->oidlisttail->oidcount].rrdtype = RRD_NOTRACK;

				tok = strtok(NULL, " \t");
				while (tok) {
					if (strcasecmp(tok, "/u32") == 0) {
						mib->oidlisttail->oids[mib->oidlisttail->oidcount].conversion = OID_CONVERT_U32;
					}
					else if (strncasecmp(tok, "/rrd:", 5) == 0) {
						char *rrdtype = tok+5;

						if (strcasecmp(rrdtype, "COUNTER") == 0)
							mib->oidlisttail->oids[mib->oidlisttail->oidcount].rrdtype = RRD_TRACK_COUNTER;
						else if (strcasecmp(rrdtype, "GAUGE") == 0)
							 mib->oidlisttail->oids[mib->oidlisttail->oidcount].rrdtype = RRD_TRACK_GAUGE;
						else if (strcasecmp(rrdtype, "DERIVE") == 0)
							 mib->oidlisttail->oids[mib->oidlisttail->oidcount].rrdtype = RRD_TRACK_DERIVE;
						else if (strcasecmp(rrdtype, "ABSOLUTE") == 0)
							 mib->oidlisttail->oids[mib->oidlisttail->oidcount].rrdtype = RRD_TRACK_ABSOLUTE;
					}

					tok = strtok(NULL, " \t");
				}
			}

			continue;
		}

		if (verbose) {
			errprintf("Unknown MIB definition line: '%s'\n", bot);
		}
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

	return 1;
}

mibdef_t *first_mib(void)
{
	nexthandle = rbtBegin(mibdefs);

	if (nexthandle == rbtEnd(mibdefs))
		return NULL;
	else
		return (mibdef_t *)gettreeitem(mibdefs, nexthandle);
}

mibdef_t *next_mib(void)
{
	nexthandle = rbtNext(mibdefs, nexthandle);

	if (nexthandle == rbtEnd(mibdefs))
		return NULL;
	else
		return (mibdef_t *)gettreeitem(mibdefs, nexthandle);
}

mibdef_t *find_mib(char *mibname)
{
	RbtIterator handle;

	handle = rbtFind(mibdefs, mibname);
	if (handle == rbtEnd(mibdefs)) 
		return NULL;
	else
		return (mibdef_t *)gettreeitem(mibdefs, handle);
}


