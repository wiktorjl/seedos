/*
 * string.h - String Utility Functions
 *
 * Simple string operations for use throughout the kernel.
 * We can't use libc, so we implement our own.
 */

#ifndef STRING_H
#define STRING_H

#include <stdint.h>

/*
 * strcmp - Compare two null-terminated strings.
 *
 * Returns: 0 if equal, non-zero if different.
 */
int strcmp(const char *a, const char *b);

/*
 * strncmp - Compare first n characters of two strings.
 *
 * Returns: 0 if equal, non-zero if different.
 */
int strncmp(const char *a, const char *b, uint64_t n);

/*
 * strlen - Get length of null-terminated string.
 *
 * Returns: Number of characters (not including null terminator).
 */
uint64_t strlen(const char *s);

/*
 * strchr - Find first occurrence of character in string.
 *
 * Returns: Pointer to character, or NULL if not found.
 */
const char *strchr(const char *s, char c);

/*
 * memset - Fill memory with a constant byte.
 *
 * Returns: Pointer to the memory area.
 */
void *memset(void *s, int c, uint64_t n);

/*
 * memcpy - Copy memory area.
 *
 * Returns: Pointer to dest.
 */
void *memcpy(void *dest, const void *src, uint64_t n);

/*
 * parse_hex - Parse a hexadecimal number from a string.
 *
 * Accepts with or without "0x" prefix.
 * Returns: The parsed value, or 0 if invalid.
 */
uint64_t parse_hex(const char *s);

#endif /* STRING_H */
