#ifndef SETUPS__H
#define SETUPS__H

/*
** tr2latex - troff to LaTeX converter
** COPYRIGHT (C) 1987 Kamal Al-Yahya, 1991,1992 Christian Engel
**
** Module: setups.h
**
** setup file
*/

#include	<ctype.h>
#include	<errno.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<time.h>

#define	MAXLEN	(1024*1024)		/* maximum length of document */
#define	MAXWORD	250				/* maximum word length */
#define	MAXLINE	500				/* maximum line length */
#define	MAXDEF	200				/* maximum number of defines */
#define MAXARGS 128				/* maximal number of arguments */

#define EOS		'\0'			/* end of string */

#ifdef MAIN				/* can only declare globals once */
#  define GLOBAL
#else
#  define GLOBAL extern
#endif

GLOBAL int math_mode,	/* math mode status */
	       de_arg,		/* .de argument */
	       IP_stat,		/* IP status */
	       QP_stat,		/* QP status */
           TP_stat;		/* TP status */

GLOBAL int debug_o;

GLOBAL struct defs {
	char *def_macro;
	char *replace;
	int illegal;
} def[MAXDEF];

GLOBAL struct mydefs {
	char *def_macro;
	char *replace;
	int illegal;
	int arg_no;
	int par;		/* if it impiles (or contains) a par break */
} mydef[MAXDEF];

GLOBAL struct measure {
	char old_units[MAXWORD];	float old_value;
	char units[MAXWORD];		float value;
	char def_units[MAXWORD];	/* default units */
	int def_value;			/* default value: 0 means take last one */
} linespacing, indent, tmpind, space, vspace;

#endif
