/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for color <-> string conversion                       */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: color.c,v 1.1 2004-10-30 15:18:10 henrik Exp $";

#include <string.h>

#include "color.h"

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

	strncpy(inpcolor, colortext, 7);
	n = strspn(inpcolor, "abcdefghijklmnopqrstuvwxyz");
	inpcolor[n] = '\0';
	strcat(inpcolor, " ");

	if (strncmp(inpcolor, "green ", 6) == 0) {
		return COL_GREEN;
	}
	else if (strncmp(inpcolor, "yellow ", 7) == 0) {
		return COL_YELLOW;
	}
	else if (strncmp(inpcolor, "red ", 4) == 0) {
		return COL_RED;
	}
	else if (strncmp(inpcolor, "blue ", 5) == 0) {
		return COL_BLUE;
	}
	else if (strncmp(inpcolor, "clear ", 6) == 0) {
		return COL_CLEAR;
	}
	else if (strncmp(inpcolor, "purple ", 7) == 0) {
		return COL_PURPLE;
	}

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

