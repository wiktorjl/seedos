#/bin/sh

# This script sets up and runs a QEMU virtual machine with UEFI firmware (OVMF)
# and mounts an EFI System Partition containing the host's kernel.
# QEMU's firmware, in the background, will turn the "esp" directory into a virtual
# EFI System Partition.

set -e

echo "Copying kernel to EFI System Partition..."
mkdir -p esp
cp /boot/vmlinuz-$(uname -r) esp/vmlinuz

OVMF=/usr/share/qemu/OVMF.fd
echo "Using OVMF firmware at $OVMF"

echo "Starting QEMU with OVMF and EFI System Partition..."
qemu-system-x86_64 \
    -bios $OVMF \
    -drive format=raw,file=fat:rw:esp \
    -m 1G
