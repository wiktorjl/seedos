# Makefile for the kernel

CC = gcc
AS = as
LD = ld
OBJCOPY = objcopy

# Directories
SRC_DIR = src
BUILD_DIR = build
USER_DIR = $(SRC_DIR)/userspace
LIBC_DIR = $(SRC_DIR)/libc
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
C_SOURCES = kernel.c pmm.c idt.c pic.c keyboard.c programs.c process.c shell.c fb.c gdt.c console.c vmm.c syscall.c pit.c serial.c string.c elf.c tar.c tarfs.c vfs.c sched.c
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
# Build chain: .c -> _ucode.o + crt0.o + libc.a -> .elf
# ELF files are copied to initrd by build.sh, NOT embedded in kernel.
# =============================================================================
USER_PROGRAMS = info crash ls cat sh init \
                echo yes true false seq clear pwd uptime stat \
                head tail wc hexdump uniq tr sort grep \
                shutdown reboot bgcount ctest
USER_ELF_FILES = $(addprefix $(BUILD_DIR)/,$(addsuffix .elf,$(USER_PROGRAMS)))

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
              -nostdlib            \
              -I$(LIBC_DIR)/include

# All objects (user programs are in initrd, not embedded in kernel)
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS) $(TEST_OBJECTS)

# Output
KERNEL = $(BUILD_DIR)/kernel.elf

# Prevent Make from deleting intermediate files in the user program build chain
.PRECIOUS: $(BUILD_DIR)/%_ucode.o $(BUILD_DIR)/%.elf

.PHONY: all clean libc user-programs

all: $(KERNEL) $(USER_ELF_FILES)

user-programs: $(USER_ELF_FILES)

# Build libc
libc: $(LIBC_DIR)/libc.a

$(LIBC_DIR)/libc.a:
	$(MAKE) -C $(LIBC_DIR)

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

# Link C program to ELF (crt0.o + program.o + libc.a)
$(foreach prog,$(USER_PROGRAMS),$(eval $(BUILD_DIR)/$(prog).elf: $(BUILD_DIR)/crt0.o $(BUILD_DIR)/$(prog)_ucode.o $(USER_LDSCRIPT) $(LIBC_DIR)/libc.a ; $(LD) -nostdlib -static -T $(USER_LDSCRIPT) -o $$@ $(BUILD_DIR)/crt0.o $(BUILD_DIR)/$(prog)_ucode.o $(LIBC_DIR)/libc.a))


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
	$(MAKE) -C $(LIBC_DIR) clean

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
