/*
 * matrix.h - Matrix Rain Demo
 *
 * Visual demonstration of preemptive multithreading.
 * Each screen column is animated by its own kernel thread.
 */

#ifndef MATRIX_H
#define MATRIX_H

/*
 * matrix_start - Start the Matrix rain demo.
 *
 * Clears the screen and spawns one thread per column.
 * Each thread animates falling green characters independently.
 * With preemptive scheduling, all columns animate smoothly.
 */
void matrix_start(void);

/*
 * matrix_stop - Stop the Matrix rain demo.
 *
 * Signals all column threads to exit gracefully.
 * Threads will clean up their columns and terminate.
 */
void matrix_stop(void);

/*
 * matrix_is_running - Check if demo is currently running.
 *
 * Returns: 1 if running, 0 if stopped.
 */
int matrix_is_running(void);

#endif /* MATRIX_H */
