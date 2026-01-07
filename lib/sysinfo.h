#ifndef SYSINFO_H
#define SYSINFO_H

#include <stdint.h>

typedef struct {
    int cpu_count;
    uint64_t total_memory_bytes;
    uint64_t free_memory_bytes;
    uint64_t heap_free_bytes;
} sysinfo_t;

void sysinfo_init(void);
sysinfo_t *sysinfo_get(void);
void sysinfo_print_summary(void);

#endif
