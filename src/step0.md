# Booting Your Own Kernel: From Linux to Bare Metal

In this chapter, you'll boot a kernel. Then you'll understand how it worked. Then you'll write your own.

We're going to use QEMU with UEFI firmware so you can experiment without risking your actual hardware. Everything here applies equally to real machines.

## Prerequisites

Install the required tools:

```bash
sudo apt install qemu-system-x86 ovmf
```

Create a working directory:

```bash
mkdir -p ~/kernel-boot && cd ~/kernel-boot
```

## Part 1: Boot Linux from the EFI Shell

Let's prove that booting a kernel is as simple as running an executable.

### Step 1.1: Create an EFI System Partition Image

UEFI boots from a FAT32 partition. We'll create a small disk image:

```bash
# Create a 256MB disk image
dd if=/dev/zero of=disk.img bs=1M count=256

# Create a GPT partition table with an EFI System Partition
parted disk.img --script mklabel gpt
parted disk.img --script mkpart EFI fat32 1MiB 100%
parted disk.img --script set 1 esp on

# Set up a loop device and format the partition
sudo losetup -fP disk.img
LOOP=$(losetup -j disk.img | cut -d: -f1)
sudo mkfs.fat -F32 ${LOOP}p1

# Mount it
mkdir -p mnt
sudo mount ${LOOP}p1 mnt
```

### Step 1.2: Copy Your Kernel

Copy your system's kernel to the EFI partition:

```bash
sudo cp /boot/vmlinuz-$(uname -r) mnt/vmlinuz
sudo umount mnt
sudo losetup -d $LOOP
```

### Step 1.3: Locate Your OVMF Firmware

OVMF lives in different places depending on your distribution:

```bash
# Find it on your system
find /usr -name "OVMF*.fd" 2>/dev/null
```

Common locations:

| Distribution | Path |
|--------------|------|
| Debian/Ubuntu | `/usr/share/OVMF/OVMF_CODE.fd` |
| Fedora | `/usr/share/edk2/ovmf/OVMF_CODE.fd` |
| Arch | `/usr/share/edk2-ovmf/x64/OVMF_CODE.fd` |

Set a variable for convenience:

```bash
OVMF=/usr/share/OVMF/OVMF_CODE.fd  # adjust to your path
```

### Step 1.4: Boot into the EFI Shell

```bash
qemu-system-x86_64 \
    -bios $OVMF \
    -drive file=disk.img,format=raw \
    -m 1G
```

A window opens showing the UEFI firmware. Press `Esc` to enter the boot menu, or wait—it should drop you into the EFI shell.

### Step 1.5: Boot the Kernel

At the EFI shell prompt (`Shell>`), type:

```
fs0:
vmlinuz
```

Watch your screen. The kernel initializes, probes hardware, brings up drivers... then panics:

```
Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0)
```

**This is success.** You just ran a kernel like any other program. No bootloader. No GRUB. Just an executable. The kernel ran thousands of lines of initialization code—it just couldn't find a root filesystem because we didn't give it one.

Close the QEMU window, or press `Ctrl-Alt-G` to release mouse grab, then close it.

## Part 2: Boot Linux to a Shell

The panic proved the kernel runs. Now let's give it a filesystem so it can complete the boot.

We'll build a minimal initramfs from scratch—just busybox and an init script. No host-specific dependencies.

### Step 2.1: Install BusyBox

```bash
sudo apt install busybox-static
```

### Step 2.2: Create the initramfs Structure

```bash
mkdir -p initramfs/{bin,sbin,etc,proc,sys,dev}
cp /bin/busybox initramfs/bin/
```

### Step 2.3: Create Symlinks for Shell Commands

```bash
cd initramfs/bin
for cmd in sh ls cat echo mount mkdir; do
    ln -s busybox $cmd
done
cd ../..
```

### Step 2.4: Create an init Script

The kernel runs `/init` as PID 1. Create `initramfs/init`:

```bash
cat > initramfs/init << 'EOF'
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

echo "Welcome to your minimal Linux system!"
echo "Kernel: $(uname -r)"
echo
exec /bin/sh
EOF

chmod +x initramfs/init
```

### Step 2.5: Pack the initramfs

```bash
cd initramfs
find . | cpio -o -H newc | gzip > ../initrd.img
cd ..
```

### Step 2.6: Copy to the EFI Partition

```bash
sudo losetup -fP disk.img
LOOP=$(losetup -j disk.img | cut -d: -f1)
sudo mount ${LOOP}p1 mnt
sudo cp initrd.img mnt/
sudo umount mnt
sudo losetup -d $LOOP
```

### Step 2.7: Boot with initramfs

```bash
qemu-system-x86_64 \
    -bios $OVMF \
    -drive file=disk.img,format=raw \
    -m 1G
```

At the EFI shell:

```
fs0:
vmlinuz initrd=initrd.img
```

This time, Linux boots all the way to a shell prompt. You're in a minimal userspace running on bare metal. Type `ls /`, `cat /proc/cpuinfo`, or `uname -a`.

To exit, type `poweroff -f` or just close the QEMU window.

## Part 3: Why This Worked

You've now seen both outcomes:

- **Without initramfs** — Kernel panics looking for a root filesystem
- **With initramfs** — Kernel mounts the in-memory filesystem and runs `/init`

The initramfs doesn't have to be complex. Ours was ~1MB with just busybox. A production initramfs adds hardware detection, encrypted disk unlocking, and filesystem drivers—but the mechanism is identical.

> **Note:** We built our own initramfs rather than using your host's because your system's initramfs contains scripts expecting specific disk layouts (encrypted partitions, UUIDs). It would fail in QEMU the same way it would fail on different hardware.

But here's the deeper question: how did vmlinuz run as an executable?

### The EFI Stub

Modern Linux kernels are compiled with `CONFIG_EFI_STUB=y`. This wraps the kernel in a PE/COFF executable—the same format as Windows .exe files, which is what UEFI expects.

The structure looks like this:

```
┌─────────────────────────────┐
│  PE/COFF Header             │  ← UEFI reads this
│  (MZ magic, PE signature)   │
├─────────────────────────────┤
│  EFI Stub Code              │  ← Entry point: handles boot services
│  - Parse command line       │
│  - Load initrd into memory  │
│  - Exit boot services       │
│  - Jump to kernel entry     │
├─────────────────────────────┤
│  Compressed Linux Kernel    │  ← The actual kernel
└─────────────────────────────┘
```

When UEFI loads vmlinuz, it:

1. Reads the PE header
2. Finds the entry point (the EFI stub)
3. Calls it like any EFI application
4. The stub does the setup, then hands off to the real kernel

## Part 4: Examine vmlinuz Yourself

Before writing your own stub, let's prove that vmlinuz really is just a PE executable.

### Step 4.1: Check the File Type

```bash
file /boot/vmlinuz-$(uname -r)
```

Output:

```
/boot/vmlinuz-6.x.x: Linux kernel x86 boot executable bzImage, version 6.x.x...
```

The `file` command recognizes the Linux-specific parts. Let's look deeper.

### Step 4.2: Find the PE Header

```bash
xxd /boot/vmlinuz-$(uname -r) | head -20
```

You'll see `MZ` at the start—the DOS header magic bytes. Every PE executable starts with this, including vmlinuz.

### Step 4.3: Examine with objdump

```bash
objdump -f /boot/vmlinuz-$(uname -r)
```

Output will show:

```
/boot/vmlinuz-6.x.x:     file format pei-x86-64
architecture: i386:x86-64, flags 0x00000102:
EXEC_P, D_PAGED
start address 0x...
```

This is the entry point—the address where UEFI jumps to start execution. Your stub will have one too.

## Part 5: Write Your Own EFI Application

Now you understand what vmlinuz is. Let's write our own bare-metal program.

### Step 5.1: Install the Toolchain

```bash
sudo apt install gnu-efi
```

This provides headers and libraries for writing EFI applications.

### Step 5.2: Write the Code

Create `hello.c`:

```c
#include <efi.h>
#include <efilib.h>

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    
    Print(L"Hello from bare metal!\n");
    Print(L"No operating system. Just you and the CPU.\n");
    Print(L"\nPress any key to shut down...\n");
    
    // Wait for keypress
    EFI_INPUT_KEY Key;
    SystemTable->BootServices->WaitForEvent(
        1, 
        &SystemTable->ConIn->WaitForKey, 
        NULL
    );
    SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key);
    
    // Shutdown
    SystemTable->RuntimeServices->ResetSystem(
        EfiResetShutdown, 
        EFI_SUCCESS, 
        0, 
        NULL
    );
    
    return EFI_SUCCESS;
}
```

### Step 5.3: Create a Makefile

Create `Makefile`:

```makefile
ARCH = x86_64
GNUEFI = /usr/include/efi
EFILIB = /usr/lib

CFLAGS = -I$(GNUEFI) -I$(GNUEFI)/$(ARCH) -I$(GNUEFI)/protocol \
         -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
         -Wall -DEFI_FUNCTION_WRAPPER

LDFLAGS = -nostdlib -znocombreloc -T /usr/lib/elf_$(ARCH)_efi.lds \
          -shared -Bsymbolic -L$(EFILIB) $(EFILIB)/crt0-efi-$(ARCH).o

all: hello.efi

hello.so: hello.o
	ld $(LDFLAGS) hello.o -o hello.so -lefi -lgnuefi

hello.efi: hello.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	        -j .rel -j .rela -j .reloc \
	        --target=efi-app-$(ARCH) hello.so hello.efi

hello.o: hello.c
	gcc $(CFLAGS) -c hello.c -o hello.o

clean:
	rm -f hello.o hello.so hello.efi
```

### Step 5.4: Build It

```bash
make
```

You now have `hello.efi`—your first bare-metal program.

### Step 5.5: Verify It's a PE Executable

```bash
file hello.efi
objdump -f hello.efi
```

Same format as vmlinuz. Same entry point structure. Different code inside.

### Step 5.6: Boot It

Mount your disk image and copy the EFI application:

```bash
sudo losetup -fP disk.img
LOOP=$(losetup -j disk.img | cut -d: -f1)
sudo mount ${LOOP}p1 mnt
sudo cp hello.efi mnt/
sudo umount mnt
sudo losetup -d $LOOP
```

Boot QEMU:

```bash
qemu-system-x86_64 \
    -bios $OVMF \
    -drive file=disk.img,format=raw \
    -m 1G
```

At the EFI shell:

```
fs0:
hello.efi
```

Your message appears on screen. Your code is running on bare metal—no Linux, no Windows, nothing between you and the hardware except UEFI firmware.

## Part 6: What You've Learned

1. **vmlinuz is an EFI application** — The kernel includes a stub that makes it directly bootable by UEFI
2. **UEFI is a runtime** — It provides services (console, filesystem, memory allocation) until you tell it to stop
3. **You can write bare-metal code** — The same toolchain that builds vmlinuz can build your programs
4. **PE/COFF is the universal format** — UEFI uses Windows executable format, which is why cross-platform booting works

## What's Next

Your hello.efi runs inside UEFI's protected environment. UEFI is still handling memory management, providing console services, and maintaining control of the hardware.

In the next chapter, we'll:

1. **Exit Boot Services** — Tell UEFI we're taking over
2. **Set up our own page tables** — Direct control of virtual memory
3. **Switch to Long Mode** — Enter 64-bit mode properly
4. **Write a kernel entry point** — Jump from EFI stub to kernel code

The stub you just wrote is the foundation. Everything else builds on this.
