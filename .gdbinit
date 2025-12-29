# GDB init for SeedOS kernel debugging

# Connect to QEMU's GDB server
target remote localhost:1234

# Load kernel symbols
symbol-file build/kernel.elf

# Useful breakpoints
break kmain

# Display settings
set disassembly-flavor intel
set print pretty on

# Enable TUI mode with source, disassembly, and registers
tui enable
layout split
tui reg general

# Fix arrow keys for command history in TUI mode
# Use Ctrl+P/Ctrl+N as alternative, or press Ctrl+X O to switch focus
set tui active-border-mode bold
focus cmd

# Helpful aliases
define regs
    info registers
end

define stack
    x/16xg $rsp
end

define code
    x/10i $rip
end
