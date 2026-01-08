/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * System information interface
 */

#ifndef _SYSINFO_H
#define _SYSINFO_H

#include <stdint.h>

/**
 * struct sysinfo_t - System information
 * @cpu_count: number of CPUs detected
 * @total_memory_bytes: total usable RAM in bytes
 * @free_memory_bytes: free RAM in bytes
 * @heap_free_bytes: free kernel heap space in bytes
 */
typedef struct {
	int cpu_count;
	uint64_t total_memory_bytes;
	uint64_t free_memory_bytes;
	uint64_t heap_free_bytes;
} sysinfo_t;

/**
 * sysinfo_init - Initialize system information
 */
void sysinfo_init(void);

/**
 * sysinfo_get - Get pointer to system information structure
 *
 * Return: pointer to the global sysinfo_t structure
 */
sysinfo_t *sysinfo_get(void);

/**
 * sysinfo_print_summary - Print a formatted system summary to the console
 */
void sysinfo_print_summary(void);

#endif /* _SYSINFO_H */
