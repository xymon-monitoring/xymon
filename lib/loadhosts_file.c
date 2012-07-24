/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the hosts.cfg  */
/* file and keeping track of what hosts are known, their aliases and planned  */
/* downtime settings etc.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid_file[] = "$Id$";

static int get_page_name_title(char *buf, char *key, char **name, char **title)
{
	*name = *title = NULL;

	*name = buf + strlen(key); *name += strspn(*name, " \t\r\n");
	if (strlen(*name) > 0) {
		/* (*name) now points at the start of the name. Find end of name */
		*title = *name;
		*title += strcspn(*title, " \t\r\n");
		/* Null-terminate the name */
		**title = '\0'; (*title)++;

		*title += strspn(*title, " \t\r\n");
		return 0;
	}

	return 1;
}

static int pagematch(pagelist_t *pg, char *name)
{
	char *p = strrchr(pg->pagepath, '/');

	if (p) {
		return (strcmp(p+1, name) == 0);
	}
	else {
		return (strcmp(pg->pagepath, name) == 0);
	}
}


static strbuffer_t *contentbuffer = NULL;

static int prepare_fromfile(char *hostsfn, char *extrainclude)
{
	static void *hostfiles = NULL;
	FILE *hosts;
	strbuffer_t *inbuf;

	/* First check if there were no modifications at all */
	if (hostfiles) {
		if (!stackfmodified(hostfiles)){
			return 1;
		}
		else {
			stackfclist(&hostfiles);
			hostfiles = NULL;
		}
	}

	if (!contentbuffer) contentbuffer = newstrbuffer(0);
	clearstrbuffer(contentbuffer);

	hosts = stackfopen(hostsfn, "r", &hostfiles);
	if (hosts == NULL) return -1;

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, extrainclude)) {
		sanitize_input(inbuf, 0, 0);
		addtostrbuffer(contentbuffer, inbuf);
		addtobuffer(contentbuffer, "\n");
	}

	stackfclose(hosts);
	freestrbuffer(inbuf);

	return 0;
}

static int prepare_fromnet(void)
{
	static char contentmd5[33] = { '\0', };
	sendreturn_t *sres;
	sendresult_t sendstat;
	char *fdata, *fhash;
	int dontsendcopy = dontsendmessages;

	dontsendmessages = 0;
	sres = newsendreturnbuf(1, NULL);
	sendstat = sendmessage("config hosts.cfg", NULL, XYMON_TIMEOUT, sres);
	dontsendmessages = dontsendcopy;
	if (sendstat != XYMONSEND_OK) {
		freesendreturnbuf(sres);
		errprintf("Cannot load hosts.cfg from xymond, code %d\n", sendstat);
		return -1;
	}

	fdata = getsendreturnstr(sres, 1);
	freesendreturnbuf(sres);
	fhash = md5hash(fdata);
	if (strcmp(contentmd5, fhash) == 0) {
		/* No changes */
		xfree(fdata);
		return 1;
	}

	if (contentbuffer) freestrbuffer(contentbuffer);
	contentbuffer = convertstrbuffer(fdata, 0);
	strcpy(contentmd5, fhash);

	return 0;
}


char *hostscfg_content(void)
{
	return strdup(STRBUF(contentbuffer));
}

int load_hostnames(char *hostsfn, char *extrainclude, int fqdn)
{
	/* Return value: 0 for load OK, 1 for "No files changed since last load", -1 for error (file not found) */
	int prepresult;
	int groupid, pageidx;
	char *hostname, *dgname;
	pagelist_t *curtoppage, *curpage, *pgtail;
	namelist_t *nametail = NULL;
	void * htree;
	char *cfgdata, *inbol, *ineol, insavchar = '\0';
	char *lcopy = NULL;

	load_hostinfo(NULL);

	if (*hostsfn == '!')
		prepresult = prepare_fromfile(hostsfn+1, extrainclude);
	else if (extrainclude)
		prepresult = prepare_fromfile(hostsfn, extrainclude);
	else if ((*hostsfn == '@') || (strcmp(hostsfn, xgetenv("HOSTSCFG")) == 0)) {
		prepresult = prepare_fromnet();
		if (prepresult == -1) {
			errprintf("Failed to load from xymond, reverting to file-load\n");
			prepresult = prepare_fromfile(xgetenv("HOSTSCFG"), extrainclude);
		}
	}
	else
		prepresult = prepare_fromfile(hostsfn, extrainclude);

	/* Did we get the data ? */
	if (prepresult == -1) {
		errprintf("Cannot load host data\n");
		return -1;
	}

	/* Any modifications at all ? */
	if (prepresult == 1) {
		dbgprintf("No files modified, skipping reload of %s\n", hostsfn);
		return 1;
	}

	configloaded = 1;
	initialize_hostlist();
	curpage = curtoppage = pgtail = pghead;
	pageidx = groupid = 0;
	dgname = NULL;

	htree = xtreeNew(strcasecmp);
	inbol = cfgdata = hostscfg_content();
	while (inbol && *inbol) {
		char *key, *keyp;

		inbol += strspn(inbol, " \t");
		ineol = strchr(inbol, '\n'); 
		if (ineol) {
			while ((ineol > inbol) && (isspace(*ineol) || (*ineol == '\n'))) ineol--;
			if (*ineol != '\n') ineol++;

			insavchar = *ineol;
			*ineol = '\0';
		}
		if ((*inbol == '#') || (strlen(inbol) == 0)) goto nextline;

		if (lcopy) xfree(lcopy);
		lcopy = strdup(inbol);
		key = strtok_r(lcopy, " \t\r\n", &keyp);

		if (strncmp(inbol, "page", 4) == 0) {
			pagelist_t *newp;
			char *name, *title;

			pageidx = groupid = 0;
			if (dgname) xfree(dgname); dgname = NULL;
			if (get_page_name_title(inbol, "page", &name, &title) == 0) {
				newp = (pagelist_t *)malloc(sizeof(pagelist_t));
				newp->pagepath = strdup(name);
				newp->pagetitle = (title ? strdup(title) : NULL);
				newp->next = NULL;

				pgtail->next = newp;
				pgtail = newp;

				curpage = curtoppage = newp;
			}
		}
		else if (strncmp(inbol, "subpage", 7) == 0) {
			pagelist_t *newp;
			char *name, *title;

			pageidx = groupid = 0;
			if (dgname) xfree(dgname); dgname = NULL;
			if (get_page_name_title(inbol, "subpage", &name, &title) == 0) {
				newp = (pagelist_t *)malloc(sizeof(pagelist_t));
				newp->pagepath = malloc(strlen(curtoppage->pagepath) + strlen(name) + 2);
				sprintf(newp->pagepath, "%s/%s", curtoppage->pagepath, name);
				newp->pagetitle = malloc(strlen(curtoppage->pagetitle) + strlen(title) + 2);
				sprintf(newp->pagetitle, "%s/%s", curtoppage->pagetitle, title);
				newp->next = NULL;

				pgtail->next = newp;
				pgtail = newp;

				curpage = newp;
			}
		}
		else if (strncmp(inbol, "subparent", 9) == 0) {
			pagelist_t *newp, *parent;
			char *pname, *name, *title;

			pageidx = groupid = 0;
			if (dgname) xfree(dgname); dgname = NULL;
			parent = NULL;
			if (get_page_name_title(inbol, "subparent", &pname, &title) == 0) {
				for (parent = pghead; (parent && !pagematch(parent, pname)); parent = parent->next);
			}

			if (parent && (get_page_name_title(title, "", &name, &title) == 0)) {
				newp = (pagelist_t *)malloc(sizeof(pagelist_t));
				newp->pagepath = malloc(strlen(parent->pagepath) + strlen(name) + 2);
				sprintf(newp->pagepath, "%s/%s", parent->pagepath, name);
				newp->pagetitle = malloc(strlen(parent->pagetitle) + strlen(title) + 2);
				sprintf(newp->pagetitle, "%s/%s", parent->pagetitle, title);
				newp->next = NULL;

				pgtail->next = newp;
				pgtail = newp;

				curpage = newp;
			}
		}
		else if (strncmp(inbol, "group", 5) == 0) {
			char *tok, *inp;

			groupid++;
			if (dgname) xfree(dgname); dgname = NULL;

			tok = strtok(inbol, " \t");
			if ((strcmp(tok, "group-only") == 0) || (strcmp(tok, "group-except") == 0)) {
				tok = strtok(NULL, " \t");
			}
			if (tok) tok = strtok(NULL, "\r\n");

			if (tok) {
				char *inp;

				/* Strip HTML tags from the string */
				dgname = (char *)malloc(strlen(tok) + 1);
				*dgname = '\0';

				inp = tok;
				while (*inp) {
					char *tagstart, *tagend;

					tagstart = strchr(inp, '<');
					if (tagstart) {
						tagend = strchr(tagstart, '>');

						*tagstart = '\0';
						if (*inp) strcat(dgname, inp);
						if (tagend) {
							inp = tagend+1;
						}
						else {
							/* Unmatched '<', keep all of the string */
							*tagstart = '<';
							strcat(dgname, tagstart);
							inp += strlen(inp);
						}
					}
					else {
						strcat(dgname, inp);
						inp += strlen(inp);
					}
				}
			}
		}
		else if (conn_is_ip(key) != 0) {
			char *startoftags, *tag, *delim;
			int elemidx, elemsize;
			char groupidstr[10];
			xtreePos_t handle;

			namelist_t *newitem = calloc(1, sizeof(namelist_t));
			namelist_t *iwalk, *iprev;

			hostname = strtok_r(NULL, " \t\r\n", &keyp);
			/* Hostname beginning with '@' are "no-display" hosts. But we still want them. */
			if (*hostname == '@') hostname++;

			if (!fqdn) {
				/* Strip any domain from the hostname */
				char *p = strchr(hostname, '.');
				if (p) *p = '\0';
			}

			newitem->ip = strdup(key);
			sprintf(groupidstr, "%d", groupid);
			newitem->groupid = strdup(groupidstr);
			newitem->dgname = (dgname ? strdup(dgname) : strdup("NONE"));
			newitem->pageindex = pageidx++;

			newitem->hostname = strdup(hostname);
			/* If the ip is a NULL ip, then this host has lower preference */
			newitem->preference = ((strcmp(newitem->ip, "0.0.0.0") == 0) || (strcmp(newitem->ip, "::") == 0)) ? 0 : 1;
			newitem->logname = strdup(newitem->hostname);
			{ char *p = newitem->logname; while ((p = strchr(p, '.')) != NULL) { *p = '_'; } }
			newitem->page = curpage;
			newitem->defaulthost = defaulthost;

			startoftags = strchr(inbol, '#');
			if (startoftags == NULL) startoftags = ""; else startoftags++;
			startoftags += strspn(startoftags, " \t\r\n");
			newitem->allelems = strdup(startoftags);
			elemsize = 5;
			newitem->elems = (char **)malloc((elemsize+1)*sizeof(char *));

			tag = newitem->allelems; elemidx = 0;
			while (tag && *tag) {
				if (elemidx == elemsize) {
					elemsize += 5;
					newitem->elems = (char **)realloc(newitem->elems, (elemsize+1)*sizeof(char *));
				}
				newitem->elems[elemidx] = tag;

				/* Skip until we hit a whitespace or a quote */
				tag += strcspn(tag, " \t\r\n\"");
				if (*tag == '"') {
					delim = tag;

					/* Hit a quote - skip until the next matching quote */
					tag = strchr(tag+1, '"');
					if (tag != NULL) { 
						/* Found end-quote, NULL the item here and move on */
						*tag = '\0'; tag++; 
					}

					/* Now move quoted data one byte down (including the NUL) to kill quotechar */
					memmove(delim, delim+1, strlen(delim));
				}
				else if (*tag) {
					/* Normal end of item, NULL it and move on */
					*tag = '\0'; tag++;
				}
				else {
					/* End of line - no more to do. */
					tag = NULL;
				}

				/* 
				 * If we find a "noconn", drop preference value to 0.
				 * If we find a "prefer", up reference value to 2.
				 */
				if ((newitem->preference == 1) && (strcmp(newitem->elems[elemidx], "noconn") == 0))
					newitem->preference = 0;
				else if (strcmp(newitem->elems[elemidx], "prefer") == 0)
					newitem->preference = 2;

				/* Skip whitespace until start of next tag */
				if (tag) tag += strspn(tag, " \t\r\n");
				elemidx++;
			}

			newitem->elems[elemidx] = NULL;

			/* See if this host is defined before */
			handle = xtreeFind(htree, newitem->hostname);
			if (strcasecmp(newitem->hostname, ".default.") == 0) {
				/* The pseudo DEFAULT host */
				newitem->next = NULL;
				defaulthost = newitem;
			}
			else if (handle == xtreeEnd(htree)) {
				/* New item, so add to end of list */
				newitem->next = NULL;
				if (namehead == NULL) 
					namehead = nametail = newitem;
				else {
					nametail->next = newitem;
					nametail = newitem;
				}
				xtreeAdd(htree, newitem->hostname, newitem);
			}
			else {
				/* Find the existing record - compare the record pointer instead of the name */
				namelist_t *existingrec = (namelist_t *)xtreeData(htree, handle);
				for (iwalk = namehead, iprev = NULL; ((iwalk != existingrec) && iwalk); iprev = iwalk, iwalk = iwalk->next) ;
 				if (newitem->preference <= iwalk->preference) {
					/* Add after the existing (more preferred) entry */
					newitem->next = iwalk->next;
					iwalk->next = newitem;
				}
				else {
					/* New item has higher preference, so add before the iwalk item (i.e. after iprev) */
					if (iprev == NULL) {
						newitem->next = namehead;
						namehead = newitem;
					}
					else {
						newitem->next = iprev->next;
						iprev->next = newitem;
					}
				}
			}

			newitem->clientname = xmh_find_item(newitem, XMH_CLIENTALIAS);
			if (newitem->clientname == NULL) newitem->clientname = newitem->hostname;
			newitem->downtime = xmh_find_item(newitem, XMH_DOWNTIME);
		}


nextline:
		if (ineol) {
			*ineol = insavchar;
			if (*ineol != '\n') ineol = strchr(ineol, '\n');

			inbol = (ineol ? ineol+1 : NULL);
		}
		else
			inbol = NULL;
	}

	if (lcopy) xfree(lcopy);
	xfree(cfgdata);
	if (dgname) xfree(dgname);
	xtreeDestroy(htree);

	build_hosttree();

	return 0;
}

