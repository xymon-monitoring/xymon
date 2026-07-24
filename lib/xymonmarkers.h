/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Parser for self-describing metric markers carried in status messages:     */
/*                                                                            */
/*   <!--XYMON METRICS[: <name>]        store: RRD block, one value line;    */
/*                                      name optional, defaults to the test  */
/*   DS:<ds>:<type>:<hb>:<min>:<max>    per instance, parsed by the devmon   */
/*   <instance> <v1>[:<v2>...]          block writer (do_devmon.c)           */
/*   -->                                                                     */
/*                                                                            */
/*   <!--XYMON GRAPH[: <name>] [instances=<N>|instances=all] -->  (name optional) */
/*                                      show: render gdef <name> on this     */
/*                                      test's page                          */
/*                                                                            */
/*   <!--DEVMON RRD: <name> ...        legacy alias (devmon SNMP collector): */
/*   ...                          -->   store and show combined              */
/*                                                                            */
/* Markers are recognized at line start only. Graph paging counts are        */
/* resolved per graph: an explicit instances= wins, else the instance-line       */
/* count of the same-named METRICS block in the same message, else 0         */
/* (render unsliced).                                                        */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                          */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),      */
/* version 2. See the file "COPYING" for details.                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __XYMONMARKERS_H__
#define __XYMONMARKERS_H__

#define XYMON_METRICS_MARKER "<!--XYMON METRICS"
#define XYMON_GRAPH_MARKER   "<!--XYMON GRAPH"
#define DEVMON_RRD_MARKER    "<!--DEVMON RRD: "

/*
 * Marker names become gdef lookup keys, RRD file-name prefixes and URL
 * parameters, so they are restricted to [A-Za-z0-9_-] and bounded in
 * size and count. Lines that fail validation are ignored silently.
 */
#define XYMON_MARKER_NAMELEN_MAX 64
#define XYMON_MARKER_MAX         256

typedef struct xymonmarker_t {
	char *name;
	int store;		/* METRICS (or DEVMON RRD) block present */
	int show;		/* GRAPH marker (or DEVMON RRD) present */
	int blockinstances;		/* instance lines in the METRICS block(s) */
	int instancespec;		/* instances= attribute: N, 0 = all, -1 = absent */
	struct xymonmarker_t *next;
} xymonmarker_t;

/* Recognize the (optionally-named) XYMON METRICS marker at line start `bol`.
 * Returns 2 = named block (*name = malloc'd copy, caller owns it);
 *         1 = unnamed block (*name = NULL; caller supplies the test name);
 *         0 = not a METRICS marker (*name = NULL). */
extern int xymon_metrics_marker(char *bol, char **name);
extern xymonmarker_t *xymon_markers_parse(char *msg, const char *defname);
extern void xymon_markers_free(xymonmarker_t *head);
extern int xymon_marker_instancecount(xymonmarker_t *marker);
extern int xymon_markers_have_store(char *msg);
extern int xymon_markers_devmon_unparsed(char *msg);

#endif
