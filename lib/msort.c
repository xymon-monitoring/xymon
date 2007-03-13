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

static char rcsid[] = "$Id: msort.c,v 1.2 2007-03-13 13:56:26 henrik Exp $";

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "libbbgen.h"

static void *merge(void *left, void *right, 
		   msortcompare_fn_t comparefn, 
		   msortgetnext_fn_t getnext, 
		   msortsetnext_fn_t setnext)
{
	void *head, *tail;

	head = tail = NULL;

	while (left && right) {
		if (comparefn(left, right)) {
			if (tail) {
				setnext(tail, left);
			}
			else {
				head = left;
			}

			tail = left;
			left = getnext(left);
		}
		else {
			if (tail) {
				setnext(tail, right);
			}
			else {
				head = right;
			}

			tail = right;
			right = getnext(right);
		}
	}

	if (left) {
		if (tail) setnext(tail, left); else head = tail = left;
	}

	if (right) {
		if (tail) setnext(tail, right); else head = tail = right;
	}

	return head;
}

void *mergesort(void *head, msortcompare_fn_t comparefn, msortgetnext_fn_t getnext, msortsetnext_fn_t setnext)
{
	void *left, *right, *middle, *walk, *walknext;

	/* First check if list is empty or has only one element */
	if ((head == NULL) || (getnext(head) == NULL)) return head;

	/* 
	 * Find the middle element of the list.
	 * We do this by walking the list until we reach the end.
	 * "middle" takes one step at a time, whereas "walk" takes two.
	 */
	middle = head; 
	walk = getnext(head); /* "walk" must be ahead of "middle" */
	while (walk && (walknext = getnext(walk))) {
		middle = getnext(middle);
		walk = getnext(walknext);
	}

	/* Split the list in two halves, and sort each of them. */
	left = head;
	right = getnext(middle);
	setnext(middle, NULL);

	left = mergesort(left, comparefn, getnext, setnext);
	right = mergesort(right, comparefn, getnext, setnext);

	/* We have sorted the two halves, now we must merge them together */
	return merge(left, right, comparefn, getnext, setnext);
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
	return (((rec_t *)a)->key < ((rec_t *)b)->key);
}

void * record_getnext(void *a)
{
	return ((rec_t *)a)->next;
}

void record_setnext(void *a, void *newval)
{
	((rec_t *)a)->next = (rec_t *)newval;
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
	head = mergesort(head, record_compare, record_getnext, record_setnext);
	dumplist(head);

	return 0;
}
#endif

