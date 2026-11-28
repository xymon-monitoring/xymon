/*
 * Merge the current xymonserver.cfg file with a new template. New entries are added,
 * and existing ones are copied over from the current setup.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

typedef struct entry_t {
	char *name;
	char *val;
	int copied;
	struct entry_t *next;
	int extracount;
	char **extralines;
} entry_t;

typedef struct newname_t {
	char *oldname, *newname;
	struct newname_t *next;
} newname_t;

entry_t *head = NULL;
entry_t *tail = NULL;
newname_t *newnames = NULL;
char *lastblankandcomment = NULL;

int main(int argc, char *argv[])
{
	char *curfn, *curbckfn, *srcfn, *curtmp;
	FILE *curfd, *curbckfd, *srcfd;
	char delim = '=';
	char alldelims[10];
	char l[32768];
	entry_t *ewalk;
	struct stat st;
	int showit = 1;

	if (argc < 3) { printf("Usage: %s TEMPLATEFILE DESTINATIONFILE\n", argv[0]); return 1; }
	srcfn = strdup(argv[1]);
	curfn = strdup(argv[2]);
	if (argc > 3) {
		int i;
		char *p;

		for (i=3; (i < argc); i++) {
			p = strchr(argv[i], '=');
			if (p) {
				newname_t *newitem = (newname_t *)malloc(sizeof(newname_t));
				*p = '\0';
				newitem->oldname = strdup(argv[i]);
				newitem->newname = strdup(p+1);
				newitem->next = newnames;
				newnames = newitem;
			}
		}
	}

	curbckfn = (char *)malloc(strlen(curfn) + 5);
	sprintf(curbckfn, "%s.bak", curfn);
	curtmp = (char *)malloc(strlen(curfn) + 32);
	sprintf(curtmp, "%s.inst.%ld", curfn, (long)getpid());

	if (strstr(srcfn, ".csv")) {
		delim = ';';
		strcpy(alldelims, ";");
	}
	else {
		sprintf(alldelims, "%c+-", delim);
	}

	/* lstat, not stat: a symlink at the name is not followed. It is left
	 * unread (and replaced below), so its target is never copied into .bak. A
	 * stat error other than "absent" is a failure, not a reason to replace. */
	if (lstat(curfn, &st) == -1) {
		if (errno != ENOENT) { printf("Cannot stat config file %s\n", curfn); return 1; }
		showit = 0; goto nooriginal;
	}
	if (S_ISLNK(st.st_mode)) { showit = 0; goto nooriginal; }

	{ int cfd = open(curfn, O_RDONLY | O_NOFOLLOW); curfd = (cfd >= 0) ? fdopen(cfd, "r") : NULL; if (cfd >= 0 && !curfd) close(cfd); }
	unlink(curbckfn);
	/* The .bak is a private backup the administrator reads as root: created
	 * root-owned, mode 0600, not given the config's Xymon ownership, which a
	 * backup does not need. */
	{ int bfd = open(curbckfn, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600); curbckfd = (bfd >= 0) ? fdopen(bfd, "w") : NULL; if (bfd >= 0 && !curbckfd) close(bfd); }
	if (curfd == NULL) { printf("Cannot open config file %s\n", curfn); return 1; }
	if (curbckfd == NULL) { printf("Cannot create backup file %s\n", curbckfn); return 1; }

	while (fgets(l, sizeof(l), curfd)) {
		char *bol, *p;

		fprintf(curbckfd, "%s", l);

		bol = l + strspn(l, " \t\r\n");
		if ((*bol == '#') || (*bol == '\0')) {
			if (!lastblankandcomment)
				lastblankandcomment = strdup(bol);
			else {
				lastblankandcomment = (char *)realloc(lastblankandcomment, strlen(lastblankandcomment) + strlen(bol) + 1);
				strcat(lastblankandcomment, bol);
			}
			continue;
		}

		if (tail && ((strncmp(bol, "include ", 8) == 0) || (strncmp(bol, "directory ", 10) == 0))) {
			if (!tail->extralines) {
				tail->extracount = 1;
				tail->extralines = (char **)malloc(sizeof(char *));
			}
			else {
				tail->extracount++;
				tail->extralines = (char **)realloc(tail->extralines, (tail->extracount*sizeof(char *)));
			}

			if (!lastblankandcomment)
				tail->extralines[tail->extracount-1] = strdup(bol);
			else {
				tail->extralines[tail->extracount-1] = (char *)malloc(1 + strlen(bol) + strlen(lastblankandcomment));
				sprintf(tail->extralines[tail->extracount-1], "%s%s", lastblankandcomment, bol);
			}

			if (lastblankandcomment) {
				free(lastblankandcomment);
				lastblankandcomment = NULL;
			}

			continue;
		}

		p = bol + strcspn(bol, alldelims);
		if (*p) {
			entry_t *newent;

			if (*p == delim) {
				*p = '\0';
				newent = (entry_t *)calloc(1, sizeof(entry_t));
				newent->name = strdup(bol);
				*p = delim;
				newent->val = strdup(l);

				if (tail == NULL) {
					tail = head = newent;
				}
				else {
					tail->next = newent;
					tail = newent;
				}
			}
			else if (*(p+1) == delim) {
				char sav = *p;
				entry_t *walk;

				*p = '\0';
				for (walk = head; (walk && (strcmp(walk->name, bol) != 0)); walk = walk->next) ;
				*p = sav;
				if (walk) {
					if (!walk->extralines) {
						walk->extracount = 1;
						walk->extralines = (char **)malloc(sizeof(char *));
					}
					else {
						walk->extracount++;
						walk->extralines = (char **)realloc(walk->extralines, (walk->extracount*sizeof(char *)));
					}

					if (!lastblankandcomment)
						walk->extralines[walk->extracount-1] = strdup(bol);
					else {
						walk->extralines[walk->extracount-1] = (char *)malloc(1 + strlen(bol) + strlen(lastblankandcomment));
						sprintf(walk->extralines[walk->extracount-1], "%s%s", lastblankandcomment, bol);
					}
				}
			}

			if (lastblankandcomment) {
				free(lastblankandcomment);
				lastblankandcomment = NULL;
			}
		}
	}
	if (ferror(curfd)) { printf("Read failed on %s\n", curfn); fclose(curbckfd); return 1; }
	fclose(curfd);
	{ int berr = ferror(curbckfd); if (fclose(curbckfd) != 0 || berr) { printf("Backup write failed on %s\n", curbckfn); return 1; } }

	if (lastblankandcomment) {
		/* Add this to the last entry */
		if (!tail->extralines) {
			tail->extracount = 1;
			tail->extralines = (char **)malloc(sizeof(char *));
		}
		else {
			tail->extracount++;
			tail->extralines = (char **)realloc(tail->extralines, (tail->extracount*sizeof(char *)));
		}

		tail->extralines[tail->extracount-1] = strdup(lastblankandcomment);
	}

nooriginal:
	/* Build the replacement in a temp in the destination's directory, then rename
	 * it over the name: the live config is untouched until a complete, checked
	 * result is ready, and the rename acts on the name, never a symlink's target.
	 * Open the template first, so a missing one fails before anything is created. */
	srcfd = fopen(srcfn, "r");
	if (srcfd == NULL) { printf("Cannot open template file %s\n", srcfn); return 1; }
	unlink(curtmp);
	{ int wfd = open(curtmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600); curfd = (wfd >= 0) ? fdopen(wfd, "w") : NULL; if (wfd >= 0 && !curfd) close(wfd); }
	if (curfd == NULL) { printf("Cannot create temporary %s\n", curtmp); fclose(srcfd); return 1; }
	if (fchmod(fileno(curfd), S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) != 0) { printf("Cannot set mode of %s\n", curtmp); unlink(curtmp); return 1; }

	while (fgets(l, sizeof(l), srcfd)) {
		char *bol, *p;

		bol = l + strspn(l, " \t\r\n");
		if ((*bol == '#') || (*bol == '\0') || (strncmp(bol, "include ", 8) == 0) || (strncmp(bol, "directory ", 10) == 0)) {
			fprintf(curfd, "%s", l);
			continue;
		}

		p = strchr(bol, delim);
		if (p) {
			/* Find the old value */
			*p = '\0';
			for (ewalk = head; (ewalk && strcmp(ewalk->name, bol)); ewalk = ewalk->next) ;
			if (!ewalk) {
				/* See if it's been renamed */
				newname_t *nwalk;
				for (nwalk = newnames; (nwalk && strcmp(nwalk->newname, bol)); nwalk = nwalk->next) ;
				if (nwalk) {
					/* It has - find the value of the old setting */
					for (ewalk = head; (ewalk && strcmp(ewalk->name, nwalk->oldname)); ewalk = ewalk->next) ;
					if (ewalk) {
						/* Merge it with the new name */
						char *newval;
						char *oval = strchr(ewalk->val, delim);
						newval = (char *)malloc(strlen(nwalk->newname) + strlen(oval) + 1);
						sprintf(newval, "%s%s", nwalk->newname, oval);
						ewalk->val = newval;
					}
				}
			}
			*p = delim;

			if (ewalk) {
				fprintf(curfd, "%s", ewalk->val);
				if (ewalk->extralines) {
					int i;

					for (i = 0; (i < ewalk->extracount); i++) 
						fprintf(curfd, "%s", ewalk->extralines[i]);
				}
				ewalk->copied = 1;
			}
			else {
				if (showit) printf("Adding new entry to %s: %s", curfn, l);
				fprintf(curfd, "%s", l);
			}
		}
		else {
			fprintf(curfd, "%s", l);
		}
	}

	/* Copy over any local settings that have been added */
	for (ewalk = head; (ewalk); ewalk = ewalk->next) {
		if (!ewalk->copied) {
			fprintf(curfd, "%s", ewalk->val);
		}
	}

	/* A buffered write error (ENOSPC, say) surfaces only at flush/close, and a
	 * template read error -- including fgets on a directory handed as the template
	 * -- would have stopped the loop early: report either, discard the temp, and
	 * leave the live config in place. Compute ferror before fclose, which clears
	 * the stream, and always close. */
	if (ferror(srcfd)) { printf("Read failed on template %s\n", srcfn); unlink(curtmp); return 1; }
	if (fflush(curfd) != 0 || (fsync(fileno(curfd)) != 0 && errno != EINVAL && errno != ENOTSUP)) {
		printf("Cannot flush %s\n", curtmp); fclose(curfd); unlink(curtmp); return 1;
	}
	{ int werr = ferror(curfd); if (fclose(curfd) != 0 || werr) { printf("Write failed on %s\n", curtmp); unlink(curtmp); return 1; } }
	fclose(srcfd);
	/* Atomic replace, on the name. */
	if (rename(curtmp, curfn) != 0) { printf("Cannot install %s\n", curfn); unlink(curtmp); return 1; }

	return 0;
}

