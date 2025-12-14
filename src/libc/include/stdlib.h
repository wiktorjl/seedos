/*
 * stdlib.h - Standard Library Definitions
 */

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

/* Memory allocation */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

/* Process control */
void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));

/* String conversion */
int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);

/* Absolute value */
int abs(int j);
long labs(long j);

/* Null pointer */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Exit status */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif /* _STDLIB_H */
