// Logo display

#include "logo.h"
#include "console.h"
#include "types.h"

extern const uint32_t logo_data[];

void logo_display(void) {
    console_draw_image(logo_data, LOGO_WIDTH, LOGO_HEIGHT, 0, 0);
    console_set_cursor(0, LOGO_HEIGHT + 8);
}
