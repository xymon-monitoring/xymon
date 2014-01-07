/*
 * Merge the current "[...]" sectioned file with a new template. New entries are added,
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
	struct stat st;
	char l[32768];
	entry_t *ewalk;
	entry_t *newent = NULL;
	int adding = 0;
	int showit = 1;

	if (argc < 3) {
		printf("Usage: %s DESTINATIONFILE TEMPLATEFILE\n", argv[0]);
		return 1;
	}

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

	if (stat(curfn, &st) == -1) { showit = 0; goto nooriginal; }

	curfd = fopen(curfn, "r");
	unlink(curbckfn); curbckfd = fopen(curbckfn, "w");
	if (curfd == NULL) { printf("Cannot open configuration file %s\n", curfn); return 1; }
	if (curbckfd == NULL) { printf("Cannot open backup file %s\n", curbckfn); return 1; }

	while (fgets(l, sizeof(l), curfd)) {
		char *bol, *p;
		newname_t *nwalk;

		fprintf(curbckfd, "%s", l);

		bol = l + strspn(l, " \t\r\n");
		if ((*bol == '#') || (*bol == '\0')) continue;

		if ((*bol == '[') && strchr(bol, ']')) {
			newent = (entry_t *)malloc(sizeof(entry_t));
			p = strchr(bol, ']'); *p = '\0';
			for (nwalk = newnames; (nwalk && strcmp(nwalk->oldname, bol+1)); nwalk = nwalk->next);
			if (nwalk) {
				newent->name = strdup(nwalk->newname);
				newent->val = (char *)malloc(strlen(nwalk->newname) + 4);
				sprintf(newent->val, "[%s]\n", nwalk->newname);
			}
			else {
				newent->name = strdup(bol+1);
				*p = ']';
				newent->val = strdup(l);
			}
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
		else if (newent) {
			newent->val = (char *)realloc(newent->val, strlen(newent->val) + strlen(l) + 1);
			strcat(newent->val, l);
		}
	}
	fclose(curfd);
	fclose(curbckfd);

nooriginal:
	srcfd = fopen(srcfn, "r");
	unlink(curfn); curfd = fopen(curfn, "w");
	if (srcfd == NULL) { printf("Cannot open template file %s\n", srcfn); return 1; }
	if (curfd == NULL) { printf("Cannot open config file %s\n", curfn); return 1; }
	fchmod(fileno(curfd), S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);

	while (fgets(l, sizeof(l), srcfd)) {
		char *bol, *p;

		bol = l + strspn(l, " \t\r\n");

		if ((*bol == '[') && strchr(bol, ']')) {
			p = strchr(bol, ']'); *p = '\0';
			for (ewalk = head; (ewalk && strcmp(ewalk->name, bol+1)); ewalk = ewalk->next) ;
			*p = ']';

			if (ewalk) {
				fprintf(curfd, "%s", ewalk->val);
				ewalk->copied = 1;
				adding = 0;
			}
			else {
				if (showit) printf("Adding new entry to %s: %s", curfn, l);
				fprintf(curfd, "%s", l);
				adding = 1;
			}
		}
		else if (adding || (*bol == '#') || (*bol == '\0')) {
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

