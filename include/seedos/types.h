/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common type definitions
 *
 * Aggregates standard types for kernel use:
 *   - Integer types (uint8_t, int64_t, etc.) from <stdint.h>
 *   - Size types (size_t, NULL) from <stddef.h>
 *   - Boolean types (bool, true, false) from <stdbool.h>
 */

#ifndef _TYPES_H
#define _TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * POSIX-like types not provided by freestanding headers
 */
typedef int64_t ssize_t;    /* Signed size (for read/write return values) */
typedef int64_t off_t;      /* File offset */
typedef int32_t pid_t;      /* Process ID */
typedef uint32_t mode_t;    /* File mode/permissions */
typedef uint32_t uid_t;     /* User ID */
typedef uint32_t gid_t;     /* Group ID */

#endif /* _TYPES_H */