/*----------------------------------------------------------------------------*/
/* Hobbit memory information tool for NetBSD.                                 */
/* This tool retrieves information about the total and free RAM.              */
/*                                                                            */
/* Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: netbsd-meminfo.c,v 1.3 2006-05-03 21:12:33 henrik Exp $";

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/swap.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
	int hw_physmem[] = { CTL_HW, HW_PHYSMEM };
	int physmem;

	int hw_pagesize[] = { CTL_HW, HW_PAGESIZE };
	int pagesize;

	int vm_vmtotal[] = { CTL_VM, VM_METER };
	struct vmtotal vmdata;

	size_t len;
	int result;
	int swapcount, i;
	struct swapent *swaplist, *s;
	unsigned long swaptotal, swapused;

	len = sizeof(physmem);
	result = sysctl(hw_physmem, sizeof(hw_physmem) / sizeof(*hw_physmem), &physmem, &len, NULL, 0);
	if (result != 0) return 1;

	len = sizeof(pagesize);
	result = sysctl(hw_pagesize, sizeof(hw_pagesize) / sizeof(*hw_pagesize), &pagesize, &len, NULL, 0);
	if (result != 0) return 1;

	len = sizeof(vmdata);
	result = sysctl(vm_vmtotal, sizeof(vm_vmtotal) / sizeof(*vm_vmtotal), &vmdata, &len, NULL, 0);

	/* Get swap statistics */
	swapcount = swapctl(SWAP_NSWAP, NULL, 0);
	swaplist = (struct swapent *)malloc(swapcount * sizeof(struct swapent));
	result = swapctl(SWAP_STATS, swaplist, swapcount);
	s = swaplist; swaptotal = swapused = 0;
	for (i = 0, s = swaplist; (i < swapcount); i++, s++) {
		if (s->se_flags & SWF_ENABLE) {
			swaptotal += s->se_nblks;
			swapused  += s->se_inuse;
		}
	}
	free(swaplist);
	/* swap stats are in disk blocks of 512 bytes, so divide by 2 for KB and 1024 for MB */
	swaptotal /= (2*1024);
	swapused /= (2*1024);

	// printf("Pagesize:%d\n", pagesize);
	printf("Total:%d\n", (physmem / (1024 * 1024)));
	printf("Free:%d\n", (pagesize / 1024)*(vmdata.t_free / 1024));
	printf("Swaptotal:%lu\n", swaptotal);
	printf("Swapused:%lu\n", swapused);

	return 0;
}

