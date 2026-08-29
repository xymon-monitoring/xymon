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
		if (st->srcname) xfree(st->srcname);
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

		/* "expect ... as NAME" binds the reply it accepted. */
		if ((st->type == STEP_EXPECT) && st->varname) {
			if (nknown < 64) known[nknown++] = st->varname;
			continue;
		}

		if (st->type == STEP_CAPTURE) {
			/*
			 * An extraction reads a name and binds others. Reading one
			 * that nothing binds yields an empty value for every reply,
			 * which is silent at run time.
			 */
			if (st->srcname && !name_is_known(known, nknown, st->srcname))
				errprintf("Service %s: '%s ~ ... as %s' reads a value that is "
					  "never bound before it - it can only bind empty\n",
					  rec->svcname, st->srcname,
					  (st->varname ? st->varname : "?"));

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
				errprintf("Service %s: '%s ~ ...' tests a value that is never "
					  "bound before it - the test can only fail\n",
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
 * rather than a heuristic, which is what lets it refuse rather than warn.
 *
 * Which one would win depends on how much of the reply has arrived, and
 * so on how the server split it across packets. Refuse the definition and
 * name the fix: match the shared prefix in one state, distinguish in the
 * next.
 */
/* The three names an edge may use instead of a state. */
#define IS_TERMINAL_NAME(s) \
	(((s) != NULL) && ((strcmp((s), "success") == 0) || \
	                   (strcmp((s), "warning") == 0) || \
	                   (strcmp((s), "fail") == 0)))

/*
 * The shape of a state, checked when the file is read.
 *
 * protocols.cfg(5) says a state does one thing and waits for one answer: at
 * most one action, the clock that bounds the wait, and the expects that end
 * the state by naming where each answer leads. Those were conventions that
 * the parser accepted any violation of, so a file could read as one machine
 * and run as another -- a clock below the expects bounds nothing, a second
 * wait in a state has no name to fail under, and an expect with no target
 * leaves the file silent about where the dialogue went.
 *
 * Every one of them is decidable here, which is where this grammar has
 * decided such things belong.
 */
static void refuse_misshapen_states(svcinfo_t *rec)
{
	svcstep_t *st;
	char *statename = NULL;
	int actions = 0, sawexpect = 0;

	/* Only entries that use states promise this shape. */
	for (st = rec->steps; (st); st = st->next) if (st->type == STEP_LABEL) break;
	if (!st) return;

	statename = NULL;
	for (st = rec->steps; (st); st = st->next) {
		switch (st->type) {
		  case STEP_LABEL:
			statename = st->label;
			actions = sawexpect = 0;
			/*
			 * success, warning and fail are answers, not places. An edge
			 * naming one of them declares the verdict before any label is
			 * looked up, so a state called success can be written, parsed,
			 * and never reached -- and the reachability check cannot see
			 * it, because the edge did resolve.
			 */
			if (statename && (IS_TERMINAL_NAME(statename))) {
				errprintf("Service %s: state '%s' takes a name reserved for a verdict - "
					  "'-> %s' ends the test rather than jumping, so this state can "
					  "never be reached\n", rec->svcname, statename, statename);
				rec->flags |= TCP_DIALOGUE_BROKEN;
			}
			break;

		  case STEP_SEND:
		  case STEP_STARTTLS:
		  case STEP_CREDS:
			if (!statename) break;
			if (++actions > 1) {
				errprintf("Service %s: state '%s' has more than one action - a state "
					  "does one thing and waits for one answer, so give the second "
					  "action a state of its own and reach it with an edge\n",
					  rec->svcname, statename);
				rec->flags |= TCP_DIALOGUE_BROKEN;
			}
			if (sawexpect) {
				errprintf("Service %s: state '%s' acts again after it has waited - that "
					  "is a second wait in one state, and nothing can name the "
					  "state it failed in\n", rec->svcname, statename);
				rec->flags |= TCP_DIALOGUE_BROKEN;
			}
			break;

		  case STEP_TIMEOUT:
		  case STEP_IDLE:
			if (!statename) break;
			if (sawexpect) {
				errprintf("Service %s: state '%s' sets a clock below its expects, where it "
					  "bounds nothing - a clock arms the wait that FOLLOWS it\n",
					  rec->svcname, statename);
				rec->flags |= TCP_DIALOGUE_BROKEN;
			}
			break;

		  case STEP_EXPECT:
			if (!statename) break;
			/*
			 * Consecutive expects are ONE wait. A second wait would need a
			 * step between the groups, and every such step -- an action or
			 * a clock -- is already refused above, so there is nothing left
			 * to count here.
			 */
			sawexpect = 1;
			if (st->action == ACT_NEXT) {
				errprintf("Service %s: state '%s' has an expect with no '-> TARGET' - an "
					  "expect ends its state, so the file has to say where the "
					  "dialogue goes next\n", rec->svcname, statename);
				rec->flags |= TCP_DIALOGUE_BROKEN;
			}
			break;

		  default:
			break;
		}
	}
}


static void refuse_overlapping_groups(svcinfo_t *rec)
{
	svcstep_t *st;

	for (st = rec->steps; (st); st = st->next) {
		svcstep_t *a, *b, *last;

		if (st->type != STEP_EXPECT) continue;

		/* the group is this step and the expects immediately after it */
		for (last = st; (last->next && (last->next->type == STEP_EXPECT)); last = last->next) ;

		for (a = st; (a != last); a = a->next) {
			if (a->oneof) continue;		/* EOF competes with no byte pattern */
			for (b = a->next; ; b = b->next) {
				int n;

				/*
				 * A frame competes with everything: "expect bytes(3)" and
				 * "expect \"220\"" both accept the same three bytes, and
				 * which one won would depend on nothing the file says. Two
				 * frames are worse -- the shorter always wins, so the longer
				 * is dead. Neither is decidable by looking at the bytes, so
				 * refuse the group rather than pick.
				 */
				if (a->wantbytes || b->wantbytes) {
					if (!b->oneof) {
						errprintf("Service %s: 'expect bytes(%d)' shares a state with another "
							  "expect - a frame is decided by length and a literal by "
							  "content, so which one takes the reply is unsayable. Give "
							  "the frame a state of its own\n",
							  rec->svcname, a->wantbytes ? a->wantbytes : b->wantbytes);
						rec->flags |= TCP_DIALOGUE_BROKEN;
					}
					if (b == last) break;
					continue;
				}

				n = (b->oneof) ? 0 : ((a->len < b->len) ? a->len : b->len);

				if ((n > 0) && (memcmp(a->text, b->text, n) == 0)) {
					/*
					 * An error that only says "these overlap" leaves the
					 * author stuck, because what they wanted is reasonable.
					 * Name the fix.
					 */
					errprintf("Service %s: 'expect \"%.30s\"' and 'expect \"%.30s\"' overlap - "
						  "both match the same reply. Match the shared prefix in one "
						  "state, then distinguish with 'expect ... as NAME' and a "
						  "'NAME ~ ...' edge in the next state\n",
						  rec->svcname, a->text, b->text);
					rec->flags |= TCP_DIALOGUE_BROKEN;
				}
				if (b == last) break;
			}
		}

		st = last;	/* skip past the group we just examined */
	}
}

/*
 * Resolve "-> NAME" to the step that "state NAME" defines. Done once
 * after the whole entry is read, so an edge may point forwards -- a retry
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
			errprintf("Service %s: '-> %s' has no matching state - "
				  "the branch is dropped\n", rec->svcname, st->target);
			st->action = ACT_NEXT;
		}
	}

	if (rec->startlabel) {
		for (lbl = rec->steps; (lbl); lbl = lbl->next)
			if ((lbl->type == STEP_LABEL) && lbl->label &&
			    (strcmp(lbl->label, rec->startlabel) == 0)) break;

		if (lbl) rec->startstep = lbl;
		else {
			errprintf("Service %s: 'start %s' has no matching state\n",
				  rec->svcname, rec->startlabel);
			rec->flags |= TCP_DIALOGUE_BROKEN;
		}
	}
}


/*
 * Three mistakes that are properties of the graph rather than of any one
 * line, and so cannot be seen a line at a time:
 *
 *   - a state nothing can reach: dead config, usually an edge naming the
 *     wrong existing state, which the "no matching state" check passes
 *   - a state that waits with no timer: legal, but it can only ever fail
 *     as an unattributed global timeout, which is the complaint this
 *     whole feature exists to answer
 *   - a state with no way to finish: a cycle whose every exit leads back
 *     into itself, so the dialogue can never end except on the ceiling
 *
 * Run after resolve_svcsteps(), which is what turns targets into pointers.
 */
static void check_graph(svcinfo_t *rec)
{
	svcstep_t *st;
	int nsteps = 0, nlabels = 0, i, changed;
	svcstep_t **idx;
	char *reach, *ends;

	for (st = rec->steps; (st); st = st->next) {
		nsteps++;
		if (st->type == STEP_LABEL) nlabels++;
	}
	if (nsteps == 0) return;

	/*
	 * Only entries written as a state machine. A positional dialogue has
	 * no states to be unreachable and no state boundary for a timer to
	 * belong to, so every one of these would fire on it and say nothing
	 * -- and a check that cries wolf on the shipped file gets ignored,
	 * which costs the real warnings too.
	 */
	if (nlabels == 0) return;

	idx   = (svcstep_t **)calloc(nsteps, sizeof(svcstep_t *));
	reach = (char *)calloc(nsteps, 1);
	ends  = (char *)calloc(nsteps, 1);
	if (!idx || !reach || !ends) { if (idx) xfree(idx); if (reach) xfree(reach);
				       if (ends) xfree(ends); return; }

	for (st = rec->steps, i = 0; (st); st = st->next, i++) idx[i] = st;

	/* Which steps can be entered at all, from wherever the dialogue starts. */
	for (i = 0; i < nsteps; i++)
		if (idx[i] == (rec->startstep ? rec->startstep : rec->steps)) reach[i] = 1;
	do {
		changed = 0;
		for (i = 0; i < nsteps; i++) {
			int j;

			if (!reach[i]) continue;
			st = idx[i];
			/*
			 * A step that always leaves does not fall through to the
			 * one written after it: an unconditional jump goes to its
			 * target, and a terminal ends the dialogue. Following
			 * ->next regardless would make every state reachable and
			 * the check useless.
			 */
			/*
			 * A timeout or idle edge is a way OUT of the state, not a
			 * step the dialogue stops at: the state carries on past it
			 * when the budget does not fire. Letting the terminal test
			 * below claim "timeout(10) -> fail" ends the walk there, so
			 * every state after it was reported unreachable -- which is
			 * every state in an entry that budgets its first one.
			 */
			if ((st->type == STEP_TIMEOUT) || (st->type == STEP_IDLE)) {
				if ((st->action == ACT_GOTO) && st->targetstep)
					for (j = 0; j < nsteps; j++)
						if ((idx[j] == st->targetstep) && !reach[j]) { reach[j] = 1; changed = 1; }
				if (st->next)
					for (j = 0; j < nsteps; j++)
						if ((idx[j] == st->next) && !reach[j]) { reach[j] = 1; changed = 1; }
				continue;
			}
			/*
			 * An expect that takes an edge does not continue to whatever
			 * follows its group -- that step is entered only when some
			 * alternative matches and simply carries on. The next
			 * alternative is reachable whatever this one's edge does,
			 * because it is what gets tried when this one does not match.
			 * That includes an edge that ends the test: handled by the
			 * terminal case below, "expect A -> warning" written before
			 * "expect B -> state" made B's state unreachable, so the same
			 * two alternatives warned or not depending on their order.
			 */
			if (st->type == STEP_EXPECT) {
				if ((st->action == ACT_GOTO) && st->targetstep)
					for (j = 0; j < nsteps; j++)
						if ((idx[j] == st->targetstep) && !reach[j]) { reach[j] = 1; changed = 1; }
				if (st->next &&
				    ((st->next->type == STEP_EXPECT) || (st->action == ACT_NEXT)))
					for (j = 0; j < nsteps; j++)
						if ((idx[j] == st->next) && !reach[j]) { reach[j] = 1; changed = 1; }
				continue;
			}
			if ((st->type == STEP_JUMP) || (st->action == ACT_FAIL) ||
			    (st->action == ACT_WARNING) || (st->action == ACT_SUCCESS)) {
				int j;

				if ((st->action == ACT_GOTO) && st->targetstep)
					for (j = 0; j < nsteps; j++)
						if ((idx[j] == st->targetstep) && !reach[j]) { reach[j] = 1; changed = 1; }
				continue;
			}
			if (st->next)
				for (j = 0; j < nsteps; j++)
					if ((idx[j] == st->next) && !reach[j]) { reach[j] = 1; changed = 1; }
			if ((st->action == ACT_GOTO) && st->targetstep)
				for (j = 0; j < nsteps; j++)
					if ((idx[j] == st->targetstep) && !reach[j]) { reach[j] = 1; changed = 1; }
		}
	} while (changed);

	for (i = 0; i < nsteps; i++) {
		if (reach[i] || (idx[i]->type != STEP_LABEL) || !idx[i]->label) continue;
		errprintf("Service %s: state '%s' cannot be reached - no edge names it "
			  "and nothing falls into it\n", rec->svcname, idx[i]->label);
	}

	/*
	 * A state waits if it has an expect; it is timed if a timeout was set
	 * anywhere in it. Both reset at the state boundary.
	 */
	{
		char *statename = NULL;
		int timed = 0, waits = 0, reported = 0;

		for (st = rec->steps; ; st = st->next) {
			if (!st || (st->type == STEP_LABEL)) {
				if (waits && !timed && !reported) {
					if (statename)
						errprintf("Service %s: state '%s' waits for a reply with no "
							  "timeout - it can only fail as an unattributed "
							  "global timeout\n", rec->svcname, statename);
					else
						errprintf("Service %s: a state waits for a reply with no "
							  "timeout - it can only fail as an unattributed "
							  "global timeout\n", rec->svcname);
				}
				if (!st) break;
				statename = st->label; timed = 0; waits = 0; reported = 0;
				continue;
			}
			if ((st->type == STEP_TIMEOUT) || (st->type == STEP_IDLE)) timed = 1;
			if (st->type == STEP_EXPECT)  waits = 1;
		}
	}

	/*
	 * A state with no way out. Asked per state rather than as a global
	 * reachability question: "can this step reach the end of the list"
	 * is answered yes by falling through to a state nothing can enter,
	 * which is exactly the config being complained about.
	 *
	 * So: if every edge in a state names that same state, and none of
	 * them is a terminal, the dialogue can never leave it.
	 */
	for (i = 0; i < nsteps; i++) {
		svcstep_t *own, *scan;
		int edges = 0, escapes = 0;

		if (!reach[i] || (idx[i]->type != STEP_LABEL) || !idx[i]->label) continue;
		own = idx[i];

		for (scan = own->next; (scan && (scan->type != STEP_LABEL)); scan = scan->next) {
			if ((scan->action == ACT_FAIL) || (scan->action == ACT_WARNING) ||
			    (scan->action == ACT_SUCCESS)) { edges++; escapes++; continue; }
			if (scan->action == ACT_GOTO) {
				edges++;
				if (scan->targetstep != own) escapes++;
				continue;
			}
			/* a step that simply continues is itself a way onward */
			if ((scan->type == STEP_SEND) || (scan->type == STEP_STARTTLS)) continue;
			if (scan->type == STEP_EXPECT) { edges++; escapes++; }
		}

		if (edges && !escapes)
			errprintf("Service %s: state '%s' has no way to finish - every edge "
				  "in it leads back to itself\n", rec->svcname, idx[i]->label);
	}

	xfree(idx); xfree(reach); xfree(ends);
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

		st->action    = tmpl->action;
		st->seconds   = tmpl->seconds;
		st->oneof     = tmpl->oneof;
		st->wantbytes = tmpl->wantbytes;
		if (tmpl->target)  st->target  = strdup(tmpl->target);
		if (tmpl->label)   st->label   = strdup(tmpl->label);
		if (tmpl->varname) st->varname = strdup(tmpl->varname);
		if (tmpl->srcname) st->srcname = strdup(tmpl->srcname);
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
int lookup_credentials(char *name, char **user, char **pass)
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

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_EXPECT;

				/*
				 * "expect bytes(N)" waits for a frame of N bytes rather
				 * than for text. LDAP, MySQL, DNS-over-TCP and AMQP put a
				 * length in front of a message instead of ending it with a
				 * line, so neither a literal nor "until" can say where the
				 * reply stops. It consumes exactly N and keeps the rest,
				 * like every other expect.
				 */
				rest = skipwhitespace(l+6);
				if (strncmp(rest, "bytes(", 6) == 0) {
					int n = atoi(rest + 6);
					char *close = strchr(rest, ')');

					if (!close) {
						errprintf("Service %s: 'expect bytes(' without ')'\n", first->rec->svcname);
						first->rec->flags |= TCP_DIALOGUE_BROKEN;
						continue;
					}
					if ((n <= 0) || (n > MAX_DIALOGUE_BYTES)) {
						errprintf("Service %s: 'expect bytes(%d)' is outside 1..%d\n",
							  first->rec->svcname, n, MAX_DIALOGUE_BYTES);
						first->rec->flags |= TCP_DIALOGUE_BROKEN;
						continue;
					}
					tmpl.wantbytes = n;
					rest = skipwhitespace(close + 1);
				}
				else {
					getescapestring(rest, &txt, &txtlen);
					tmpl.text = txt; tmpl.len = txtlen;
					rest = skipwhitespace(after_quoted(rest));
				}

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

					if (tmpl.wantbytes) {
						errprintf("Service %s: 'expect bytes(%d) until ...' - a frame of "
							  "N bytes already says where the reply ends\n",
							  first->rec->svcname, tmpl.wantbytes);
						first->rec->flags |= TCP_DIALOGUE_BROKEN;
					}

					getescapestring(tp, &untiltxt, &untillen);
					tmpl.until = untiltxt;
					tmpl.untillen = untillen;
					rest = skipwhitespace(after_quoted(tp));
				}

				/*
				 * "as NAME" binds the reply this expect accepted, and is
				 * written here rather than on a step of its own because a
				 * separate step has to say WHICH reply it means by its
				 * position. One written a state too late bound the reply
				 * from an earlier state and said nothing: the value was
				 * wrong rather than missing, so no check could see it.
				 */
				if (strncmp(rest, "as ", 3) == 0) {
					char *nm = skipwhitespace(rest + 2);
					char *end = nm;

					while (*end && (*end != ' ') && (*end != '\t')) end++;
					if (end == nm) errprintf("'expect ... as' with no name\n");
					else {
						if (*end) { *end = '\0'; rest = skipwhitespace(end + 1); }
						else rest = end;
						tmpl.varname = nm;
					}
				}

				/* Anything left after that is this edge's target. */
				if (*rest) {
					act = strtok(rest, " \t");
					if (act && (strcmp(act, "->") == 0)) {
						/*
						 * Every edge is "<condition> -> TARGET". Three
						 * target names are reserved and declare the
						 * verdict rather than naming a state, so a
						 * dialogue says how it ended instead of leaving
						 * the driver to infer it from running out of
						 * steps.
						 */
						char *tgt = strtok(NULL, " \t");

						if (!tgt) errprintf("'expect ... ->' with no target\n");
						else if (strcmp(tgt, "fail") == 0)    tmpl.action = ACT_FAIL;
						else if (strcmp(tgt, "warning") == 0) tmpl.action = ACT_WARNING;
						else if (strcmp(tgt, "success") == 0) tmpl.action = ACT_SUCCESS;
						else { tmpl.target = tgt; tmpl.action = ACT_GOTO; }
					}
					else if (act) errprintf("Unknown expect action: %s - edges are "
								"'<condition> -> TARGET'\n", act);
				}

				/*
				 * exptext is what the single-shot probe matches. A frame
				 * has no literal to put there, and it never runs on that
				 * path anyway -- and xfree() aborts on NULL, so both of
				 * these have to know that txt may be absent.
				 */
				if (txt) {
					for (walk = first; (walk); walk = walk->next) {
						if (walk->rec->exptext == NULL) {
							walk->rec->exptext = (unsigned char *)strdup((char *)txt);
							walk->rec->explen  = txtlen;
							walk->rec->expofs  = 0; /* HACK - not used right now */
						}
					}
				}
				emit_step(first, &tmpl);
				if (txt) xfree(txt);
				if (untiltxt) xfree(untiltxt);
			}
		}
		else if (strncmp(l, "state ", 6) == 0) {
			/*
			 * Names the state that follows: the expects up to the next
			 * step that is not one. It is also what an edge aims at, so
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
				else if (strcmp(tgt, "fail") == 0)    tmpl.action = ACT_FAIL;
				else if (strcmp(tgt, "warning") == 0) tmpl.action = ACT_WARNING;
				else if (strcmp(tgt, "success") == 0) tmpl.action = ACT_SUCCESS;
				else { tmpl.target = tgt; tmpl.action = ACT_GOTO; }
				emit_step(first, &tmpl);
			}
		}
		else if (strstr(l, "~")) {
			/*
			 * "NAME ~ \"regex\" -> TARGET" branches on a value already
			 * bound; "NAME ~ \"regex\" as N1;N2" binds new ones out of it.
			 * Both are recognised by shape rather than by a leading
			 * keyword, because the line begins with the name it reads.
			 *
			 * The regex reads a value already in hand, never the socket.
			 * That is what allows a regex here and not in an expect: it
			 * decides nothing about what arrives, so it cannot race a
			 * partial read, and it needs no maximum match length.
			 */
			if (first) {
				svcstep_t tmpl;
				char *var, *op, *rest, *after;
				unsigned char *txt = NULL;
				int txtlen = 0;

				var = strtok(l, " \t");
				op  = (var ? strtok(NULL, " \t") : NULL);
				rest = (op ? skipwhitespace(op + strlen(op) + 1) : NULL);

				if (!var || !op || (strcmp(op, "~") != 0) || !rest || !*rest) {
					errprintf("Usage: NAME ~ \"regex\" -> TARGET, or "
						  "NAME ~ \"regex\" as NAME[;NAME]\n");
				}
				else {
					getrawstring(rest, &txt, &txtlen);
					after = skipwhitespace(after_quoted(rest));

					memset(&tmpl, 0, sizeof(tmpl));
					tmpl.text = txt; tmpl.len = txtlen;
					tmpl.re = (void *)1;	/* emit_step compiles one per record */

					if (strncmp(after, "as ", 3) == 0) {
						tmpl.type = STEP_CAPTURE;
						tmpl.srcname = var;
						tmpl.varname = strtok(skipwhitespace(after + 2), " \t");

						if (!tmpl.varname)
							errprintf("'%s ~ ... as' with no name\n", var);
						else {
							pcre2_code *probe = compileregex_opts((char *)txt, 0);

							if (probe) {
								uint32_t ngroups = 0, nnames = 1;
								char *q;

								for (q = tmpl.varname; (*q); q++)
									if (*q == ';') nnames++;

								pcre2_pattern_info(probe, PCRE2_INFO_CAPTURECOUNT,
										   &ngroups);
								freeregex(probe);

								/*
								 * One name per group, in order. A group
								 * with no name throws its value away and a
								 * name with no group can never be bound;
								 * both are silent at run time.
								 */
								if (ngroups == 0)
									errprintf("Service %s: '%s ~ \"%s\" as %s' has "
										  "no capture group - it can never "
										  "bind a value\n",
										  first->rec->svcname, var, txt,
										  tmpl.varname);
								else if (ngroups != nnames)
									errprintf("Service %s: '%s ~ \"%s\" as %s' has "
										  "%u capture group(s) but %u name(s)\n",
										  first->rec->svcname, var, txt,
										  tmpl.varname, ngroups, nnames);
							}
							emit_step(first, &tmpl);
						}
					}
					else if (strncmp(after, "->", 2) == 0) {
						char *tgt = strtok(skipwhitespace(after + 2), " \t");

						tmpl.type = STEP_WHEN;
						tmpl.varname = var;

						if (!tgt) errprintf("'%s ~ ... ->' with no target\n", var);
						else {
							if (strcmp(tgt, "fail") == 0)         tmpl.action = ACT_FAIL;
							else if (strcmp(tgt, "warning") == 0) tmpl.action = ACT_WARNING;
							else if (strcmp(tgt, "success") == 0) tmpl.action = ACT_SUCCESS;
							else { tmpl.target = tgt; tmpl.action = ACT_GOTO; }
							emit_step(first, &tmpl);
						}
					}
					else errprintf("'%s ~ \"...\"' is followed by neither "
						       "'-> TARGET' nor 'as NAME'\n", var);
				}
				if (txt) xfree(txt);
			}
		}
		else if (strncmp(l, "timeout(", 8) == 0) {
			/*
			 * "timeout(N) -> TARGET": how long this state may take, and
			 * where to go when it does not. Without one, a state that never
			 * matches is bounded only by --timeout, which ends the whole
			 * test and cannot say which state stalled.
			 *
			 * The budget covers writing the action as well as reading the
			 * reply: a peer that will not accept the send is as stuck as one
			 * that will not answer it. --timeout still wins, so per-state
			 * budgets can never sum past it.
			 */
			char *close = strchr(l, ')');
			int secs = atoi(l + 8);

			if (!close) errprintf("Service %s: 'timeout(' with no ')'\n",
					      (first ? first->rec->svcname : "?"));
			else if (secs <= 0) {
				errprintf("Service %s: 'timeout(%d)' - the budget must be a positive "
					  "number of seconds\n",
					  (first ? first->rec->svcname : "?"), secs);
			}
			else if (first) {
				svcstep_t tmpl;
				char *arrow = strstr(close, "->");

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_TIMEOUT;
				tmpl.seconds = secs;
				tmpl.action = ACT_FAIL;	/* a budget with no target ends the test */

				if (arrow) {
					char *tgt = strtok(skipwhitespace(arrow + 2), " \t");

					if (!tgt) errprintf("'timeout(%d) ->' with no target\n", secs);
					else if (strcmp(tgt, "fail") == 0)    tmpl.action = ACT_FAIL;
					else if (strcmp(tgt, "warning") == 0) tmpl.action = ACT_WARNING;
					else if (strcmp(tgt, "success") == 0) tmpl.action = ACT_SUCCESS;
					else { tmpl.target = tgt; tmpl.action = ACT_GOTO; }
				}
				emit_step(first, &tmpl);
			}
		}
		else if (strncmp(l, "idle(", 5) == 0) {
			/*
			 * "idle(N) -> TARGET": how long this state may go with NOTHING
			 * arriving. Unlike timeout(N) the clock restarts whenever a byte
			 * does arrive, which is the difference between slow and stopped:
			 * a large EHLO trickling in over thirty seconds is a working
			 * server on a poor link, while five seconds of silence in the
			 * middle of that reply is one that has died. One clock cannot
			 * tell those apart, and reports the same colour for both.
			 */
			char *close = strchr(l, ')');
			int secs = atoi(l + 5);

			if (!close) errprintf("Service %s: 'idle(' with no ')'\n",
					      (first ? first->rec->svcname : "?"));
			else if (secs <= 0) {
				errprintf("Service %s: 'idle(%d)' - the budget must be a positive "
					  "number of seconds\n",
					  (first ? first->rec->svcname : "?"), secs);
			}
			else if (first) {
				svcstep_t tmpl;
				char *arrow = strstr(close, "->");

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_IDLE;
				tmpl.seconds = secs;
				tmpl.action = ACT_FAIL;	/* a budget with no target ends the test */

				if (arrow) {
					char *tgt = strtok(skipwhitespace(arrow + 2), " \t");

					if (!tgt) errprintf("'idle(%d) ->' with no target\n", secs);
					else if (strcmp(tgt, "fail") == 0)    tmpl.action = ACT_FAIL;
					else if (strcmp(tgt, "warning") == 0) tmpl.action = ACT_WARNING;
					else if (strcmp(tgt, "success") == 0) tmpl.action = ACT_SUCCESS;
					else { tmpl.target = tgt; tmpl.action = ACT_GOTO; }
				}
				emit_step(first, &tmpl);
			}
		}
		else if (strncmp(l, "start ", 6) == 0) {
			/* Where the dialogue begins. Without it, the first step. */
			char *nm = skipwhitespace(l + 6);

			if (!*nm) errprintf("'start' with no state name\n");
			else for (walk = first; (walk); walk = walk->next) {
				if (walk->rec->startlabel) xfree(walk->rec->startlabel);
				walk->rec->startlabel = strdup(nm);
			}
		}
		else if (strncmp(l, "framing ", 8) == 0) {
			/*
			 * How a message ends on this connection, which the socket
			 * never says: "line" is the greeting protocols, and
			 * "length(W, big|little)" is the binary ones that send a
			 * count first -- DNS over TCP, MySQL, LDAP, AMQP. It is a
			 * property of the protocol rather than of one reply, so it
			 * belongs on the entry and not on every expect.
			 */
			if (first) {
				char *nm = skipwhitespace(l + 8);
				svclist_t *w;

				if (strncmp(nm, "line", 4) == 0) {
					for (w = first; (w); w = w->next) w->rec->framing = FRAMING_LINE;
				}
				else if (strncmp(nm, "terminator ", 11) == 0) {
					/*
					 * A sequence that ends a message wherever it falls --
					 * a NUL, a blank line, a sentinel a custom protocol
					 * chose. "until" cannot say this: it compares the
					 * start of a LINE, so a terminator that is not at a
					 * line boundary is invisible to it.
					 */
					unsigned char *t = NULL;
					int tlen = 0;

					getescapestring(skipwhitespace(nm + 10), &t, &tlen);
					if (!t || (tlen < 1)) {
						errprintf("Service %s: 'framing terminator' needs a quoted "
							  "sequence\n", first->rec->svcname);
						first->rec->flags |= TCP_DIALOGUE_BROKEN;
						if (t) xfree(t);
					}
					else {
						for (w = first; (w); w = w->next) {
							w->rec->framing      = FRAMING_TERM;
							w->rec->frameterm    = (unsigned char *)malloc(tlen + 1);
							memcpy(w->rec->frameterm, t, tlen);
							w->rec->frameterm[tlen] = '\0';
							w->rec->frametermlen = tlen;
						}
						xfree(t);
					}
				}
				else if (strncmp(nm, "length(", 7) == 0) {
					int width = atoi(nm + 7);
					char *comma = strchr(nm, ',');
					int big = 1;

					if (comma) {
						char *e = skipwhitespace(comma + 1);

						if (strncmp(e, "little", 6) == 0) big = 0;
						else if (strncmp(e, "big", 3) != 0) {
							errprintf("Service %s: framing length endianness is 'big' or "
								  "'little', not '%s'\n", first->rec->svcname, e);
							first->rec->flags |= TCP_DIALOGUE_BROKEN;
						}
					}
					if ((width < 1) || (width > 4)) {
						errprintf("Service %s: framing length width is 1..4 bytes, not %d\n",
							  first->rec->svcname, width);
						first->rec->flags |= TCP_DIALOGUE_BROKEN;
					}
					else {
						for (w = first; (w); w = w->next) {
							w->rec->framing    = FRAMING_LENGTH;
							w->rec->framewidth = width;
							w->rec->framebig   = big;
						}
					}
				}
				else {
					errprintf("Service %s: unknown framing '%s' - it is 'line', "
						  "'length(WIDTH, big|little)' or 'terminator \"SEQ\"'\n",
						  first->rec->svcname, nm);
					first->rec->flags |= TCP_DIALOGUE_BROKEN;
				}
			}
		}
		else if (strncmp(l, "transport ", 10) == 0) {
			/*
			 * Which probe runs this entry. Only tcp is implemented; anything
			 * else must refuse rather than be silently treated as tcp, or a
			 * datagram entry would quietly become a stream one.
			 */
			char *nm = skipwhitespace(l + 10);

			if (strcmp(nm, "tcp") != 0) {
				errprintf("Service %s: transport '%s' is not implemented - "
					  "only 'tcp' is supported\n",
					  (first ? first->rec->svcname : "?"), nm);
				for (walk = first; (walk); walk = walk->next)
					walk->rec->flags |= TCP_DIALOGUE_BROKEN;
			}
		}
		else if (strncmp(l, "always ", 7) == 0) {
			/* An unconditional edge, for a state with nothing to wait for. */
			char *rest = skipwhitespace(l + 7);

			if (strncmp(rest, "->", 2) != 0) errprintf("'always' without '->'\n");
			else if (first) {
				svcstep_t tmpl;
				char *tgt = strtok(skipwhitespace(rest + 2), " \t");

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_JUMP;
				if (!tgt) errprintf("'always ->' with no target\n");
				else { tmpl.target = tgt; tmpl.action = ACT_GOTO; emit_step(first, &tmpl); }
			}
		}
		else if (strncmp(l, "eof ", 4) == 0) {
			/*
			 * The peer closing is an outcome, not automatically a fault:
			 * after QUIT it is the correct one. Carried as an alternative
			 * that matches EOF instead of bytes, so it sits in the same
			 * group as the expects it competes with.
			 */
			char *rest = skipwhitespace(l + 4);

			if (strncmp(rest, "->", 2) != 0) errprintf("'eof' without '->'\n");
			else if (first) {
				svcstep_t tmpl;
				char *tgt = strtok(skipwhitespace(rest + 2), " \t");

				memset(&tmpl, 0, sizeof(tmpl));
				tmpl.type = STEP_EXPECT;
				tmpl.oneof = 1;
				tmpl.text = (unsigned char *)strdup("");
				tmpl.len = 0;
				if (!tgt) errprintf("'eof ->' with no target\n");
				else if (strcmp(tgt, "fail") == 0)    tmpl.action = ACT_FAIL;
				else if (strcmp(tgt, "warning") == 0) tmpl.action = ACT_WARNING;
				else if (strcmp(tgt, "success") == 0) tmpl.action = ACT_SUCCESS;
				else { tmpl.target = tgt; tmpl.action = ACT_GOTO; }
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
				/*
				 * options REPLACES rather than adds, so a second line
				 * silently discards the first. Complete check, so refuse.
				 */
				if (first->rec->sawoptions) {
					errprintf("Service %s: a second 'options' line - options replaces "
						  "rather than adds, so the first would be discarded. Write "
						  "one line with every option on it\n", first->rec->svcname);
					first->rec->flags |= TCP_DIALOGUE_BROKEN;
				}
				first->rec->sawoptions = 1;
				char *opt;

				/*
				 * options REPLACES the option bits rather than adding to
				 * them, so a later 'options' line does not inherit an
				 * earlier one. A refusal is not an option bit and must
				 * survive: it says the definition is unusable, and an
				 * 'options' line below it would otherwise clear it and let
				 * the service report green.
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
		svcinfo[i].framing    = walk->rec->framing;
		svcinfo[i].framewidth = walk->rec->framewidth;
		svcinfo[i].framebig   = walk->rec->framebig;
		svcinfo[i].frameterm    = walk->rec->frameterm;
		svcinfo[i].frametermlen = walk->rec->frametermlen;
		svcinfo[i].steps   = walk->rec->steps;
		svcinfo[i].startlabel = walk->rec->startlabel;
		/*
		 * The array is malloc'd, not calloc'd, and every field here is
		 * assigned by hand -- so a field left out is not NULL, it is
		 * whatever the heap held. startstep is only set by resolve when
		 * the entry has a 'start' line, so it has to be cleared first.
		 */
		svcinfo[i].startstep = NULL;
		resolve_svcsteps(&svcinfo[i]);
		check_graph(&svcinfo[i]);
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
					/*
					 * A frame is the driver's business whatever else the
					 * entry looks like: the single-shot probe compares the
					 * start of one read and knows nothing about waiting for
					 * N bytes, so a lone "expect bytes(N)" left on that path
					 * would silently be a banner match.
					 */
					if (st->wantbytes) nother++;
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
			if ((first_is_expect && (nsend > 0)) || (nsend > 1) || (nexp > 1) || nother ||
			    (svcinfo[i].framing != FRAMING_LINE))
				svcinfo[i].flags |= TCP_DIALOGUE;

			/*
			 * Under length framing the peer says where every message ends,
			 * so a clause that says it again is either redundant or a
			 * contradiction -- and there is no way to tell which from the
			 * file. Refuse both rather than pick one.
			 */
			if (svcinfo[i].framing != FRAMING_LINE) {
				svcstep_t *fst;

				for (fst = svcinfo[i].steps; (fst); fst = fst->next) {
					if (fst->type != STEP_EXPECT) continue;
					if (fst->until)
						errprintf("Service %s: 'until' under framing %s - the framing "
							  "already says where the message ends\n",
							  svcinfo[i].svcname,
							  (svcinfo[i].framing == FRAMING_LENGTH) ? "length" : "terminator");
					else if (fst->wantbytes)
						errprintf("Service %s: 'expect bytes(%d)' under framing %s - the "
							  "framing already says how long the message is\n",
							  svcinfo[i].svcname, fst->wantbytes,
							  (svcinfo[i].framing == FRAMING_LENGTH) ? "length" : "terminator");
					else continue;
					svcinfo[i].flags |= TCP_DIALOGUE_BROKEN;
				}
			}

			check_undefined_vars(&svcinfo[i]);
			refuse_overlapping_groups(&svcinfo[i]);
			refuse_misshapen_states(&svcinfo[i]);

			/*
			 * A clause that only the driver implements, on an entry that
			 * stays on the classic single-shot probe, does nothing at all:
			 * "until" is ignored and the first line decides the test. That
			 * is decidable here -- the shape of the entry is known -- so it
			 * is refused rather than left to surprise somebody.
			 */
			if ((svcinfo[i].flags & TCP_DIALOGUE) == 0) {
				svcstep_t *cst;

				for (cst = svcinfo[i].steps; (cst); cst = cst->next) {
					if (cst->type != STEP_EXPECT) continue;
					if (cst->until)
						errprintf("Service %s: 'until' on an entry the dialogue driver does "
							  "not run - a lone expect is the classic probe, which "
							  "matches one line and knows no terminator. Add the step "
							  "that follows it\n", svcinfo[i].svcname);
					else if (cst->varname)
						errprintf("Service %s: 'as %s' on an entry the dialogue driver does "
							  "not run - nothing later can read the value\n",
							  svcinfo[i].svcname, cst->varname);
					else continue;
					svcinfo[i].flags |= TCP_DIALOGUE_BROKEN;
				}
			}

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

