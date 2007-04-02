/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2007 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __MSORT_H__
#define __MSORT_H__

typedef int (msortcompare_fn_t)(void *, void *);
typedef void * (msortgetnext_fn_t)(void *);
typedef void (msortsetnext_fn_t)(void *, void *);
extern void *msort(void *head, msortcompare_fn_t comparefn, msortgetnext_fn_t getnext, msortsetnext_fn_t setnext);

#endif

