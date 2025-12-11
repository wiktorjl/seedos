#!/bin/bash
#
# build.sh - Compile kernel and create bootable ISO
#
# Usage: ./scripts/build.sh
#        (or from scripts/: ./build.sh)
#

set -e

# Source common utilities
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

cd_to_root

print_header "BUILD"

# Build directory for all artifacts
BUILD_DIR="build"

# Step 1: Compile the kernel
echo "Compiling kernel..."
make
print_success "Kernel compiled"

# Step 2: Create ISO directory structure inside build/
echo "Creating ISO directory structure..."
rm -rf "$BUILD_DIR/iso_root"
mkdir -p "$BUILD_DIR/iso_root/boot/limine"
mkdir -p "$BUILD_DIR/iso_root/EFI/BOOT"

# Step 3: Copy kernel and config
echo "Copying kernel and bootloader config..."
cp "$BUILD_DIR/kernel.elf" "$BUILD_DIR/iso_root/boot/"
cp limine.conf "$BUILD_DIR/iso_root/boot/limine/"

# Step 4: Copy Limine bootloader files
echo "Copying Limine bootloader files..."
cp limine/limine-bios.sys "$BUILD_DIR/iso_root/boot/limine/"
cp limine/limine-bios-cd.bin "$BUILD_DIR/iso_root/boot/limine/"
cp limine/limine-uefi-cd.bin "$BUILD_DIR/iso_root/boot/limine/"

# Copy Limine EFI executables
cp limine/BOOTX64.EFI "$BUILD_DIR/iso_root/EFI/BOOT/"
cp limine/BOOTIA32.EFI "$BUILD_DIR/iso_root/EFI/BOOT/"

# Step 5: Create the bootable ISO
echo "Creating bootable ISO..."
xorriso -as mkisofs -R -r -J \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -hfsplus -apm-block-size 2048 \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    "$BUILD_DIR/iso_root" -o "$BUILD_DIR/seed.iso" 2>/dev/null

# Step 6: Install Limine for legacy BIOS boot
echo "Installing Limine BIOS stages..."
./limine/limine bios-install "$BUILD_DIR/seed.iso" 2>/dev/null

print_success "Build complete: $BUILD_DIR/seed.iso"
