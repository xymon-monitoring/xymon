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
	XTREE_STATUS_KEY_NOT_FOUND,
	XTREE_STATUS_NOTREE
} xtreeStatus_t;

#ifdef HAVE_BINARY_TREE
typedef struct treerec_t {
	char *key;
	void *userdata;
	int (*compare)(const char *a, const char *b);
	struct treerec_t *link;
} treerec_t;

typedef struct xtree_t {
	void *root;
	int (*compare)(const char *a, const char *b);
} xtree_t;
#endif

#ifdef HAVE_BINARY_TREE
#define xtreeEnd(X) (NULL)
typedef void *xtreePos_t;
#else
#define xtreeEnd(X) (-1)
typedef int xtreePos_t;
#endif

extern void *xtreeNew(int(*xtreeCompare)(const char *a, const char *b));
extern void xtreeDestroy(void *treehandle);
extern xtreeStatus_t xtreeAdd(void *treehandle, char *key, void *userdata);
extern void *xtreeDelete(void *treehandle, char *key);
extern xtreePos_t xtreeFind(void *treehandle, char *key);
extern xtreePos_t xtreeFirst(void *treehandle);
extern xtreePos_t xtreeNext(void *treehandle, xtreePos_t pos);
extern char *xtreeKey(void *treehandle, xtreePos_t pos);
extern void *xtreeData(void *treehandle, xtreePos_t pos);

#endif

