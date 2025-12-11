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

# Step 1: Compile the kernel
echo "Compiling kernel..."
make
print_success "Kernel compiled"

# Step 2: Create ISO directory structure
echo "Creating ISO directory structure..."
rm -rf iso_root
mkdir -p iso_root/boot/limine
mkdir -p iso_root/EFI/BOOT

# Step 3: Copy kernel and config
echo "Copying kernel and bootloader config..."
cp kernel.elf iso_root/boot/
cp limine.conf iso_root/boot/limine/

# Step 4: Copy Limine bootloader files
echo "Copying Limine bootloader files..."
cp limine/limine-bios.sys iso_root/boot/limine/
cp limine/limine-bios-cd.bin iso_root/boot/limine/
cp limine/limine-uefi-cd.bin iso_root/boot/limine/

# Copy Limine EFI executables
cp limine/BOOTX64.EFI iso_root/EFI/BOOT/
cp limine/BOOTIA32.EFI iso_root/EFI/BOOT/

# Step 5: Create the bootable ISO
echo "Creating bootable ISO..."
xorriso -as mkisofs -R -r -J \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -hfsplus -apm-block-size 2048 \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    iso_root -o seed.iso 2>/dev/null

# Step 6: Install Limine for legacy BIOS boot
echo "Installing Limine BIOS stages..."
./limine/limine bios-install seed.iso 2>/dev/null

print_success "Build complete: seed.iso"
