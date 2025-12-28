.PHONY: all clean run debug limine

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
CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
          -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=kernel

# Default target
all: $(ISO)

$(BUILD):
	mkdir -p $(BUILD)

# Convert logo image to raw BGRA
$(BUILD)/seedos.bin $(BUILD)/seedos.h: $(DATA)/seedos.png $(SCRIPTS)/convert-image.sh | $(BUILD)
	$(SCRIPTS)/convert-image.sh $(DATA)/seedos.png $(BUILD)/seedos.bin

# Assemble limine.S (boot protocol markers)
$(BUILD)/limine_asm.o: $(SRC)/limine.S | $(BUILD)
	cc -c $< -o $@

# Compile limine.c (boot protocol requests)
$(BUILD)/limine.o: $(SRC)/limine.c $(SRC)/limine.h | $(BUILD)
	cc $(CFLAGS) -c $< -o $@

# Assemble boot.S (entry point and embedded data)
$(BUILD)/boot.o: $(SRC)/boot.S $(DATA)/font.bin $(BUILD)/seedos.bin | $(BUILD)
	cc -I$(BUILD) -I$(DATA) -c $< -o $@

# Compile console.c
$(BUILD)/console.o: $(SRC)/console.c $(SRC)/console.h $(SRC)/limine.h | $(BUILD)
	cc $(CFLAGS) -c $< -o $@

# Compile io.c
$(BUILD)/io.o: $(SRC)/io.c $(SRC)/io.h $(SRC)/config.h $(SRC)/console.h | $(BUILD)
	cc $(CFLAGS) -c $< -o $@

# Compile log.c
$(BUILD)/log.o: $(SRC)/log.c $(SRC)/log.h $(SRC)/config.h $(SRC)/console.h | $(BUILD)
	cc $(CFLAGS) -c $< -o $@

# Compile kernel.c
$(BUILD)/kernel.o: $(SRC)/kernel.c $(SRC)/limine.h $(SRC)/console.h $(BUILD)/seedos.h $(SRC)/io.h $(SRC)/log.h | $(BUILD)
	cc $(CFLAGS) -I$(BUILD) -c $< -o $@

# Object files
OBJS := $(BUILD)/limine_asm.o $(BUILD)/limine.o $(BUILD)/boot.o \
        $(BUILD)/console.o $(BUILD)/io.o $(BUILD)/log.o $(BUILD)/kernel.o

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

# Run with GDB server
debug: $(ISO)
	qemu-system-x86_64 -bios $(OVMF) -cdrom $(ISO) -serial stdio -s -S

# Run without debugging
run: $(ISO)
	qemu-system-x86_64 -bios $(OVMF) -cdrom $(ISO) -serial stdio

# Remove build artifacts
clean:
	rm -rf $(BUILD)
