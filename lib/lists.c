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

static char rcsid[] = "$Id: tcplib.c,v 1.14 2011/12/26 12:07:06 henrik Exp henrik $";


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

	dbgprintf("moving item %s from %s to %s\n", info, (fromlist ? fromlist->listname : "<NULL>"), (tolist ? tolist->listname : "<NULL>"));

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

