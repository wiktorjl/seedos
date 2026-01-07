.PHONY: all clean run debug limine compdb

# Directories (Linux-style layout)
BUILD    := build
DATA     := data
SCRIPTS  := scripts

# Output files
KERNEL   := $(BUILD)/kernel.elf
ISO      := $(BUILD)/seed.iso
ISO_ROOT := $(BUILD)/iso_root

# UEFI firmware
OVMF := /usr/share/ovmf/OVMF.fd

# Limine bootloader
LIMINE_DIR    := limine
LIMINE_REPO   := https://github.com/limine-bootloader/limine.git
LIMINE_BRANCH := v8.x-binary

# Include paths (all directories with headers)
INCLUDES := -Iinclude \
            -Iinclude/seedos \
            -Iarch/x86/include \
            -Iarch/x86/include/asm \
            -Iarch/x86/boot \
            -Iarch/x86/kernel \
            -Ikernel \
            -Imm \
            -Idrivers/tty \
            -Idrivers/input \
            -Iinit \
            -Ilib

# Compiler flags for freestanding kernel
CFLAGS := -g -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
          -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=kernel \
          -fno-omit-frame-pointer $(INCLUDES)

# Find all source files in new structure
C_SRCS := $(shell find arch kernel mm drivers init lib -name '*.c' 2>/dev/null)
ASM_SRCS := $(shell find arch kernel -name '*.S' 2>/dev/null)

# Object files - flatten into build directory
C_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(C_SRCS)))
ASM_OBJS := $(patsubst %.S,$(BUILD)/%.o,$(notdir $(ASM_SRCS)))
OBJS := $(C_OBJS) $(ASM_OBJS)

# VPATH for finding source files
VPATH := arch/x86/boot:arch/x86/kernel:kernel:mm:drivers/tty:drivers/input:init:lib

# Default target
all: $(ISO)

$(BUILD):
	mkdir -p $(BUILD)

# Convert logo image to raw BGRA
$(BUILD)/logo.bin: $(DATA)/seedos.png $(SCRIPTS)/convert-image.sh | $(BUILD)
	$(SCRIPTS)/convert-image.sh $(DATA)/seedos.png

# Pattern rule for C files
$(BUILD)/%.o: %.c | $(BUILD)
	cc $(CFLAGS) -c $< -o $@

# Pattern rule for assembly files
$(BUILD)/%.o: %.S | $(BUILD)
	cc -g $(INCLUDES) -I$(BUILD) -I$(DATA) -c $< -o $@

# boot.S needs logo.bin and font.bin
$(BUILD)/boot.o: $(DATA)/font.bin $(BUILD)/logo.bin

# Link kernel (linker script moved to arch/x86/boot/)
$(KERNEL): $(OBJS) arch/x86/boot/linker.ld
	ld -nostdlib -static -T arch/x86/boot/linker.ld -o $@ $(OBJS)

# Create bootable ISO
$(ISO): $(KERNEL) config/limine.conf | limine
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL) $(ISO_ROOT)/boot/
	cp config/limine.conf $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $(ISO) 2>/dev/null

# Fetch Limine if needed
limine:
	@if [ ! -d "$(LIMINE_DIR)" ]; then \
		echo "Cloning Limine bootloader..."; \
		git clone $(LIMINE_REPO) --branch=$(LIMINE_BRANCH) --depth=1; \
		$(MAKE) -C $(LIMINE_DIR); \
	fi

# Run with GDB server
debug: $(ISO)
	qemu-system-x86_64 -bios $(OVMF) -cdrom $(ISO) -serial stdio -s -S

# VS Code debug target
debug-vscode: $(ISO)
	qemu-system-x86_64 -bios $(OVMF) -cdrom $(ISO) -serial file:$(BUILD)/serial.log -s -S

# Run without debugging
run: $(ISO)
	qemu-system-x86_64 -bios $(OVMF) -cdrom $(ISO) -serial stdio

# Remove build artifacts
clean:
	rm -rf $(BUILD)

# Generate compile_commands.json for IDE/clangd support
compdb:
	rm -f compile_commands.json
	bear -- $(MAKE) clean
	bear -- $(MAKE)

# Show found sources (for debugging Makefile)
show-sources:
	@echo "C sources: $(C_SRCS)"
	@echo "ASM sources: $(ASM_SRCS)"
	@echo "Objects: $(OBJS)"
