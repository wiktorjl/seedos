.PHONY: all clean run debug limine

# Build output directory
BUILD    := build
# Kernel executable
KERNEL   := $(BUILD)/kernel.elf
# Bootable ISO image
ISO      := $(BUILD)/seed.iso
# ISO filesystem root
ISO_ROOT := $(BUILD)/iso_root

# UEFI firmware
OVMF         := /usr/share/ovmf/OVMF.fd
# Limine bootloader directory
LIMINE_DIR   := limine
LIMINE_REPO  := https://github.com/limine-bootloader/limine.git
# Binary release branch
LIMINE_BRANCH := v8.x-binary

# Default: build ISO
all: $(ISO)

# Create build directory
$(BUILD):
	mkdir -p $(BUILD)

# Assemble boot.S
$(BUILD)/boot.o: boot.S | $(BUILD)
	cc -c $< -o $@

# Link kernel
$(KERNEL): $(BUILD)/boot.o linker.ld
	ld -nostdlib -static -T linker.ld -o $@ $(BUILD)/boot.o

# Create bootable ISO
$(ISO): $(KERNEL) limine.conf | limine
	# Clean ISO root
	rm -rf $(ISO_ROOT)
	# Create boot directories
	mkdir -p $(ISO_ROOT)/boot/limine
	mkdir -p $(ISO_ROOT)/EFI/BOOT
	# Copy kernel
	cp $(KERNEL) $(ISO_ROOT)/boot/
	# Copy config
	cp limine.conf $(ISO_ROOT)/boot/limine/
	# Copy UEFI boot catalog
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	# Copy UEFI bootloader
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	# Create bootable ISO
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
# -s: GDB server on localhost:1234
# -S: pause until debugger connects
debug: $(ISO)
	qemu-system-x86_64 -bios $(OVMF) -cdrom $(ISO) -serial stdio -s -S

# Run without debugging
run: $(ISO)
	qemu-system-x86_64 -bios $(OVMF) -cdrom $(ISO) -serial stdio

# Remove build artifacts
clean:
	rm -rf $(BUILD)
