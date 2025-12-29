/*
 * types.h - Common Type Definitions
 *
 * This header provides common types used throughout the kernel.
 * Include this instead of individual standard headers for consistency.
 *
 * Provides:
 *   - Standard integer types (uint8_t, int64_t, etc.) from <stdint.h>
 *   - Size types (size_t, NULL) from <stddef.h>
 *   - Boolean types (bool, true, false) from <stdbool.h>
 *   - POSIX-like types (ssize_t, off_t, pid_t)
 */

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Signed size type for functions that return size or negative error */
typedef int64_t  ssize_t;

/* File offset type */
typedef int64_t  off_t;

/* Process ID type */
typedef uint32_t pid_t;

#endif /* TYPES_H */