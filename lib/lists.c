/*----------------------------------------------------------------------------*/
/*                                                                            */
/* Generic double-linked list implementation.                                 */
/*                                                                            */
/* Copyright (C) 2011-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";


#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "libxymon.h"

listhead_t *list_create(char *name)
{
	listhead_t *result = (listhead_t *)calloc(1, sizeof(listhead_t));
	result->listname = strdup(name);
	return result;
}

void list_item_move(listhead_t *tolist, listitem_t *rec, char *info)
{
	listhead_t *fromlist = (listhead_t *)rec->keeper;

	// dbgprintf("moving item %s from %s to %s\n", info, (fromlist ? fromlist->listname : "<NULL>"), (tolist ? tolist->listname : "<NULL>"));

	if (fromlist) {
		if (rec->next) rec->next->previous = rec->previous;
		if (rec->previous) rec->previous->next = rec->next;

		if (rec == fromlist->head) {
			/* removing the head of the list */
			fromlist->head = rec->next;
		}

		if (rec == fromlist->tail) {
			/* removing the tail of the list */
			fromlist->tail = rec->previous;
		}

		rec->next = rec->previous = rec->keeper = NULL;
		fromlist->len -= 1;
	}

	if (tolist) {
		if (tolist->tail) {
			tolist->tail->next = rec;
			rec->next = NULL;
			rec->previous = tolist->tail;
			tolist->tail = rec;
		}
		else {
			tolist->head = tolist->tail = rec;
			rec->previous = rec->next = NULL;
		}

		rec->next = NULL;
		rec->keeper = tolist;
		tolist->len += 1;
	}
}

listitem_t *list_item_create(listhead_t *listhead, void *data, char *info)
{
	listitem_t *rec = (listitem_t *)calloc(1, sizeof(listitem_t));
	rec->data = data;
	list_item_move(listhead, rec, info);

	return rec;
}

void *list_item_delete(listitem_t *rec, char *info)
{
	void *result;

	list_item_move(NULL, rec, info);
	result = rec->data;
	xfree(rec);

	return result;
}


typedef struct shufflerec_t {
	long int num;
	listitem_t *item;
} shufflerec_t;

static int shufflecomp(const void *v1, const void *v2)
{
	shufflerec_t *r1 = (shufflerec_t *)v1;
	shufflerec_t *r2 = (shufflerec_t *)v2;

	if (r1->num > r2->num) return 1;
	else if (r1->num < r2->num) return -1;
	else return 0;
}

void list_shuffle(listhead_t *list)
{
	struct timeval tv;
	shufflerec_t *srecs;
	int i;
	listitem_t *walk;

	gettimeofday(&tv, NULL);
	srandom(tv.tv_usec);
	srecs = (shufflerec_t *)calloc(list->len, sizeof(shufflerec_t));

	for (i=0, walk=list->head; (walk); walk=walk->next, i++) {
		srecs[i].num = random();
		srecs[i].item = walk;
	}

	qsort(&srecs[0], list->len, sizeof(shufflerec_t), shufflecomp);

	for (i=0; (i < list->len); i++) {
		srecs[i].item->previous = (i == 0) ? NULL : srecs[i-1].item;
		srecs[i].item->next = (i == list->len-1) ? NULL : srecs[i+1].item;
	}
	list->head = srecs[0].item;
	list->tail = srecs[list->len-1].item;

	xfree(srecs);
}


#if 0
typedef struct rec_t {
	char *key;
	int val;
} rec_t;

void listdump(listhead_t *lh)
{
	listitem_t *walk;
	int i;

	for (walk = lh->head, i = 0; (walk); walk = walk->next, i++) {
		rec_t *r = walk->data;

		printf("Record %d : Key %s, value %d\n", i, r->key, r->val);
	}
}

int main(int argc, char **argv)
{
	listhead_t *mylist;
	rec_t r1 = { "welkrj", 12 };
	rec_t r2 = { "uof83lkj", 498 };
	rec_t r3 = { "adnmwekh", 102 };
	rec_t r4 = { "iww03k", 592 };
	rec_t r5 = { "dckofoi", 1045 };

	mylist = list_create("mylist");
	list_item_create(mylist, &r1, r1.key);
	list_item_create(mylist, &r2, r2.key);
	list_item_create(mylist, &r3, r3.key);
	list_item_create(mylist, &r4, r4.key);
	list_item_create(mylist, &r5, r5.key);

	printf("Before shuffle:\n"); listdump(mylist);
	list_shuffle(mylist);
	printf("\n\nAfter shuffle:\n"); listdump(mylist);

	return 0;
}
#endif

