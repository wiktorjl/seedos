file build/kernel.elf
target remote localhost:1234    

set history save on
set history filename ~/.gdb_history

break _start
layout split
layout regs
continue  
