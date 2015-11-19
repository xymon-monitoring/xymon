/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the host       */
/* configuration from xymond, for either a single host or all hosts.          */
/*                                                                            */
/* Copyright (C) 2011-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid_net[] = "$Id: loadhosts_file.c 6745 2011-09-04 06:01:06Z storner $";

static char *hivalhost = NULL;
static char *hivals[XMH_LAST] = { NULL, };
static char *hivalbuf = NULL;
static namelist_t hival_hostinfo;	/* Used as token for userspace. Also holds raw data in "elems" */

int load_hostinfo(char *targethost)
{
	sendreturn_t *sres;
	sendresult_t sendstat;
	char *msg, *bol, *eoln, *key, *val;
	int elemsize = 0;

	xmh_item_list_setup();

	if (hivalhost) {
		xfree(hivalhost);
		hivalhost = NULL;
	}
	if (hivalbuf) {
		xfree(hivalbuf); hivalbuf = NULL;
		xfree(hival_hostinfo.elems);
	}

	if (!targethost) return -1;

	msg = (char *)malloc(200 + strlen(targethost));
	sprintf(msg, "hostinfo clone=%s", targethost);

	sres = newsendreturnbuf(1, NULL);
	sendstat = sendmessage(msg, NULL, XYMON_TIMEOUT, sres);
	xfree(msg);
	if (sendstat != XYMONSEND_OK) {
		errprintf("Cannot load info for host '%s': %s\n", targethost, strxymonsendresult(sendstat));
		freesendreturnbuf(sres);
		return -1;
	}

	hivalbuf = getsendreturnstr(sres, 1);
	if (strlen(hivalbuf) == 0) {
		errprintf("No such host '%s'\n", targethost);
		return -2;
	}

	hivalhost = strdup(targethost);
	memset(hivals, 0, sizeof(hivals));
	memset(&hival_hostinfo, 0, sizeof(hival_hostinfo));
	hival_hostinfo.elems = (char **)calloc(1, sizeof(char *));

	bol = hivalbuf;
	while (bol && *bol) {
		int idx;

		/* 
		 * The "clone" output is multiline: 
		 * Lines beginning with XMH_ are the item-values, 
		 * all others are elem entries.
		 */

		eoln = strchr(bol, '\n');
		if (eoln) *eoln = '\0';

		key = bol;
		if (strncmp(key, "XMH_", 4) == 0) {
			val = strchr(bol, ':'); if (val) { *val = '\0'; val++; }
			idx = xmh_key_idx(key);
			if ((idx >= 0) && (idx < XMH_LAST)) hivals[idx] = val;
		}
		else {
			elemsize++;
			hival_hostinfo.elems = (char **)realloc(hival_hostinfo.elems, (elemsize+1)*sizeof(char *));
			hival_hostinfo.elems[elemsize-1] = bol;
		}

		bol = (eoln ? eoln+1 : NULL);
	}
	hival_hostinfo.elems[elemsize] = NULL;

	hival_hostinfo.hostname = hivals[XMH_HOSTNAME];
	hival_hostinfo.ip = strdup(hivals[XMH_IP] ? hivals[XMH_IP] : "");

	return 0;
}

