Hello, World! Let's write an OS!

# Current status

In Step 0, we have explored UEFI and learned how to use it to boot Linux kernel. Now it is time to take the next incremental step and instead of Linux, we will boot our own minimal kernel. We will obtain a proof that it boots and works by attaching a debugger to it. In the subsequent step, we will build upon this solution by improving and solidyfing a bit more the the build scripts.

## What are we adding?

### boos.S
### kernel.c
### linker.ld
### Makefile

## Step 1

1. Boot our own kernel in debug mode.
2. Set up a breakpoint at kernel_main (kernel.c)
3. Attach GDB to Qemu
4. Run the OS and break at kernel_main
5. Observe our kernel loop