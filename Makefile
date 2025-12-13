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
C_SOURCES = kernel.c pmm.c idt.c pic.c keyboard.c programs.c process.c shell.c fb.c gdt.c console.c vmm.c syscall.c pit.c serial.c string.c elf.c tar.c tarfs.c vfs.c
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
# Build chain: .c -> _ucode.o + crt0.o -> .elf -> _bin.c -> _bin.o
# =============================================================================
USER_PROGRAMS = hello info heap count alpha stars loop crash input ctest filetest
USER_PROG_OBJS = $(addprefix $(BUILD_DIR)/,$(addsuffix _bin.o,$(USER_PROGRAMS)))

# C compiler flags for userspace (NO -mcmodel=kernel!)
USER_CFLAGS = -ffreestanding       \
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
              -nostdlib

# All objects
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS) $(TEST_OBJECTS) $(USER_PROG_OBJS)

# Output
KERNEL = $(BUILD_DIR)/kernel.elf

# Prevent Make from deleting intermediate files in the user program build chain
.PRECIOUS: $(BUILD_DIR)/%_ucode.o $(BUILD_DIR)/%.elf $(USER_DIR)/%_bin.c

.PHONY: all clean

all: $(KERNEL)

# Create build directory if needed
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# =============================================================================
# User Program Build Rules
#
# Build chain for C user programs:
#   1. Compile crt0.s -> crt0.o (C runtime startup)
#   2. Compile %.c -> %_ucode.o
#   3. Link crt0.o + %_ucode.o -> %.elf
#   4. Generate _bin.c with xxd
#   5. Compile _bin.c -> _bin.o (embedded in kernel)
# =============================================================================

# User program linker script
USER_LDSCRIPT = $(USER_DIR)/user.ld

# Compile crt0.s (C runtime startup)
$(BUILD_DIR)/crt0.o: $(USER_DIR)/crt0.s | $(BUILD_DIR)
	$(CC) -c -o $@ $<

# Compile .c -> .o (C user programs)
$(BUILD_DIR)/%_ucode.o: $(USER_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -I$(USER_DIR) -c $< -o $@

# Link C program to ELF (crt0.o + program.o)
$(foreach prog,$(USER_PROGRAMS),$(eval $(BUILD_DIR)/$(prog).elf: $(BUILD_DIR)/crt0.o $(BUILD_DIR)/$(prog)_ucode.o $(USER_LDSCRIPT) ; $(LD) -nostdlib -static -T $(USER_LDSCRIPT) -o $$@ $(BUILD_DIR)/crt0.o $(BUILD_DIR)/$(prog)_ucode.o))

# Generate C file .elf -> _bin.c
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
