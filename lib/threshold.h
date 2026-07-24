/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* threshold_eval: the shared "current value vs declared threshold -> a       */
/* severity" engine. One place turns a metric value + a THRESHOLD spec into   */
/* ok/warn/crit, so the status table, the graph annotations and (RFC #218)    */
/* the alert path all agree on what a declared threshold means.               */
/* See lib/threshold.c.                                                       */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                           */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __THRESHOLD_H__
#define __THRESHOLD_H__

#define THRESHOLD_OK   0
#define THRESHOLD_WARN 1
#define THRESHOLD_CRIT 2

/* Evaluate the current `value` of DS `dsname` against a threshold spec
 * "<ds>:<relop><operand>[:<sev>][,...]" - the XYMON METRICS THRESHOLD
 * declaration, and the fileset-index t= field, share this format. `relop` is
 * one of > < >= <= ; `operand` is a number or another declared DS name;
 * `sev` is warn|crit (crit when omitted). Only rules whose base DS equals
 * `dsname` are considered. A DS-name operand is resolved by
 * getval(operand, &out, ud) (return non-zero on success); pass NULL to make
 * DS-vs-DS rules not fire. Returns the worst severity that fires
 * (THRESHOLD_OK if none). Malformed rules are skipped, never fatal. */
extern int threshold_eval(const char *thrspec, const char *dsname, double value,
			  int (*getval)(const char *ds, double *out, void *ud), void *ud);

#endif
