/*
 * Merge the current hobbitserver.cfg file with a new template. New entries are added,
 * and existing ones are copied over from the current setup.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

typedef struct entry_t {
	char *name;
	char *val;
	int copied;
	struct entry_t *next;
} entry_t;

typedef struct newname_t {
	char *oldname, *newname;
	struct newname_t *next;
} newname_t;

entry_t *head = NULL;
entry_t *tail = NULL;
newname_t *newnames = NULL;

int main(int argc, char *argv[])
{
	char *curfn, *curbckfn, *srcfn;
	FILE *curfd, *curbckfd, *srcfd;
	char delim = '=';
	char l[32768];
	entry_t *ewalk;
	struct stat st;
	int showit = 1;

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

	if (strstr(srcfn, ".csv")) delim = ';';

	if (stat(curfn, &st) == -1) { showit = 0; goto nooriginal; }

	curfd = fopen(curfn, "r");
	unlink(curbckfn); curbckfd = fopen(curbckfn, "w");
	if (curfd == NULL) { printf("Cannot open config file %s\n", curfn); return 1; }
	if (curbckfd == NULL) { printf("Cannot create backup file %s\n", curbckfn); return 1; }

	while (fgets(l, sizeof(l), curfd)) {
		char *bol, *p;

		fprintf(curbckfd, "%s", l);

		bol = l + strspn(l, " \t\r\n");
		if ((*bol == '#') || (*bol == '\0')) continue;

		p = strchr(bol, delim);
		if (p) {
			entry_t *newent;

			*p = '\0';
			newent = (entry_t *)malloc(sizeof(entry_t));
			newent->name = strdup(bol);
			*p = delim;
			newent->val = strdup(l);
			newent->copied = 0;
			newent->next = NULL;

			if (tail == NULL) {
				tail = head = newent;
			}
			else {
				tail->next = newent;
				tail = newent;
			}
		}
	}
	fclose(curfd);
	fclose(curbckfd);

nooriginal:
	srcfd = fopen(srcfn, "r");
	unlink(curfn); curfd = fopen(curfn, "w");
	if (srcfd == NULL) { printf("Cannot open template file %s\n", srcfn); return 1; }
	if (curfd == NULL) { printf("Cannot create config file %s\n", curfn); return 1; }
	while (fgets(l, sizeof(l), srcfd)) {
		char *bol, *p;

		bol = l + strspn(l, " \t\r\n");
		if ((*bol == '#') || (*bol == '\0')) {
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

	fclose(curfd);
	fclose(srcfd);

	return 0;
}

