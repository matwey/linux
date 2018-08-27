/* Module internals
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/elf.h>
#include <asm/module.h>

extern struct key *modsign_keyring;
#ifdef CONFIG_MODULE_SIG_BLACKLIST
extern struct key *modsign_blacklist;
#endif

struct load_info {
	/* pointer to module in temporary copy, freed at end of load_module() */
	struct module *mod;
	Elf_Ehdr *hdr;
	unsigned long len;
	Elf_Shdr *sechdrs;
	char *secstrings, *strtab;
	unsigned long *strmap;
	unsigned long symoffs, stroffs;
	struct _ddebug *debug;
	unsigned int num_debug;
	bool sig_ok;
	struct {
		unsigned int sym, str, mod, vers, info, pcpu, unwind;
	} index;
};

extern int mod_verify_sig(const void *mod, struct load_info *info, bool truncate_only);
