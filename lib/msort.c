/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains a "mergesort" implementation for sorting linked lists.         */
/*                                                                            */
/* Based on http://en.wikipedia.org/wiki/Merge_sort pseudo code, adapted for  */
/* use in a generic library routine.                                          */
/*                                                                            */
/* Copyright (C) 2007 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: msort.c,v 1.1 2007-02-25 22:50:26 henrik Exp $";

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <string.h>


#include "libbbgen.h"

/* The linked list records MUST have the "next" field as the first field of the record */
typedef struct msortrec_t {
	struct msortrec_t *next;
} msortrec_t;

static msortrec_t *merge(msortrec_t *left, msortrec_t *right, msortcompare_fn_t comparefn_in)
{
	msortrec_t *head, *tail;

	head = tail = NULL;

	while (left && right) {
		if (comparefn_in(left, right)) {
			if (tail) {
				tail->next = left;
			}
			else {
				head = left;
			}

			tail = left;
			left = left->next;
		}
		else {
			if (tail) {
				tail->next = right;
			}
			else {
				head = right;
			}

			tail = right;
			right = right->next;
		}
	}

	if (left) {
		if (tail) tail->next = left; else head = tail = left;
	}

	if (right) {
		if (tail) tail->next = right; else head = tail = right;
	}

	return head;
}

void *mergesort(void *head_in, msortcompare_fn_t comparefn_in)
{
	msortrec_t *head, *left, *right, *middle, *walk;

	head = (msortrec_t *)head_in;

	/* First check if list is empty or has only one element */
	if ((head == NULL) || (head->next == NULL)) return head;

	/* 
	 * Find the middle element of the list.
	 * We do this by walking the list until we reach the end.
	 * "middle" takes one step at a time, whereas "walk" takes two.
	 */
	middle = head; 
	walk = head->next; /* "walk" must be ahead of "middle" */
	while (walk && walk->next) {
		middle = middle->next;
		walk = walk->next->next;
	}

	/* Split the list in two halves, and sort each of them. */
	left = head;
	right = middle->next;
	middle->next = NULL;

	left = (msortrec_t *)mergesort(left, comparefn_in);
	right = (msortrec_t *)mergesort(right, comparefn_in);

	/* We have sorted the two halves, now we must merge them together */
	return (void *)merge(left, right, comparefn_in);
}


#ifdef STANDALONE

typedef struct rec_t {
	struct rec_t *next;
	int key;
	char *val;
} rec_t;


void dumplist(rec_t *head)
{
	rec_t *walk;

	walk = head; while (walk) { printf("%4d ", walk->key); walk = walk->next; } printf("\n");
	walk = head; while (walk) { printf("%4s ", walk->val); walk = walk->next; } printf("\n");
	printf("\n");
}


int record_compare(void *a, void *b)
{
	rec_t *arec, *brec;

	arec = (rec_t *)a;
	brec = (rec_t *)b;

	return (arec->key < brec->key);
}


int main(int argc, char *argv[])
{
	int i;
	rec_t *head, *newrec;
	struct timeval tv;
	struct timezone tz;
	
	gettimeofday(&tv, &tz);
	srand(tv.tv_usec);
	head = NULL;
	for (i=0; i<20; i++) {
		char v[10];

		newrec = (rec_t *)calloc(1, sizeof(rec_t));
		newrec->key = random() % 1000;
		sprintf(v, "%c", i+'A');
		newrec->val = strdup(v);
		newrec->next = head;
		head = newrec;
	}

	dumplist(head);
	head = mergesort(head, record_compare);
	dumplist(head);

	return 0;
}
#endif

