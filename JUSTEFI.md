# Booting with Pure UEFI (No Bootloader)

This guide demonstrates booting kernels directly via UEFI without a bootloader like Limine.

## Prerequisites

```bash
sudo apt install qemu-system-x86 ovmf busybox-static
```

## Part 1: Boot Linux Kernel Without Filesystem

Linux with `CONFIG_EFI_STUB` is itself a valid EFI executable. We can boot it directly.

### Step 1: Get a kernel with EFI stub

```bash
# Use your distro's kernel (already has EFI stub)
cp /boot/vmlinuz-$(uname -r) vmlinuz

# Or download a minimal one
# curl -LO https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.tar.xz
```

### Step 2: Create EFI boot structure

```bash
mkdir -p esp/EFI/BOOT
cp vmlinuz esp/EFI/BOOT/BOOTX64.EFI
```

### Step 3: Create FAT image for EFI System Partition

```bash
dd if=/dev/zero of=esp.img bs=1M count=64
mkfs.fat -F 32 esp.img
mmd -i esp.img ::/EFI
mmd -i esp.img ::/EFI/BOOT
mcopy -i esp.img vmlinuz ::/EFI/BOOT/BOOTX64.EFI
```

### Step 4: Boot with QEMU UEFI

```bash
qemu-system-x86_64 \
    -bios /usr/share/OVMF/OVMF_CODE.fd \
    -drive format=raw,file=esp.img \
    -nographic \
    -append "console=ttyS0"
```

**Expected result:** Kernel boots, panics with "No init found" (no rootfs).

---

## Part 2: Add initrd with BusyBox Shell

### Step 1: Create minimal initramfs with BusyBox

```bash
mkdir -p initramfs/{bin,sbin,etc,proc,sys,dev,tmp}

# Copy busybox static binary
cp /bin/busybox initramfs/bin/
cd initramfs/bin
for cmd in sh ls cat echo mount mkdir; do
    ln -s busybox $cmd
done
cd ../..
```

### Step 2: Create init script

```bash
cat > initramfs/init << 'EOF'
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

echo "Welcome to minimal Linux!"
exec /bin/sh
EOF
chmod +x initramfs/init
```

### Step 3: Pack initramfs

```bash
cd initramfs
find . | cpio -H newc -o | gzip > ../initramfs.cpio.gz
cd ..
```

### Step 4: Update ESP with kernel and initrd

```bash
mcopy -i esp.img vmlinuz ::/vmlinuz
mcopy -i esp.img initramfs.cpio.gz ::/initramfs.cpio.gz
```

### Step 5: Create EFI startup script

```bash
cat > startup.nsh << 'EOF'
vmlinuz initrd=initramfs.cpio.gz console=ttyS0 rdinit=/init
EOF
mcopy -i esp.img startup.nsh ::/startup.nsh
```

### Step 6: Boot with initramfs

```bash
qemu-system-x86_64 \
    -bios /usr/share/OVMF/OVMF_CODE.fd \
    -drive format=raw,file=esp.img \
    -nographic \
    -m 512M
```

**Expected result:** Linux boots to a BusyBox shell. Type `ls`, `cat /proc/cpuinfo`, etc.

---

## Part 3: Boot Our Kernel (EFI Stub Version)

To boot our noop kernel directly from UEFI, we must compile it as an EFI application.

### Step 1: Create EFI kernel source

```bash
cat > kernel_efi.c << 'EOF'
#include <stdint.h>

// Minimal EFI types
typedef void *EFI_HANDLE;
typedef void *EFI_SYSTEM_TABLE;
typedef uint64_t EFI_STATUS;

// EFI application entry point
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    // Infinite loop - our "kernel"
    while (1) {
        __asm__ volatile ("hlt");
    }
    return 0;
}
EOF
```

### Step 2: Create linker script for EFI

```bash
cat > efi.ld << 'EOF'
ENTRY(efi_main)

SECTIONS {
    . = 0;

    .text : {
        *(.text*)
    }

    .data : {
        *(.data*)
        *(.rodata*)
    }

    .bss : {
        *(.bss*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
EOF
```

### Step 3: Compile as EFI PE32+ executable

```bash
# Compile to object file
gcc -c -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
    -DEFI_FUNCTION_WRAPPER -o kernel_efi.o kernel_efi.c

# Link as shared object first
ld -nostdlib -znocombreloc -T efi.ld -shared -Bsymbolic \
    -o kernel_efi.so kernel_efi.o

# Convert to PE format using objcopy
objcopy -j .text -j .data -j .bss -j .rodata \
    --target=efi-app-x86_64 kernel_efi.so kernel.efi
```

### Step 4: Update ESP with our kernel

```bash
mcopy -oi esp.img kernel.efi ::/EFI/BOOT/BOOTX64.EFI
```

### Step 5: Boot our kernel

```bash
qemu-system-x86_64 \
    -bios /usr/share/OVMF/OVMF_CODE.fd \
    -drive format=raw,file=esp.img \
    -nographic \
    -s -S &

# In another terminal, attach GDB
gdb -ex "target remote :1234" -ex "break efi_main" -ex "continue"
```

**Expected result:** QEMU halts in our infinite loop. GDB shows we're in `efi_main`.

---

## Quick Reference: All-in-One Script

```bash
#!/bin/bash
# Build everything from scratch

set -e

# Create directories
mkdir -p justefi && cd justefi

# === PART 3: Our EFI Kernel ===

cat > kernel_efi.c << 'KERNEL'
#include <stdint.h>
typedef void *EFI_HANDLE;
typedef void *EFI_SYSTEM_TABLE;
typedef uint64_t EFI_STATUS;

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    while (1) { __asm__ volatile ("hlt"); }
    return 0;
}
KERNEL

# Compile EFI application
gcc -c -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
    -o kernel_efi.o kernel_efi.c
ld -nostdlib -znocombreloc -shared -Bsymbolic \
    -e efi_main -o kernel_efi.so kernel_efi.o
objcopy -j .text -j .data -j .bss \
    --target=efi-app-x86_64 kernel_efi.so kernel.efi

# Create ESP
dd if=/dev/zero of=esp.img bs=1M count=64
mkfs.fat -F 32 esp.img
mmd -i esp.img ::/EFI
mmd -i esp.img ::/EFI/BOOT
mcopy -i esp.img kernel.efi ::/EFI/BOOT/BOOTX64.EFI

echo "Run with:"
echo "qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd -drive format=raw,file=esp.img -nographic -s -S"
```

---

## Notes

- **EFI stub**: Linux kernel compiled with `CONFIG_EFI_STUB=y` can be executed directly by UEFI firmware
- **PE32+ format**: UEFI expects Windows PE executable format, not ELF
- **No bootloader needed**: UEFI itself acts as a bootloader - it can load and execute EFI applications
- **startup.nsh**: EFI shell script that runs automatically (like autoexec.bat)
- **initramfs**: Compressed cpio archive containing minimal root filesystem loaded into RAM
