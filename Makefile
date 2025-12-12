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
C_SOURCES = kernel.c pmm.c idt.c pic.c keyboard.c programs.c process.c shell.c fb.c gdt.c console.c vmm.c syscall.c pit.c serial.c string.c elf.c
ASM_SOURCES = boot.S isr.S gdt_load.S context_switch.S

# Test source files (in test/)
TEST_SOURCES = test_framework.c test_pmm.c test_vmm.c test_gdt.c test_idt.c test_syscall.c test_console.c

# Object files (in build directory)
C_OBJECTS = $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ASM_OBJECTS = $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.S=.o))
TEST_OBJECTS = $(addprefix $(BUILD_DIR)/,$(TEST_SOURCES:.c=.o))

# =============================================================================
# User Programs
#
# Each .s file in src/userspace/ becomes an embedded binary:
#   hello.s -> hello.o -> hello.bin -> hello_bin.c -> hello_bin.o
# =============================================================================
USER_PROGRAMS = info heap hello loop crash count alpha stars
USER_PROG_OBJS = $(addprefix $(BUILD_DIR)/,$(addsuffix _bin.o,$(USER_PROGRAMS)))

# All objects
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS) $(TEST_OBJECTS) $(USER_PROG_OBJS)

# Output
KERNEL = $(BUILD_DIR)/kernel.elf

.PHONY: all clean

all: $(KERNEL)

# Create build directory if needed
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# =============================================================================
# User Program Build Rules
#
# Pattern rules to build any user program as ELF:
#   1. Assemble .s to .o
#   2. Link to ELF executable
#   3. Generate C file with xxd
#   4. Compile C file to kernel object
# =============================================================================

# User program linker script
USER_LDSCRIPT = $(USER_DIR)/user.ld

# Step 1: Assemble .s -> .o
$(BUILD_DIR)/%_user.o: $(USER_DIR)/%.s | $(BUILD_DIR)
	$(CC) -c -o $@ $<

# Step 2: Link to ELF executable .o -> .elf
$(BUILD_DIR)/%.elf: $(BUILD_DIR)/%_user.o $(USER_LDSCRIPT)
	$(LD) -nostdlib -static -T $(USER_LDSCRIPT) -o $@ $<

# Step 3: Generate C file .elf -> _bin.c
$(USER_DIR)/%_bin.c: $(BUILD_DIR)/%.elf
	@echo "Generating $@ from $<"
	@echo '#include "user_programs.h"' > $@
	@echo '' >> $@
	@echo 'unsigned char $*_bin[] = {' >> $@
	@xxd -i < $< >> $@
	@echo '};' >> $@
	@echo "unsigned int $*_bin_len = $$(wc -c < $<);" >> $@

# Step 4: Compile generated C -> .o
$(BUILD_DIR)/%_bin.o: $(USER_DIR)/%_bin.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# =============================================================================
# Kernel Build Rules
# =============================================================================

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
	rm -f $(USER_DIR)/*_bin.c

# IDE setup - generates .clangd config and compile_commands.json for VS Code/clangd
.PHONY: ide-setup
ide-setup:
	@echo "Setting up IDE support..."
	@if ! command -v bear >/dev/null 2>&1; then \
		echo "Error: 'bear' is not installed."; \
		echo "Install it with: sudo apt install bear"; \
		exit 1; \
	fi
	@echo "Creating .clangd config..."
	@echo 'CompileFlags:' > .clangd
	@echo '  Add:' >> .clangd
	@echo '    - -Wno-unused-parameter' >> .clangd
	@echo '  Remove:' >> .clangd
	@echo '    - -mno-red-zone  # clangd does not need this' >> .clangd
	@echo "Generating compile_commands.json..."
	@$(MAKE) clean
	@bear -- $(MAKE)
	@echo "IDE setup complete. Install the clangd extension in VS Code."
