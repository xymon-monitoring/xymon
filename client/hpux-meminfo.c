/*
 * From the www.deadcat.net hpux-bb-memory.tar.gz submission.
 *
 * Released under the GNU GPL according to deadcat; no
 * author or license information is in the archive.
 * If you wish to claim ownership of this code, please
 * contact Henrik Storner <henrik-bb@hswn.dk>
 */

#include <sys/pstat.h>
#include <stdio.h>

main(int argc, char *argv[])
{
	struct pst_static sbuf;
	struct pst_dynamic dbuf;
	unsigned long pgsizekb;
	unsigned long kpages;

	pstat_getstatic(&sbuf, sizeof(sbuf), 1, 0);
	pstat_getdynamic(&dbuf, sizeof(sbuf), 1, 0);
	pgsizekb = sbuf.page_size / 1024;
	kpages = dbuf.psd_free / 1024;

	printf("Total:%ld\n", sbuf.physical_memory/256);
	printf("Free:%lu\n", pgsizekb*kpages);
}

