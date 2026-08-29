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
static svcinfo_t default_svcinfo[] = {
	/*           ------------- data to send ------   ---- green data ------ flags */
	/* name      databytes            length          databytes offset len        */
	{ "ftp",     "quit\r\n",          0,                  "220",	0, 0,	(TCP_GET_BANNER), 21 },
	{ "ssh",     NULL,                0,                  "SSH",	0, 0,	(TCP_GET_BANNER), 22 },
	{ "ssh1",    NULL,                0,                  "SSH",	0, 0,	(TCP_GET_BANNER), 22 },
	{ "ssh2",    NULL,                0,                  "SSH",	0, 0,	(TCP_GET_BANNER), 22 },
	{ "telnet",  NULL,                0,                  NULL,	0, 0,	(TCP_GET_BANNER|TCP_TELNET), 23 },
	{ "smtp",    NULL,                0,                  "220",	0, 0,	(TCP_GET_BANNER), 25 }, /* No send: speaking before the 220 is an RFC 5321/2920 violation */
	{ "pop",     "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 110 },
	{ "pop2",    "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 109 },
	{ "pop-2",   "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 109 },
	{ "pop3",    "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 110 },
	{ "pop-3",   "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER), 110 },
	{ "imap",    "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 143 },
	{ "imap2",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 143 },
	{ "imap3",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 220 },
	{ "imap4",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER), 143 },
	{ "nntp",    "quit\r\n",          0,                  "200",	0, 0,	(TCP_GET_BANNER), 119 },
	{ "ldap",    NULL,                0,                  NULL,     0, 0,	(0), 389 },
	{ "rsync",   NULL,                0,                  "@RSYNCD",0, 0,	(TCP_GET_BANNER), 873 },
	{ "bbd",     "dummy",             0,                  NULL,	0, 0,	(0), 1984 },
	{ "ftps",    "quit\r\n",          0,                  "220",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 990 },
	{ "telnets", NULL,                0,                  NULL, 	0, 0,	(TCP_GET_BANNER|TCP_TELNET|TCP_SSL), 992 },
	{ "smtps",   NULL,                0,                  "220",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 0 }, /* Non-standard port - IANA. No send, as for smtp */
	{ "pop3s",   "quit\r\n",          0,                  "+OK",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 995 },
	{ "imaps",   "ABC123 LOGOUT\r\n", 0,                  "* OK",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 993 },
	{ "nntps",   "quit\r\n",          0,                  "200",	0, 0,	(TCP_GET_BANNER|TCP_SSL), 563 },
	{ "ldaps",   NULL,                0,                  NULL,     0, 0,	(TCP_SSL), 636 },
	{ "clamd",   "PING\r\n",          0,                  "PONG",   0, 0,	(0), 3310 },
	{ "vnc",     "RFB 000.000\r\n",   0,                  "RFB ",   0, 0,   (TCP_GET_BANNER), 5900 },
	{ NULL,      NULL,                0,                  NULL,	0, 0,	(0), 0 }	/* Default behaviour: Just try a connect */
};

static svcinfo_t *svcinfo = default_svcinfo;

/*
 * Append one step to a service's dialogue, preserving file order. Every alias
 * in an [a|b|c] header gets its own copy: the records are freed independently,
 * so they cannot share a list.
 */
static svcstep_t *add_svcstep(svcinfo_t *rec, int type, unsigned char *text, int len)
{
	svcstep_t *step, *walk;

	step = (svcstep_t *)calloc(1, sizeof(svcstep_t));
	step->type = type;
	step->len  = len;
	step->text = (unsigned char *)malloc(len + 1);
	if (text) memcpy(step->text, text, len);
	step->text[len] = '\0';

	if (rec->steps == NULL) rec->steps = step;
	else {
		for (walk = rec->steps; (walk->next); walk = walk->next) ;
		walk->next = step;
	}

	return step;
}


/*
 * A quoted string, verbatim. Regex arguments must NOT go through
 * getescapestring(): it consumes the backslash, so "\\d" reaches PCRE as
 * "d" and "\\+" as "+", quietly turning the pattern into a different one
 * (or an invalid one). PCRE handles its own escapes.
 */
static void getrawstring(char *msg, unsigned char **buf, int *buflen)
{
	char *inp = msg, *end;
	int len;

	if (*inp == '"') inp++;
	end = strchr(inp, '"');
	len = (end ? (int)(end - inp) : (int)strlen(inp));

	*buf = (unsigned char *)malloc(len + 1);
	memcpy(*buf, inp, len);
	(*buf)[len] = '\0';
	if (buflen) *buflen = len;
}


/*
 * Where a quoted string ends. Deliberately the same rule getescapestring
 * uses -- scan to the next '"' -- so the two agree on where the value
 * stops and the trailing keywords begin. (Neither supports \" ; that is a
 * pre-existing limit of the string lexer, not one introduced here.)
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
 * Report a mistyped expansion when the file is read, not when the step
 * runs. ${sha1:x} is otherwise read as a variable literally named
 * "sha1:x", found to be unset, and expanded to nothing -- and a step that
 * is never reached would never say so at all.
 */
static void check_expansions(char *svcname, unsigned char *txt, int len)
{
	int i;

	for (i = 0; (i < len - 1); i++) {
		int depth, j, blen;
		char *colon, body[256];

		if (!((txt[i] == '$') && (txt[i+1] == '{'))) continue;

		depth = 1;
		for (j = i + 2; ((j < len) && depth); j++) {
			if ((txt[j] == '$') && ((j+1) < len) && (txt[j+1] == '{')) { depth++; j++; }
			else if (txt[j] == '}') depth--;
		}
		if (depth) continue;			/* unterminated: left alone */

		blen = (j - 1) - (i + 2);
		if ((blen <= 0) || (blen >= (int)sizeof(body))) continue;
		memcpy(body, txt + i + 2, blen);
		body[blen] = '\0';

		colon = strchr(body, ':');
		if (colon) {
			*colon = '\0';
			if ((strcmp(body, "md5") != 0) && (strcmp(body, "base64") != 0))
				errprintf("Service %s: unknown expansion ${%s:...} - "
					  "known functions are md5 and base64\n", svcname, body);
		}
	}
}


/*
 * Every ${name} a send refers to must be bound by something earlier in the
 * same entry -- a capture, or the username/password that credentials binds.
 * An unbound one is not an error at run time: it expands to nothing, the
 * command goes out malformed, and the server rejects it, so the test fails
 * for a reason that has nothing to do with the mistake.
 *
 * Only plain ${name} references are checked. A body carrying ':' is a
 * function call, and check_expansions() has already vetted the name.
 */
/*
 * Release one entry's step list. The reload path already promises not to
 * leak, and a step list holds more than the strings: the compiled regexes
 * are the largest allocations here, and the credentials a step carries
 * should not outlive the configuration that named them.
 */
static void free_svcsteps(svcinfo_t *rec)
{
	svcstep_t *st = rec->steps;

	while (st) {
		svcstep_t *next = st->next;

		if (st->text)    xfree(st->text);
		if (st->until)   xfree(st->until);
		if (st->target)  xfree(st->target);
		if (st->label)   xfree(st->label);
		if (st->varname) xfree(st->varname);
		if (st->user) { memset(st->user, 0, strlen(st->user)); xfree(st->user); }
		if (st->pass) { memset(st->pass, 0, strlen(st->pass)); xfree(st->pass); }
		if (st->re)      freeregex((pcre2_code *)st->re);
		xfree(st);
		st = next;
	}
	rec->steps = NULL;
}


static int name_is_known(char **known, int nknown, const char *name)
{
	int k;

	for (k = 0; (k < nknown); k++)
		if (strcmp(known[k], name) == 0) return 1;

	return 0;
}


static void check_undefined_vars(svcinfo_t *rec)
{
	svcstep_t *st;
	char *known[64];
	int nknown = 0;

	for (st = rec->steps; (st); st = st->next) {
		int i;

		if (st->type == STEP_CREDS) {
			if (nknown + 2 <= 64) {
				known[nknown++] = "username";
				known[nknown++] = "password";
			}
			continue;
		}

		if (st->type == STEP_CAPTURE) {
			/* "as a;b;c" binds three names, not one called "a;b;c". */
			if (st->varname) {
				char *p = st->varname;

				while (p && *p && (nknown < 64)) {
					char *semi = strchr(p, ';');
					int len = (semi ? (int)(semi - p) : (int)strlen(p));
					char *one = (char *)malloc(len + 1);

					memcpy(one, p, len); one[len] = '\0';
					known[nknown++] = one;
					p = (semi ? semi + 1 : NULL);
				}
			}
			continue;
		}

		if (st->type == STEP_WHEN) {
			/* Testing a name nothing binds silently takes the else-arm. */
			if (st->varname && !name_is_known(known, nknown, st->varname))
				errprintf("Service %s: 'when %s ~ ...' tests a value that is never "
					  "captured before it - the test can only fail\n",
					  rec->svcname, st->varname);
			continue;
		}

		if ((st->type != STEP_SEND) || !st->text) continue;

		for (i = 0; (i < st->len - 1); i++) {
			int j, blen;
			char name[128];

			if (!((st->text[i] == '$') && (st->text[i+1] == '{'))) continue;

			/* Plain ${name} only: a '{' or ':' inside means a nested
			   reference or a function call, and check_expansions()
			   has already vetted the function name. */
			for (j = i + 2; ((j < st->len) && (st->text[j] != '}')); j++)
				if ((st->text[j] == '{') || (st->text[j] == ':')) break;
			if ((j >= st->len) || (st->text[j] != '}')) continue;

			blen = j - (i + 2);
			if ((blen <= 0) || (blen >= (int)sizeof(name))) continue;
			memcpy(name, st->text + i + 2, blen);
			name[blen] = '\0';

			if (!name_is_known(known, nknown, name))
				errprintf("Service %s: ${%s} is never captured or bound before it is "
					  "used - it will expand to nothing\n", rec->svcname, name);

			i = j;
		}
	}
}


/*
 * Two alternatives can both match the same reply exactly when one is a
 * prefix of the other -- matching is prefix-anchored, so patterns that
 * diverge anywhere can never both match. That makes this check complete
 * rather than a heuristic.
 *
 * Such a group is ambiguous: which one wins depends on how much of the
 * reply has arrived, and therefore on how the server split it across
 * packets. Say so, and record the longest pattern so the driver can wait
 * for the group to be decidable instead of racing.
 */
static void mark_ambiguous_groups(svcinfo_t *rec)
{
	svcstep_t *st;

	for (st = rec->steps; (st); st = st->next) {
		svcstep_t *a, *b, *last;
		int maxlen = 0, clash = 0;

		if (st->type != STEP_EXPECT) continue;

		/* the group is this step and the expects immediately after it */
		for (last = st; (last->next && (last->next->type == STEP_EXPECT)); last = last->next) ;
		for (a = st; ; a = a->next) {
			if (a->len > maxlen) maxlen = a->len;
			if (a == last) break;
		}

		for (a = st; (a != last); a = a->next) {
			for (b = a->next; ; b = b->next) {
				int n = (a->len < b->len) ? a->len : b->len;

				if ((n > 0) && (memcmp(a->text, b->text, n) == 0)) {
					clash = 1;
					errprintf("Service %s: 'expect \"%.30s\"' and 'expect \"%.30s\"' overlap - "
						  "both match the same reply, and which one wins depends on how "
						  "the server split it\n", rec->svcname, a->text, b->text);
				}
				if (b == last) break;
			}
		}

		if (clash)
			for (a = st; ; a = a->next) {
				a->ambiguous = 1;
				a->maxaltlen = maxlen;
				if (a == last) break;
			}

		st = last;	/* skip past the group we just examined */
	}
}

/* Has this entry produced an expect yet? A capture binds the reply that the
   preceding expect accepted, so one with nothing in front of it can only ever
   bind an empty value. */
static int has_expect_step(svcinfo_t *rec)
{
	svcstep_t *st;

	for (st = rec->steps; (st); st = st->next)
		if (st->type == STEP_EXPECT) return 1;

	return 0;
}


/*
 * Resolve "goto NAME" to the step that "label NAME" defines. Done once
 * after the whole entry is read, so a goto may point forwards -- a retry
 * loop points backwards, and both are ordinary edges here.
 */
static void resolve_svcsteps(svcinfo_t *rec)
{
	svcstep_t *st, *lbl;

	for (st = rec->steps; (st); st = st->next) {
		if (st->action != ACT_GOTO || st->target == NULL) continue;

		for (lbl = rec->steps; (lbl); lbl = lbl->next)
			if ((lbl->type == STEP_LABEL) && lbl->label &&
			    (strcmp(lbl->label, st->target) == 0)) break;

		if (lbl) st->targetstep = lbl;
		else {
			errprintf("Service %s: 'goto %s' has no matching state - "
				  "the branch is dropped\n", rec->svcname, st->target);
			st->action = ACT_NEXT;
		}
	}
}


typedef struct svclist_t {
	struct svcinfo_t *rec;
	struct svclist_t *next;
} svclist_t;


/*
 * Emit one step to every record in the current [a|b|c] section. `first`
 * heads that section and the aliases follow it, so walking to the end of
 * the list is exactly this section -- later sections have not been read.
 */
static void emit_step(svclist_t *first, svcstep_t *tmpl)
{
	svclist_t *walk;

	for (walk = first; (walk); walk = walk->next) {
		svcstep_t *st = add_svcstep(walk->rec, tmpl->type, tmpl->text, tmpl->len);

		st->action  = tmpl->action;
		st->seconds = tmpl->seconds;
		if (tmpl->target)  st->target  = strdup(tmpl->target);
		if (tmpl->label)   st->label   = strdup(tmpl->label);
		if (tmpl->varname) st->varname = strdup(tmpl->varname);
		if (tmpl->user)    st->user    = strdup(tmpl->user);
		if (tmpl->pass)    st->pass    = strdup(tmpl->pass);
		if (tmpl->until) {
			st->until = (unsigned char *)malloc(tmpl->untillen + 1);
			memcpy(st->until, tmpl->until, tmpl->untillen);
			st->until[tmpl->untillen] = '\0';
			st->untillen = tmpl->untillen;
		}

		/* One compiled copy per record: records are freed independently. */
		if (tmpl->re) st->re = (void *)compileregex_opts((char *)tmpl->text, 0);
	}
}


/*
 * Secrets do not belong in protocols.cfg -- it is world-readable and gets
 * pasted into bug reports. "credentials NAME" names an entry in
 * etc/credentials.cfg instead:
 *
 *     NAME	username	password
 */
static int lookup_credentials(char *name, char **user, char **pass)
{
	char fn[PATH_MAX];
	FILE *fd;
	char line[1024];
	int found = 0;

	*user = *pass = NULL;
	snprintf(fn, sizeof(fn), "%s/etc/credentials.cfg",
		 (xgetenv("XYMONHOME") ? xgetenv("XYMONHOME") : "."));
	fd = fopen(fn, "r");
	if (fd == NULL) {
		errprintf("Cannot open %s for 'credentials %s'\n", fn, name);
		return 0;
	}

	while (!found && fgets(line, sizeof(line), fd)) {
		char *nam, *u, *p;

		if ((line[0] == '#') || (line[0] == '\n')) continue;
		nam = strtok(line, " \t\r\n");
		if (!nam || strcmp(nam, name) != 0) continue;
		u = strtok(NULL, " \t\r\n");
		p = strtok(NULL, " \t\r\n");
		if (u && p) { *user = strdup(u); *pass = strdup(p); found = 1; }
	}
	fclose(fd);

	if (!found) errprintf("No credentials entry named '%s' in %s\n", name, fn);
	return found;
}


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
		errprintf("Cannot open TCP service-definitions file %s - using defaults\n", filename);
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
				svcstep_t tmpl;
				unsigned char *txt = NULL;
				int txtlen = 0;

				getescapestring(skipwhitespace(l+4), &txt, &txtlen);
				/*
				 * sendtxt keeps the FIRST send only, which is what a
				 * single-step probe has always meant. The step list is
				 * what a dialogue is driven from.
				 */
				for (walk = first; (walk); walk = walk->next) {
					if (walk->rec->sendtxt == NULL) {
						walk->rec->sendtxt = (unsigned char *)strdup((char *)txt);
						walk->rec->sendlen = txtlen;
					}
				}
				check_expansions(first->rec->svcname, txt, txtlen);

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_SEND; tmpl.text = txt; tmpl.len = txtlen;
				emit_step(first, &tmpl);
				xfree(txt);
			}
		}
		else if (strncmp(l, "expect ", 7) == 0) {
			if (first) {
				svcstep_t tmpl;
				unsigned char *txt = NULL, *untiltxt = NULL;
				int txtlen = 0, untillen = 0;
				char *rest, *act;

				getescapestring(skipwhitespace(l+6), &txt, &txtlen);

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_EXPECT; tmpl.text = txt; tmpl.len = txtlen;

				rest = skipwhitespace(after_quoted(skipwhitespace(l+6)));

				/*
				 * "until" says where the reply ENDS. Without it an expect
				 * takes a single line, which is wrong for every protocol that
				 * answers with several: SMTP and FTP continue with "250-" and
				 * finish with "250 ", NNTP ends a block with ".", IMAP ends
				 * with the command tag. Naming the terminator covers all three
				 * without teaching the parser any of them.
				 */
				if (strncmp(rest, "until ", 6) == 0) {
					char *tp = skipwhitespace(rest + 5);

					getescapestring(tp, &untiltxt, &untillen);
					tmpl.until = untiltxt;
					tmpl.untillen = untillen;
					rest = skipwhitespace(after_quoted(tp));
				}

				/* Anything left after that is this edge's action. */
				if (*rest) {
					act = strtok(rest, " \t");
					if (act && (strcmp(act, "->") == 0)) {
						/*
						 * Every edge is "<condition> -> TARGET". "fail" is
						 * a reserved target: the dialogue ends there.
						 */
						char *tgt = strtok(NULL, " \t");

						if (!tgt) errprintf("'expect ... ->' with no target\n");
						else if (strcmp(tgt, "fail") == 0) tmpl.action = ACT_FAIL;
						else { tmpl.target = tgt; tmpl.action = ACT_GOTO; }
					}
					else if (act) errprintf("Unknown expect action: %s - edges are "
								"'<condition> -> TARGET'\n", act);
				}

				for (walk = first; (walk); walk = walk->next) {
					if (walk->rec->exptext == NULL) {
						walk->rec->exptext = (unsigned char *)strdup((char *)txt);
						walk->rec->explen  = txtlen;
						walk->rec->expofs  = 0; /* HACK - not used right now */
					}
				}
				emit_step(first, &tmpl);
				xfree(txt);
				if (untiltxt) xfree(untiltxt);
			}
		}
		else if (strncmp(l, "state ", 6) == 0) {
			/*
			 * Names the state that follows: the expects up to the next
			 * step that is not one. It is also what "goto" aims at, so
			 * naming a state and marking a jump target are the same act
			 * rather than two keywords for one idea.
			 */
			if (first) {
				svcstep_t tmpl;

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_LABEL;
				tmpl.label = strtok(skipwhitespace(l+5), " \t");
				if (tmpl.label) emit_step(first, &tmpl);
				else errprintf("'state' with no name\n");
			}
		}
		else if (strncmp(l, "capture-regex ", 14) == 0) {
			if (first) {
				svcstep_t tmpl;
				unsigned char *txt = NULL;
				int txtlen = 0;
				char *rest, *kw;

				getrawstring(skipwhitespace(l+13), &txt, &txtlen);
				rest = skipwhitespace(after_quoted(skipwhitespace(l+13)));
				kw = strtok(rest, " \t");

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_CAPTURE; tmpl.text = txt; tmpl.len = txtlen;
				tmpl.re = (void *)1;	/* emit_step compiles one per record */
				if (kw && (strcmp(kw, "as") == 0)) tmpl.varname = strtok(NULL, " \t");

				if (tmpl.varname) {
					pcre2_code *probe;
					uint32_t ngroups = 0;

					if (!has_expect_step(first->rec))
						errprintf("Service %s: 'capture-regex ... as %s' before any expect - "
							  "there is no reply to capture from, it will bind empty\n",
							  first->rec->svcname, tmpl.varname);

					/*
					 * The value bound is group 1, so a pattern with no
					 * parenthesised group can never bind anything: ${name}
					 * would expand to nothing for every reply, forever.
					 * Unconditional, so worth saying at load time.
					 */
					probe = compileregex_opts((char *)txt, 0);
					if (probe) {
						uint32_t nnames = 1;
						char *p;

						for (p = tmpl.varname; (*p); p++) if (*p == ';') nnames++;

						pcre2_pattern_info(probe, PCRE2_INFO_CAPTURECOUNT, &ngroups);
						freeregex(probe);

						/*
						 * One name per group, in order. A mismatch means
						 * either a group whose value is thrown away or a
						 * name that can never be bound, and both are
						 * silent at run time -- so say it here.
						 */
						if (ngroups == 0)
							errprintf("Service %s: 'capture-regex \"%s\" as %s' has no "
								  "capture group - it can never bind a value\n",
								  first->rec->svcname, txt, tmpl.varname);
						else if (ngroups != nnames)
							errprintf("Service %s: 'capture-regex \"%s\" as %s' has %u "
								  "capture group(s) but %u name(s)\n",
								  first->rec->svcname, txt, tmpl.varname,
								  ngroups, nnames);
					}

					emit_step(first, &tmpl);
				}
				else errprintf("Usage: capture-regex \"regex\" as NAME\n");
				xfree(txt);
			}
		}
		else if (strncmp(l, "capture ", 8) == 0) {
			/* No regex: bind the whole reply that just matched. */
			if (first) {
				svcstep_t tmpl;
				char *kw = strtok(skipwhitespace(l+7), " \t");

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_CAPTURE;
				if (kw && (strcmp(kw, "as") == 0)) tmpl.varname = strtok(NULL, " \t");

				if (tmpl.varname) {
					if (!has_expect_step(first->rec))
						errprintf("Service %s: 'capture as %s' before any expect - "
							  "there is no reply to capture from, it will bind empty\n",
							  first->rec->svcname, tmpl.varname);
					emit_step(first, &tmpl);
				}
				else errprintf("Usage: capture as NAME\n");
			}
		}
		else if (strncmp(l, "credentials ", 12) == 0) {
			if (first) {
				svcstep_t tmpl;
				char *nam = strtok(skipwhitespace(l+11), " \t");

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_CREDS;
				if (nam && lookup_credentials(nam, &tmpl.user, &tmpl.pass)) {
					tmpl.varname = nam;
					emit_step(first, &tmpl);
					if (tmpl.user) xfree(tmpl.user);
					if (tmpl.pass) xfree(tmpl.pass);
				}
			}
		}
		else if (strncmp(l, "else ", 5) == 0) {
			/*
			 * The arm taken when no '~' edge in this state matched. An
			 * unconditional edge that only makes sense after one, so it is
			 * emitted as a jump and never reached if a '~' above it already
			 * left the state.
			 */
			char *rest = skipwhitespace(l + 5);

			if (strncmp(rest, "->", 2) != 0) errprintf("'else' without '->'\n");
			else if (first) {
				svcstep_t tmpl;
				char *tgt = strtok(skipwhitespace(rest + 2), " \t");

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_JUMP;
				if (!tgt) errprintf("'else ->' with no target\n");
				else if (strcmp(tgt, "fail") == 0) tmpl.action = ACT_FAIL;
				else { tmpl.target = tgt; tmpl.action = ACT_GOTO; }
				emit_step(first, &tmpl);
			}
		}
		else if (strstr(l, "~") && strstr(l, "->")) {
			/*
			 * "NAME ~ \"regex\" -> TARGET": branch on a value already bound.
			 * Recognised by shape rather than by a leading keyword, because
			 * the line begins with the name being tested. The regex tests a
			 * captured value, never the socket, so it decides nothing about
			 * what arrives and cannot race a partial read -- which is why a
			 * regex is allowed here and not in an expect.
			 */
			if (first) {
				svcstep_t tmpl;
				char *var, *op, *rest;
				unsigned char *txt = NULL;
				int txtlen = 0;

				var = strtok(l, " \t");
				op  = (var ? strtok(NULL, " \t") : NULL);
				rest = (op ? skipwhitespace(op + strlen(op) + 1) : NULL);

				if (!var || !op || (strcmp(op, "~") != 0) || !rest || !*rest) {
					errprintf("Usage: NAME ~ \"regex\" -> TARGET\n");
				}
				else {
					char *arrow;

					getrawstring(rest, &txt, &txtlen);
					arrow = strstr(skipwhitespace(after_quoted(rest)), "->");

					memset(&tmpl, 0, sizeof(tmpl));
					tmpl.type = STEP_WHEN;
					tmpl.varname = var;
					tmpl.text = txt; tmpl.len = txtlen;
					tmpl.re = (void *)1;	/* emit_step compiles a copy per record */

					if (!arrow) errprintf("'%s ~ ...' with no '->'\n", var);
					else {
						char *tgt = strtok(skipwhitespace(arrow + 2), " \t");

						if (!tgt) errprintf("'%s ~ ... ->' with no target\n", var);
						else if (strcmp(tgt, "fail") == 0) tmpl.action = ACT_FAIL;
						else { tmpl.target = tgt; tmpl.action = ACT_GOTO; }
						emit_step(first, &tmpl);
					}
				}
				if (txt) xfree(txt);
			}
		}
		else if (strncmp(l, "timeout ", 8) == 0) {
			/*
			 * How long the wait that follows may take. Without one, a
			 * step that never matches is bounded only by --timeout, which
			 * ends the whole test and cannot say which step stalled.
			 *
			 * The budget covers the write as well as the read: a peer that
			 * will not accept the send is as stuck as one that will not
			 * answer it, and the global ceiling still wins either way.
			 */
			char *arg = skipwhitespace(l + 8);
			int secs = atoi(arg);

			if (secs <= 0) {
				errprintf("Service %s: 'timeout %s' - the budget must be a positive number of seconds\n",
					  (first ? first->rec->svcname : "?"), arg);
			}
			else if (first) {
				svcstep_t tmpl;

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_TIMEOUT;
				tmpl.seconds = secs;
				emit_step(first, &tmpl);
			}
		}
		else if (strcmp(l, "starttls") == 0) {
			/*
			 * Explicit TLS. Everything before this line is plaintext,
			 * everything after it is encrypted on the same socket. The
			 * service does NOT carry TCP_SSL -- that flag means TLS from
			 * the first byte, which is the other thing entirely.
			 */
			if (first) {
				svcstep_t tmpl;

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_STARTTLS;
				emit_step(first, &tmpl);
			}
		}
		else if (strncmp(l, "options ", 8) == 0) {
			if (first) {
				char *opt;

				first->rec->flags = 0;
				l = skipwhitespace(l+7);
				opt = strtok(l, ",");
				while (opt) {
					if      (strcmp(opt, "ssl") == 0)    first->rec->flags |= TCP_SSL;
					else if (strcmp(opt, "banner") == 0) first->rec->flags |= TCP_GET_BANNER;
					else if (strcmp(opt, "telnet") == 0) first->rec->flags |= TCP_TELNET;
					else if (strncmp(opt, "alpn=", 5) == 0) {
						first->rec->alpns = strdup(opt+5);
					}
					else errprintf("Unknown option: %s\n", opt);

					opt = strtok(NULL, ",");
				}
				for (walk = first->next; (walk); walk = walk->next) {
					walk->rec->flags = first->rec->flags;
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
			 * Say so. An unrecognised line used to be a silent no-op, so
			 * a typo simply deleted a step and the probe ran anyway --
			 * reporting green on a conversation it never had. This is the
			 * same treatment "options" already gives an unknown option.
			 */
			errprintf("Unknown protocols.cfg directive%s%s: %s\n",
				  (first ? " in service " : ""),
				  (first ? first->rec->svcname : ""), l);
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
		svcinfo[i].steps   = walk->rec->steps;
		resolve_svcsteps(&svcinfo[i]);
		/*
		 * A dialogue is anything that is not the classic one-shot shape.
		 * Deciding it here, once, keeps do_tcp_tests() from re-deriving it
		 * per pass -- and means every existing entry ("send" then "expect",
		 * or either alone) keeps the exact code path it has always used.
		 */
		{
			svcstep_t *st;
			int nsend = 0, nexp = 0, nother = 0, first_is_expect = 0;

			for (st = walk->rec->steps; (st); st = st->next) {
				if (st == walk->rec->steps) first_is_expect = (st->type == STEP_EXPECT);
				if (st->type == STEP_SEND) nsend++;
				else if (st->type == STEP_EXPECT) {
					nexp++;
					if (st->action != ACT_NEXT) nother++;	/* a branch edge */
				}
				else nother++;					/* when/capture/label/... */
			}
			/*
			 * A lone "expect" with nothing to send is the classic
			 * shape -- rsync and svn are exactly that -- so it stays
			 * on the old path. What needs the driver is an expect that
			 * something is sent AFTER, which is the ordering the
			 * single-shot probe cannot express.
			 */
			if ((first_is_expect && (nsend > 0)) || (nsend > 1) || (nexp > 1) || nother)
				svcinfo[i].flags |= TCP_DIALOGUE;

			check_undefined_vars(&svcinfo[i]);
			mark_ambiguous_groups(&svcinfo[i]);

			/*
			 * "options ssl" is TLS from the first byte; "starttls" upgrades
			 * a plaintext connection part-way. Asking for both would start a
			 * second handshake inside the first, which fails in a way that
			 * reads like a broken server rather than a broken config.
			 */
			if (svcinfo[i].flags & TCP_SSL) {
				for (st = walk->rec->steps; (st); st = st->next) {
					if (st->type != STEP_STARTTLS) continue;
					errprintf("Service %s: 'starttls' with 'options ssl' - the connection "
						  "is already TLS; drop one of them\n", svcinfo[i].svcname);
					break;
				}
			}
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
	else
		return NULL;
}

