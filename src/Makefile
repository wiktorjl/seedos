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

# Compiler flags for freestanding kernel
CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
          -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=kernel

# Assemble limine.S (boot protocol markers)
$(BUILD)/limine_asm.o: limine.S | $(BUILD)
	cc -c $< -o $@

# Compile limine.c (boot protocol requests)
$(BUILD)/limine.o: limine.c limine.h | $(BUILD)
	cc $(CFLAGS) -c $< -o $@

# Convert seedos.png to raw BGRA
seedos.bin seedos.h: seedos.png convert-image.sh
	./convert-image.sh seedos.png seedos.bin

# Assemble boot.S (entry point)
$(BUILD)/boot.o: boot.S font.bin seedos.bin | $(BUILD)
	cc -c boot.S -o $@

# Compile console.c
$(BUILD)/console.o: console.c console.h limine.h | $(BUILD)
	cc $(CFLAGS) -c $< -o $@

# Compile kernel.c
$(BUILD)/kernel.o: kernel.c limine.h console.h seedos.h | $(BUILD)
	cc $(CFLAGS) -c $< -o $@

# Link kernel
$(KERNEL): $(BUILD)/limine_asm.o $(BUILD)/limine.o $(BUILD)/boot.o $(BUILD)/console.o $(BUILD)/kernel.o linker.ld
	ld -nostdlib -static -T linker.ld -o $@ $(BUILD)/limine_asm.o $(BUILD)/limine.o $(BUILD)/boot.o $(BUILD)/console.o $(BUILD)/kernel.o

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
	rm -rf $(BUILD) seedos.bin seedos.h
