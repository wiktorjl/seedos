/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Logo display
 *
 * Regenerate image data with: scripts/convert-image.sh
 */

#ifndef _LOGO_H
#define _LOGO_H

#include "types.h"

#define LOGO_WIDTH 604
#define LOGO_HEIGHT 219

/* Raw BGRA pixel data embedded via .incbin in boot.S */
extern uint32_t logo_data[];

/**
 * logo_display - Display the SeedOS logo on the console
 */
void logo_display(void);

#endif /* _LOGO_H */
