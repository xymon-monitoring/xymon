/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module for Hobbit, responsible for loading the bb-hosts  */
/* file and keeping track of what hosts are known, their aliases and planned  */
/* downtime settings etc.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid_file[] = "$Id: loadhosts_file.c,v 1.30 2008-01-03 09:59:13 henrik Exp $";

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

void load_hostnames(char *bbhostsfn, char *extrainclude, int fqdn)
{
	static void *bbhfiles = NULL;
	FILE *bbhosts;
	int ip1, ip2, ip3, ip4, groupid, pageidx;
	char hostname[4096];
	strbuffer_t *inbuf;
	pagelist_t *curtoppage, *curpage, *pgtail;
	namelist_t *nametail = NULL;
	RbtHandle htree;

	/* First check if there were no modifications at all */
	if (bbhfiles) {
		if (!stackfmodified(bbhfiles)){
			dbgprintf("No files modified, skipping reload of %s\n", bbhostsfn);
			return;
		}
		else {
			stackfclist(&bbhfiles);
			bbhfiles = NULL;
		}
	}

	MEMDEFINE(hostname);
	MEMDEFINE(l);

	configloaded = 1;
	initialize_hostlist();
	curpage = curtoppage = pgtail = pghead;
	pageidx = groupid = 0;

	bbhosts = stackfopen(bbhostsfn, "r", &bbhfiles);
	if (bbhosts == NULL) return;

	inbuf = newstrbuffer(0);
	htree = rbtNew(name_compare);
	while (stackfgets(inbuf, extrainclude)) {
		sanitize_input(inbuf, 0, 0);

		if (strncmp(STRBUF(inbuf), "page", 4) == 0) {
			pagelist_t *newp;
			char *name, *title;

			pageidx = groupid = 0;
			if (get_page_name_title(STRBUF(inbuf), "page", &name, &title) == 0) {
				newp = (pagelist_t *)malloc(sizeof(pagelist_t));
				newp->pagepath = strdup(name);
				newp->pagetitle = (title ? strdup(title) : NULL);
				newp->next = NULL;

				pgtail->next = newp;
				pgtail = newp;

				curpage = curtoppage = newp;
			}
		}
		else if (strncmp(STRBUF(inbuf), "subpage", 7) == 0) {
			pagelist_t *newp;
			char *name, *title;

			pageidx = groupid = 0;
			if (get_page_name_title(STRBUF(inbuf), "subpage", &name, &title) == 0) {
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
		else if (strncmp(STRBUF(inbuf), "subparent", 9) == 0) {
			pagelist_t *newp, *parent;
			char *pname, *name, *title;

			pageidx = groupid = 0;
			parent = NULL;
			if (get_page_name_title(STRBUF(inbuf), "subparent", &pname, &title) == 0) {
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
		else if (strncmp(STRBUF(inbuf), "group", 5) == 0) {
			groupid++;
		}
		else if (sscanf(STRBUF(inbuf), "%d.%d.%d.%d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {
			char *startoftags, *tag, *delim;
			int elemidx, elemsize;
			char clientname[4096];
			char downtime[4096];
			char groupidstr[10];
			RbtIterator handle;

			namelist_t *newitem = calloc(1, sizeof(namelist_t));
			namelist_t *iwalk, *iprev;

			MEMDEFINE(clientname);
			MEMDEFINE(downtime);

			/* Hostname beginning with '@' are "no-display" hosts. But we still want them. */
			if (*hostname == '@') memmove(hostname, hostname+1, strlen(hostname));

			if (!fqdn) {
				/* Strip any domain from the hostname */
				char *p = strchr(hostname, '.');
				if (p) *p = '\0';
			}

			sprintf(newitem->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
			sprintf(groupidstr, "%d", groupid);
			newitem->groupid = strdup(groupidstr);
			newitem->pageindex = pageidx++;

			newitem->bbhostname = strdup(hostname);
			if (ip1 || ip2 || ip3 || ip4) newitem->preference = 1; else newitem->preference = 0;
			newitem->logname = strdup(newitem->bbhostname);
			{ char *p = newitem->logname; while ((p = strchr(p, '.')) != NULL) { *p = '_'; } }
			newitem->page = curpage;
			newitem->defaulthost = defaulthost;

			clientname[0] = downtime[0] = '\0';
			startoftags = strchr(STRBUF(inbuf), '#');
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
			handle = rbtFind(htree, newitem->bbhostname);
			if (strcasecmp(newitem->bbhostname, ".default.") == 0) {
				/* The pseudo DEFAULT host */
				newitem->next = NULL;
				defaulthost = newitem;
			}
			else if (handle == rbtEnd(htree)) {
				/* New item, so add to end of list */
				newitem->next = NULL;
				if (namehead == NULL) 
					namehead = nametail = newitem;
				else {
					nametail->next = newitem;
					nametail = newitem;
				}
				rbtInsert(htree, newitem->bbhostname, newitem);
			}
			else {
				/* Find the existing record - compare the record pointer instead of the name */
				namelist_t *existingrec = (namelist_t *)gettreeitem(htree, handle);
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

			newitem->clientname = bbh_find_item(newitem, BBH_CLIENTALIAS);
			if (newitem->clientname == NULL) newitem->clientname = newitem->bbhostname;
			newitem->downtime = bbh_find_item(newitem, BBH_DOWNTIME);

			MEMUNDEFINE(clientname);
			MEMUNDEFINE(downtime);
		}
	}
	stackfclose(bbhosts);
	freestrbuffer(inbuf);
	rbtDelete(htree);

	MEMUNDEFINE(hostname);
	MEMUNDEFINE(l);

	build_hosttree();

	return;
}

