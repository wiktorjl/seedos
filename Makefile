# Makefile for the kernel

CC = gcc
AS = as
LD = ld

# C compiler flags
CFLAGS = -ffreestanding       \
         -fno-pic             \
         -fno-pie             \
         -mno-red-zone        \
         -mno-sse             \
         -mno-sse2            \
         -mcmodel=kernel      \
         -Wall                \
         -Wextra              \
         -O2                  \
         -g

# Linker flags
LDFLAGS = -nostdlib           \
          -static             \
          -T linker.ld

# Source files
C_SOURCES = kernel.c pmm.c idt.c pic.c keyboard.c shell.c fb.c gdt.c console.c vmm.c syscall.c user_program.c tests.c
ASM_SOURCES = boot.S isr.S gdt_load.S context_switch.S

# Object files
C_OBJECTS = $(C_SOURCES:.c=.o)
ASM_OBJECTS = $(ASM_SOURCES:.S=.o)
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Output
KERNEL = kernel.elf

.PHONY: all clean

all: $(KERNEL)

$(KERNEL): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(KERNEL)
	rm -f *.log
