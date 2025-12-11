# Makefile for the kernel

CC = gcc
AS = as
LD = ld
OBJCOPY = objcopy

# Directories
SRC_DIR = src
BUILD_DIR = build
USER_DIR = $(SRC_DIR)/userspace
TEST_DIR = test

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
         -g                   \
         -I$(SRC_DIR)         \
         -I$(USER_DIR)        \
         -I$(TEST_DIR)

# Linker flags
LDFLAGS = -nostdlib           \
          -static             \
          -T $(SRC_DIR)/linker.ld

# Kernel source files (in src/)
C_SOURCES = kernel.c pmm.c idt.c pic.c keyboard.c shell.c fb.c gdt.c console.c vmm.c syscall.c pit.c
ASM_SOURCES = boot.S isr.S gdt_load.S context_switch.S

# Test source files (in test/)
TEST_SOURCES = test_framework.c test_pmm.c test_vmm.c test_gdt.c test_idt.c test_syscall.c test_console.c

# Object files (in build directory)
C_OBJECTS = $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ASM_OBJECTS = $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.S=.o))
TEST_OBJECTS = $(addprefix $(BUILD_DIR)/,$(TEST_SOURCES:.c=.o))

# User program artifacts
USER_PROG_SRC = $(USER_DIR)/user_program.s
USER_PROG_OBJ = $(BUILD_DIR)/user_prog.o
USER_PROG_BIN = $(BUILD_DIR)/user.bin
USER_PROG_C = $(USER_DIR)/user_program.c
USER_PROG_KERNEL_OBJ = $(BUILD_DIR)/user_program.o

# All objects
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS) $(TEST_OBJECTS) $(USER_PROG_KERNEL_OBJ)

# Output
KERNEL = $(BUILD_DIR)/kernel.elf

.PHONY: all clean

all: $(KERNEL)

# Create build directory if needed
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build user program: assemble .s -> .o -> .bin -> .c
$(USER_PROG_OBJ): $(USER_PROG_SRC) | $(BUILD_DIR)
	$(CC) -c -o $@ $<

$(USER_PROG_BIN): $(USER_PROG_OBJ)
	$(OBJCOPY) -O binary $< $@

$(USER_PROG_C): $(USER_PROG_BIN)
	@echo "Generating $@ from $<"
	@echo '#include "user_program.h"' > $@
	@echo '' >> $@
	@echo 'unsigned char user_bin[] = {' >> $@
	@xxd -i < $< >> $@
	@echo '};' >> $@
	@echo "unsigned int user_bin_len = $$(wc -c < $<);" >> $@

# Compile generated user_program.c
$(USER_PROG_KERNEL_OBJ): $(USER_PROG_C) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS) $(SRC_DIR)/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# Compile kernel source files from src/
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile test source files from test/
$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
