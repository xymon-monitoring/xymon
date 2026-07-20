/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Parser for self-describing metric markers in status messages. See         */
/* xymonmarkers.h for the wire format.                                       */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                          */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),      */
/* version 2. See the file "COPYING" for details.                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char xymonmarkers_rcsid[] = "$Id$";

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "libxymon.h"

/* Copy and validate a marker name: [A-Za-z0-9_-]{1,NAMELEN_MAX}, terminated
 * by a blank or end-of-line. Leading blanks are skipped - the block
 * writer tokenizes the banner and so accepts them; this parser must
 * accept exactly what the writer accepts, or a routed block stores
 * nothing / a storable block is never routed. The blank set is the
 * writer's tokenizer set: strtok(" \t") for METRICS blocks, but the
 * legacy devmon banner splits with strtok(" ") only - a tab there is
 * part of the (then unparseable) name, not a separator around it. A CR
 * counts as a terminator only at end-of-line (the writer sees
 * "name\r-->" as one invalid token). Returns a malloc'ed copy, or NULL. */
static char *marker_name(char *p, const char *blanks)
{
	char *result;
	int len = 0;

	p += strspn(p, blanks);
	len = strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
	if ((len == 0) || (len > XYMON_MARKER_NAMELEN_MAX)) return NULL;
	if (p[len] && !strchr(blanks, p[len]) && (p[len] != '\n') &&
	    !((p[len] == '\r') && ((p[len+1] == '\n') || (p[len+1] == '\0')))) return NULL;

	result = (char *)xmalloc(len + 1);
	memcpy(result, p, len); result[len] = '\0';

	return result;
}

/* Does a banner attribute word end here? Space/tab/EOL - matching the
 * block writer's strtok(" \t") tokens, where a CR disappears only as part
 * of a CRLF line ending. */
static int marker_attr_end(const char *q)
{
	return ((*q == '\0') || (*q == ' ') || (*q == '\t') || (*q == '\n') ||
		((*q == '\r') && ((q[1] == '\n') || (q[1] == '\0'))));
}

/* strstr bounded to the current line: statuses run to hundreds of KB, and
 * an unbounded search from every line would make parsing quadratic. */
static char *line_strstr(char *bol, char *eoln, const char *needle)
{
	size_t nlen = strlen(needle);
	char *end = (eoln ? eoln : bol + strlen(bol));
	char *p;

	if ((size_t)(end - bol) < nlen) return NULL;
	for (p = bol; (p <= end - nlen); p++) {
		if ((*p == *needle) && (strncmp(p, needle, nlen) == 0)) return p;
	}
	return NULL;
}

static xymonmarker_t *find_or_add(xymonmarker_t **head, xymonmarker_t **tail, int *count, char *name)
{
	xymonmarker_t *walk;

	for (walk = *head; (walk && strcmp(walk->name, name)); walk = walk->next) ;
	if (walk) { xfree(name); return walk; }

	if (*count >= XYMON_MARKER_MAX) { xfree(name); return NULL; }

	walk = (xymonmarker_t *)xcalloc(1, sizeof(xymonmarker_t));
	walk->name = name;
	walk->instancespec = -1;
	if (*tail) (*tail)->next = walk; else *head = walk;
	*tail = walk;
	(*count)++;

	return walk;
}

xymonmarker_t *xymon_markers_parse(char *msg)
{
	xymonmarker_t *head = NULL, *tail = NULL;
	int count = 0;
	char *bol, *eoln;
	xymonmarker_t *block = NULL;	/* non-NULL while inside a METRICS/DEVMON block */
	int block_metrics = 0;		/* current block opened by METRICS, not the legacy banner */
	int blockds = 0;		/* DS specs declared by the current block's first DS line */

	if (!msg) return NULL;

	for (bol = msg; (bol && *bol); bol = (eoln ? eoln+1 : NULL)) {
		char *close;
		int selfclosed;

		eoln = strchr(bol, '\n');
		/* A banner carrying its own "-->" is an empty, self-closed block. */
		close = line_strstr(bol, eoln, "-->");
		selfclosed = (close != NULL);

		/* Marker banners are recognized even inside an open block, like
		 * the block writer does - a new banner simply starts the next
		 * block. Everything else on block lines is content. */
		if (strncmp(bol, XYMON_METRICS_MARKER, strlen(XYMON_METRICS_MARKER)) == 0) {
			char *name = marker_name(bol + strlen(XYMON_METRICS_MARKER), " \t");
			if (name) {
				block = find_or_add(&head, &tail, &count, name);
				block_metrics = 1;
				blockds = 0;
				if (block) {
					block->store = 1;
					/* Unknown banner attributes are ignored - the
					 * dialect's generic forward compatibility. */
				}
				if (selfclosed) block = NULL;
			}
		}
		else if (strncmp(bol, DEVMON_RRD_MARKER, strlen(DEVMON_RRD_MARKER)) == 0) {
			/* Legacy devmon banner: store and show combined. The block
			 * writer accepts ANY name here and switches blocks
			 * unconditionally, so even a banner whose name this parser
			 * rejects must close the open block - or its instance
			 * lines would count into the PREVIOUS marker. */
			char *name = marker_name(bol + strlen(DEVMON_RRD_MARKER), " ");
			block = NULL;
			block_metrics = 0;
			blockds = 0;
			if (name) {
				block = find_or_add(&head, &tail, &count, name);
				if (block) { block->store = 1; block->show = 1; }
				if (selfclosed) block = NULL;
			}
		}
		else if (strncmp(bol, XYMON_GRAPH_MARKER, strlen(XYMON_GRAPH_MARKER)) == 0) {
			char *p = bol + strlen(XYMON_GRAPH_MARKER);
			char *name = marker_name(p, " \t");
			if (name) {
				xymonmarker_t *marker = find_or_add(&head, &tail, &count, name);
				if (marker) {
					marker->show = 1;

					/* Optional attributes up to end-of-line / closing
					 * marker. Same tokens as the METRICS scan above:
					 * an attribute starts after a blank and ends at
					 * one, so "note_instances=4" is no instances=
					 * and "instances=allergic" is no instances=all. */
					while (*p && (*p != '\n') && strncmp(p, "-->", 3)) {
						if ((*p == ' ') || (*p == '\t')) {
							char *a = p + 1;
							if ((strncmp(a, "instances=all", 13) == 0) && marker_attr_end(a+13)) marker->instancespec = 0;
							else if ((strncmp(a, "instances=", 10) == 0) && isdigit((unsigned char)a[10])) {
								char *e = a + 10;
								while (isdigit((unsigned char)*e)) e++;
								if (marker_attr_end(e)) marker->instancespec = atoi(a+10);
							}
						}
						p++;
					}
				}
			}
		}
		else if (block) {
			/* Inside a data block: count the lines that create RRD files.
			 * The writer only writes "instance value" lines - exactly two
			 * space-separated fields - so count precisely those, or the
			 * paging count would exceed the files that exist. */
			if (strncmp(bol, "-->", 3) == 0) {
				block = NULL;
			}
			else if (strncmp(bol, "DS:", 3) == 0) {
				/* Dataset definitions, not an instance. The DS count is
				 * how many values an instance line must carry to create
				 * a file - exactly what the writer requires. The writer
				 * resumes its scan at column [numds] on EVERY DS line,
				 * so a later DS line can extend the declaration from
				 * that position; mirror it exactly, or the two would
				 * disagree on which lines create files. */
				char *p = bol;
				char *end = (eoln ? eoln : bol + strlen(bol));
				int col = 0;

				while (p < end) {
					while ((p < end) && (*p == ' ')) p++;
					if (p >= end) break;
					if (col >= blockds) {
						/* The writer reads at most MAXCOLS (20)
						 * columns per line (do_devmon.c), so a
						 * 21st DS spec never becomes a dataset -
						 * cap identically, or an instance line
						 * carrying the 20 values the writer DOES
						 * store would not count here. */
						if ((blockds >= 20) || (strncmp(p, "DS:", 3) != 0)) break;
						blockds++;
					}
					col++;
					while ((p < end) && (*p != ' ')) p++;
				}
			}
			else {
				char *p = bol + strspn(bol, " ");
				size_t kwlen = strspn(p, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
				size_t f1;

				/* In a METRICS block, an ALL-CAPS keyword ending in ':'
				 * opens a declaration line (DS: is the known one) - never
				 * an instance, even for keywords this parser has never
				 * heard of. Same contract as the block writer. Legacy
				 * DEVMON blocks predate the contract and may carry
				 * instances named like a keyword ("CPU:1"). */
				if (block_metrics && (kwlen > 0) && (p[kwlen] == ':')) continue;
				f1 = strcspn(p, " \r\n");
				if ((f1 > 0) && (p[f1] == ' ')) {
					char *q = p + f1 + strspn(p + f1, " ");
					size_t f2 = strcspn(q, " \r\n");
					char *rest = q + f2 + strspn(q + f2, " ");
					if ((f2 > 0) && ((*rest == '\0') || (*rest == '\n') || (*rest == '\r'))) {
						/* Count only lines the writer will actually
						 * write: one non-empty colon-separated value
						 * per declared DS, and a DS line must exist. */
						int vals = 0;
						size_t vi = 0;
						while (vi < f2) {
							while ((vi < f2) && (q[vi] == ':')) vi++;
							if (vi < f2) { vals++; while ((vi < f2) && (q[vi] != ':')) vi++; }
						}
						if ((blockds > 0) && (vals >= blockds)) block->blockinstances++;
					}
				}
			}
		}
	}

	/* A block left unclosed at end-of-message is malformed: whatever was
	 * counted is the status text, not instances. Unknown count degrades
	 * to an unsliced render instead of an inflated slicing. */
	if (block) block->blockinstances = 0;

	return head;
}

void xymon_markers_free(xymonmarker_t *head)
{
	xymonmarker_t *walk, *zombie;

	for (walk = head; (walk); ) {
		zombie = walk; walk = walk->next;
		xfree(zombie->name);
		xfree(zombie);
	}
}

/*
 * The paging count used when rendering this marker's graph: an explicit
 * instances= attribute wins; else the number of instance lines in the message's
 * own METRICS block (exact by construction); else 0 = render unsliced.
 */
int xymon_marker_instancecount(xymonmarker_t *marker)
{
	if (marker->instancespec >= 0) return marker->instancespec;
	if (marker->store && (marker->blockinstances > 0)) return marker->blockinstances;
	return 0;
}

/*
 * Cheap probe used by the RRD-writer dispatch: does this message carry a
 * data block? Only line-anchored banners count.
 */
int xymon_markers_have_store(char *msg)
{
	char *p;

	if (!msg) return 0;

	for (p = msg; (p); ) {
		p = strstr(p, "<!--");
		if (!p) return 0;
		if ((p == msg) || (*(p-1) == '\n')) {
			/* Apply the writer's own name validation: a banner the
			 * writer will reject must not divert the status away from
			 * its built-in handler - that would store NOTHING, where
			 * either storing or falling back would be correct. */
			if (strncmp(p, XYMON_METRICS_MARKER, strlen(XYMON_METRICS_MARKER)) == 0) {
				char *name = marker_name(p + strlen(XYMON_METRICS_MARKER), " \t");
				if (name) { xfree(name); return 1; }
			}
			if (strncmp(p, DEVMON_RRD_MARKER, strlen(DEVMON_RRD_MARKER)) == 0) return 1;
		}
		p += 4;
	}

	return 0;
}

/* Show-side parity guard: does the message carry a legacy DEVMON banner
 * whose name the marker parser rejects? The block writer accepts ANY
 * banner name (it only maps '/' to ','), so such a block still stores
 * RRD files - but it gets no marker, and marker-driven rendering would
 * silently lose its graphs. The caller must then keep the legacy
 * service-level fallback rendering alongside the markers it did parse. */
int xymon_markers_devmon_unparsed(char *msg)
{
	char *p;

	if (!msg) return 0;

	for (p = msg; (p); ) {
		p = strstr(p, "<!--");
		if (!p) return 0;
		if ((p == msg) || (*(p-1) == '\n')) {
			if (strncmp(p, DEVMON_RRD_MARKER, strlen(DEVMON_RRD_MARKER)) == 0) {
				char *name = marker_name(p + strlen(DEVMON_RRD_MARKER), " ");
				if (!name) return 1;
				xfree(name);
			}
		}
		p += 4;
	}

	return 0;
}
