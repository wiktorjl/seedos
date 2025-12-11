# Makefile for the kernel

CC = gcc
AS = as
LD = ld

# Build directory for all artifacts
BUILD_DIR = build

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

# Object files (in build directory)
C_OBJECTS = $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ASM_OBJECTS = $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.S=.o))
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Output
KERNEL = $(BUILD_DIR)/kernel.elf

.PHONY: all clean

all: $(KERNEL)

# Create build directory if needed
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(KERNEL): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
