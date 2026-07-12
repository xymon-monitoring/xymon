/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                      */
/*                                                                            */
/* Generic RRDEXCLUDE/RRDINCLUDE trending filter (issue #244), shared between the     */
/* RRD writer (xymond/do_rrd.c) and the graph-paging line counter             */
/* (lib/htmllog.c) so their semantics can never drift apart.                  */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __RRDFILTER_H__
#define __RRDFILTER_H__

/* Returns 1 when the RRDEXCLUDE/RRDINCLUDE environment settings say the RRD
 * instance "fn" (with or without a trailing ".rrd") of test "testname"
 * must not be tracked. */
extern int rrd_is_filtered(char *testname, char *fn);

#endif
