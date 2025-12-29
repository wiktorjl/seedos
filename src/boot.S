# SeedOS entry point and embedded data

.section .rodata

.global font_data
font_data:
    .incbin "font.bin"      # Spleen 8x16 bitmap font (256 glyphs, 16 bytes each)

.global logo_data
logo_data:
    .incbin "logo.bin"      # Logo image (raw BGRA pixels)

.section .text
.global _start
.extern kmain
.extern limine_base_revision

_start:
    # Limine stores [magic0, magic1, revision] at limine_base_revision.
    # On success, Limine replaces magic1 (offset +8) with the supported revision.
    # If magic1 is unchanged, the bootloader doesn't support our protocol version.
    mov limine_base_revision+8(%rip), %rax
    mov $0x6a7b384944536bdc, %rcx   # Original magic1 value
    cmp %rcx, %rax
    je .halt                        # Halt if protocol unsupported

    xor %rbp, %rbp                  # Zero frame pointer to terminate backtrace
    call kmain

.halt:
    hlt
    jmp .halt
