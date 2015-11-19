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

static char rcsid[] = "$Id: files.c 6712 2011-07-31 21:01:52Z storner $";

#include "config.h"
#ifdef HAVE_BINARY_TREE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "tree.h"


#ifdef HAVE_BINARY_TREE
#include <search.h>

static treerec_t *i_curr = NULL;


void xtree_empty(void *dummy)
{
	free(dummy);
}


static int xtree_i_compare(const void *pa, const void *pb)
{
	const treerec_t *reca = pa, *recb = pb;
	return (reca->compare)(reca->key, recb->key);
}


void *xtreeNew(int(*xtreeCompare)(const char *a, const char *b))
{
	xtree_t *newtree;

	newtree = (xtree_t *)calloc(1, sizeof(xtree_t));
	newtree->compare = xtreeCompare;
	newtree->root = NULL;

	return newtree;
}

void xtreeDestroy(void *treehandle)
{
	xtree_t *tree = treehandle;

#ifdef DEBUG
	if (tree == NULL) fprintf(stderr, "Asked to destroy a NULL tree? %p\n", treehandle);
#endif
	if (tree == NULL) return;
	tdestroy(tree->root, free);
	free(treehandle);
}

xtreeStatus_t xtreeAdd(void *treehandle, char *key, void *userdata)
{
	xtree_t *tree = treehandle;
	treerec_t *rec, **erec;

	if (!tree) return XTREE_STATUS_NOTREE;

	rec = (treerec_t *)calloc(1, sizeof(treerec_t));
	rec->key = key;
	rec->userdata = userdata;
	rec->compare = tree->compare;

	erec = tsearch(rec, &tree->root, xtree_i_compare);
	if (erec == NULL) {
		free(rec);
		return XTREE_STATUS_MEM_EXHAUSTED;
	}
	if (*erec != rec) {
		/* Was already there */
		free(rec);
		return XTREE_STATUS_DUPLICATE_KEY;
	}

	return XTREE_STATUS_OK;
}


void *xtreeDelete(void *treehandle, char *key)
{
	xtree_t *tree = treehandle;
	treerec_t **result, *zombie, rec;
	void *userdata;

	if (!tree) return NULL;

	rec.key = key;
	rec.userdata = NULL;
	rec.compare = tree->compare;
	result = tfind(&rec, &tree->root, xtree_i_compare);
	if (result == NULL) {
		/* Not found */
		return NULL;
	}

	userdata = (*result)->userdata;
	zombie = (*result);
	tdelete(&rec, &tree->root, xtree_i_compare);
	free(zombie->key); /* Can't assume this is ours. We'll leak, but use xtreeDestroy to walk */
	free(zombie);

	return userdata;
}


xtreePos_t xtreeFind(void *treehandle, char *key)
{
	xtree_t *tree = treehandle;
	treerec_t **result, rec;

	if (!tree) return NULL;

	rec.key = key;
	rec.userdata = NULL;
	rec.compare = tree->compare;
	result = tfind(&rec, &tree->root, xtree_i_compare);

	return (result ? *result : NULL);
}


static void xtree_i_action(const void *nodep, const VISIT which, const int depth)
{
	treerec_t *rec = NULL;

	switch (which) {
	  case preorder:
		break;
	  case postorder:
		rec = *(treerec_t **) nodep;
		break;
	  case endorder:
		break;
	  case leaf:
		rec = *(treerec_t **) nodep;
		break;
	}

	if (rec) {
		/*
		 * Each time here, we have rec pointing to the next record in the tree, and i_curr is then
		 * pointing to the previous record. So build a linked list of the records going backwards
		 * as we move through the tree.
		 *
		 * R0 <- R1:link <- R2:link <- R3:link
		 *                              ^
		 *                              i_curr
		 *
		 * becomes
		 *
		 * R0 <- R1:link <- R2:link <- R3:link <- rec:link
		 *                                         ^
		 *                                         i_curr
		 */
		rec->link = i_curr;
		i_curr = rec;
	}
}

xtreePos_t xtreeFirst(void *treehandle)
{
	xtree_t *tree = treehandle;
	treerec_t *walk, *right, *left;

	if (!tree) return NULL;

	i_curr = NULL;
	twalk(tree->root, xtree_i_action);
	if (!i_curr) return NULL;

	/*
	 * We have walked the tree and created a reverse-linked list of the records.
	 * Now reverse the list so we get the records in the right sequence.
	 * i_curr points to the last entry.
	 *
	 * R1 <- R2 <- R3 <- R4
	 *                   ^
	 *                   i_curr
	 *
	 * must be reversed to
	 *
	 * R1 -> R2 -> R3 -> R4
	 */

	walk = i_curr;
	right = NULL;
	while (walk->link) {
		left = walk->link;
		walk->link = right;
		right = walk;
		walk = left;
	}
	walk->link = right;

	i_curr = NULL;
	return walk;
}

xtreePos_t xtreeNext(void *treehandle, xtreePos_t pos)
{
	return pos ? ((treerec_t *)pos)->link : NULL;
}


char *xtreeKey(void *treehandle, xtreePos_t pos)
{
	return pos ? ((treerec_t *)pos)->key : NULL;
}

void *xtreeData(void *treehandle, xtreePos_t pos)
{
	return pos ? ((treerec_t *)pos)->userdata : NULL;
}

#else

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
#endif


#ifdef STANDALONE
int main(int argc, char **argv)
{
	char buf[1024], key[1024], data[1024];
	void *th = NULL;
	xtreePos_t n;
	xtreeStatus_t stat;
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
			stat = xtreeAdd(th, strdup(key), strdup(data));
			printf("Result: %d\n", stat);
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
			if (n != xtreeEnd(th)) {
				printf("Found record: Data was '%s'\n", (char *)xtreeData(th, n));
			}
			else {
				printf("No record\n");
			}
			break;

		  case 'U': case 'u':
			n = xtreeFirst(th);
			while (n != xtreeEnd(th)) {
				printf("Key '%s', data '%s'\n", (char *)xtreeKey(th, n), (char *)xtreeData(th, n));
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

