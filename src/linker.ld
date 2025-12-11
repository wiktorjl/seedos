ENTRY(_start)

KERNEL_VADDR = 0xFFFFFFFF80000000;

SECTIONS
{
	. = KERNEL_VADDR;

	.text : {
		*(.text .text.*)
	}

	. = ALIGN(4096);

	.rodata : {
		*(.rodata .rodata.*)
	}

	. = ALIGN(4096);
	
	.data : {
		*(.data .data.*)
	}
	
	/* Limine request structures - must be in loaded image and writable */
	.limine_requests : {
		*(.limine_requests)	
	}

	.bss : {
		*(COMMON)             /* Uninitialized globals from C */
		*(.bss .bss.*)
	}
}
