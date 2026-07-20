/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* A small parser for brace-delimited, verb-keyword configuration - the       */
/* nginx/BIND/dhcpd style used by test.cfg and graph.cfg. Whitespace is       */
/* insignificant (matching every other Xymon config loader); hierarchy is     */
/* carried by "{ }", statements are separated by ";" or newline, "#" begins   */
/* a comment. A statement is a list of words followed by either a "{ }" body  */
/* (a block) or a terminator (a verb).                                        */
/*                                                                            */
/* The parser is deliberately dumb: it produces a generic tree of nodes, each */
/* a word-list plus child nodes. Typed config layers (testcfg, graphcfg)      */
/* interpret the tree; the grammar lives here once.                           */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                           */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __BRACEPARSE_H__
#define __BRACEPARSE_H__

typedef struct bracenode_t {
	char **words;			/* the statement's words (words[0] is the "kind") */
	int nwords;
	struct bracenode_t **children;	/* child statements, if this was a "{ }" block */
	int nchildren;
	int line;			/* 1-based source line of the statement, for errors */
} bracenode_t;

/*
 * Parse brace-config text into a synthetic root node whose children are the
 * top-level statements. Returns NULL and fills errbuf on a syntax error
 * (unbalanced braces). The input buffer is not modified. Free with
 * braceparse_free().
 */
extern bracenode_t *braceparse(const char *text, char *errbuf, int errbufsz);
extern void braceparse_free(bracenode_t *node);

/* Convenience accessors for the typed layers. */
extern bracenode_t *bracenode_child(bracenode_t *node, const char *kind, const char *name);
extern const char *bracenode_verb(bracenode_t *node, const char *keyword);  /* first arg word, "" if flag-only, NULL if absent */
extern int bracenode_has(bracenode_t *node, const char *keyword);           /* keyword present (as a verb or block kind) */

#endif
