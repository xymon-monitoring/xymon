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

static char rcsid[] = "$Id: stackio.c,v 1.9 2005-07-16 10:11:46 henrik Exp $";

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "libbbgen.h"

typedef struct fgetsbuf_t {
	FILE *fd;
	char inbuf[4096+1];
	char *inbufp;
	int moretoread;
	struct fgetsbuf_t *next;
} fgetsbuf_t;
static fgetsbuf_t *fgetshead = NULL;

typedef struct stackfd_t {
	FILE *fd;
	struct stackfd_t *next;
} stackfd_t;
static stackfd_t *fdhead = NULL;
static char *stackfd_base = NULL;
static char *stackfd_mode = NULL;


/*
 * initfgets() and unlimfgets() implements a fgets() style
 * input routine that can handle arbitrarily long input lines.
 * Buffer space for the input is dynamically allocate and
 * expanded, until the input hits a newline character.
 * Simultaneously, lines ending with a '\' character are
 * merged into one line, allowing for transparent handling
 * of very long lines.
 *
 * This interface is also used by the stackfgets() routine.
 *
 * If you open a file directly, after getting the FILE
 * descriptor call initfgets(FILE). Then use unlimfgets()
 * to read data one line at a time. You must read until
 * unlimfgets() returns NULL (at which point you should not
 * call unlimfgets() again with this fd).
 */
int initfgets(FILE *fd)
{
	fgetsbuf_t *newitem;

	newitem = (fgetsbuf_t *)malloc(sizeof(fgetsbuf_t));
	*(newitem->inbuf) = '\0';
	newitem->inbufp = newitem->inbuf;
	newitem->moretoread = 1;
	newitem->fd = fd;
	newitem->next = fgetshead;
	fgetshead = newitem;
	return 0;
}

char *unlimfgets(char **buffer, int *bufsz, FILE *fd)
{
	fgetsbuf_t *fg;
	size_t n;
	char *eoln = NULL;

	for (fg = fgetshead; (fg && (fg->fd != fd)); fg = fg->next) ;
	if (!fg) {
		errprintf("umlimfgets() called with bad input FD\n"); 
		return NULL;
	}

	/* End of file ? */
	if (!(fg->moretoread) && (*(fg->inbufp) == '\0')) {
		if (fg == fgetshead) {
			fgetshead = fgetshead->next;
			free(fg);
		}
		else {
			fgetsbuf_t *prev;
			for (prev = fgetshead; (prev->next != fg); prev = prev->next) ;
			prev->next = fg->next;
			free(fg);
		}
		return NULL;
	}

	/* Make sure the output buffer is empty */
	if (*buffer) **buffer = '\0';

	while (!eoln && (fg->moretoread || *(fg->inbufp))) {
		int continued = 0;

		if (*(fg->inbufp)) {
			/* Have some data in the buffer */
			eoln = strchr(fg->inbufp, '\n');
			if (eoln) { 
				/* See if there's a continuation character just before the eoln */
				char *contchar = eoln-1;
				while ((contchar > fg->inbufp) && isspace((int)*contchar) && (*contchar != '\\')) contchar--;
				continued = (*contchar == '\\');

				if (continued) {
					*contchar = '\0';
					addtobuffer(buffer, bufsz, fg->inbufp);
					fg->inbufp = eoln+1; 
					eoln = NULL;
				}
				else {
					char savech = *(eoln+1); 
					*(eoln+1) = '\0';
					addtobuffer(buffer, bufsz, fg->inbufp);
					*(eoln+1) = savech; 
					fg->inbufp = eoln+1; 
				}
			}
			else {
				/* No newline in buffer, so add all of it to the output buffer */
				addtobuffer(buffer, bufsz, fg->inbufp);

				/* Input buffer is now empty */
				*(fg->inbuf) = '\0';
				fg->inbufp = fg->inbuf;
			}
		}

		if (!eoln && !continued) {
			/* Get data for the input buffer */
			char *inpos = fg->inbuf;
			size_t insize = sizeof(fg->inbuf);

			/* If the last byte we read was a continuation char, we must do special stuff. */
			if (*buffer) {
				int n = strlen(*buffer);
				char *contchar = *buffer + n - 1;
				while ((contchar > *buffer) && isspace((int)*contchar) && (*contchar != '\\')) contchar--;

				if (*contchar == '\\') {
					/*
					 * Remove the cont. char from the output buffer, and stuff it into
					 * the input buffer again - so we can check if there's a new-line coming.
					 */
					*contchar = '\0';
					*(fg->inbuf) = '\\';
					inpos++;
					insize--;
				}
			}

			n = fread(inpos, 1, insize-1, fd);
			*(inpos + n) = '\0';
			fg->inbufp = fg->inbuf;
			if (n < insize-1) fg->moretoread = 0;
		}
	}

	return *buffer;
}

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
		initfgets(newfd);
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


char *stackfgets(char **buffer, unsigned int *bufferlen, char *includetag1, char *includetag2)
{
	char *result;

	result = unlimfgets(buffer, bufferlen, fdhead->fd);

	if (result && 
		( (strncmp(*buffer, includetag1, strlen(includetag1)) == 0) ||
		  (includetag2 && (strncmp(*buffer, includetag2, strlen(includetag2)) == 0)) )) {
		char *newfn = strchr(*buffer, ' ');
		char *eol = strchr(*buffer, '\n');

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


#ifdef STANDALONE
int main(int argc, char *argv[])
{
	char *fn;
	FILE *fd;
	char *inbuf = NULL;
	int inbufsz;

	fn = argv[1];
	fd = stackfopen(fn, "r");
	if (!fd) { errprintf("Cannot open file %s\n", fn); return 1; }

	while (stackfgets(&inbuf, &inbufsz, "include", NULL)) printf("%s", inbuf);

	stackfclose(fd);
	if (inbuf) xfree(inbuf);

	return 0;
}
#endif

