/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                      */
/*                                                                            */
/* Generic per-RRD-file RRDEXCLUDE/RRDINCLUDE trending filter (issue #244).   */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __RRDFILTER_H__
#define __RRDFILTER_H__

/* Returns 1 when RRDEXCLUDE/RRDINCLUDE say the RRD instance "fn" (with or
 * without a trailing ".rrd") of test "testname" must not be tracked or
 * graphed. */
extern int rrd_is_filtered(char *testname, char *fn);

#endif
