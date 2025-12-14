/*
 * stdlib.c - Standard Library Functions
 *
 * Implements memory allocation using a free-list allocator on top of sbrk().
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "internal/syscall.h"

/* ============================================================================
 * Memory Allocator
 *
 * Uses a simple free-list with first-fit allocation strategy.
 * Each block has a header containing size, free status, and next pointer.
 * ============================================================================ */

/* Block header - 32 bytes for alignment */
struct block_header {
    size_t size;                 /* Size of data area (not including header) */
    struct block_header *next;   /* Next block in free list (when free) */
    int free;                    /* 1 if free, 0 if allocated */
    unsigned int magic;          /* Magic number for validation */
};

#define BLOCK_MAGIC 0xDEADBEEF
#define ALIGN16(x)  (((x) + 15) & ~15)  /* 16-byte alignment */
#define HEADER_SIZE (sizeof(struct block_header))

static struct block_header *free_list = NULL;

void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size = ALIGN16(size);

    /* First-fit search through free list */
    struct block_header *prev = NULL;
    struct block_header *curr = free_list;

    while (curr) {
        if (curr->free && curr->size >= size) {
            /* Found a fit - mark as allocated */
            curr->free = 0;

            /* Remove from free list */
            if (prev) {
                prev->next = curr->next;
            } else {
                free_list = curr->next;
            }

            return (void *)(curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }

    /* No free block found - extend heap with sbrk */
    size_t total = HEADER_SIZE + size;
    struct block_header *block = (struct block_header *)__syscall1(__NR_sbrk, total);
    if (block == (void *)-1) {
        return NULL;
    }

    block->size = size;
    block->next = NULL;
    block->free = 0;
    block->magic = BLOCK_MAGIC;

    return (void *)(block + 1);
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }

    struct block_header *block = (struct block_header *)ptr - 1;

    /* Validate block */
    if (block->magic != BLOCK_MAGIC) {
        return;  /* Invalid pointer - silently ignore */
    }

    /* Mark as free and add to free list */
    block->free = 1;
    block->next = free_list;
    free_list = block;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    struct block_header *block = (struct block_header *)ptr - 1;

    /* Validate block */
    if (block->magic != BLOCK_MAGIC) {
        return NULL;
    }

    /* If already big enough, return same pointer */
    if (block->size >= size) {
        return ptr;
    }

    /* Allocate new block and copy */
    void *new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        free(ptr);
    }
    return new_ptr;
}

/* ============================================================================
 * Process Control
 * ============================================================================ */

void exit(int status) {
    __syscall1(__NR_exit, status);
    __builtin_unreachable();
}

void abort(void) {
    exit(127);
}

/* ============================================================================
 * String Conversion
 * ============================================================================ */

int atoi(const char *nptr) {
    int result = 0;
    int sign = 1;

    /* Skip whitespace */
    while (isspace(*nptr)) {
        nptr++;
    }

    /* Handle sign */
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    /* Parse digits */
    while (isdigit(*nptr)) {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }

    return sign * result;
}

long atol(const char *nptr) {
    return (long)strtol(nptr, NULL, 10);
}

long long atoll(const char *nptr) {
    return (long long)strtol(nptr, NULL, 10);
}

long strtol(const char *nptr, char **endptr, int base) {
    long result = 0;
    int sign = 1;
    const char *start = nptr;

    /* Skip whitespace */
    while (isspace(*nptr)) {
        nptr++;
    }

    /* Handle sign */
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    /* Handle base prefix */
    if (base == 0) {
        if (*nptr == '0') {
            nptr++;
            if (*nptr == 'x' || *nptr == 'X') {
                base = 16;
                nptr++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*nptr == '0' && (*(nptr + 1) == 'x' || *(nptr + 1) == 'X')) {
            nptr += 2;
        }
    }

    /* Parse digits */
    while (*nptr) {
        int digit;
        if (isdigit(*nptr)) {
            digit = *nptr - '0';
        } else if (*nptr >= 'a' && *nptr <= 'z') {
            digit = *nptr - 'a' + 10;
        } else if (*nptr >= 'A' && *nptr <= 'Z') {
            digit = *nptr - 'A' + 10;
        } else {
            break;
        }

        if (digit >= base) {
            break;
        }

        result = result * base + digit;
        nptr++;
    }

    if (endptr) {
        *endptr = (char *)(nptr == start ? start : nptr);
    }

    return sign * result;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    /* For simplicity, just cast strtol result */
    return (unsigned long)strtol(nptr, endptr, base);
}

/* ============================================================================
 * Absolute Value
 * ============================================================================ */

int abs(int j) {
    return j < 0 ? -j : j;
}

long labs(long j) {
    return j < 0 ? -j : j;
}

/* ============================================================================
 * Pseudo-Random Number Generation (simple LCG)
 * ============================================================================ */

static unsigned int rand_seed = 1;

int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (unsigned int)(rand_seed / 65536) % (RAND_MAX + 1);
}

void srand(unsigned int seed) {
    rand_seed = seed;
}

/* ============================================================================
 * Sorting and Searching
 * ============================================================================ */

/*
 * qsort - Quicksort implementation
 *
 * For simplicity, uses a basic quicksort with median-of-three pivot selection.
 */
static void swap_bytes(void *a, void *b, size_t size) {
    unsigned char *pa = (unsigned char *)a;
    unsigned char *pb = (unsigned char *)b;
    for (size_t i = 0; i < size; i++) {
        unsigned char tmp = pa[i];
        pa[i] = pb[i];
        pb[i] = tmp;
    }
}

static void qsort_impl(void *base, size_t nmemb, size_t size,
                       int (*compar)(const void *, const void *)) {
    if (nmemb <= 1) return;

    unsigned char *arr = (unsigned char *)base;
    unsigned char *pivot = arr + (nmemb - 1) * size;

    size_t i = 0;
    for (size_t j = 0; j < nmemb - 1; j++) {
        if (compar(arr + j * size, pivot) < 0) {
            swap_bytes(arr + i * size, arr + j * size, size);
            i++;
        }
    }
    swap_bytes(arr + i * size, pivot, size);

    if (i > 1) qsort_impl(arr, i, size, compar);
    if (nmemb - i - 1 > 1) qsort_impl(arr + (i + 1) * size, nmemb - i - 1, size, compar);
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    if (!base || nmemb <= 1 || size == 0) return;
    qsort_impl(base, nmemb, size, compar);
}

/*
 * bsearch - Binary search in a sorted array
 */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    const unsigned char *arr = (const unsigned char *)base;
    size_t low = 0;
    size_t high = nmemb;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const void *elem = arr + mid * size;
        int cmp = compar(key, elem);
        if (cmp < 0) {
            high = mid;
        } else if (cmp > 0) {
            low = mid + 1;
        } else {
            return (void *)elem;
        }
    }
    return NULL;
}
