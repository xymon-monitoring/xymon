/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for reading configuration files with "include"s.      */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: stackio.c,v 1.1 2004-10-30 15:31:37 henrik Exp $";

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "errormsg.h"
#include "misc.h"
#include "stackio.h"

typedef struct stackfd_t {
	FILE *fd;
	struct stackfd_t *next;
} stackfd_t;
static stackfd_t *fdhead = NULL;
static char stackfd_base[PATH_MAX];
static char stackfd_mode[10];


FILE *stackfopen(char *filename, char *mode)
{
	FILE *newfd;
	stackfd_t *newitem;
	char stackfd_filename[PATH_MAX];

	if (fdhead == NULL) {
		char *p;

		strcpy(stackfd_base, filename);
		p = strrchr(stackfd_base, '/'); if (p) *(p+1) = '\0';

		strcpy(stackfd_mode, mode);

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
			free(olditem);
		}
		stackfd_base[0] = '\0';
		stackfd_mode[0] = '\0';
		result = 0;
	}
	else {
		olditem = fdhead;
		fdhead = fdhead->next;
		result = fclose(olditem->fd);
		free(olditem);
	}

	return result;
}

static char *read_line_1(struct linebuf_t *buffer, FILE *stream, int *docontinue)
{
	char l[PATH_MAX];
	char *p, *start;

	*docontinue = 0;
	if (fgets(l, sizeof(l), stream) == NULL) return NULL;

	p = strchr(l, '\n'); if (p) *p = '\0';

	/* Strip leading spaces */
	for (start=l; (*start && isspace((int) *start)); start++) ;

	/* Strip trailing spaces while looking for continuation character */
	for (p = start + strlen(start) - 1; ((p > start) && (isspace((int) *p) || (*p == '\\')) ); p--) {
		if (*p == '\\') *docontinue = 1;
	}
	*(p+1) = '\0';

	if ((strlen(start) + strlen(buffer->buf) + 2) > buffer->buflen) {
		buffer->buflen += MAX_LINE_LEN;
		buffer->buf = (char *)realloc(buffer->buf, buffer->buflen);
	}

	strcat(buffer->buf, start);
	if (*docontinue) strcat(buffer->buf, " ");
	return buffer->buf;
}

char *read_line(struct linebuf_t *buffer, FILE *stream)
{
	char *result = NULL;
	int docontinue = 0;

	if (buffer->buf == NULL) {
		buffer->buflen = MAX_LINE_LEN;
		buffer->buf = (char *)malloc(buffer->buflen);
	}
	*(buffer->buf) = '\0';

	do {
		result = read_line_1(buffer, stream, &docontinue);
	} while (result && docontinue);

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

