/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 03, 04 Ralf Baechle
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_MACH_TANGO3_SPACES_H
#define _ASM_MACH_TANGO3_SPACES_H

#define PHYS_OFFSET	_AC(0x04000000, UL)
#define UNCAC_BASE	_AC(0xa4000000, UL)
#define IO_BASE		UNCAC_BASE

#include <asm/mach-generic/spaces.h>

#endif /* _ASM_MACH_TANGO3_SPACES_H */
