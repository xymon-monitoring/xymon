/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for reading configuration files with "include"s.      */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>

#include "libxymon.h"

typedef struct filelist_t {
	char *filename;
	time_t mtime;
	size_t fsize;
	struct filelist_t *next;
} filelist_t;

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
	filelist_t **listhead;
	struct stackfd_t *next;
} stackfd_t;
static stackfd_t *fdhead = NULL;
static char *stackfd_base = NULL;
static char *stackfd_mode = NULL;
static htnames_t *fnlist = NULL;

/*
 * initfgets() and unlimfgets() implements a fgets() style
 * input routine that can handle arbitrarily long input lines.
 * Buffer space for the input is dynamically allocated and
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

char *unlimfgets(strbuffer_t *buffer, FILE *fd)
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
	clearstrbuffer(buffer);

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
					addtobuffer(buffer, fg->inbufp);
					fg->inbufp = eoln+1; 
					eoln = NULL;
				}
				else {
					char savech = *(eoln+1); 
					*(eoln+1) = '\0';
					addtobuffer(buffer, fg->inbufp);
					*(eoln+1) = savech; 
					fg->inbufp = eoln+1; 
				}
			}
			else {
				/* No newline in buffer, so add all of it to the output buffer */
				addtobuffer(buffer, fg->inbufp);

				/* Input buffer is now empty */
				*(fg->inbuf) = '\0';
				fg->inbufp = fg->inbuf;
			}
		}

		if (!eoln && !continued) {
			/* Get data for the input buffer */
			char *inpos = fg->inbuf;
			size_t insize = sizeof(fg->inbuf);

			/* If the last byte we read was a continuation char, we must do special stuff.
			 *
			 * Mike Romaniw discovered that if we hit an input with a newline exactly at
			 * the point of a buffer refill, then strlen(*buffer) is 0, and contchar then
			 * points before the start of the buffer. Bad. But this can only happen when
			 * the previous char WAS a newline, and hence it is not a continuation line.
			 * So the simple fix is to only do the cont-char stuff if **buffer is not NUL.
			 * Hence the test for both *buffer and **buffer.
			 */
			if (STRBUF(buffer) && *STRBUF(buffer)) {
				char *contchar = STRBUF(buffer) + STRBUFLEN(buffer) - 1;
				while ((contchar > STRBUF(buffer)) && isspace((int)*contchar) && (*contchar != '\\')) contchar--;

				if (*contchar == '\\') {
					/*
					 * Remove the cont. char from the output buffer, and stuff it into
					 * the input buffer again - so we can check if there's a new-line coming.
					 */
					strbufferchop(buffer, 1);
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

	return STRBUF(buffer);
}

FILE *stackfopen(char *filename, char *mode, void **v_listhead)
{
	FILE *newfd;
	stackfd_t *newitem;
	char stackfd_filename[PATH_MAX];
	filelist_t **listhead = (filelist_t **)v_listhead;

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

	dbgprintf("Opening file %s\n", stackfd_filename);
	newfd = fopen(stackfd_filename, stackfd_mode);
	if (newfd != NULL) {
		newitem = (stackfd_t *) malloc(sizeof(stackfd_t));
		newitem->fd = newfd;
		newitem->listhead = listhead;
		newitem->next = fdhead;
		fdhead = newitem;
		initfgets(newfd);

		if (listhead) {
			struct filelist_t *newlistitem;
			struct stat st;

			fstat(fileno(newfd), &st);
			newlistitem = (filelist_t *)malloc(sizeof(filelist_t));
			newlistitem->filename = strdup(stackfd_filename);
			newlistitem->mtime = st.st_mtime;
			newlistitem->fsize = st.st_size;
			newlistitem->next = *listhead;
			*listhead = newlistitem;
		}
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
		while (fnlist) {
			htnames_t *tmp = fnlist;
			fnlist = fnlist->next;
			xfree(tmp->name); xfree(tmp);
		}
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

int stackfmodified(void *v_listhead)
{
	/* Walk the list of filenames, and see if any have changed */
	filelist_t *walk;
	struct stat st;

	for (walk=(filelist_t *)v_listhead; (walk); walk = walk->next) {
		if (stat(walk->filename, &st) == -1) {
			dbgprintf("File %s no longer exists\n", walk->filename);
			return 1; /* File has disappeared */
		}
		if (st.st_mtime != walk->mtime) {
			dbgprintf("File %s new timestamp\n", walk->filename);
			return 1; /* Timestamp has changed */
		}
		if (S_ISREG(st.st_mode) && (st.st_size != walk->fsize)) {
			dbgprintf("File %s new size\n", walk->filename);
			return 1; /* Size has changed */
		}
	}

	return 0;
}

void stackfclist(void **v_listhead)
{
	/* Free the list of filenames */
	filelist_t *tmp;

	if ((v_listhead == NULL) || (*v_listhead == NULL)) return;

	while (*v_listhead) {
		tmp = (filelist_t *) *v_listhead;
		*v_listhead = ((filelist_t *) *v_listhead)->next;
		xfree(tmp->filename);
		xfree(tmp);
	}
	*v_listhead = NULL;
}

static int namecompare(const void *v1, const void *v2)
{
	char **n1 = (char **)v1;
	char **n2 = (char **)v2;

	/* Sort in reverse order, so when we add them to the list in LIFO, it will be alpha-sorted */
	return -strcmp(*n1, *n2);
}

static void addtofnlist(char *dirname, void **v_listhead)
{
	filelist_t **listhead = (filelist_t **)v_listhead;
	DIR *dirfd;
	struct dirent *d;
	struct stat st;
	char dirfn[PATH_MAX], fn[PATH_MAX];
	char **fnames = NULL;
	int fnsz = 0;
	int i;

	if (*dirname == '/') strcpy(dirfn, dirname); else sprintf(dirfn, "%s/%s", stackfd_base, dirname);

	if ((dirfd = opendir(dirfn)) == NULL) {
		errprintf("Cannot open directory %s\n", dirfn);
		return;
	}

	/* Add the directory itself to the list of files we watch for modifications */
	if (listhead) {
		filelist_t *newlistitem;

		stat(dirfn, &st);
		newlistitem = (filelist_t *)malloc(sizeof(filelist_t));
		newlistitem->filename = strdup(dirfn);
		newlistitem->mtime = st.st_mtime;
		newlistitem->fsize = 0; /* We dont check sizes of directories */
		newlistitem->next = *listhead;
		*listhead = newlistitem;
	}

	while ((d = readdir(dirfd)) != NULL) {
		int fnlen = strlen(d->d_name);

		/* Skip all dot-files */
		if (*(d->d_name) == '.') continue;

		/* Skip editor backups - file ending with '~' */
		if (*(d->d_name + fnlen - 1) == '~') continue;

		/* Skip RCS files - they end with ",v" */
		if ((fnlen >= 2) && (strcmp(d->d_name + fnlen - 2, ",v") == 0)) continue;

		/* Skip Debian installer left-overs - they end with ".dpkg-new"  or .dpkg-orig */
		if ((fnlen >= 9) && (strcmp(d->d_name + fnlen - 9, ".dpkg-new") == 0)) continue;
		if ((fnlen >= 10) && (strcmp(d->d_name + fnlen - 10, ".dpkg-orig") == 0)) continue;


		/* Skip RPM package debris - they end with ".rpmsave" or .rpmnew */
		if ((fnlen >= 8) && (strcmp(d->d_name + fnlen - 8, ".rpmsave") == 0)) continue;
		if ((fnlen >= 7) && (strcmp(d->d_name + fnlen - 7, ".rpmnew") == 0)) continue;

		sprintf(fn, "%s/%s", dirfn, d->d_name);
		if (stat(fn, &st) == -1) continue;

		if (S_ISDIR(st.st_mode)) {
			/* Skip RCS sub-directories */
			if (strcmp(d->d_name, "RCS") == 0) continue;
			addtofnlist(fn, v_listhead);
		}

		/* Skip everything that isn't a regular file */
		if (!S_ISREG(st.st_mode)) continue;

		if (fnsz == 0) fnames = (char **)malloc(2*sizeof(char **)); 
		else fnames = (char **)realloc(fnames, (fnsz+2)*sizeof(char **));
		fnames[fnsz] = strdup(fn);

		fnsz++;
	}

	closedir(dirfd);

	if (fnsz) {
		qsort(fnames, fnsz, sizeof(char *), namecompare);
		for (i=0; (i<fnsz); i++) {
			htnames_t *newitem = malloc(sizeof(htnames_t));
			newitem->name = fnames[i];
			newitem->next = fnlist;
			fnlist = newitem;
		}

		xfree(fnames);
	}
}

char *stackfgets(strbuffer_t *buffer, char *extraincl)
{
	char *result;

	result = unlimfgets(buffer, fdhead->fd);

	if (result) {
		char *bufpastwhitespace = STRBUF(buffer) + strspn(STRBUF(buffer), " \t");

		if ( (strncmp(bufpastwhitespace, "include ", 8) == 0) ||
		     (extraincl && (strncmp(bufpastwhitespace, extraincl, strlen(extraincl)) == 0)) ) {
			char *newfn, *eol, eolchar;

			eol = bufpastwhitespace + strcspn(bufpastwhitespace, "\r\n"); if (eol) { eolchar = *eol; *eol = '\0'; }
			newfn = bufpastwhitespace + strcspn(bufpastwhitespace, " \t");
			newfn += strspn(newfn, " \t");
		
			if (*newfn && (stackfopen(newfn, "r", (void **)fdhead->listhead) != NULL))
				return stackfgets(buffer, extraincl);
			else {
				errprintf("WARNING: Cannot open include file '%s', line was:%s\n", 
					  newfn, STRBUF(buffer));
				if (eol) *eol = eolchar;
				return result;
			}
		}
		else if (strncmp(bufpastwhitespace, "directory ", 10) == 0) {
			char *dirfn, *eol, eolchar;

			eol = bufpastwhitespace + strcspn(bufpastwhitespace, "\r\n"); if (eol) { eolchar = *eol; *eol = '\0'; }
			dirfn = bufpastwhitespace + 9;
			dirfn += strspn(dirfn, " \t");

			if (*dirfn) addtofnlist(dirfn, (void **)fdhead->listhead);
			if (fnlist && (stackfopen(fnlist->name, "r", (void **)fdhead->listhead) != NULL)) {
				htnames_t *tmp = fnlist;

				fnlist = fnlist->next;
				xfree(tmp->name); xfree(tmp);
				return stackfgets(buffer, extraincl);
			}
			else if (fnlist) {
				htnames_t *tmp = fnlist;

				errprintf("WARNING: Cannot open include file '%s', line was:%s\n", fnlist->name, buffer);
				fnlist = fnlist->next;
				xfree(tmp->name); xfree(tmp);
				if (eol) *eol = eolchar;
				return result;
			}
			else {
				/* Empty directory include - return a blank line */
				*result = '\0'; 
				return result;
			}
		}
	}
	else if (result == NULL) {
		/* end-of-file on read */
		stackfclose(NULL);
		if (fnlist) {
			if (stackfopen(fnlist->name, "r", (void **)fdhead->listhead) != NULL) {
				htnames_t *tmp = fnlist;

				fnlist = fnlist->next;
				xfree(tmp->name); xfree(tmp);
				return stackfgets(buffer, extraincl);
			}
			else {
				htnames_t *tmp = fnlist;

				errprintf("WARNING: Cannot open include file '%s', line was:%s\n", fnlist->name, buffer);
				fnlist = fnlist->next;
				xfree(tmp->name); xfree(tmp);
				return result;
			}
		}
		else if (fdhead != NULL)
			return stackfgets(buffer, extraincl);
		else
			return NULL;
	}

	return result;
}


#ifdef STANDALONE
int main(int argc, char *argv[])
{
	char *fn, *p;
	char cmd[1024];
	FILE *fd;
	strbuffer_t *inbuf = newstrbuffer(0);
	void *listhead = NULL;
	int done, linenum;

	fn = strdup(argv[1]);
	strcpy(cmd, "!");
	done = 0;
	while (!done) {
		if (*cmd == '!') {
			fd = stackfopen(fn, "r", &listhead);
			linenum = 1;
			if (!fd) { errprintf("Cannot open file %s\n", fn); continue; }

			while (stackfgets(inbuf, NULL)) {
				linenum++;
				printf("%s", STRBUF(inbuf));
			}
			stackfclose(fd);
		}
		else if (*cmd == '?') {
			filelist_t *walk = (filelist_t *)listhead;

			while (walk) {
				printf("%s %lu\n", walk->filename, (unsigned long)walk->fsize);
				walk = walk->next;
			}
			if (stackfmodified(listhead)) printf("File(s) have been modified\n");
			else printf("No changes\n");
		}
		else if (*cmd == '.') {
			done = 1;
			continue;
		}
		else {
			xfree(fn); fn = strdup(cmd);
			stackfclist(&listhead);
			strcpy(cmd, "!");
			continue;
		}

		printf("\nCmd: "); fflush(stdout);
		fgets(cmd, sizeof(cmd), stdin);
		p = strchr(cmd, '\n'); if (p) *p = '\0';
	}

	xfree(fn);
	stackfclist(&listhead);

	return 0;
}
#endif

