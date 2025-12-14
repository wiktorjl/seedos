/*
 * stdio.h - Standard Input/Output
 */

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* Standard file descriptors (as integers for simplicity) */
#define stdin  0
#define stdout 1
#define stderr 2

/* End-of-file indicator */
#define EOF (-1)

/* Buffer size */
#define BUFSIZ 1024

/* Character output */
int putchar(int c);
int puts(const char *s);

/* Character input */
int getchar(void);

/* Formatted output */
int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);

/* Variadic versions */
int vprintf(const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#endif /* _STDIO_H */
