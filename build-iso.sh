#!/bin/bash
set -e

# Build the kernel first
make

# Clean and create ISO directory structure
rm -rf iso_root
mkdir -p iso_root/boot/limine
mkdir -p iso_root/EFI/BOOT

# Copy kernel and config
cp kernel.elf iso_root/boot/
cp limine.conf iso_root/boot/limine/

# Copy Limine files
cp limine/limine-bios.sys iso_root/boot/limine/
cp limine/limine-bios-cd.bin iso_root/boot/limine/
cp limine/limine-uefi-cd.bin iso_root/boot/limine/

# Copy Limine EFI executables
cp limine/BOOTX64.EFI iso_root/EFI/BOOT/
cp limine/BOOTIA32.EFI iso_root/EFI/BOOT/

# Create the bootable ISO
xorriso -as mkisofs -R -r -J \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -hfsplus -apm-block-size 2048 \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    iso_root -o myos.iso

# Install Limine stage 1 and 2 for legacy BIOS boot
./limine/limine bios-install myos.iso

echo ""
echo "Created myos.iso"
echo ""
echo "Boot with BIOS:"
echo "  qemu-system-x86_64 -cdrom myos.iso -nographic -serial mon:stdio"
echo ""
echo "Boot with UEFI:"
echo "  qemu-system-x86_64 -cdrom myos.iso -nographic -serial mon:stdio -bios /usr/share/ovmf/OVMF.fd"
