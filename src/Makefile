.PHONY: all clean run debug limine  # Non-file targets

BUILD = build                        # Build output directory
KERNEL = $(BUILD)/kernel.elf         # Kernel executable
ISO = $(BUILD)/seed.iso              # Bootable ISO image
ISO_ROOT = $(BUILD)/iso_root         # ISO filesystem root

OVMF = /usr/share/ovmf/OVMF.fd       # UEFI firmware
LIMINE_DIR = limine                  # Limine bootloader directory
LIMINE_REPO = https://github.com/limine-bootloader/limine.git
LIMINE_BRANCH = v8.x-binary          # Binary release branch

all: $(ISO)                          # Default: build ISO

$(BUILD):
	mkdir -p $(BUILD)                # Create build directory

$(BUILD)/boot.o: boot.S | $(BUILD)
	cc -c boot.S -o $@               # Assemble boot.S

$(KERNEL): $(BUILD)/boot.o linker.ld
	ld -nostdlib -static -T linker.ld -o $@ $(BUILD)/boot.o  # Link kernel

$(ISO): $(KERNEL) limine.conf | limine
	rm -rf $(ISO_ROOT)                             # Clean ISO root
	mkdir -p $(ISO_ROOT)/boot/limine               # Create boot directories
	mkdir -p $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL) $(ISO_ROOT)/boot/                 # Copy kernel
	cp limine.conf $(ISO_ROOT)/boot/limine/        # Copy config
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/  # UEFI boot catalog
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/            # UEFI bootloader
	xorriso -as mkisofs -R -r -J \                 # Create bootable ISO
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $(ISO) 2>/dev/null

limine:                                          # Fetch Limine if needed
	@if [ ! -d "$(LIMINE_DIR)" ]; then \
		echo "Cloning Limine bootloader..."; \
		git clone $(LIMINE_REPO) --branch=$(LIMINE_BRANCH) --depth=1; \
		$(MAKE) -C $(LIMINE_DIR); \
	fi

debug: $(ISO)                                    # Run with GDB server
	qemu-system-x86_64 -bios $(OVMF) -cdrom $(ISO) -serial stdio -s -S
	# -s: GDB server on localhost:1234, -S: pause until debugger connects

run: $(ISO)                                      # Run without debugging
	qemu-system-x86_64 -bios $(OVMF) -cdrom $(ISO) -serial stdio

clean:                                           # Remove build artifacts
	rm -rf $(BUILD)

# Clean build artifacts
clean:
	rm -rf $(BUILD)
