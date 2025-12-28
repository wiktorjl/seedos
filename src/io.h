// Unified I/O interface (routes to console and serial backends)

#ifndef IO_H
#define IO_H

void io_init(void);
void putchar(char c);
void puts(const char *str);

#endif
