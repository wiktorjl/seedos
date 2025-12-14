/*
 * stdio.c - Standard Input/Output
 *
 * Implements printf/sprintf with common format specifiers,
 * FILE streams, and buffered I/O.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* ============================================================================
 * FILE Stream Implementation
 * ============================================================================ */

/* Static FILE structures for standard streams */
static FILE _stdin_file = {
    .fd = STDIN_FILENO,
    .flags = _F_READ,
    .buf = NULL,
    .bufsiz = 0,
    .bufpos = 0,
    .buflen = 0
};

static FILE _stdout_file = {
    .fd = STDOUT_FILENO,
    .flags = _F_WRITE,
    .buf = NULL,
    .bufsiz = 0,
    .bufpos = 0,
    .buflen = 0
};

static FILE _stderr_file = {
    .fd = STDERR_FILENO,
    .flags = _F_WRITE,
    .buf = NULL,
    .bufsiz = 0,
    .bufpos = 0,
    .buflen = 0
};

/* Pointers to standard streams */
FILE *stdin = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

/* Pool of FILE structures for fopen */
static FILE _file_pool[FOPEN_MAX];
static int _file_pool_used[FOPEN_MAX];

/* Allocate a FILE from the pool */
static FILE *alloc_file(void) {
    for(int i = 0; i < FOPEN_MAX; i++) {
        if(!_file_pool_used[i]) {
            _file_pool_used[i] = 1;
            memset(&_file_pool[i], 0, sizeof(FILE));
            return &_file_pool[i];
        }
    }
    return NULL;
}

/* Free a FILE back to the pool */
static void free_file(FILE *fp) {
    for(int i = 0; i < FOPEN_MAX; i++) {
        if(&_file_pool[i] == fp) {
            _file_pool_used[i] = 0;
            return;
        }
    }
}

FILE *fopen(const char *path, const char *mode) {
    int flags = 0;
    int file_flags = 0;

    /* Parse mode string */
    if(mode[0] == 'r') {
        flags = O_RDONLY;
        file_flags = _F_READ;
    }else if(mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        file_flags = _F_WRITE;
    }else if(mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        file_flags = _F_WRITE;
    }else {
        return NULL;
    }

    /* Check for '+' (read/write) */
    if(mode[1] == '+' || (mode[1] && mode[2] == '+')) {
        flags = O_RDWR | (flags & ~(O_RDONLY | O_WRONLY));
        file_flags = _F_READ | _F_WRITE;
    }

    int fd = open(path, flags);
    if(fd < 0) {
        return NULL;
    }

    FILE *fp = alloc_file();
    if(!fp) {
        close(fd);
        return NULL;
    }

    fp->fd = fd;
    fp->flags = file_flags;
    fp->buf = NULL;
    fp->bufsiz = 0;
    fp->bufpos = 0;
    fp->buflen = 0;

    return fp;
}

int fclose(FILE *stream) {
    if(!stream) {
        return EOF;
    }

    /* Flush any buffered output */
    fflush(stream);

    /* Free allocated buffer */
    if(stream->flags & _F_ALLOC) {
        free(stream->buf);
    }

    int ret = close(stream->fd);

    /* Return FILE to pool */
    free_file(stream);

    return (ret < 0) ? EOF : 0;
}

int fflush(FILE *stream) {
    if(!stream) {
        return 0;  /* Flush all - not implemented */
    }

    /* Flush write buffer */
    if((stream->flags & _F_WRITE) && stream->bufpos > 0) {
        ssize_t written = write(stream->fd, stream->buf, stream->bufpos);
        if(written < 0) {
            stream->flags |= _F_ERR;
            return EOF;
        }
        stream->bufpos = 0;
    }

    return 0;
}

int fgetc(FILE *stream) {
    if(!stream || !(stream->flags & _F_READ)) {
        return EOF;
    }

    if(stream->flags & _F_EOF) {
        return EOF;
    }

    unsigned char c;
    ssize_t n = read(stream->fd, &c, 1);
    if(n <= 0) {
        if(n == 0) {
            stream->flags |= _F_EOF;
        }else {
            stream->flags |= _F_ERR;
        }
        return EOF;
    }

    return c;
}

int getc(FILE *stream) {
    return fgetc(stream);
}

char *fgets(char *s, int size, FILE *stream) {
    if(!s || size <= 0 || !stream) {
        return NULL;
    }

    int i = 0;
    while(i < size - 1) {
        int c = fgetc(stream);
        if(c == EOF) {
            if(i == 0) {
                return NULL;  /* EOF at start */
            }
            break;
        }
        s[i++] = (char)c;
        if(c == '\n') {
            break;
        }
    }

    s[i] = '\0';
    return s;
}

int fputc(int c, FILE *stream) {
    if(!stream || !(stream->flags & _F_WRITE)) {
        return EOF;
    }

    unsigned char ch = (unsigned char)c;
    ssize_t n = write(stream->fd, &ch, 1);
    if(n != 1) {
        stream->flags |= _F_ERR;
        return EOF;
    }

    return (unsigned char)c;
}

int fputs(const char *s, FILE *stream) {
    if(!s || !stream) {
        return EOF;
    }

    size_t len = strlen(s);
    ssize_t n = write(stream->fd, s, len);
    if(n < 0) {
        stream->flags |= _F_ERR;
        return EOF;
    }

    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if(!ptr || !stream || size == 0 || nmemb == 0) {
        return 0;
    }

    if(!(stream->flags & _F_READ)) {
        return 0;
    }

    size_t total = size * nmemb;
    ssize_t n = read(stream->fd, ptr, total);
    if(n <= 0) {
        if(n == 0) {
            stream->flags |= _F_EOF;
        }else {
            stream->flags |= _F_ERR;
        }
        return 0;
    }

    return (size_t)n / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if(!ptr || !stream || size == 0 || nmemb == 0) {
        return 0;
    }

    if(!(stream->flags & _F_WRITE)) {
        return 0;
    }

    size_t total = size * nmemb;
    ssize_t n = write(stream->fd, ptr, total);
    if(n < 0) {
        stream->flags |= _F_ERR;
        return 0;
    }

    return (size_t)n / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if(!stream) {
        return -1;
    }

    /* Flush before seeking */
    fflush(stream);

    off_t ret = lseek(stream->fd, offset, whence);
    if(ret < 0) {
        return -1;
    }

    /* Clear EOF flag on successful seek */
    stream->flags &= ~_F_EOF;
    stream->buflen = 0;
    stream->bufpos = 0;

    return 0;
}

long ftell(FILE *stream) {
    if(!stream) {
        return -1;
    }

    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

void rewind(FILE *stream) {
    if(stream) {
        fseek(stream, 0, SEEK_SET);
        clearerr(stream);
    }
}

int feof(FILE *stream) {
    return stream ? (stream->flags & _F_EOF) != 0 : 0;
}

int ferror(FILE *stream) {
    return stream ? (stream->flags & _F_ERR) != 0 : 0;
}

void clearerr(FILE *stream) {
    if(stream) {
        stream->flags &= ~(_F_EOF | _F_ERR);
    }
}

int fileno(FILE *stream) {
    return stream ? stream->fd : -1;
}

/* ============================================================================
 * Character I/O (using FILE streams)
 * ============================================================================ */

int putchar(int c) {
    return fputc(c, stdout);
}

int puts(const char *s) {
    if(fputs(s, stdout) == EOF) {
        return EOF;
    }
    return fputc('\n', stdout);
}

int getchar(void) {
    return fgetc(stdin);
}

/* ============================================================================
 * Printf Implementation
 * ============================================================================ */

/* Format a number into buffer, returns length */
static int format_unsigned(char *buf, unsigned long n, int base, int uppercase) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24];
    int i = 0;

    if(n == 0) {
        buf[0] = '0';
        return 1;
    }

    while(n > 0) {
        tmp[i++] = digits[n % base];
        n /= base;
    }

    int len = i;
    int j = 0;
    while(i > 0) {
        buf[j++] = tmp[--i];
    }
    return len;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    size_t pos = 0;

    /* Helper macro to add a character */
    #define PUT(c) do { \
        if(pos < size - 1) str[pos] = (c); \
        pos++; \
    } while(0)

    while(*format) {
        if(*format != '%') {
            PUT(*format++);
            continue;
        }

        format++;  /* skip '%' */

        /* Parse flags */
        int zero_pad = 0;
        int left_align = 0;

        while(*format == '0' || *format == '-') {
            if(*format == '0') zero_pad = 1;
            if(*format == '-') left_align = 1;
            format++;
        }

        /* Parse width - can be * for dynamic width */
        int width = 0;
        if(*format == '*') {
            width = va_arg(ap, int);
            if(width < 0) {
                left_align = 1;
                width = -width;
            }
            format++;
        }else {
            while(*format >= '0' && *format <= '9') {
                width = width * 10 + (*format++ - '0');
            }
        }

        /* Parse precision */
        int precision = -1;  /* -1 means not specified */
        if(*format == '.') {
            format++;
            if(*format == '*') {
                precision = va_arg(ap, int);
                if(precision < 0) precision = 0;
                format++;
            }else {
                precision = 0;
                while(*format >= '0' && *format <= '9') {
                    precision = precision * 10 + (*format++ - '0');
                }
            }
        }

        /* Parse length modifier */
        int is_long = 0;
        if(*format == 'l') {
            is_long = 1;
            format++;
            if(*format == 'l') {
                format++;  /* ll treated same as l for simplicity */
            }
        }

        /* Parse format specifier */
        char buf[24];
        int len = 0;
        int is_negative = 0;
        char pad_char = (zero_pad && !left_align) ? '0' : ' ';

        switch(*format++) {
            case 'd':
            case 'i': {
                long val = is_long ? va_arg(ap, long) : va_arg(ap, int);
                if(val < 0) {
                    is_negative = 1;
                    val = -val;
                }
                len = format_unsigned(buf, (unsigned long)val, 10, 0);
                break;
            }

            case 'u': {
                unsigned long val = is_long ? va_arg(ap, unsigned long)
                                           : va_arg(ap, unsigned int);
                len = format_unsigned(buf, val, 10, 0);
                break;
            }

            case 'x': {
                unsigned long val = is_long ? va_arg(ap, unsigned long)
                                           : va_arg(ap, unsigned int);
                len = format_unsigned(buf, val, 16, 0);
                break;
            }

            case 'X': {
                unsigned long val = is_long ? va_arg(ap, unsigned long)
                                           : va_arg(ap, unsigned int);
                len = format_unsigned(buf, val, 16, 1);
                break;
            }

            case 'p': {
                unsigned long val = (unsigned long)va_arg(ap, void *);
                PUT('0');
                PUT('x');
                width = width > 2 ? width - 2 : 0;
                len = format_unsigned(buf, val, 16, 0);
                break;
            }

            case 's': {
                const char *s = va_arg(ap, const char *);
                if(!s) s = "(null)";
                size_t slen = strlen(s);

                /* Apply precision - limit string length */
                if(precision >= 0 && (size_t)precision < slen) {
                    slen = (size_t)precision;
                }

                /* Right padding for left align */
                if(!left_align) {
                    while(width > (int)slen) {
                        PUT(' ');
                        width--;
                    }
                }

                /* Print string up to slen characters */
                for(size_t i = 0; i < slen; i++) {
                    PUT(s[i]);
                }

                /* Left padding for right align */
                if(left_align) {
                    while(width > (int)slen) {
                        PUT(' ');
                        width--;
                    }
                }
                continue;
            }

            case 'c': {
                char c = (char)va_arg(ap, int);
                PUT(c);
                continue;
            }

            case '%':
                PUT('%');
                continue;

            default:
                /* Unknown specifier - print as-is */
                PUT('%');
                PUT(*(format - 1));
                continue;
        }

        /* Calculate padding needed */
        int total_len = len + (is_negative ? 1 : 0);
        int padding = (width > total_len) ? width - total_len : 0;

        /* Left-aligned: content first, then padding */
        if(left_align) {
            if(is_negative) PUT('-');
            for(int i = 0; i < len; i++) PUT(buf[i]);
            while(padding--) PUT(' ');
        }else {
            /* Right-aligned: padding, then content */
            if(pad_char == '0' && is_negative) {
                PUT('-');
                is_negative = 0;
            }
            while(padding--) PUT(pad_char);
            if(is_negative) PUT('-');
            for(int i = 0; i < len; i++) PUT(buf[i]);
        }
    }

    /* Null-terminate */
    if(size > 0) {
        str[pos < size ? pos : size - 1] = '\0';
    }

    return (int)pos;

    #undef PUT
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, (size_t)-1, format, ap);
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsprintf(str, format, ap);
    va_end(ap);
    return ret;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    int to_write = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
    if(stream) {
        fwrite(buf, 1, to_write, stream);
    }
    return len;
}

int vprintf(const char *format, va_list ap) {
    return vfprintf(stdout, format, ap);
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vprintf(format, ap);
    va_end(ap);
    return ret;
}
