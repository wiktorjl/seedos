git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1

find /usr/share -name "OVMF*.fd" 2>/dev/null
/usr/share/qemu/OVMF.fd

mkdir -p iso_root/boot/limine
mkdir -p iso_root/EFI/BOOT
cp limine/limine-uefi-cd.bin iso_root/boot/limine/
cp limine/BOOTX64.EFI iso_root/EFI/BOOT
touch iso_root/limine.conf

xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    -o limine-only.iso iso_root

qemu-system-x86_64 \
    -bios /usr/share/qemu/OVMF.fd \
    -cdrom limine-only.iso \
    -serial stdio
