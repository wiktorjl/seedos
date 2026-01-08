// SPDX-License-Identifier: GPL-2.0-only
/*
 * Console output driver
 *
 * Text and graphics rendering to framebuffer with scrollback history.
 * Uses 8x16 bitmap font with automatic line wrapping and scrolling.
 */

#include "console.h"
#include "font.h"

#define SCROLLBACK_LINES	1000
#define MAX_COLS		160

#define CURSOR_BLINK_TICKS	50
#define CURSOR_COLOR		0xCCCCCC

typedef struct {
	char ch;
	uint32_t color;
} console_cell_t;

typedef struct {
	console_cell_t cells[MAX_COLS];
	int length;
} console_line_t;

static console_line_t scrollback[SCROLLBACK_LINES];
static int scrollback_head;
static int scrollback_count;
static int view_offset;
static int current_col;

static struct limine_framebuffer *framebuffer;
static int screen_rows, screen_cols;

static int cursor_visible;
static int cursor_enabled;
static uint64_t cursor_last_toggle;
static int cursor_draw_x, cursor_draw_y;

static int fullscreen_mode;

static void scrollback_init(void)
{
	scrollback_head = 0;
	scrollback_count = 1;
	view_offset = 0;
	current_col = 0;
	scrollback[0].length = 0;
}

static void scrollback_newline(void)
{
	scrollback_head = (scrollback_head + 1) % SCROLLBACK_LINES;
	if (scrollback_count < SCROLLBACK_LINES)
		scrollback_count++;
	scrollback[scrollback_head].length = 0;
	current_col = 0;
}

static void scrollback_putchar(char c, uint32_t color)
{
	if (current_col < MAX_COLS) {
		console_line_t *line = &scrollback[scrollback_head];
		line->cells[current_col].ch = c;
		line->cells[current_col].color = color;
		current_col++;
		line->length = current_col;
	}
}

static console_line_t *scrollback_get_line(int lines_from_bottom)
{
	int idx;

	if (lines_from_bottom >= scrollback_count)
		return NULL;
	idx = (scrollback_head - lines_from_bottom + SCROLLBACK_LINES) % SCROLLBACK_LINES;
	return &scrollback[idx];
}

static void get_cursor_position(int *x, int *y)
{
	int cursor_row = scrollback_count - 1;

	if (cursor_row >= screen_rows)
		cursor_row = screen_rows - 1;
	*y = cursor_row * 16;
	*x = current_col * 8;
}

static void render_view(void)
{
	int row;

	if (!framebuffer)
		return;

	console_fill_rect(0, 0, framebuffer->width, framebuffer->height, 0x000000);

	for (row = 0; row < screen_rows; row++) {
		int lines_from_bottom = view_offset + (screen_rows - 1 - row);
		console_line_t *line = scrollback_get_line(lines_from_bottom);
		int y, col;

		if (!line)
			continue;

		y = row * 16;
		for (col = 0; col < line->length && col < screen_cols; col++) {
			int x = col * 8;
			console_draw_char(line->cells[col].ch, x, y, line->cells[col].color);
		}
	}
}

static void draw_cursor(void)
{
	int x, y;

	if (!framebuffer || view_offset > 0)
		return;

	get_cursor_position(&x, &y);

	if (x < 0 || y < 0 || x >= (int)framebuffer->width || y + 16 > (int)framebuffer->height)
		return;

	console_fill_rect(x, y + 14, 8, 2, CURSOR_COLOR);
	cursor_draw_x = x;
	cursor_draw_y = y;
}

static void erase_cursor(void)
{
	if (!framebuffer)
		return;

	if (cursor_draw_x < 0 || cursor_draw_y < 0 ||
	    cursor_draw_x >= (int)framebuffer->width ||
	    cursor_draw_y + 16 > (int)framebuffer->height)
		return;

	console_fill_rect(cursor_draw_x, cursor_draw_y + 14, 8, 2, 0x000000);
}

/**
 * console_init - Initialize the console with a framebuffer
 * @fb: pointer to Limine framebuffer structure
 */
void console_init(struct limine_framebuffer *fb)
{
	framebuffer = fb;
	cursor_visible = 0;
	cursor_enabled = 1;
	cursor_last_toggle = 0;
	cursor_draw_x = 0;
	cursor_draw_y = 0;

	if (fb) {
		screen_cols = fb->width / 8;
		screen_rows = fb->height / 16;
	} else {
		screen_cols = 80;
		screen_rows = 25;
	}

	scrollback_init();
}

/**
 * console_get_cursor - Get current cursor position in pixels
 * @x: output X coordinate
 * @y: output Y coordinate
 */
void console_get_cursor(int *x, int *y)
{
	get_cursor_position(x, y);
}

/**
 * console_set_cursor - Set cursor position in pixels
 * @x: X coordinate
 * @y: Y coordinate
 */
void console_set_cursor(int x, int y)
{
	int target_row;

	current_col = x / 8;
	if (current_col >= MAX_COLS)
		current_col = MAX_COLS - 1;

	target_row = y / 16;
	while (scrollback_count <= target_row && scrollback_count < SCROLLBACK_LINES)
		scrollback_newline();
}

/**
 * console_get_dimensions - Get screen size in character cells
 * @cols: output column count
 * @rows: output row count
 */
void console_get_dimensions(int *cols, int *rows)
{
	if (cols)
		*cols = screen_cols;
	if (rows)
		*rows = screen_rows;
}

/**
 * console_fill_rect - Fill a rectangle with a solid color
 * @x: X coordinate of top-left corner
 * @y: Y coordinate of top-left corner
 * @w: width in pixels
 * @h: height in pixels
 * @color: 32-bit RGB fill color
 */
void console_fill_rect(int x, int y, int w, int h, uint32_t color)
{
	uint32_t *fb;
	uint64_t pitch;
	int fb_w, fb_h;
	int row, col;

	if (!framebuffer)
		return;

	fb = (uint32_t *)framebuffer->address;
	pitch = framebuffer->pitch / 4;
	fb_w = framebuffer->width;
	fb_h = framebuffer->height;

	for (row = 0; row < h && y + row < fb_h; row++) {
		for (col = 0; col < w && x + col < fb_w; col++)
			fb[(y + row) * pitch + (x + col)] = color;
	}
}

/**
 * console_clear - Clear the entire screen
 * @color: fill color
 */
void console_clear(uint32_t color)
{
	if (!framebuffer)
		return;
	console_fill_rect(0, 0, framebuffer->width, framebuffer->height, color);
	scrollback_init();
}

/**
 * console_putchar - Write a character at the cursor position
 * @c: character to write
 * @color: 32-bit RGB color
 */
void console_putchar(char c, uint32_t color)
{
	int screen_row;

	if (!framebuffer)
		return;
	if (fullscreen_mode)
		return;

	if (view_offset > 0) {
		view_offset = 0;
		render_view();
	}

	screen_row = scrollback_count - 1;
	if (screen_row >= screen_rows)
		screen_row = screen_rows - 1;

	if (c == '\n') {
		scrollback_newline();
		if (scrollback_count > screen_rows)
			render_view();
	} else if (c == '\r') {
		current_col = 0;
	} else if (c == '\b') {
		if (current_col > 0) {
			current_col--;
			scrollback[scrollback_head].length = current_col;
			console_fill_rect(current_col * 8, screen_row * 16, 8, 16, 0x000000);
		}
	} else if (c == '\t') {
		int old_col = current_col;
		int i;

		for (i = 0; i < 4 && current_col < MAX_COLS; i++)
			scrollback_putchar(' ', color);
		for (i = old_col; i < current_col; i++)
			console_draw_char(' ', i * 8, screen_row * 16, color);
	} else {
		int draw_col = current_col;
		scrollback_putchar(c, color);
		console_draw_char(c, draw_col * 8, screen_row * 16, color);
	}

	if (current_col >= screen_cols) {
		scrollback_newline();
		if (scrollback_count > screen_rows)
			render_view();
	}
}

/**
 * console_puts - Write a null-terminated string
 * @str: string to write
 * @color: 32-bit RGB color
 */
void console_puts(const char *str, uint32_t color)
{
	while (*str)
		console_putchar(*str++, color);
}

/**
 * console_draw_char - Draw a character at absolute position
 * @c: character to draw
 * @x: X coordinate in pixels
 * @y: Y coordinate in pixels
 * @color: 32-bit RGB color
 */
void console_draw_char(char c, int x, int y, uint32_t color)
{
	const uint8_t *glyph;
	uint32_t *fb;
	uint64_t pitch;
	int fb_w, fb_h;
	int max_row, max_col;
	int row, col;

	if (!framebuffer)
		return;

	fb_w = framebuffer->width;
	fb_h = framebuffer->height;

	if (x < 0 || y < 0 || x >= fb_w || y >= fb_h)
		return;

	glyph = font_data + (unsigned char)c * 16;
	fb = (uint32_t *)framebuffer->address;
	pitch = framebuffer->pitch / 4;

	max_row = (y + 16 > fb_h) ? fb_h - y : 16;
	max_col = (x + 8 > fb_w) ? fb_w - x : 8;

	for (row = 0; row < max_row; row++) {
		uint8_t bits = glyph[row];
		for (col = 0; col < max_col; col++) {
			if (bits & (0x80 >> col))
				fb[(y + row) * pitch + (x + col)] = color;
			else
				fb[(y + row) * pitch + (x + col)] = 0x000000;
		}
	}
}

/**
 * console_draw_string - Draw a string at absolute position
 * @str: string to draw
 * @x: X coordinate in pixels
 * @y: Y coordinate in pixels
 * @color: 32-bit RGB color
 */
void console_draw_string(const char *str, int x, int y, uint32_t color)
{
	while (*str) {
		console_draw_char(*str++, x, y, color);
		x += 8;
	}
}

/**
 * console_draw_image - Draw a raw pixel buffer to the screen
 * @pixels: array of 32-bit RGB pixels
 * @img_w: image width in pixels
 * @img_h: image height in pixels
 * @x: X coordinate for top-left corner
 * @y: Y coordinate for top-left corner
 */
void console_draw_image(const uint32_t *pixels, int img_w, int img_h, int x, int y)
{
	uint32_t *fb;
	uint64_t pitch;
	int fb_w, fb_h;
	int row, col;

	if (!framebuffer)
		return;

	fb = (uint32_t *)framebuffer->address;
	pitch = framebuffer->pitch / 4;
	fb_w = framebuffer->width;
	fb_h = framebuffer->height;

	for (row = 0; row < img_h && y + row < fb_h; row++) {
		for (col = 0; col < img_w && x + col < fb_w; col++)
			fb[(y + row) * pitch + (x + col)] = pixels[row * img_w + col];
	}
}

/**
 * console_scroll_back - Scroll back into history
 * @lines: number of lines to scroll back
 */
void console_scroll_back(int lines)
{
	int max_offset;

	if (!framebuffer || lines <= 0)
		return;

	max_offset = scrollback_count > screen_rows ? scrollback_count - screen_rows : 0;

	view_offset += lines;
	if (view_offset > max_offset)
		view_offset = max_offset;

	render_view();
}

/**
 * console_scroll_forward - Scroll forward toward live view
 * @lines: number of lines to scroll forward
 */
void console_scroll_forward(int lines)
{
	if (!framebuffer || lines <= 0)
		return;

	view_offset -= lines;
	if (view_offset < 0)
		view_offset = 0;

	render_view();
}

/**
 * console_scroll_to_bottom - Return to live view
 */
void console_scroll_to_bottom(void)
{
	if (view_offset == 0)
		return;

	view_offset = 0;
	render_view();

	if (cursor_visible)
		draw_cursor();
}

/**
 * console_is_scrolled_back - Check if viewing history
 *
 * Return: non-zero if scrolled back, 0 if at live view
 */
int console_is_scrolled_back(void)
{
	return view_offset > 0;
}

/**
 * console_update_cursor - Update cursor blink state
 * @ticks: current tick count
 */
void console_update_cursor(uint64_t ticks)
{
	int cur_x, cur_y;

	if (!cursor_enabled || view_offset > 0)
		return;

	get_cursor_position(&cur_x, &cur_y);

	if (cursor_visible && (cur_x != cursor_draw_x || cur_y != cursor_draw_y)) {
		erase_cursor();
		draw_cursor();
		cursor_last_toggle = ticks;
		return;
	}

	if (ticks - cursor_last_toggle >= CURSOR_BLINK_TICKS) {
		cursor_last_toggle = ticks;

		if (cursor_visible) {
			erase_cursor();
			cursor_visible = 0;
		} else {
			draw_cursor();
			cursor_visible = 1;
		}
	}
}

/**
 * console_hide_cursor - Hide the blinking cursor
 */
void console_hide_cursor(void)
{
	if (cursor_visible) {
		erase_cursor();
		cursor_visible = 0;
	}
}

/**
 * console_show_cursor - Show the blinking cursor
 */
void console_show_cursor(void)
{
	if (!cursor_visible && view_offset == 0) {
		draw_cursor();
		cursor_visible = 1;
	}
}

/**
 * console_set_fullscreen - Enable or disable fullscreen mode
 * @enabled: 1 to suppress text output, 0 to restore
 */
void console_set_fullscreen(int enabled)
{
	fullscreen_mode = enabled;
	if (enabled)
		console_hide_cursor();
	else
		console_show_cursor();
}

/**
 * console_get_fullscreen - Check if fullscreen mode is active
 *
 * Return: non-zero if fullscreen, 0 if normal
 */
int console_get_fullscreen(void)
{
	return fullscreen_mode;
}
