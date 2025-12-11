#!/bin/bash
size=$(stty size)
qemu-system-x86_64 -cdrom myos.iso -nographic -serial mon:stdio -bios /usr/share/ovmf/OVMF.fd 2>&1 | tee output.log
printf '\e[8;%d;%dt' ${size%% *} ${size##* }
