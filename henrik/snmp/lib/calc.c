/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2003-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

#include "libbbgen.h"

long compute(char *expression, int *error)
{
	/*
	 * This routine evaluates an expression.
	 *
	 * Expressions are of the form "expr [operator expr]" or "(expr)"
	 * "operator" is + - / * % & | && || > >= < <= ==
	 *
	 * All operators have equal precedence!
	 *
	 */
	char *exp, *startp, *operator;
	char *inp, *outp;
	char op;
	long xval, yval, result;

	if (*error) return -1;

	/* Copy expression except whitespace */
	exp = (char *) malloc(strlen(expression)+1);
	inp = expression; outp=exp;
	do {
		if (!isspace((int) *inp)) { *outp = *inp; outp++; }
		inp++;
	} while (*inp);
	*outp = '\0';

	/* First find the value of the first sub-expression */
	startp = exp;
	while (isspace((int) *startp)) startp++;
	if (*startp == '(') {
		/* Starts with parentheses:
		 * - find matching end parentheses
		 * - find the operator following the end parentheses
		 * - compute value of expression inside parentheses (recursive call)
		 */
		int pcount = 1;
		char *endp;

		for (endp = startp+1; (*endp && pcount); ) {
			if (*endp == '(') pcount++;
			else if (*endp == ')') pcount--;
			if (pcount) endp++;
		}

		if (*endp == '\0') { *error = 1; return -1; }
		operator = endp+1;
		*endp = '\0';
		xval = compute(startp+1, error);
	}
	else {
		/* No parentheses --> it's a number */
		xval = strtol(startp, &operator, 10);
		if (operator == startp) { *error = 2; return -1; }
	}

	/* Now loop over the following operators and expressions */
	do {
		/* There may not be an operator */
		if (*operator) {

			while (isspace((int) *operator)) operator++;
			op = *operator;
			/* For the && and || operators ... */
			if      ((op == '&') && (*(operator+1) == '&')) { op = 'a'; operator++; }
			else if ((op == '|') && (*(operator+1) == '|')) { op = 'o'; operator++; }
			else if ((op == '>') && (*(operator+1) == '=')) { op = 'g'; operator++; }
			else if ((op == '<') && (*(operator+1) == '=')) { op = 'l'; operator++; }
			else if ((op == '=') && (*(operator+1) == '=')) { op = 'e'; operator++; }

			/* Since there is an operator, there must be a value after the operator */
			startp = operator + 1;
			while (isspace((int) *startp)) startp++;
			if (*startp == '(') {
				int pcount = 1;
				char *endp;

				for (endp = startp+1; (*endp && pcount);) {
					if (*endp == '(') pcount++;
					else if (*endp == ')') pcount--;
					if (pcount) endp++;
				}

				operator = endp+1;
				*endp = '\0';
				yval = compute(startp+1, error);
			}
			else {
				yval = strtol(startp, &operator, 10);
				if (operator == startp) { *error = 3; return -1; }
			}

			switch (op) {
			  case '+': xval = (xval + yval);  break;
			  case '-': xval = (xval - yval);  break;
			  case '*': xval = (xval * yval);  break;
			  case '/': if (yval) xval = (xval / yval); 
				    else { *error = 10; return -1; }
				    break;
			  case '%': if (yval) xval = (xval % yval); 
				    else { *error = 10; return -1; }
				    break;
			  case '&': xval = (xval & yval);  break;
			  case 'a': xval = (xval && yval); break;
			  case '|': xval = (xval | yval);  break;
			  case 'o': xval = (xval || yval); break;
			  case '>': xval = (xval > yval);  break;
			  case 'g': xval = (xval >= yval); break;
			  case '<': xval = (xval < yval);  break;
			  case 'l': xval = (xval <= yval); break;
			  case 'e': xval = (xval == yval); break;
			  default : { *error = 4; return -1; }
			}
		}
		else {
			/* Do nothing - no operator, so result is the xval */
		}

		result = xval;
	} while (*operator);

	xfree(exp);
	return result;
}

#ifdef STANDALONE

int main(int argc, char *argv[])
{
	long result;
	int error = 0;

	result = compute(argv[1], &error);
	printf("%s = %ld\n", argv[1], result);

	return error;
}
#endif

