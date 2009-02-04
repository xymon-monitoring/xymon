/*
 * This is an implementation of a red/black search tree by
 * Thomas Niemann.
 *
 * It was found at http://www.epaperpress.com/sortsearch/rbt.html
 * where there is a general permission to use this. From
 * http://www.epaperpress.com/sortsearch/title.html:
 *
 * "Source code, when part of a software project, may be used freely
 * without reference to the author."
 *
 * I am grateful to Thomas for his permission to use this.
 *
 * Henrik Storner 2004-11-11 <henrik@storner.dk>
 */

#ifndef RBT_H
#define RBT_H

typedef enum {
    RBT_STATUS_OK,
    RBT_STATUS_MEM_EXHAUSTED,
    RBT_STATUS_DUPLICATE_KEY,
    RBT_STATUS_KEY_NOT_FOUND
} RbtStatus;

typedef void *RbtIterator;
typedef void *RbtHandle;

RbtHandle rbtNew(int(*compare)(void *a, void *b));
/*
 * create red-black tree
 * parameters:
 *     compare  pointer to function that compares keys
 *              return 0   if a == b
 *              return < 0 if a < b
 *              return > 0 if a > b
 * returns:
 *     handle   use handle in calls to rbt functions
 */


void rbtDelete(RbtHandle h);
/* destroy red-black tree */

RbtStatus rbtInsert(RbtHandle h, void *key, void *value);
/* insert key/value pair */

RbtStatus rbtErase(RbtHandle h, RbtIterator i);
/* delete node in tree associated with iterator */
/* this function does not free the key/value pointers */

RbtIterator rbtNext(RbtHandle h, RbtIterator i);
/* return ++i */

RbtIterator rbtBegin(RbtHandle h);
/* return pointer to first node */

RbtIterator rbtEnd(RbtHandle h);
/* return pointer to one past last node */

void rbtKeyValue(RbtHandle h, RbtIterator i, void **key, void **value);
/* returns key/value pair associated with iterator */

RbtIterator rbtFind(RbtHandle h, void *key);
/* returns iterator associated with key */

/* Utility functions used in Hobbit */
extern int name_compare(void *a, void *b);
extern int string_compare(void *a, void *b);
extern int int_compare(void *a, void *b);
extern void *gettreeitem(RbtHandle tree, RbtIterator handle);

#endif

