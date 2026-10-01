/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Author: Michal Wilczynski <m.wilczynski@samsung.com>
 *
 * Freestanding <limits.h> for the vendored upstream LZ4 sources; lz4.c needs
 * UINT_MAX and lz4hc.c needs INT_MAX.
 */
#ifndef __LZ4_FREESTANDING_LIMITS_H__
#define __LZ4_FREESTANDING_LIMITS_H__

#include <linux/limits.h>

/* v4.9 has no kernel-internal <linux/limits.h>: the include above resolves
 * to the uapi header, which carries neither of the two limits the comment
 * names, and <linux/kernel.h> is too heavy for the pre-boot environment.
 * Both are spelled byte-for-byte as <linux/kernel.h> spells them, so its
 * later definition is a benign redefinition for gcc and clang alike.
 */
#ifndef UINT_MAX
#define UINT_MAX	(~0U)
#endif
#ifndef INT_MAX
#define INT_MAX		((int)(~0U>>1))
#endif

#endif
