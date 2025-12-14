# Makefile for SeedOS libc
#
# Builds libc.a static library for userspace programs

CC = gcc
AR = ar

# Freestanding userspace flags (no kernel mcmodel!)
CFLAGS = -ffreestanding       \
         -fno-pic             \
         -fno-pie             \
         -mno-red-zone        \
         -mno-sse             \
         -mno-sse2            \
         -Wall                \
         -Wextra              \
         -Wno-unused-parameter \
         -O2                  \
         -g                   \
         -nostdlib            \
         -I./include          \
         -I.

# Source files
SRCS = stdio.c stdlib.c string.c ctype.c unistd.c fcntl.c errno.c stat.c dirent.c stubs.c env.c
OBJS = $(SRCS:.c=.o)

# Output
TARGET = libc.a

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Dependencies on headers
stdio.o: include/stdio.h include/stddef.h include/stdarg.h
stdlib.o: include/stdlib.h include/stddef.h include/string.h include/ctype.h internal/syscall.h
string.o: include/string.h include/stddef.h include/stdlib.h
ctype.o: include/ctype.h
unistd.o: include/unistd.h include/stddef.h internal/syscall.h
fcntl.o: include/fcntl.h internal/syscall.h
errno.o: include/errno.h
stat.o: include/sys/stat.h internal/syscall.h
dirent.o: include/dirent.h include/stdlib.h include/fcntl.h include/unistd.h internal/syscall.h

clean:
	rm -f $(OBJS) $(TARGET)
