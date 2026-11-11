ENTRY(_start)
SECTIONS {
  . = 64M;   /* kernel di 64MB supaya 4MB..60MB bebas dipakai loader .exe (ImageBase 0x400000) */
  .multiboot : { KEEP(*(.multiboot)) }
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(COMMON) *(.bss*) }
  /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
