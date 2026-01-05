.PHONY: all clean run debug limine compdb

# Directories
SRC      := src
DATA     := data
SCRIPTS  := scripts
BUILD    := build

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

# Compiler flags for freestanding kernel
CFLAGS := -g -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
          -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=kernel \
		  -fno-omit-frame-pointer

# Source files
C_SRCS   := $(wildcard $(SRC)/*.c)
ASM_SRCS := $(wildcard $(SRC)/*.S)

# Object files
C_OBJS   := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst $(SRC)/%.S,$(BUILD)/%.o,$(ASM_SRCS))
OBJS     := $(C_OBJS) $(ASM_OBJS)

# Default target
all: $(ISO)

$(BUILD):
	mkdir -p $(BUILD)

# Convert logo image to raw BGRA (run manually: scripts/convert-image.sh data/seedos.png)
$(BUILD)/logo.bin: $(DATA)/seedos.png $(SCRIPTS)/convert-image.sh | $(BUILD)
	$(SCRIPTS)/convert-image.sh $(DATA)/seedos.png

# Pattern rule for C files
$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	cc $(CFLAGS) -c $< -o $@

# Pattern rule for assembly files
$(BUILD)/%.o: $(SRC)/%.S | $(BUILD)
	cc -g -I$(BUILD) -I$(DATA) -c $< -o $@

# boot.S needs logo.bin and font.bin
$(BUILD)/boot.o: $(DATA)/font.bin $(BUILD)/logo.bin

# Link kernel
$(KERNEL): $(OBJS) $(SRC)/linker.ld
	ld -nostdlib -static -T $(SRC)/linker.ld -o $@ $(OBJS)

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

# Run with GDB server (stops at startup, waiting for debugger)
debug: $(ISO)
	qemu-system-x86_64 -bios $(OVMF) -cdrom $(ISO) -serial stdio -s -S

# VS Code debug target (serial to file to avoid terminal conflicts)
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
