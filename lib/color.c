/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for color <-> string conversion                       */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: color.c,v 1.12 2006-05-03 21:12:33 henrik Exp $";

#include <string.h>
#include <stdlib.h>

#include "libbbgen.h"

int use_recentgifs = 0;

char *colorname(int color)
{
	static char *cs = "";

	switch (color) {
	  case COL_CLEAR:  cs = "clear"; break;
	  case COL_BLUE:   cs = "blue"; break;
	  case COL_PURPLE: cs = "purple"; break;
	  case COL_GREEN:  cs = "green"; break;
	  case COL_YELLOW: cs = "yellow"; break;
	  case COL_RED:    cs = "red"; break;
	  default:
			   cs = "unknown";
			   break;
	}

	return cs;
}

int parse_color(char *colortext)
{
	char inpcolor[10];
	int n;

	MEMDEFINE(inpcolor);

	strncpy(inpcolor, colortext, 7);
	inpcolor[7] = '\0';
	n = strspn(inpcolor, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
	inpcolor[n] = '\0';
	strcat(inpcolor, " ");

	if (strncasecmp(inpcolor, "green ", 6) == 0) {
		MEMUNDEFINE(inpcolor);
		return COL_GREEN;
	}
	else if (strncasecmp(inpcolor, "yellow ", 7) == 0) {
		MEMUNDEFINE(inpcolor);
		return COL_YELLOW;
	}
	else if (strncasecmp(inpcolor, "red ", 4) == 0) {
		MEMUNDEFINE(inpcolor);
		return COL_RED;
	}
	else if (strncasecmp(inpcolor, "blue ", 5) == 0) {
		MEMUNDEFINE(inpcolor);
		return COL_BLUE;
	}
	else if (strncasecmp(inpcolor, "clear ", 6) == 0) {
		MEMUNDEFINE(inpcolor);
		return COL_CLEAR;
	}
	else if (strncasecmp(inpcolor, "purple ", 7) == 0) {
		MEMUNDEFINE(inpcolor);
		return COL_PURPLE;
	}

	MEMUNDEFINE(inpcolor);
	return -1;
}

int eventcolor(char *colortext)
{
	if 	(strcmp(colortext, "cl") == 0)	return COL_CLEAR;
	else if (strcmp(colortext, "bl") == 0)	return COL_BLUE;
	else if (strcmp(colortext, "pu") == 0)	return COL_PURPLE;
	else if (strcmp(colortext, "gr") == 0)	return COL_GREEN;
	else if (strcmp(colortext, "ye") == 0)	return COL_YELLOW;
	else if (strcmp(colortext, "re") == 0)	return COL_RED;
	else return -1;
}

char *dotgiffilename(int color, int acked, int oldage)
{
	static char *filename = NULL; /* yellow-recent.gif */

	/* Allocate the first time, never free */
	if (filename == NULL) filename = (char *)malloc(20);

	strcpy(filename, colorname(color));
	if (acked) {
		strcat(filename, "-ack");
	}
	else if (use_recentgifs) {
		strcat(filename, (oldage ? "" : "-recent"));
	}
	strcat(filename, ".gif");

	return filename;
}

#ifndef CLIENTONLY
int colorset(char *colspec, int excludeset)
{
	char *cspeccopy = strdup(colspec);
	int c, ac;
	char *p;
	char *pp;

	p = strtok_r(cspeccopy, ",", &pp);
	ac = 0;
	while (p) {
		c = parse_color(p);
		if (c != -1) ac = (ac | (1 << c));
		p = strtok_r(NULL, ",", &pp);
	}
	xfree(cspeccopy);

	/* Some color may be forbidden */
	ac = (ac & ~excludeset);
	return ac;
}
#endif

