/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains a "mergesort" implementation for sorting linked lists.         */
/*                                                                            */
/* Based on http://en.wikipedia.org/wiki/Merge_sort pseudo code, adapted for  */
/* use in a generic library routine.                                          */
/*                                                                            */
/* Copyright (C) 2009-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: msort.c 6712 2011-07-31 21:01:52Z storner $";

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "libxymon.h"

#if 0
static void *merge(void *left, void *right, 
		   msortcompare_fn_t comparefn, 
		   msortgetnext_fn_t getnext, 
		   msortsetnext_fn_t setnext)
{
	void *head, *tail;

	head = tail = NULL;

	while (left && right) {
		if (comparefn(left, right) < 0) {
			/* Add the left item to the resultlist */
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
			/* Add the right item to the resultlist */
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

	/* One or both lists have ended. Add whatever elements may remain */
	if (left) {
		if (tail) setnext(tail, left); else head = tail = left;
	}

	if (right) {
		if (tail) setnext(tail, right); else head = tail = right;
	}

	return head;
}

void *msort(void *head, msortcompare_fn_t comparefn, msortgetnext_fn_t getnext, msortsetnext_fn_t setnext)
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
	walk = getnext(middle); /* "walk" must be ahead of "middle" */
	while (walk && (walknext = getnext(walk))) {
		middle = getnext(middle);
		walk = getnext(walknext);
	}

	/* Split the list in two halves, and sort each of them. */
	left = head;
	right = getnext(middle);
	setnext(middle, NULL);

	left = msort(left, comparefn, getnext, setnext);
	right = msort(right, comparefn, getnext, setnext);

	/* We have sorted the two halves, now we must merge them together */
	return merge(left, right, comparefn, getnext, setnext);
}
#endif

void *msort(void *head, msortcompare_fn_t comparefn, msortgetnext_fn_t getnext, msortsetnext_fn_t setnext)
{
	void *walk;
	int len, i;
	void **plist;

	for (walk = head, len=0; (walk); walk = getnext(walk)) len++;
	plist = malloc((len+1) * sizeof(void *));
	for (walk = head, i=0; (walk); walk = getnext(walk)) plist[i++] = walk;
	plist[len] = NULL;

	qsort(plist, len, sizeof(plist[0]), comparefn);

	for (i=0, head = plist[0]; (i < len); i++) setnext(plist[i], plist[i+1]);
	xfree(plist);

	return head;
}


#ifdef STANDALONE

typedef struct rec_t {
	struct rec_t *next;
	char *key;
	char *val;
} rec_t;


void dumplist(rec_t *head)
{
	rec_t *walk;

	walk = head; while (walk) { printf("%p : %-15s:%3s\n", walk, walk->key, walk->val); walk = walk->next; } printf("\n");
	printf("\n");
}


int record_compare(void **a, void **b)
{
	rec_t **reca = (rec_t **)a;
	rec_t **recb = (rec_t **)b;
	return strcasecmp((*reca)->key, (*recb)->key);
}

void * record_getnext(void *a)
{
	return ((rec_t *)a)->next;
}

void record_setnext(void *a, void *newval)
{
	((rec_t *)a)->next = (rec_t *)newval;
}

/* 50 most popular US babynames in 2006: http://www.ssa.gov/OACT/babynames/ */
char *tdata[] = {
"Jacob",
"Emily",
"Michael",
"Emma",
"Joshua",
"Madison",
"Ethan",
"Isabella",
"Matthew",
"Ava",
"Daniel",
"Abigail",
"Christopher",
"Olivia",
"Andrew",
"Hannah",
"Anthony",
"Sophia",
"William",
"Samantha",
"Joseph",
"Elizabeth",
"Alexander",
"Ashley",
"David",
"Mia",
"Ryan",
"Alexis",
"Noah",
"Sarah",
"James",
"Natalie",
"Nicholas",
"Grace",
"Tyler",
"Chloe",
"Logan",
"Alyssa",
"John",
"Brianna",
"Christian",
"Ella",
"Jonathan",
"Taylor",
"Nathan",
"Anna",
"Benjamin",
"Lauren",
"Samuel",
"Hailey",
"Dylan",
"Kayla",
"Brandon",
"Addison",
"Gabriel",
"Victoria",
"Elijah",
"Jasmine",
"Aiden",
"Savannah",
"Angel",
"Julia",
"Jose",
"Jessica",
"Zachary",
"Lily",
"Caleb",
"Sydney",
"Jack",
"Morgan",
"Jackson",
"Katherine",
"Kevin",
"Destiny",
"Gavin",
"Lillian",
"Mason",
"Alexa",
"Isaiah",
"Alexandra",
"Austin",
"Kaitlyn",
"Evan",
"Kaylee",
"Luke",
"Nevaeh",
"Aidan",
"Brooke",
"Justin",
"Makayla",
"Jordan",
"Allison",
"Robert",
"Maria",
"Isaac",
"Angelina",
"Landon",
"Rachel",
"Jayden",
"Gabriella",
NULL };

int main(int argc, char *argv[])
{
	int i;
	rec_t *head, *newrec, *tail;

	head = tail = NULL;
	for (i=0; (tdata[i]); i++) {
		char numstr[10];
		newrec = (rec_t *)calloc(1, sizeof(rec_t));
		newrec->key = strdup(tdata[i]);
		sprintf(numstr, "%d", i+1); newrec->val = strdup(numstr);

		if (tail) {
			tail->next = newrec;
			tail = newrec;
		}
		else {
			head = tail = newrec;
		}
	}

	dumplist(head);
	head = msort(head, record_compare, record_getnext, record_setnext);
	dumplist(head);

	return 0;
}
#endif

