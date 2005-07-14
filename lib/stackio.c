/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for reading configuration files with "include"s.      */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: stackio.c,v 1.7 2005-07-14 17:26:34 henrik Exp $";

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "libbbgen.h"

typedef struct stackfd_t {
	FILE *fd;
	struct stackfd_t *next;
} stackfd_t;
static stackfd_t *fdhead = NULL;
static char *stackfd_base = NULL;
static char *stackfd_mode = NULL;


FILE *stackfopen(char *filename, char *mode)
{
	FILE *newfd;
	stackfd_t *newitem;
	char stackfd_filename[PATH_MAX];

	MEMDEFINE(stackfd_filename);

	if (fdhead == NULL) {
		char *p;

		stackfd_base = strdup(filename);
		p = strrchr(stackfd_base, '/'); if (p) *(p+1) = '\0';

		stackfd_mode = strdup(mode);

		strcpy(stackfd_filename, filename);
	}
	else {
		if (*filename == '/')
			strcpy(stackfd_filename, filename);
		else
			sprintf(stackfd_filename, "%s/%s", stackfd_base, filename);
	}

	newfd = fopen(stackfd_filename, stackfd_mode);
	if (newfd != NULL) {
		newitem = (stackfd_t *) malloc(sizeof(stackfd_t));
		newitem->fd = newfd;
		newitem->next = fdhead;
		fdhead = newitem;
	}

	MEMUNDEFINE(stackfd_filename);

	return newfd;
}


int stackfclose(FILE *fd)
{
	int result;
	stackfd_t *olditem;

	if (fd != NULL) {
		/* Close all */
		while (fdhead != NULL) {
			olditem = fdhead;
			fdhead = fdhead->next;
			fclose(olditem->fd);
			xfree(olditem);
		}
		xfree(stackfd_base);
		xfree(stackfd_mode);
		result = 0;
	}
	else {
		olditem = fdhead;
		fdhead = fdhead->next;
		result = fclose(olditem->fd);
		xfree(olditem);
	}

	return result;
}


char *stackfgets(char *buffer, unsigned int bufferlen, char *includetag1, char *includetag2)
{
	char *result;

	result = fgets(buffer, bufferlen, fdhead->fd);

	if (result && 
		( (strncmp(buffer, includetag1, strlen(includetag1)) == 0) ||
		  (includetag2 && (strncmp(buffer, includetag2, strlen(includetag2)) == 0)) )) {
		char *newfn = strchr(buffer, ' ');
		char *eol = strchr(buffer, '\n');

		while (newfn && *newfn && isspace((int)*newfn)) newfn++;
		if (eol) *eol = '\0';
		
		if (newfn && (stackfopen(newfn, "r") != NULL))
			return stackfgets(buffer, bufferlen, includetag1, includetag2);
		else {
			errprintf("WARNING: Cannot open include file '%s', line was:%s\n", textornull(newfn), buffer);
			if (eol) *eol = '\n';
			return result;
		}
	}
	else if (result == NULL) {
		/* end-of-file on read */
		stackfclose(NULL);
		if (fdhead != NULL)
			return stackfgets(buffer, bufferlen, includetag1, includetag2);
		else
			return NULL;
	}

	return result;
}

char *unlimfgets(char **buffer, int *bufsz, FILE *fd)
{
	static char inbuf[4096];
	static char *inbufp = inbuf;
	static int moretoread = 1;
	size_t n;
	char *eoln = NULL;

	if (fd == NULL) {
		/* Clear buffer and control vars */
		*inbuf = '\0'; inbufp = inbuf; moretoread = 1;
		return NULL;
	}

	/* End of file ? */
	if (!moretoread && (*inbufp == '\0')) return NULL;

	/* Make sure the output buffer is empty */
	if (*buffer) **buffer = '\0';

	while (!eoln && (moretoread || *inbufp)) {
		if (*inbufp) {
			/* Have some data in the buffer */
			char savech = '\0';

			eoln = strchr(inbufp, '\n');
			if (eoln) { savech = *(eoln+1); *(eoln+1) = '\0'; }
			addtobuffer(buffer, bufsz, inbufp);
			if (eoln) { 
				/* Advance inbufp */
				*(eoln+1) = savech; 
				inbufp = eoln+1; 
			}
			else {
				/* Input buffer is now empty */
				*inbuf = '\0';
				inbufp = inbuf;
			}
		}

		if (!eoln) {
			/* Get data for the input buffer */
			n = fread(inbuf, 1, sizeof(inbuf)-1, fd);
			inbuf[n] = '\0';
			inbufp = inbuf;
			if (n < sizeof(inbuf)) moretoread = 0;
		}
	}

	return *buffer;
}

