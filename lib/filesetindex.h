/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* The writer-kept fileset index: one small per-host file recording every    */
/* RRD file the writer maintains, with its last data-write timestamp.        */
/* Bookkeeping happens at event time in xymond_rrd (the single creator of    */
/* RRD files); renderers read the one file instead of re-counting status     */
/* lines or scanning directories. The record format is extensible: units     */
/* and threshold relations ride the same entries.                            */
/*                                                                            */
/* File: $XYMONRRDS/<host>/.fileset-index, "<rrdfn> <ts> [k=v ...]" lines.   */
/* Readers must ignore fields beyond the first two.                          */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                          */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),      */
/* version 2. See the file "COPYING" for details.                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __FILESETINDEX_H__
#define __FILESETINDEX_H__

#include <stdio.h>
#include <time.h>
#include <limits.h>

/* Schema specs are wire-fed; longer than this and a reader's line buffer
 * would truncate them mid-token on the way back in. Reject loudly. */
#define FSIDX_SPECMAX 1024
/* Worst-case record: filename (PATH_MAX) + timestamp + generation + four
 * capped specs + field prefixes and separators. The 16384 reserve keeps
 * the bound compatible with pre-existing index files whose records could
 * carry a retired b= baseline up to MAX_LINE_LEN (stackio.h). Every
 * reader's line buffer must hold this, or a long record splits and its
 * tail parses as bogus extra records. */
#define FSIDX_LINEMAX (PATH_MAX + 4*(FSIDX_SPECMAX + 8) + 16384 + 128)

/* Writer side (xymond_rrd). Event time and commit time are split: schema
 * declarations (and a new entry's existence) are noted when the sample is
 * processed; the freshness timestamp advances only after rrdtool ACCEPTS
 * the update, so rejected updates never look fresh. Hostnames reach this
 * API raw off the channel; every call that builds a path from one rejects
 * hostnames containing '/' and the literal "."/".." (they would escape
 * the RRD tree), and rrdfns that cannot survive the record format. */
extern void fsidx_note_schema(char *rrddir, char *hostname, char *rrdfn, time_t ts);
extern void fsidx_note_commit(char *rrddir, char *hostname, char *rrdfn, time_t ts);
extern void fsidx_set_units(char *unitspec);	/* sticky "ds:unit[,...]" for following writes; NULL clears */
extern void fsidx_set_thresholds(char *thrspec);	/* sticky "base:relop-operand:sev[,...]"; NULL clears */
extern void fsidx_set_dsnames(char *dsnspec);	/* sticky "ds1,ds2" positional names; NULL clears */
extern void fsidx_set_heartbeats(char *hbspec);	/* sticky "ds:heartbeat[,...]"; NULL clears */
extern int fsidx_pending_min_heartbeat(void);	/* smallest pending declared heartbeat; 0 = none */
/* Every loaded entry; cb may be NULL to only probe. Returns -1 when the
 * host is not loaded (no knowledge - distinct from zero entries), else
 * the entry count. */
extern int fsidx_entry_foreach(char *hostname, void (*cb)(const char *, time_t, const char *, void *), void *userdata);
extern void fsidx_flush(char *rrddir, char *hostname);
extern void fsidx_flush_now(char *rrddir, char *hostname);	/* the file must be current NOW (rename, drop) */
extern void fsidx_flush_all(char *rrddir);
extern void fsidx_drop(char *rrddir, char *hostname);

/* Reader side (CGIs, htmllog): number of fresh index entries whose filename
 * is "<prefix>.<instance>.rrd", or -1 when the host has no readable index
 * (callers keep their previous behaviour). maxage 0 = no freshness cut. */
extern int fsidx_count_prefix(char *hostname, char *prefix, time_t maxage, void *exemptpat);

/* Same, but entries matched by a compiled regex (a gdef's FNPATTERN).
 * `pattern` is a pcre2_code* passed as void* to keep this header free of
 * the PCRE include-order dance. */
extern int fsidx_count_pattern(char *hostname, void *pattern, time_t maxage, void *exemptpat);
/* exemptpat (optional, pcre2_code*): entries it matches count even
 * beyond maxage - the EXSTALEPATTERN exemption, kept identical between
 * the counters and the renderer's filter. */

/* Overlong-line guard for ANY index reader: an oversized physical line
 * splits at fgets and its tail can parse as a plausible record. Returns
 * 1 (and discards the rest of the line) when the chunk was incomplete. */
extern int fsidx_line_truncated(char *line, FILE *fd);

/* The per-DS units recorded for one file: malloc'd "ds:unit[,...]" spec,
 * or NULL (no index, or no units declared). Caller frees. */
extern char *fsidx_units(char *hostname, char *rrdfn);

/* The threshold relations recorded for one file: malloc'd
 * "base:relop-operand:sev[,...]" spec, or NULL. Caller frees. */
extern char *fsidx_thresholds(char *hostname, char *rrdfn);

#endif
