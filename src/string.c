/*
 * string.c - String Utility Functions
 *
 * Simple string operations for use throughout the kernel.
 * We can't use libc, so we implement our own.
 */

#include "string.h"
#include <stddef.h>

/*
 * strcmp - Compare two null-terminated strings.
 */
int strcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return *a - *b;
        a++;
        b++;
    }
    return *a - *b;
}

/*
 * strncmp - Compare first n characters of two strings.
 */
int strncmp(const char *a, const char *b, uint64_t n) {
    while (n > 0 && *a && *b) {
        if (*a != *b) return *a - *b;
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return *a - *b;
}

/*
 * strlen - Get length of null-terminated string.
 */
uint64_t strlen(const char *s) {
    uint64_t len = 0;
    while (*s++) len++;
    return len;
}

/*
 * strchr - Find first occurrence of character in string.
 */
const char *strchr(const char *s, char c) {
    while (*s) {
        if (*s == c) return s;
        s++;
    }
    return NULL;
}

/*
 * memset - Fill memory with a constant byte.
 */
void *memset(void *s, int c, uint64_t n) {
    unsigned char *p = s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

/*
 * memcpy - Copy memory area.
 */
void *memcpy(void *dest, const void *src, uint64_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/*
 * strncpy - Copy at most n characters from src to dest.
 *
 * If src is shorter than n, the remainder of dest is filled with NULs.
 */
char *strncpy(char *dest, const char *src, uint64_t n) {
    uint64_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

/*
 * strcpy - Copy string from src to dest.
 */
char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

/*
 * strcat - Concatenate two strings.
 */
char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;  /* Find end of dest */
    while ((*d++ = *src++));  /* Copy src */
    return dest;
}

/*
 * parse_hex - Parse a hexadecimal number from a string.
 *
 * Accepts with or without "0x" prefix.
 */
uint64_t parse_hex(const char *s) {
    uint64_t result = 0;

    /* Skip optional "0x" or "0X" prefix */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    while (*s) {
        char c = *s;
        uint64_t digit;

        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            break;  /* Stop at first non-hex character */
        }

        result = (result << 4) | digit;
        s++;
    }

    return result;
}
