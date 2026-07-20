/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Generic brace-delimited config parser. See braceparse.h for the grammar.  */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                           */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char braceparse_rcsid[] = "$Id$";

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "libxymon.h"

/* Tokeniser state, threaded through the recursive parse. */
typedef struct bp_state_t {
	const char *p;		/* current read position */
	int line;		/* current 1-based line */
	char err[200];		/* set on failure */
	int failed;
} bp_state_t;

typedef enum { BP_WORD, BP_OPEN, BP_CLOSE, BP_SEMI, BP_EOF } bp_toktype_t;

typedef struct bp_tok_t {
	bp_toktype_t type;
	char *word;		/* malloc'd when type == BP_WORD */
	int line;
} bp_tok_t;

static bracenode_t *bp_newnode(int line)
{
	bracenode_t *n = (bracenode_t *)xcalloc(1, sizeof(bracenode_t));
	n->line = line;
	return n;
}

static void bp_addword(bracenode_t *n, char *w)
{
	/* xrealloc refuses a NULL pointer, so the first growth mallocs */
	n->words = (char **)(n->words ? xrealloc(n->words, (n->nwords + 1) * sizeof(char *))
				      : xmalloc(sizeof(char *)));
	n->words[n->nwords++] = w;
}

static void bp_addchild(bracenode_t *n, bracenode_t *c)
{
	n->children = (bracenode_t **)(n->children ? xrealloc(n->children, (n->nchildren + 1) * sizeof(bracenode_t *))
						   : xmalloc(sizeof(bracenode_t *)));
	n->children[n->nchildren++] = c;
}

/* Read the next token. A word may be bare (letters/digits/most punctuation)
 * or "double-quoted" (to hold spaces and { } ; ). Newlines act like ';'. */
static bp_tok_t bp_next(bp_state_t *st)
{
	bp_tok_t tok;
	tok.word = NULL;

	for (;;) {
		/* Skip blanks and comments; a newline is a statement separator. */
		while (*st->p && (*st->p != '\n') && isspace((int)(unsigned char)*st->p)) st->p++;
		if (*st->p == '#') { while (*st->p && (*st->p != '\n')) st->p++; }
		/* The separator belongs to the line the newline ENDS - error
		 * messages naming it must not point one line past it. */
		if (*st->p == '\n') { tok.type = BP_SEMI; tok.line = st->line; st->line++; st->p++; return tok; }
		if (*st->p == '\0') { tok.type = BP_EOF; tok.line = st->line; return tok; }
		break;
	}

	tok.line = st->line;
	if (*st->p == '{') { st->p++; tok.type = BP_OPEN; return tok; }
	if (*st->p == '}') { st->p++; tok.type = BP_CLOSE; return tok; }
	if (*st->p == ';') { st->p++; tok.type = BP_SEMI; return tok; }

	tok.type = BP_WORD;
	if (*st->p == '"') {
		strbuffer_t *sb = newstrbuffer(0);
		char c[2]; c[1] = '\0';
		st->p++;
		while (*st->p && (*st->p != '"')) {
			if (*st->p == '\n') st->line++;
			c[0] = *st->p++; addtobuffer(sb, c);
		}
		if (*st->p == '"') st->p++;
		else {
			/* An unterminated quote would silently swallow the rest
			 * of the file into one word - reject the whole config. */
			snprintf(st->err, sizeof(st->err), "unterminated quote (opened at line %d)", tok.line);
			st->failed = 1;
		}
		tok.word = xstrdup(STRBUF(sb));
		freestrbuffer(sb);
	}
	else {
		const char *start = st->p;
		while (*st->p && !isspace((int)(unsigned char)*st->p) &&
		       (*st->p != '{') && (*st->p != '}') && (*st->p != ';') && (*st->p != '#')) st->p++;
		tok.word = (char *)xmalloc(st->p - start + 1);
		memcpy(tok.word, start, st->p - start);
		tok.word[st->p - start] = '\0';
	}
	return tok;
}

#define BP_MAXDEPTH 64

/* Parse statements until CLOSE (when inblock) or EOF (top level). */
static void bp_parse_body(bp_state_t *st, bracenode_t *parent, int inblock, int depth)
{
	for (;;) {
		bracenode_t *stmt = NULL;
		bp_tok_t tok;

		/* skip leading separators */
		do { tok = bp_next(st); } while (tok.type == BP_SEMI);

		if (tok.type == BP_EOF) {
			if (inblock) { snprintf(st->err, sizeof(st->err), "unexpected end of file, missing '}'"); st->failed = 1; }
			return;
		}
		if (tok.type == BP_CLOSE) {
			if (!inblock) { snprintf(st->err, sizeof(st->err), "unexpected '}' at line %d", tok.line); st->failed = 1; }
			return;
		}
		if (tok.type == BP_OPEN) {
			snprintf(st->err, sizeof(st->err), "unexpected '{' at line %d", tok.line); st->failed = 1; return;
		}

		/* tok is the first WORD of a statement */
		stmt = bp_newnode(tok.line);
		bp_addword(stmt, tok.word);
		for (;;) {
			tok = bp_next(st);
			if (tok.type == BP_WORD) { bp_addword(stmt, tok.word); continue; }
			if (tok.type == BP_SEMI || tok.type == BP_EOF) { break; }  /* a verb */
			if (tok.type == BP_OPEN) {                                  /* a block */
				if (depth >= BP_MAXDEPTH) {
					snprintf(st->err, sizeof(st->err), "blocks nested too deeply at line %d (max %d)", tok.line, BP_MAXDEPTH);
					st->failed = 1;
				}
				else bp_parse_body(st, stmt, 1, depth+1);
				break;
			}
			if (tok.type == BP_CLOSE) {  /* verb ended by the block's close - push it back conceptually */
				bp_addchild(parent, stmt);
				/* At top level a '}' is never legal: erroring here,
				 * instead of returning quietly, is what keeps a stray
				 * '}' from silently discarding the rest of the file. */
				if (!inblock) { snprintf(st->err, sizeof(st->err), "unexpected '}' at line %d", tok.line); st->failed = 1; }
				return;  /* caller (the CLOSE) is consumed here; body ends */
			}
		}
		bp_addchild(parent, stmt);
		if (st->failed) return;
	}
}

bracenode_t *braceparse(const char *text, char *errbuf, int errbufsz)
{
	bp_state_t st;
	bracenode_t *root;

	st.p = text; st.line = 1; st.err[0] = '\0'; st.failed = 0;
	root = bp_newnode(0);
	bp_parse_body(&st, root, 0, 0);
	if (st.failed) {
		if (errbuf && errbufsz) { strncpy(errbuf, st.err, errbufsz - 1); errbuf[errbufsz - 1] = '\0'; }
		braceparse_free(root);
		return NULL;
	}
	return root;
}

void braceparse_free(bracenode_t *node)
{
	int i;

	if (!node) return;
	for (i = 0; i < node->nwords; i++) xfree(node->words[i]);
	if (node->words) xfree(node->words);
	for (i = 0; i < node->nchildren; i++) braceparse_free(node->children[i]);
	if (node->children) xfree(node->children);
	xfree(node);
}

bracenode_t *bracenode_child(bracenode_t *node, const char *kind, const char *name)
{
	int i;

	if (!node) return NULL;
	for (i = 0; i < node->nchildren; i++) {
		bracenode_t *c = node->children[i];
		if (c->nwords < 1) continue;
		if (kind && strcasecmp(c->words[0], kind)) continue;
		if (name && ((c->nwords < 2) || strcmp(c->words[1], name))) continue;
		return c;
	}
	return NULL;
}

const char *bracenode_verb(bracenode_t *node, const char *keyword)
{
	int i;

	if (!node) return NULL;
	for (i = 0; i < node->nchildren; i++) {
		bracenode_t *c = node->children[i];
		if ((c->nwords >= 1) && (strcasecmp(c->words[0], keyword) == 0))
			return (c->nwords >= 2) ? c->words[1] : "";
	}
	return NULL;
}

int bracenode_has(bracenode_t *node, const char *keyword)
{
	return (bracenode_verb(node, keyword) != NULL);
}
