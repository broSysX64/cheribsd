/*-
 * Copyright (c) 2006 Wojciech A. Koszek <wkoszek@FreeBSD.org>
 * Copyright (c) 2012-2014 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/cons.h>
#include <sys/exec.h>
#include <sys/endian.h>
#include <sys/linker.h>
#include <sys/ucontext.h>
#include <sys/proc.h>
#include <sys/kdb.h>
#include <sys/ptrace.h>
#include <sys/reboot.h>
#include <sys/signalvar.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/user.h>

#ifdef FDT
#include <contrib/libfdt/fdt.h>
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#endif

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>

#include <machine/bootinfo.h>
#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpuregs.h>
#include <machine/hwfunc.h>
#include <machine/md_var.h>
#include <machine/metadata.h>
#include <machine/pmap.h>
#include <machine/trap.h>

#define	FDT_SOURCE_NONE		0
#define	FDT_SOURCE_LOADER	1
#define	FDT_SOURCE_ROM		2
#define	FDT_SOURCE_STATIC	3

#if defined(FDT_DTB_STATIC_ONLY) && !defined(FDT_DTB_STATIC)
#define FDT_DTB_STATIC
#endif

extern int	*edata;
extern int	*end;

void
platform_cpu_init()
{
	/* Nothing special */
}

static void
mips_init(void)
{
	int i;
#ifdef FDT
	struct mem_region mr[FDT_MEM_REGIONS];
	uint64_t val;
	int mr_cnt;
	int j;
#endif

	for (i = 0; i < 10; i++) {
		phys_avail[i] = 0;
	}

	/* phys_avail regions are in bytes */
	phys_avail[0] = MIPS_KSEG0_TO_PHYS(kernel_kseg0_end);
	phys_avail[1] = ctob(realmem);

	dump_avail[0] = phys_avail[0];
	dump_avail[1] = phys_avail[1];

	physmem = realmem;

#ifdef FDT
	if (fdt_get_mem_regions(mr, &mr_cnt, &val) == 0) {

		physmem = btoc(val);

		KASSERT((phys_avail[0] >= mr[0].mr_start) && \
			(phys_avail[0] < (mr[0].mr_start + mr[0].mr_size)),
			("First region is not within FDT memory range"));

		/* Limit size of the first region */
		phys_avail[1] = (mr[0].mr_start + MIN(mr[0].mr_size, ctob(realmem)));
		dump_avail[1] = phys_avail[1];

		/* Add the rest of regions */
		for (i = 1, j = 2; i < mr_cnt; i++, j+=2) {
			phys_avail[j] = mr[i].mr_start;
			phys_avail[j+1] = (mr[i].mr_start + mr[i].mr_size);
			dump_avail[j] = phys_avail[j];
			dump_avail[j+1] = phys_avail[j+1];
		}
	}
#endif

	init_param1();
	init_param2(physmem);
	mips_cpu_init();
	pmap_bootstrap();
	mips_proc0_init();
	mutex_init();
	kdb_init();
#ifdef KDB
	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif
}

/*
 * Perform a board-level soft-reset.
 */
void
platform_reset(void)
{

	/* XXX SMP will likely require us to do more. */
	__asm__ __volatile__(
		"mfc0 $k0, $12\n\t"
		"li $k1, 0x00100000\n\t"
		"or $k0, $k0, $k1\n\t"
		"mtc0 $k0, $12\n");
	for( ; ; )
		__asm__ __volatile("wait");
}

#ifdef FDT
/* Parse cmd line args as env - copied from xlp_machdep. */
/* XXX-BZ this should really be centrally provided for all (boot) code. */
static void
_parse_bootargs(char *cmdline)
{
	char *n, *v;

	while ((v = strsep(&cmdline, " \n")) != NULL) {
		if (*v == '\0')
			continue;
		if (*v == '-') {
			while (*v != '\0') {
				v++;
				switch (*v) {
				case 'a': boothowto |= RB_ASKNAME; break;
				/* Someone should simulate that ;-) */
				case 'C': boothowto |= RB_CDROM; break;
				case 'd': boothowto |= RB_KDB; break;
				case 'D': boothowto |= RB_MULTIPLE; break;
				case 'm': boothowto |= RB_MUTE; break;
				case 'g': boothowto |= RB_GDB; break;
				case 'h': boothowto |= RB_SERIAL; break;
				case 'p': boothowto |= RB_PAUSE; break;
				case 'r': boothowto |= RB_DFLTROOT; break;
				case 's': boothowto |= RB_SINGLE; break;
				case 'v': boothowto |= RB_VERBOSE; break;
				}
			}
		} else {
			n = strsep(&v, "=");
			if (v == NULL)
				kern_setenv(n, "1");
			else
				kern_setenv(n, v);
		}
	}
}
#endif

void
platform_start(__register_t a0, __register_t a1,  __register_t a2, 
    __register_t a3)
{
	struct bootinfo *bootinfop;
	vm_offset_t kernend;
	uint64_t platform_counter_freq;
	int argc = a0;
	char **argv = (char **)a1;
	char **envp = (char **)a2;
	long memsize;
#ifdef FDT
	char buf[2048];		/* early stack supposedly big enough */
	vm_offset_t dtbp = 0;
	phandle_t chosen;
	void *kmdp;
	int dtb_needs_swap = 0; /* error */
	size_t dtb_size = 0;
#ifndef FDT_DTB_STATIC_ONLY
	struct fdt_header *dtb_rom, *dtb;
	uint32_t *swapptr;
#endif
	int fdt_source = FDT_SOURCE_NONE;
#endif
	int i;

	/* clear the BSS and SBSS segments */
	kernend = (vm_offset_t)&end;
	memset(&edata, 0, kernend - (vm_offset_t)(&edata));

	mips_postboot_fixup();

	mips_pcpu0_init();

	/*
	 * Over time, we've changed out boot-time binary interface for the
	 * kernel.  Miniboot simply provides a 'memsize' in a3, whereas the
	 * FreeBSD boot loader provides a 'bootinfo *' in a3.  While slightly
	 * grody, we support both here by detecting 'pointer-like' values in
	 * a3 and assuming physical memory can never be that big.
	 *
	 * XXXRW: Pull more values than memsize out of bootinfop -- e.g.,
	 * module information.
	 */
	if (a3 >= 0x9800000000000000ULL) {
		bootinfop = (void *)a3;
		memsize = bootinfop->bi_memsize;
		preload_metadata = (caddr_t)bootinfop->bi_modulep;
	} else {
		bootinfop = NULL;
		memsize = a3;
	}

	kmdp = preload_search_by_type("elf kernel");
	/*
	 * Configure more boot-time parameters passed in by loader.
	 */
	boothowto = MD_FETCH(kmdp, MODINFOMD_HOWTO, int);
	init_static_kenv(MD_FETCH(kmdp, MODINFOMD_ENVP, char *), 0);


#ifdef FDT
#ifndef FDT_DTB_STATIC_ONLY
	/*
	 * Find the dtb passed in by the boot loader (currently fictional).
	 *
	 * Prefer a dtb provided as a module to one from bootinfo as we may
	 * have loaded an alternative one or created a modified version.
	 */
	dtbp = MD_FETCH(kmdp, MODINFOMD_DTBP, vm_offset_t);
	if (dtbp == (vm_offset_t)NULL &&
	    bootinfop != NULL && bootinfop->bi_dtb != (bi_ptr_t)NULL) {
		dtbp = bootinfop->bi_dtb;
		fdt_source = FDT_SOURCE_LOADER;
	}

	/* Try to find an FDT directly in the hardware */
	if (dtbp == (vm_offset_t)NULL) {
		dtb_rom = (void*)(intptr_t)0x900000007f010000;
		if (dtb_rom->magic == FDT_MAGIC) {
			dtb_needs_swap = 0;
			dtb_size = dtb_rom->totalsize;
		} else if (dtb_rom->magic == bswap32(FDT_MAGIC)) {
			dtb_needs_swap = 1;
			dtb_size = bswap32(dtb_rom->totalsize);
		}
		if (dtb_size != 0) {
			/* Steal a bit of memory... */
			dtb = (void *)kernel_kseg0_end;
			/* Round alignment from linker script. */
			kernel_kseg0_end += roundup2(dtb_size, 64 / 8);
			memcpy(dtb, dtb_rom, dtb_size);
			if (dtb_needs_swap)
				for (swapptr = (uint32_t *)dtb;
				    swapptr < (uint32_t *)dtb + (dtb_size/sizeof(*dtb));
				    swapptr++)
					*swapptr = bswap32(*swapptr);
			dtbp = (vm_offset_t)dtb;
			fdt_source = FDT_SOURCE_ROM;
		}
	}
#endif /* !FDT_DTB_STATIC_ONLY */

#if defined(FDT_DTB_STATIC)
	/*
	 * In case the device tree blob was not retrieved (from metadata) try
	 * to use the statically embedded one.
	 */
	if (dtbp == (vm_offset_t)NULL) {
		dtbp = (vm_offset_t)&fdt_static_dtb;
		fdt_source = FDT_SOURCE_STATIC;
	}
#endif

	if (OF_install(OFW_FDT, 0) == FALSE)
		while (1);
	if (OF_init((void *)dtbp) != 0)
		while (1);

	/*
	 * Get bootargs from FDT if specified.
	 */
	chosen = OF_finddevice("/chosen");
	if (OF_getprop(chosen, "bootargs", buf, sizeof(buf)) != -1)
		_parse_bootargs(buf);
#endif

	/*
	 * XXXRW: We have no way to compare wallclock time to cycle rate on
	 * BERI, so for now assume we run at the MALTA default (100MHz).
	 */
	platform_counter_freq = MIPS_DEFAULT_HZ;
	mips_timer_early_init(platform_counter_freq);

	cninit();
	printf("entry: platform_start()\n");

#ifdef FDT
	if (dtbp != (vm_offset_t)NULL) {
		printf("Using FDT at %p from ", (void *)dtbp);
		switch (fdt_source) {
		case FDT_SOURCE_LOADER:
			printf("loader");
			break;
		case FDT_SOURCE_ROM:
			printf("ROM");
			break;
		case FDT_SOURCE_STATIC:
			printf("kernel");
			break;
		default:
			printf("unknown source %d", fdt_source);
			break;
		}
		printf("\n");
	}
	if (dtb_size != 0 && dtb_needs_swap)
		printf("FDT was byteswapped\n");
#endif

	bootverbose = 1;
	if (bootverbose) {
		printf("cmd line: ");
		for (i = 0; i < argc; i++)
			printf("%s ", argv[i]);
		printf("\n");

		printf("envp:\n");
		for (i = 0; envp[i]; i += 2)
			printf("\t%s = %s\n", envp[i], envp[i+1]);

		if (bootinfop != NULL)
			printf("bootinfo found at %p\n", bootinfop);

		printf("memsize = %p\n", (void *)memsize);
	}

	realmem = btoc(memsize);
	mips_init();

	mips_timer_init_params(platform_counter_freq, 0);
}
