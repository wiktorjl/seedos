// SPDX-License-Identifier: GPL-2.0-only
/*
 * Matrix rain demo
 *
 * Creates a "falling characters" visual effect like The Matrix.
 * Each screen column is animated by its own kernel thread, demonstrating
 * preemptive multithreading - threads don't yield, yet all animate smoothly.
 */

#include "matrix.h"
#include "console.h"
#include "terminal.h"
#include "kthread.h"
#include "apic.h"
#include "types.h"

#define MAX_COLUMNS     160     /* Max threads (160 * 16KB = 2.5MB stack) */
#define CHAR_WIDTH      8       /* Pixels per character */
#define CHAR_HEIGHT     16

/* Speed range (pixels moved per frame) */
#define SPEED_MIN       8
#define SPEED_MAX       32

/* Trail length range (characters) */
#define TRAIL_MIN       8
#define TRAIL_MAX       18

/* Frame delay range (milliseconds) */
#define DELAY_MIN       30
#define DELAY_MAX       50

typedef struct {
	uint32_t state;
} rng_t;

static inline void rng_seed(rng_t *rng, uint32_t seed)
{
	rng->state = seed ? seed : 1;
}

static inline uint32_t rng_next(rng_t *rng)
{
	/* LCG constants from Numerical Recipes */
	rng->state = rng->state * 1664525 + 1013904223;
	return rng->state;
}

static inline uint32_t rng_range(rng_t *rng, uint32_t min, uint32_t max)
{
	return min + (rng_next(rng) % (max - min + 1));
}

/* Trail colors: bright head fading to dim tail */
static const uint32_t trail_colors[] = {
	0xFFFFFF,   /* 0: White flash (head) */
	0x00FF00,   /* 1: Bright green */
	0x00EE00,   /* 2 */
	0x00DD00,   /* 3 */
	0x00CC00,   /* 4 */
	0x00AA00,   /* 5 */
	0x008800,   /* 6 */
	0x006600,   /* 7 */
	0x004400,   /* 8 */
	0x003300,   /* 9 */
	0x002200,   /* 10 */
	0x001800,   /* 11 */
	0x001100,   /* 12: Nearly invisible */
};
#define NUM_COLORS (sizeof(trail_colors) / sizeof(trail_colors[0]))

typedef struct {
	int column;         /* Column index (0 to num_columns-1) */
	int x_pixel;        /* X position in pixels (column * 8) */
	int speed;          /* Fall speed (pixels per frame) */
	int trail_length;   /* Number of characters in trail */
	int head_y;         /* Y position of leading character */
	int screen_height;  /* Screen height in pixels */
	rng_t rng;          /* Per-thread random state */
} matrix_column_t;

/* Static storage to avoid heap fragmentation */
static matrix_column_t columns[MAX_COLUMNS];
static volatile int matrix_running = 0;
static int num_columns = 0;

static void matrix_column_entry(void *arg)
{
	matrix_column_t *col = (matrix_column_t *)arg;
	int max_y = col->screen_height;

	/* Start at random position above the screen */
	col->head_y = -(int)rng_range(&col->rng, CHAR_HEIGHT, max_y / 2);

	while (matrix_running) {
		/* Draw the trail */
		for (int i = 0; i < col->trail_length; i++) {
			int char_y = col->head_y - (i * CHAR_HEIGHT);

			/* Only draw if on screen */
			if (char_y >= 0 && char_y < max_y - CHAR_HEIGHT) {
				/* Pick a random printable character */
				char c = (char)rng_range(&col->rng, 33, 126);

				/* Pick color based on position in trail */
				int color_idx = i;
				if (color_idx >= (int)NUM_COLORS) {
					color_idx = NUM_COLORS - 1;
				}

				console_draw_char(c, col->x_pixel, char_y, trail_colors[color_idx]);
			}
		}

		/* Erase the oldest character (one past the trail) */
		int erase_y = col->head_y - (col->trail_length * CHAR_HEIGHT);
		if (erase_y >= 0 && erase_y < max_y) {
			console_fill_rect(col->x_pixel, erase_y, CHAR_WIDTH, CHAR_HEIGHT, 0x000000);
		}

		/* Move the head down */
		col->head_y += col->speed;

		/* Reset when trail is fully off screen */
		if (erase_y > max_y) {
			col->head_y = -(int)rng_range(&col->rng, CHAR_HEIGHT, max_y / 3);
			col->speed = rng_range(&col->rng, SPEED_MIN, SPEED_MAX);
			col->trail_length = rng_range(&col->rng, TRAIL_MIN, TRAIL_MAX);
		}

		/* Sleep with slight random jitter for visual variety */
		kthread_sleep(rng_range(&col->rng, DELAY_MIN, DELAY_MAX));
	}

	/* No cleanup needed - matrix_stop clears the whole screen */
}

/**
 * matrix_start - Start the matrix rain animation
 *
 * Spawns one thread per screen column. Each thread animates independently
 * with random speed and trail length.
 */
void matrix_start(void)
{
	if (matrix_running) {
		return;  /* Already running */
	}

	/* Get screen dimensions */
	int cols, rows;
	console_get_dimensions(&cols, &rows);
	int screen_height = rows * CHAR_HEIGHT;

	/* Calculate number of columns (cap at MAX_COLUMNS) */
	num_columns = cols;
	if (num_columns > MAX_COLUMNS) {
		num_columns = MAX_COLUMNS;
	}

	/* Enable fullscreen mode to suppress console output */
	console_set_fullscreen(1);

	/* Clear screen to black */
	console_fill_rect(0, 0, cols * CHAR_WIDTH, screen_height, 0x000000);

	/* Set running flag before spawning threads */
	matrix_running = 1;

	/* Seed base from APIC ticks */
	uint32_t base_seed = (uint32_t)apic_get_ticks();

	/* Spawn one thread per column */
	for (int i = 0; i < num_columns; i++) {
		matrix_column_t *col = &columns[i];

		/* Initialize column data */
		col->column = i;
		col->x_pixel = i * CHAR_WIDTH;
		col->screen_height = screen_height;

		/* Seed RNG uniquely per column */
		rng_seed(&col->rng, base_seed + i * 12345);

		/* Randomize initial parameters */
		col->speed = rng_range(&col->rng, SPEED_MIN, SPEED_MAX);
		col->trail_length = rng_range(&col->rng, TRAIL_MIN, TRAIL_MAX);
		col->head_y = 0;  /* Will be set in thread entry */

		/* Create thread (name will be like "matrix-42") */
		kthread_create("matrix", matrix_column_entry, col);
	}
}

/**
 * matrix_stop - Stop the matrix rain animation
 *
 * Signals all column threads to exit and restores the console.
 */
void matrix_stop(void)
{
	if (!matrix_running)
		return;

	/* Signal threads to stop */
	matrix_running = 0;

	/* Give threads time to exit (threads sleep 30-50ms, so 200ms is plenty) */
	kthread_sleep(200);

	/* Restore console: disable fullscreen first, then clear via terminal */
	console_set_fullscreen(0);
	terminal_clear(terminal_get_active());
}

/**
 * matrix_is_running - Check if the animation is active
 *
 * Return: 1 if running, 0 otherwise
 */
int matrix_is_running(void)
{
	return matrix_running;
}
