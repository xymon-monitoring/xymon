/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/server/selectcolumn-synonyms-harness.c
 *
 * Driver for selectcolumn() (lib/misc.c), used by selectcolumn-synonyms.sh.
 *
 * selectcolumn() finds a named column in a client's df header, and "wanted"
 * may list alternatives separated by "|".
 *
 * Prints "<label>=<column index>" per case; -1 means not found. The shell
 * owns the pass/fail decision.
 */

#include <stdio.h>
#include <string.h>
#include "libxymon.h"

/* nextcolumn() writes NULs into the heading, so every case gets a fresh copy. */
static void probe(const char *label, const char *heading, char *wanted)
{
	char buf[256];

	strncpy(buf, heading, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	printf("%s=%d\n", label, selectcolumn(buf, wanted));
}

int main(int argc, char **argv)
{
	/* df -P: POSIX heads the free column "Available". */
	static const char posix_hdr[] =
		"Filesystem 1024-blocks Used Available Capacity Mounted on";
	/* df -h on the same host: the free column is "Avail". */
	static const char human_hdr[] =
		"Filesystem Size Used Avail Capacity Mounted on";

	probe("plain_hit",   posix_hdr,  "Capacity");
	probe("plain_miss",  posix_hdr,  "Avail");
	probe("syn_first",   human_hdr,  "Avail|Available");
	probe("syn_second",  posix_hdr,  "Avail|Available");
	probe("syn_third",   posix_hdr,  "Frei|Libre|Available");
	probe("syn_miss",    posix_hdr,  "Frei|Libre");
	probe("case_fold",   posix_hdr,  "aVaIlAbLe");
	probe("empty_alt",   posix_hdr,  "|Available");
	/* ... and against an empty heading, which reaches the comparison as a
	   zero-length token rather than NULL. */
	probe("empty_both",  "",         "|Available");
	probe("empty_hdr",   "",         "Avail|Available");
	/* A longer heading must not match a shorter alternative. */
	probe("no_prefix",   "Filesystem Availability Capacity", "Avail|Available");
	/* ... nor a longer alternative match a shorter heading. */
	probe("no_suffix",   human_hdr,  "Available");

	return 0;
}
