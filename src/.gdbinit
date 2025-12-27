# Auto-loaded by GDB when started from this directory

file build/kernel.elf              # Load kernel with symbols
target remote localhost:1234       # Connect to QEMU GDB server

set history save on                # Enable command history
set history filename ~/.gdb_history

break _start                       # Break at entry point
layout split                       # Show source/asm
layout regs                        # Show registers
continue                           # Run to breakpoint
