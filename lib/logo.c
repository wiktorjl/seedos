// SPDX-License-Identifier: GPL-2.0-only
/*
 * Logo display
 */

#include "logo.h"
#include "console.h"
#include "types.h"

/**
 * logo_display - Display the SeedOS logo on the console
 */
void logo_display(void)
{
	console_draw_image(logo_data, LOGO_WIDTH, LOGO_HEIGHT, 0, 0);
	console_set_cursor(0, LOGO_HEIGHT + 8);
}
