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

#ifndef __TREE_H__
#define __TREE_H__

typedef enum {
	XTREE_STATUS_OK,
	XTREE_STATUS_MEM_EXHAUSTED,
	XTREE_STATUS_DUPLICATE_KEY,
	XTREE_STATUS_KEY_NOT_FOUND
} xtreeStatus_t;

#define xtreeEnd(X) (-1)

extern void *xtreeNew(int(*xtreeCompare)(const char *a, const char *b));
extern void xtreeDestroy(void *treehandle);
extern xtreeStatus_t xtreeAdd(void *treehandle, char *key, char *userdata);
extern void *xtreeDelete(void *treehandle, char *key);
extern int xtreeFind(void *treehandle, char *key);
extern int xtreeFirst(void *treehandle);
extern int xtreeNext(void *treehandle, int pos);
extern char *xtreeKey(void *treehandle, int pos);
extern void *xtreeData(void *treehandle, int pos);

#endif

