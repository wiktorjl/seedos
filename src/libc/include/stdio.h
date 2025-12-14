/*
 * stdio.h - Standard Input/Output
 */

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* End-of-file indicator */
#define EOF (-1)

/* Buffer size */
#define BUFSIZ 1024

/* Maximum number of open FILE streams */
#define FOPEN_MAX 16

/* File access modes */
#define _IONBF 0  /* Unbuffered */
#define _IOLBF 1  /* Line buffered */
#define _IOFBF 2  /* Fully buffered */

/* Internal flags */
#define _F_READ   0x01  /* Opened for reading */
#define _F_WRITE  0x02  /* Opened for writing */
#define _F_EOF    0x04  /* EOF reached */
#define _F_ERR    0x08  /* Error occurred */
#define _F_ALLOC  0x10  /* Buffer was allocated */

/*
 * FILE structure - represents an open file stream
 */
typedef struct _FILE {
    int fd;                 /* Underlying file descriptor */
    int flags;              /* Mode flags (_F_READ, _F_WRITE, etc.) */
    unsigned char *buf;     /* I/O buffer */
    size_t bufsiz;          /* Buffer size */
    size_t bufpos;          /* Current position in buffer */
    size_t buflen;          /* Valid bytes in buffer (for reading) */
    unsigned char smallbuf; /* Single-char buffer for unbuffered I/O */
} FILE;

/* Standard streams */
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* Character output */
int putchar(int c);
int puts(const char *s);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);

/* Character input */
int getchar(void);
int fgetc(FILE *stream);
int getc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);

/* Byte I/O */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

/* File operations */
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);

/* File positioning */
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);

/* Error handling */
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

/* Utility */
int fileno(FILE *stream);

/* Formatted output */
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);

/* Variadic versions */
int vprintf(const char *format, va_list ap);
int vfprintf(FILE *stream, const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#endif /* _STDIO_H */
