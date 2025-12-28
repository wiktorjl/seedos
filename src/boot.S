# SeedOS entry point and startup code

# Font data (Spleen 8x16 bitmap font)
.section .rodata
.global font_data
font_data:
    .incbin "font.bin"

# Entry point
.section .text
.global _start
.extern kmain
.extern limine_base_revision

_start:
    # Check if Limine modified the base revision marker
    # If unchanged, our requested protocol version is not supported
    mov limine_base_revision+8(%rip), %rax
    mov $0x6a7b384944536bdc, %rcx
    cmp %rcx, %rax
    je .not_supported

    # Call kernel main
    call kmain

.halt:
    hlt
    jmp .halt

.not_supported:
    jmp .not_supported
