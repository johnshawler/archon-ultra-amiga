/* Linker script for the Archon Ultra Amiga image: one flat image linked at 0 and relocated by stage2 to the fast RAM block it was loaded to.
 * Layout on disk: .text .rodata .data .reloc (filled in by mkimage.py); .bss is cleared by stage2. */
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : { KEEP(*(.text.start)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(4);
    _reloc_start = .;
    .reloc : { LONG(0); . = . + 0x1FFFC; }
    _raw_end = .;
    .bss : { _bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(16); }
    _end = .;
    /DISCARD/ : { *(.comment) *(.note*) }
}
