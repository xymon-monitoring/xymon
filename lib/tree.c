/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for tree-based record storage.                        */
/*                                                                            */
/* Copyright (C) 2011-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "tree.h"

typedef struct treerec_t {
	char *key;
	void *userdata;
	int deleted;
} treerec_t;

typedef struct xtree_t {
	treerec_t *entries;
	xtreePos_t treesz;
	int (*compare)(const char *a, const char *b);
} xtree_t;

static xtreePos_t binsearch(xtree_t *mytree, char *key)
{
	xtreePos_t uplim, lowlim, n;

	if (!key) return -1;

	/* Do a binary search  */
	lowlim = 0; uplim = mytree->treesz-1;

	do {
		xtreePos_t res;

		n = (uplim + lowlim) / 2;
		res = mytree->compare(key, mytree->entries[n].key);

		if (res == 0) {
			/* Found it! */
			uplim = -1; /* To exit loop */
		}
		else if (res > 0) {
		  	/* Higher up */
			lowlim = n+1;
		}
		else {
			/* Further down */
			uplim = n-1;
		}

	} while ((uplim >= 0) && (lowlim <= uplim));

	return n;
}

void *xtreeNew(int(*xtreeCompare)(const char *a, const char *b))
{
	xtree_t *newtree = (xtree_t *)calloc(1, sizeof(xtree_t));
	newtree->compare = xtreeCompare;
	return newtree;
}

void xtreeDestroy(void *treehandle)
{
	xtree_t *mytree = (xtree_t *)treehandle;
	xtreePos_t i;

	if (treehandle == NULL) return;

	/* Must delete our privately held keys in the deleted records */
	for (i = 0; (i < mytree->treesz); i++) {
		if (mytree->entries[i].deleted) free(mytree->entries[i].key);
	}
	free(mytree->entries);
	free(mytree);
}

xtreePos_t xtreeFind(void *treehandle, char *key)
{
	xtree_t *mytree = (xtree_t *)treehandle;
	xtreePos_t n;

	/* Does tree exist ? Is it empty? */
	if ((treehandle == NULL) || (mytree->treesz == 0)) return -1;

	n = binsearch(mytree, key);
	if ((n >= 0) && (n < mytree->treesz) && (mytree->entries[n].deleted == 0) && (mytree->compare(key, mytree->entries[n].key) == 0)) 
		return n;

	return -1;
}

xtreePos_t xtreeFirst(void *treehandle)
{
	xtree_t *mytree = (xtree_t *)treehandle;

	/* Does tree exist ? Is it empty? */
	if ((treehandle == NULL) || (mytree->treesz == 0)) return -1;

	return 0;
}

xtreePos_t xtreeNext(void *treehandle, xtreePos_t pos)
{
	xtree_t *mytree = (xtree_t *)treehandle;

	/* Does tree exist ? Is it empty? */
	if ((treehandle == NULL) || (mytree->treesz == 0) || (pos >= (mytree->treesz - 1)) || (pos < 0)) return -1;

	do {
		pos++;
	} while (mytree->entries[pos].deleted && (pos < mytree->treesz));

	return (pos < mytree->treesz) ? pos : -1;
}

char *xtreeKey(void *treehandle, xtreePos_t pos)
{
	xtree_t *mytree = (xtree_t *)treehandle;

	/* Does tree exist ? Is it empty? */
	if ((treehandle == NULL) || (mytree->treesz == 0) || (pos >= mytree->treesz) || (pos < 0)) return NULL;

	return mytree->entries[pos].key;
}

void *xtreeData(void *treehandle, xtreePos_t pos)
{
	xtree_t *mytree = (xtree_t *)treehandle;

	/* Does tree exist ? Is it empty? */
	if ((treehandle == NULL) || (mytree->treesz == 0) || (pos >= mytree->treesz) || (pos < 0)) return NULL;

	return mytree->entries[pos].userdata;
}


xtreeStatus_t xtreeAdd(void *treehandle, char *key, void *userdata)
{
	xtree_t *mytree = (xtree_t *)treehandle;
	xtreePos_t n;

	if (treehandle == NULL) return XTREE_STATUS_NOTREE;

	if (mytree->treesz == 0) {
		/* Empty tree, just add record */
		mytree->entries = (treerec_t *)calloc(1, sizeof(treerec_t));
		mytree->entries[0].key = key;
		mytree->entries[0].userdata = userdata;
		mytree->entries[0].deleted = 0;
	}
	else {
		n = binsearch(mytree, key);

		if ((n >= 0) && (n < mytree->treesz) && (mytree->compare(key, mytree->entries[n].key) == 0)) {
			/* Record already exists */

			if (mytree->entries[n].deleted != 0) {
				/* Revive the old record. Note that we can now discard our privately held key */
				free(mytree->entries[n].key);

				mytree->entries[n].key = key;
				mytree->entries[n].deleted = 0;
				mytree->entries[n].userdata = userdata;
				return XTREE_STATUS_OK;
			}
			else {
				/* Error */
				return XTREE_STATUS_DUPLICATE_KEY;
			}
		}

		/* Must create new record */
		if (mytree->compare(key, mytree->entries[mytree->treesz - 1].key) > 0) {
			/* Add after all the others */
			mytree->entries = (treerec_t *)realloc(mytree->entries, (1 + mytree->treesz)*sizeof(treerec_t));
			mytree->entries[mytree->treesz].key = key;
			mytree->entries[mytree->treesz].userdata = userdata;
			mytree->entries[mytree->treesz].deleted = 0;
		}
		else if (mytree->compare(key, mytree->entries[0].key) < 0) {
			/* Add before all the others */
			treerec_t *newents = (treerec_t *)malloc((1 + mytree->treesz)*sizeof(treerec_t));
			newents[0].key = key;
			newents[0].userdata = userdata;
			newents[0].deleted = 0;
			memcpy(&(newents[1]), &(mytree->entries[0]), (mytree->treesz * sizeof(treerec_t)));
			free(mytree->entries);
			mytree->entries = newents;
		}
		else {
			treerec_t *newents;

			n = binsearch(mytree, key);
			if (mytree->compare(mytree->entries[n].key, key) < 0) n++;

			/* 
			 * n now points to the record AFTER where we will insert data in the current list.
			 * So in the new list, the new record will be in position n.
			 * Check if this is a deleted record, if it is then we won't have to move anything.
			 */
			if (mytree->entries[n].deleted != 0) {
				/* Deleted record, let's re-use it. */
				free(mytree->entries[n].key);
				mytree->entries[n].key = key;
				mytree->entries[n].userdata = userdata;
				mytree->entries[n].deleted = 0;
				return XTREE_STATUS_OK;
			}

			/* Ok, must create a new list and copy entries there */
			newents = (treerec_t *)malloc((1 + mytree->treesz)*sizeof(treerec_t));

			/* Copy record 0..(n-1), i.e. n records */
			memcpy(&(newents[0]), &(mytree->entries[0]), n*sizeof(treerec_t));

			/* New record is the n'th record */
			newents[n].key = key;
			newents[n].userdata = userdata;
			newents[n].deleted = 0;

			/* Finally, copy records n..(treesz-1) from the old list to position (n+1) onwards in the new list */
			memcpy(&(newents[n+1]), &(mytree->entries[n]), (mytree->treesz - n)*sizeof(treerec_t));

			free(mytree->entries);
			mytree->entries = newents;
		}
	}

	mytree->treesz += 1;
	return XTREE_STATUS_OK;
}

void *xtreeDelete(void *treehandle, char *key)
{
	xtree_t *mytree = (xtree_t *)treehandle;
	xtreePos_t n;

	if (treehandle == NULL) return NULL;
	if (mytree->treesz == 0) return NULL;	/* Empty tree */

	n = binsearch(mytree, key);

	if ((n >= 0) && (n < mytree->treesz) && (mytree->entries[n].deleted == 0) && (mytree->compare(key, mytree->entries[n].key) == 0)) {
		mytree->entries[n].key = strdup(mytree->entries[n].key); /* Must dup the key, since user may discard it */
		mytree->entries[n].deleted = 1;
		return mytree->entries[n].userdata;
	}

	return NULL;
}


#ifdef STANDALONE
int main(int argc, char **argv)
{
	char buf[1024], key[1024], data[1024];
	void *th = NULL;
	xtreePos_t n;
	char *rec, *p;

	do {
		printf("New, Add, Find, Delete, dUmp, deStroy : "); fflush(stdout);
		if (fgets(buf, sizeof(buf), stdin) == NULL) return 0;

		switch (*buf) {
		  case 'N': case 'n':
			th = xtreeNew(strcasecmp);
			break;

		  case 'A': case 'a':
			printf("Key:");fflush(stdout); fgets(key, sizeof(key), stdin);
			p = strchr(key, '\n'); if (p) *p = '\0';
			printf("Data:");fflush(stdout); fgets(data, sizeof(data), stdin);
			p = strchr(data, '\n'); if (p) *p = '\0';
			n = xtreeAdd(th, strdup(key), strdup(data));
			printf("Result: %d\n", n);
			break;

		  case 'D': case 'd':
			printf("Key:");fflush(stdout); fgets(key, sizeof(key), stdin);
			p = strchr(key, '\n'); if (p) *p = '\0';
			rec = xtreeDelete(th, key);
			if (rec) {
				printf("Existing record deleted: Data was '%s'\n", rec);
			}
			else {
				printf("No record\n");
			}
			break;

		  case 'F': case 'f':
			printf("Key:");fflush(stdout); fgets(key, sizeof(key), stdin);
			p = strchr(key, '\n'); if (p) *p = '\0';
			n = xtreeFind(th, key);
			if (n >= 0) {
				printf("Found record: Data was '%s'\n", (char *)xtreeData(th, n));
			}
			else {
				printf("No record\n");
			}
			break;

		  case 'U': case 'u':
			n = xtreeFirst(th);
			while (n >= 0) {
				printf("%02d: Key '%s', data '%s'\n", n, (char *)xtreeKey(th, n), (char *)xtreeData(th, n));
				n = xtreeNext(th, n);
			}
			break;

		  case 'S': case 's':
			xtreeDestroy(th);
			th = NULL;
			break;
		}
	} while (1);

	return 0;
}
#endif

