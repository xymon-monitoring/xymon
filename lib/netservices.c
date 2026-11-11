/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for parsing the protocols.cfg file.                   */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libxymon.h"

/*
 * Services we know how to handle:
 * This defines what to send to them to shut down the 
 * session nicely, and whether we want to grab the
 * banner or not.
 */
/*
 * The registry of service names and their default ports.
 *
 * Names and ports only. This used to carry a send and an expect for each
 * service as well, used whenever protocols.cfg could not be read -- but
 * protocols.cfg is the configuration, and when it cannot be read nothing is
 * checked at all now, so those rows could only sit here going out of date
 * against the entries that ship. Every service named here is defined there.
 *
 * Still read: default_tcp_port() when hosts.cfg names no port, and
 * find_tcp_service() when confreport asks whether a test is a network test.
 */
/*
 * Only the terminator. The names here existed so a missing protocols.cfg
 * reported yellow rather than red, but covered 28 of the 49 services the
 * file defines -- the same fault, two colours. svccfg_unreadable says it
 * once, for every service.
 */
static svcinfo_t default_svcinfo[] = {
	{ NULL, NULL, 0, NULL, 0, 0, (0), 0 }
};

/* No protocols.cfg could be read, so nothing can be checked. */
static int svccfg_unreadable = 0;

int tcp_services_unreadable(void)
{
	return svccfg_unreadable;
}

static svcinfo_t *svcinfo = default_svcinfo;

/*
 * Copy LEN bytes and terminate. By length, never strdup(): getescapestring()
 * resolves \xNN, so the text may contain a NUL and a copy that stopped there
 * would be read past its end by the length recorded with it.
 *
 * NULL on failure; callers refuse the entry. malloc() is the plain one here
 * (the xmalloc wrappers need XYMON_MEMORY_WRAPPERS, undefined in this build).
 */
static unsigned char *dup_bytes(unsigned char *src, int len)
{
	unsigned char *p = (unsigned char *)malloc(len + 1);

	if (!p) return NULL;
	memcpy(p, src, len);
	p[len] = '\0';
	return p;
}

/*
 * Append one step to a service's dialogue, preserving file order. Every alias
 * in an [a|b|c] header gets its own copy: the records are freed independently,
 * so they cannot share a list.
 */
static svcstep_t *add_svcstep(svcinfo_t *rec, int type, unsigned char *text, int len)
{
	svcstep_t *step, *walk;

	step = (svcstep_t *)calloc(1, sizeof(svcstep_t));
	if (!step) return NULL;
	step->type = type;
	step->len  = len;
	step->text = dup_bytes(text, len);
	if (!step->text) { xfree(step); return NULL; }

	if (rec->steps == NULL) rec->steps = step;
	else {
		for (walk = rec->steps; (walk->next); walk = walk->next) ;
		walk->next = step;
	}

	return step;
}


/*
 * Where a quoted string ends. Deliberately the same rule getescapestring
 * uses -- scan to the next '"' -- so the two agree on where the value stops
 * and any trailing keywords begin.
 */
static char *after_quoted(char *p)
{
	if (*p != '"') {
		while (*p && !isspace((int)*p)) p++;
		return p;
	}
	p = strchr(p+1, '"');
	return (p ? p+1 : "");
}


/*
 * Release one entry's step list. The reload path already promises not to
 * leak, and a step list is more than the record it hangs off.
 */
static void free_svcsteps(svcinfo_t *rec)
{
	svcstep_t *st = rec->steps;

	while (st) {
		svcstep_t *next = st->next;

		if (st->text)  xfree(st->text);
		if (st->until) xfree(st->until);
		xfree(st);
		st = next;
	}
	rec->steps = NULL;
}


typedef struct svclist_t {
	struct svcinfo_t *rec;
	struct svclist_t *next;
} svclist_t;


static char *binview(unsigned char *buf, int buflen)
{
	/* Encode a string with possibly binary data into an ascii-printable form */

	static char hexchars[16] = "0123456789ABCDEF";
	static char *result = NULL;
	unsigned char *inp, *outp;
	int i;

	if (result) xfree(result);
	if (!buf) { result = strdup("[null]"); return result; }

	if (buf && (buflen == 0)) buflen = strlen(buf);
	result = (char *)malloc(4*buflen + 1);	/* Worst case: All binary */

	for (inp=buf, i=0, outp=result; (i<buflen); i++,inp++) {
		if (isprint(*inp)) {
			*outp = *inp;
			outp++;
		}
		else if (*inp == '\r') {
			*outp = '\\'; outp++;
			*outp = 'r'; outp++;
		}
		else if (*inp == '\n') {
			*outp = '\\'; outp++;
			*outp = 'n'; outp++;
		}
		else if (*inp == '\t') {
			*outp = '\\'; outp++;
			*outp = 't'; outp++;
		}
		else {
			*outp = '\\'; outp++;
			*outp = 'x'; outp++;
			*outp = hexchars[*inp / 16]; outp++;
			*outp = hexchars[*inp % 16]; outp++;
		}
	}
	*outp = '\0';

	return result;
}

char *init_tcp_services(void)
{
	STATIC_SBUF_DEFINE(xymonnetsvcs);
	static void *svcflist = NULL;

	char filename[PATH_MAX];
	struct stat st;
	FILE *fd = NULL;
	strbuffer_t *inbuf;
	svclist_t *head, *tail, *first, *walk;
	SBUF_DEFINE(searchstring);
	int svcnamebytes = 0;
	int svccount = 0;
	int i;

	MEMDEFINE(filename);

	filename[0] = '\0';
	if (xgetenv("XYMONHOME")) {
		snprintf(filename, sizeof(filename), "%s/etc/", xgetenv("XYMONHOME"));
	}
	strncat(filename, "protocols.cfg", (sizeof(filename) - strlen(filename)));

	if ((stat(filename, &st) == 0) && xymonnetsvcs) {
		/* See if we have already run and the file is unchanged - if so just pickup the result */
		if (svcflist && !stackfmodified(svcflist)) {
			return xymonnetsvcs;
		}

		/* File has changed - reload configuration. But clean up first so we don't leak memory. */
		if (svcinfo != default_svcinfo) {
			for (i=0; (svcinfo[i].svcname); i++) {
				if (svcinfo[i].svcname) xfree(svcinfo[i].svcname);
				if (svcinfo[i].sendtxt) xfree(svcinfo[i].sendtxt);
				if (svcinfo[i].exptext) xfree(svcinfo[i].exptext);
				if (svcinfo[i].alpns) xfree(svcinfo[i].alpns);
				free_svcsteps(&svcinfo[i]);
			}
			xfree(svcinfo);
			svcinfo = default_svcinfo;
		}

		xfree(xymonnetsvcs); xymonnetsvcs = NULL;
	}

	if (xgetenv("XYMONNETSVCS") == NULL) {
		putenv("XYMONNETSVCS=smtp telnet ftp pop pop3 pop-3 ssh imap ssh1 ssh2 imap2 imap3 imap4 pop2 pop-2 nntp");
	}

	fd = stackfopen(filename, "r", &svcflist);
	if (fd == NULL) {
		int i;

		/*
		 * The file IS the configuration: with no file, nothing was asked for,
		 * so nothing is checked. Same rule as for a single entry that cannot
		 * be read -- say so, and check nothing.
		 *
		 * There is no second set of probes to fall back to. The table above
		 * holds names and ports, which is what the tests below still need to
		 * report against.
		 */
		errprintf("Cannot open TCP service-definitions file %s - no service will be checked\n",
			  filename);
		svccfg_unreadable = 1;
		xymonnetsvcs = strdup(xgetenv("XYMONNETSVCS"));
		xymonnetsvcs_buflen = strlen(xymonnetsvcs)+1;

		MEMUNDEFINE(filename);
		return xymonnetsvcs;
	}

	head = tail = first = NULL;

	inbuf = newstrbuffer(0);
	while (stackfgets(inbuf, NULL)) {
		char *l, *eol;

		sanitize_input(inbuf, 1, 0);
		l = STRBUF(inbuf);

		if (*l == '[') {
			char *svcname;

			eol = strchr(l, ']'); if (eol) *eol = '\0';
			l = skipwhitespace(l+1);

			svcname = strtok(l, "|"); first = NULL;
			while (svcname) {
				svclist_t *newitem;

				svccount++;
				svcnamebytes += (strlen(svcname) + 1);

				newitem = (svclist_t *) malloc(sizeof(svclist_t));
				newitem->rec = (svcinfo_t *)calloc(1, sizeof(svcinfo_t));
				newitem->rec->svcname = strdup(svcname);
				newitem->next = NULL;

				if (first == NULL) first = newitem;

				if (head == NULL) {
					head = tail = newitem;
				}
				else {
					tail->next = newitem;
					tail = newitem;
				}

				svcname = strtok(NULL, "|");
			}
		}
		else if (strncmp(l, "send ", 5) == 0) {
			if (first) {
				unsigned char *txt = NULL;
				int txtlen = 0;

				getescapestring(skipwhitespace(l+4), &txt, &txtlen);
				/*
				 * sendtxt keeps the FIRST send only, which is what a
				 * single-step probe has always meant. The step list is
				 * what a dialogue is driven from.
				 */
				if (first->rec->sendtxt == NULL) {
					first->rec->sendtxt = txt;
					first->rec->sendlen = txtlen;
				}
				if (!add_svcstep(first->rec, STEP_SEND, txt, txtlen)) {
					errprintf("Service %s: out of memory reading protocols.cfg\n",
						  first->rec->svcname);
					first->rec->flags |= TCP_DIALOGUE_BROKEN;
				}
				for (walk = first->next; (walk); walk = walk->next) {
					if (walk->rec->sendtxt == NULL) {
						walk->rec->sendtxt = dup_bytes(txt, txtlen);
						walk->rec->sendlen = txtlen;
						if (!walk->rec->sendtxt) {
							errprintf("Service %s: out of memory reading protocols.cfg\n",
								  walk->rec->svcname);
							walk->rec->flags |= TCP_DIALOGUE_BROKEN;
						}
					}
					if (!add_svcstep(walk->rec, STEP_SEND, txt, txtlen)) {
						errprintf("Service %s: out of memory reading protocols.cfg\n",
							  walk->rec->svcname);
						walk->rec->flags |= TCP_DIALOGUE_BROKEN;
					}
				}
				/* add_svcstep() copied it; only the first send keeps the buffer. */
				if (first->rec->sendtxt != txt) xfree(txt);
			}
		}
		else if (strncmp(l, "start ", 6) == 0) {
			/*
			 * Upgrade here: plaintext before this line, encrypted after it, on the
			 * same socket. That is SMTP on 25, submission on 587 and IMAP on 143,
			 * and the only way any of them reaches a certificate -- "options ssl"
			 * is TLS from the first byte, a different port and service.
			 *
			 * A step, not an option, because WHERE it happens is the point: after
			 * the server has agreed to it, never before.
			 */
			if (first) {
				char *feat = skipwhitespace(l+5);

				/*
				 * The word after "start" names code here, not a protocol verb, and is
				 * reserved: "tls" is the only one, and anything else is refused when
				 * the file is read.
				 *
				 * Two words because the action is not protocol-specific -- FTP spells
				 * the request AUTH TLS, POP3 STLS, SMTP and IMAP STARTTLS -- and the
				 * asking is left to a send, which is what knows the protocol.
				 */
				if ((strcmp(feat, "tls") != 0) && (strcmp(feat, "iac") != 0)) {
					/*
					 * Refused, and the entry is marked so it cannot report
					 * OK. Logging alone would leave a probe that quietly
					 * checks less than the file says it does: the step is
					 * dropped, the conversation still runs, and the column
					 * stays green while nothing has upgraded. A definition
					 * that could not be read is not a service that is up.
					 */
					errprintf("Service %s: 'start %s' - a client can start 'tls' or 'iac'\n",
						  first->rec->svcname, feat);
					first->rec->flags |= TCP_DIALOGUE_BROKEN;
					for (walk = first->next; (walk); walk = walk->next)
						walk->rec->flags |= TCP_DIALOGUE_BROKEN;
				}
				else if (strcmp(feat, "iac") == 0) {
					/*
					 * Telnet option negotiation as a step. IAC belongs to the
					 * protocol, not to port 23 -- MUDs and BBSes negotiate on
					 * their own ports -- so any entry can say where it
					 * happens. TCP_TELNET arms it in contest.c, which is what
					 * "options telnet" sets too.
					 */
					first->rec->flags |= TCP_TELNET;
					if (!add_svcstep(first->rec, STEP_STARTIAC, (unsigned char *)"", 0)) {
						errprintf("Service %s: out of memory reading protocols.cfg\n",
							  first->rec->svcname);
						first->rec->flags |= TCP_DIALOGUE_BROKEN;
					}
					for (walk = first->next; (walk); walk = walk->next) {
						walk->rec->flags |= TCP_TELNET;
						if (!add_svcstep(walk->rec, STEP_STARTIAC, (unsigned char *)"", 0)) {
							errprintf("Service %s: out of memory reading protocols.cfg\n",
								  walk->rec->svcname);
							walk->rec->flags |= TCP_DIALOGUE_BROKEN;
						}
					}
				}
				else {
					svcstep_t *already;

					/*
					 * There is one connection and it is upgraded once. A
					 * file asking twice asks for something that cannot
					 * happen, and the second was skipped in silence --
					 * quietly doing less than the file says, which is the
					 * failure this grammar is meant to make impossible.
					 */
					for (already = first->rec->steps; (already); already = already->next)
						if (already->type == STEP_STARTTLS) break;

					if (already) {
						errprintf("Service %s: a second 'start tls' - the connection is upgraded once, so the extra line asks for something that cannot happen\n",
							  first->rec->svcname);
						first->rec->flags |= TCP_DIALOGUE_BROKEN;
						for (walk = first->next; (walk); walk = walk->next)
							walk->rec->flags |= TCP_DIALOGUE_BROKEN;
					}
					else {
						if (!add_svcstep(first->rec, STEP_STARTTLS, (unsigned char *)"", 0)) {
							errprintf("Service %s: out of memory reading protocols.cfg\n",
								  first->rec->svcname);
							first->rec->flags |= TCP_DIALOGUE_BROKEN;
						}
						for (walk = first->next; (walk); walk = walk->next)
							if (!add_svcstep(walk->rec, STEP_STARTTLS, (unsigned char *)"", 0)) {
								errprintf("Service %s: out of memory reading protocols.cfg\n",
									  walk->rec->svcname);
								walk->rec->flags |= TCP_DIALOGUE_BROKEN;
							}
					}
				}
			}
		}
		else if (strncmp(l, "expect ", 7) == 0) {
			if (first) {
				unsigned char *txt = NULL, *untiltxt = NULL;
				int txtlen = 0, untillen = 0;
				char *rest;
				svcstep_t *st;

				getescapestring(skipwhitespace(l+6), &txt, &txtlen);

				/*
				 * "until" says where the reply ENDS. Without it an expect
				 * takes a single line, which is wrong for every protocol that
				 * answers with several: SMTP and FTP continue with "250-" and
				 * finish with "250 ", NNTP ends a block with ".", IMAP ends
				 * with the command tag. Naming the terminator covers all three
				 * without teaching the parser any of them.
				 */
				rest = skipwhitespace(after_quoted(skipwhitespace(l+6)));
				if (strncmp(rest, "until ", 6) == 0)
					getescapestring(skipwhitespace(rest + 5), &untiltxt, &untillen);
				else if (*rest) {
					/*
					 * "untill" would drop the terminator in silence, leaving
					 * the expect to take one line out of a multi-line reply.
					 */
					errprintf("Service %s: expect takes only 'until', not: %s\n",
						  first->rec->svcname, rest);
					first->rec->flags |= TCP_DIALOGUE_BROKEN;
					for (walk = first->next; (walk); walk = walk->next)
						walk->rec->flags |= TCP_DIALOGUE_BROKEN;
				}

				if (first->rec->exptext == NULL) {
					first->rec->exptext = txt;
					first->rec->explen  = txtlen;
				}
				st = add_svcstep(first->rec, STEP_EXPECT, txt, txtlen);
				if (!st) {
					errprintf("Service %s: out of memory reading protocols.cfg\n",
						  first->rec->svcname);
					first->rec->flags |= TCP_DIALOGUE_BROKEN;
				}
				else if (untiltxt) {
					st->until = dup_bytes(untiltxt, untillen);
					st->untillen = untillen;
					if (!st->until) {
						errprintf("Service %s: out of memory reading protocols.cfg\n",
							  first->rec->svcname);
						st->untillen = 0;
						first->rec->flags |= TCP_DIALOGUE_BROKEN;
					}
				}
				for (walk = first->next; (walk); walk = walk->next) {
					if (walk->rec->exptext == NULL) {
						walk->rec->exptext = dup_bytes(txt, txtlen);
						walk->rec->explen  = txtlen;
						if (!walk->rec->exptext) {
							errprintf("Service %s: out of memory reading protocols.cfg\n",
								  walk->rec->svcname);
							walk->rec->flags |= TCP_DIALOGUE_BROKEN;
						}
						walk->rec->expofs  = 0; /* HACK - not used right now */
					}
					st = add_svcstep(walk->rec, STEP_EXPECT, txt, txtlen);
					if (!st) {
						errprintf("Service %s: out of memory reading protocols.cfg\n",
							  walk->rec->svcname);
						walk->rec->flags |= TCP_DIALOGUE_BROKEN;
					}
					else if (untiltxt) {
						st->until = dup_bytes(untiltxt, untillen);
						st->untillen = untillen;
						if (!st->until) {
							errprintf("Service %s: out of memory reading protocols.cfg\n",
								  walk->rec->svcname);
							st->untillen = 0;
							walk->rec->flags |= TCP_DIALOGUE_BROKEN;
						}
					}
				}
				if (untiltxt) xfree(untiltxt);
				/* As above: only the first expect keeps its buffer. */
				if (first->rec->exptext != txt) xfree(txt);
			}
		}
		else if (strncmp(l, "options ", 8) == 0) {
			if (first) {
				char *opt;

				/*
				 * Reset what THIS line owns. A second "options" replaces the
				 * first, but a refusal recorded by another line must survive,
				 * or the entry silently becomes runnable again depending on
				 * which line came last.
				 */
				first->rec->flags &= TCP_DIALOGUE_BROKEN;
				l = skipwhitespace(l+7);
				opt = strtok(l, ",");
				while (opt) {
					if      (strcmp(opt, "ssl") == 0)    first->rec->flags |= TCP_SSL;
					else if (strcmp(opt, "banner") == 0) first->rec->flags |= TCP_GET_BANNER;
					else if (strcmp(opt, "telnet") == 0) first->rec->flags |= TCP_TELNET;
					else if (strncmp(opt, "alpn=", 5) == 0) {
						first->rec->alpns = strdup(opt+5);
					}
					else {
						/*
						 * Refused like an unknown directive: "options
						 * sssl" would ask for TLS, get a plaintext probe,
						 * and report green.
						 */
						errprintf("Unknown option in service %s: %s\n",
							  first->rec->svcname, opt);
						first->rec->flags |= TCP_DIALOGUE_BROKEN;
					}

					opt = strtok(NULL, ",");
				}
				for (walk = first->next; (walk); walk = walk->next) {
					/*
					 * Keep the alias's own refusal. A failure while copying
					 * to one record is recorded on that record, and a plain
					 * assignment here would put it back to whatever the
					 * first one had -- leaving that alias running with a
					 * step it never got.
					 */
					walk->rec->flags = first->rec->flags |
							   (walk->rec->flags & TCP_DIALOGUE_BROKEN);
				}
			}
		}
		else if (strncmp(l, "port ", 5) == 0) {
			if (first) {
				first->rec->port = atoi(skipwhitespace(l+4));
				for (walk = first->next; (walk); walk = walk->next) {
					walk->rec->port = first->rec->port;
				}
			}
		}
		else if (*l) {
			/*
			 * Refuse the entry. Reporting an unrecognised line and running anyway
			 * is barely better than ignoring it: the step never exists, the probe
			 * runs the rest, and "sedn"/"exepct" leaves a bare greeting check that
			 * says the service is fine.
			 *
			 * This does turn that column yellow on every host carrying the entry,
			 * which is the point -- only the operator can reconcile a file that
			 * says one thing with a probe that would do another.
			 */
			errprintf("Unknown protocols.cfg directive%s%s: %s\n",
				  (first ? " in service " : ""),
				  (first ? first->rec->svcname : ""), l);
			if (first) {
				first->rec->flags |= TCP_DIALOGUE_BROKEN;
				for (walk = first->next; (walk); walk = walk->next)
					walk->rec->flags |= TCP_DIALOGUE_BROKEN;
			}
		}
	}

	if (fd) stackfclose(fd);
	freestrbuffer(inbuf);

	/* Copy from the svclist to svcinfo table */
	svcinfo = (svcinfo_t *) malloc((svccount+1) * sizeof(svcinfo_t));
	for (walk=head, i=0; (walk && (i < svccount)); walk = walk->next, i++) {
		svcinfo[i].svcname = walk->rec->svcname;
		svcinfo[i].sendtxt = walk->rec->sendtxt;
		svcinfo[i].sendlen = walk->rec->sendlen;
		svcinfo[i].exptext = walk->rec->exptext;
		svcinfo[i].explen  = walk->rec->explen;
		svcinfo[i].expofs  = walk->rec->expofs;
		svcinfo[i].flags   = walk->rec->flags;
		svcinfo[i].port    = walk->rec->port;
		svcinfo[i].alpns   = walk->rec->alpns;
		/*
		 * "options telnet" IS "start iac", spelled the way it was before
		 * there were steps. Give it the step, at the front, so one
		 * implementation serves both -- they used to diverge, and the option
		 * stripped the peer's requests without ever answering them.
		 */
		if (walk->rec->flags & TCP_TELNET) {
			svcstep_t *st;
			int have = 0;

			for (st = walk->rec->steps; (st); st = st->next)
				if (st->type == STEP_STARTIAC) { have = 1; break; }

			if (!have) {
				svcstep_t *first_step = (svcstep_t *)calloc(1, sizeof(svcstep_t));

				if (!first_step) {
					errprintf("Service %s: out of memory adding telnet negotiation\n",
						  walk->rec->svcname);
					walk->rec->flags |= TCP_DIALOGUE_BROKEN;
				}
				else {
					first_step->type = STEP_STARTIAC;
					first_step->text = dup_bytes((unsigned char *)"", 0);
					first_step->len  = 0;
					first_step->next = walk->rec->steps;
					walk->rec->steps = first_step;
				}
			}
		}

		svcinfo[i].steps   = walk->rec->steps;
		{
			svcstep_t *st; int nsend = 0, nexp = 0, nstarttls = 0;

			for (st = walk->rec->steps; (st); st = st->next) {
				if (st->type == STEP_SEND) nsend++;
				else if (st->type == STEP_STARTTLS) nstarttls++;
				else nexp++;
			}

			/*
			 * Everything here is run by the dialogue driver, whatever its
			 * shape. Two matchers for one file is what that avoids: they
			 * agree on every ordinary reply and differ on one split across
			 * segments, where the older arm judges the first read alone and
			 * calls a healthy server down. That arm stays for the
			 * compiled-in http/https tests, which never come from here.
			 */
			svcinfo[i].flags |= TCP_DIALOGUE;

			/*
			 * One connection is encrypted once. With "options ssl" the
			 * handshake has already happened at connect, so the upgrade
			 * step does nothing -- a file that says it upgrades never
			 * does, and the plaintext half it describes is never spoken.
			 */
			if (nstarttls && (svcinfo[i].flags & TCP_SSL)) {
				errprintf("Service %s: both 'options ssl' and 'start tls' - "
					  "ssl negotiates at connect, so the upgrade would do nothing\n",
					  svcinfo[i].svcname);
				svcinfo[i].flags |= TCP_DIALOGUE_BROKEN;
			}

			/*
			 * Refused entries are driven here too, whatever their shape:
			 * the driver is what honours the refusal (curstep stays NULL).
			 * The legacy path would send the command regardless.
			 */
			if (svcinfo[i].flags & TCP_DIALOGUE_BROKEN)
				svcinfo[i].flags |= TCP_DIALOGUE;
		}
	}
	memset(&svcinfo[svccount], 0, sizeof(svcinfo_t));

	/* This should not happen */
	if (walk) {
		errprintf("Whoa - didn't copy all services! svccount=%d, next service '%s'\n", 
			svccount, walk->rec->svcname);
	}

	/* Free the temp. svclist list */
	while (head) {
		/*
		 * Note: free the record struct itself, but NOT the strings
		 * inside it - those are now owned by the svcinfo records.
		 */
		walk = head;
		head = head->next;
		xfree(walk->rec);
		xfree(walk);
	}

	searchstring = strdup(xgetenv("XYMONNETSVCS"));
	searchstring_buflen = strlen(searchstring) + 1;
	SBUF_MALLOC(xymonnetsvcs, strlen(xgetenv("XYMONNETSVCS")) + svcnamebytes + 1);
	strncpy(xymonnetsvcs, xgetenv("XYMONNETSVCS"), xymonnetsvcs_buflen);
	for (i=0; (svcinfo[i].svcname); i++) {
		char *p;

		strncpy(searchstring, xgetenv("XYMONNETSVCS"), searchstring_buflen);
		p = strtok(searchstring, " ");
		while (p && (strcmp(p, svcinfo[i].svcname) != 0)) p = strtok(NULL, " ");

		if (p == NULL) {
			char *eos = xymonnetsvcs + strlen(xymonnetsvcs);
			snprintf(eos, (xymonnetsvcs_buflen - (eos - xymonnetsvcs)), " %s", svcinfo[i].svcname);
		}
	}
	xfree(searchstring);

	if (debug) {
		dump_tcp_services();
		dbgprintf("XYMONNETSVCS set to : %s\n", xymonnetsvcs);
	}

	MEMUNDEFINE(filename);
	return xymonnetsvcs;
}

void dump_tcp_services(void)
{
	int i;

	dbgprintf("Service list dump\n");
	for (i=0; (svcinfo[i].svcname); i++) {
		dbgprintf(" Name      : %s\n", svcinfo[i].svcname);
		dbgprintf("   Sendtext: %s\n", binview(svcinfo[i].sendtxt, svcinfo[i].sendlen));
		dbgprintf("   Sendlen : %d\n", svcinfo[i].sendlen);
		dbgprintf("   Exp.text: %s\n", binview(svcinfo[i].exptext, svcinfo[i].explen));
		dbgprintf("   Exp.len : %d\n", svcinfo[i].explen);
		dbgprintf("   Exp.ofs : %d\n", svcinfo[i].expofs);
		dbgprintf("   Flags   : %d\n", svcinfo[i].flags);
		dbgprintf("   Port    : %d\n", svcinfo[i].port);
	}
	dbgprintf("\n");
}

int default_tcp_port(char *svcname)
{
	int svcidx;
	int result = 0;

	for (svcidx=0; (svcinfo[svcidx].svcname && (strcmp(svcname, svcinfo[svcidx].svcname) != 0)); svcidx++) ;
	if (svcinfo[svcidx].svcname) result = svcinfo[svcidx].port;

	return result;
}

svcinfo_t *find_tcp_service(char *svcname)
{
	int svcidx;

	for (svcidx=0; (svcinfo[svcidx].svcname && (strcmp(svcname, svcinfo[svcidx].svcname) != 0)); svcidx++) ;
	if (svcinfo[svcidx].svcname) 
		return &svcinfo[svcidx];

	if (svccfg_unreadable) {
		/*
		 * The caller reads the returned flags straight away, so NULL is a
		 * crash -- one the compiled-in names used to hide. Hand back a
		 * refused definition instead: refused is what every service is
		 * without the file. One shared entry, named for the reason, since
		 * that name only ever appears in messages.
		 */
		static svcinfo_t refused = { "(no protocols.cfg)", NULL, 0, NULL, 0, 0,
					     (TCP_DIALOGUE_BROKEN | TCP_DIALOGUE), 0 };

		return &refused;
	}

	return NULL;
}

