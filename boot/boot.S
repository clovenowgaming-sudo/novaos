/* Nova OS - Multiboot2 entry + long mode + IRQ/exception stubs + PE trampolines (GNU as, Intel syntax) */
    .intel_syntax noprefix
    .section .multiboot, "a"
    .align 8
mb2_start:
    .long 0xE85250D6
    .long 0
    .long mb2_end - mb2_start
    .long 0x100000000 - (0xE85250D6 + (mb2_end - mb2_start))
    .align 8
    .short 5, 0
    .long 20
    .long 1024, 768, 32
    .align 8
    .short 0, 0
    .long 8
mb2_end:

    .section .text
    .code32
    .globl _start
_start:
    cli
    mov esp, offset stack_top
    mov edi, ebx
    mov eax, offset pdpt
    or eax, 3
    mov DWORD PTR [pml4], eax
    xor ecx, ecx
1:  mov eax, ecx
    shl eax, 12
    add eax, offset pd
    or eax, 3
    mov DWORD PTR [pdpt + ecx*8], eax
    inc ecx
    cmp ecx, 4
    jne 1b
    xor ecx, ecx
2:  mov eax, ecx
    shl eax, 21
    or eax, 0x83
    mov DWORD PTR [pd + ecx*8], eax
    mov DWORD PTR [pd + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 2048
    jne 2b
    mov eax, offset pml4
    mov cr3, eax
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    lgdt [gdt64_ptr]
    ljmp 0x08, offset long_mode

    .code64
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    lea rsp, [rip + stack_top]
    mov edi, edi
    call kmain
.hang:
    hlt
    jmp .hang

/* ---- IRQ stubs ---- */
    .globl isr_kbd, isr_mouse, isr_ignore
.macro ISR name, fn
\name:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    cld
    call \fn
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    iretq
.endm
ISR isr_kbd, irq1_handler
ISR isr_mouse, irq12_handler
isr_ignore:
    iretq

/* ---- CPU exception stubs (vector 0-31) ---- */
.macro EXC_N n
isr_exc\n:
    push 0
    push \n
    jmp exc_common
.endm
.macro EXC_E n
isr_exc\n:
    push \n
    jmp exc_common
.endm
EXC_N 0
EXC_N 1
EXC_N 2
EXC_N 3
EXC_N 4
EXC_N 5
EXC_N 6
EXC_N 7
EXC_E 8
EXC_N 9
EXC_E 10
EXC_E 11
EXC_E 12
EXC_E 13
EXC_E 14
EXC_N 15
EXC_N 16
EXC_E 17
EXC_N 18
EXC_N 19
EXC_N 20
EXC_E 21
EXC_N 22
EXC_N 23
EXC_N 24
EXC_N 25
EXC_N 26
EXC_N 27
EXC_N 28
EXC_E 29
EXC_E 30
EXC_N 31
exc_common:
    cld
    mov rdi, [rsp]          /* vector */
    mov rsi, [rsp+8]        /* error code */
    mov rdx, [rsp+16]       /* rip */
    mov rcx, cr2
    and rsp, -16
    call exc_handler
1:  hlt
    jmp 1b

/* ---- PE support ---- */
    .globl pe_top_rsp, pe_ret_pop
/* u64 pe_enter64(entry rdi, stack_top rsi, arg rdx) */
    .globl pe_enter64
pe_enter64:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rip + pe_top_rsp], rsp
    mov rax, rdi
    lea rsp, [rsi - 32]
    mov rcx, rdx
    call rax
    mov rsp, [rip + pe_top_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

/* void pe_longjmp(u64 code): leave PE (ExitProcess / crash) */
    .globl pe_longjmp
pe_longjmp:
    mov rax, rdi
    mov rsp, [rip + pe_top_rsp]
    sti
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

/* u32 pe_cb32(fn rdi, u32 *args rsi, nargs rdx, new_stack rcx(0 = keep)) : call 32-bit code in compat mode */
    .globl pe_cb32
pe_cb32:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp
    test rcx, rcx
    jz 1f
    mov [rip + pe_top_rsp], rsp
    mov rsp, rcx
1:  mov eax, edx
    shl eax, 2
    sub rsp, rax
    and rsp, -16
    xor ecx, ecx
2:  cmp ecx, edx
    jae 3f
    mov r8d, [rsi + rcx*4]
    mov [rsp + rcx*4], r8d
    inc ecx
    jmp 2b
3:  mov eax, edi
    lea r8, [rip + pe_cb32_in]
    sub rsp, 16
    mov [rsp], r8
    mov QWORD PTR [rsp + 8], 0x18
    lretq
    .code32
pe_cb32_in:
    mov bx, 0x10
    mov ds, bx
    mov es, bx
    mov bx, 0x20
    mov fs, bx
    mov ebp, esp
    call eax
    mov esp, ebp
    ljmp 0x08, offset pe_cb32_out
    .code64
pe_cb32_out:
    mov rsp, r12
    mov eax, eax
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

/* x86 import thunk: stub does "push idx; jmp thunk32_common" */
    .globl thunk32_common
    .code32
thunk32_common:
    push esi
    push edi
    push ebp
    mov ebp, esp
    ljmp 0x08, offset thunk32_64
    .code64
thunk32_64:
    mov ebp, ebp
    mov esp, esp
    mov edi, [rbp + 12]
    lea rsi, [rbp + 20]
    and rsp, -16
    cld
    call w32_dispatch
    mov rsp, rbp
    lea rcx, [rip + thunk32_ret]
    sub rsp, 16
    mov [rsp], rcx
    mov QWORD PTR [rsp + 8], 0x18
    lretq
    .code32
thunk32_ret:
    pop ebp
    pop edi
    pop esi
    add esp, 4
    mov ecx, [pe_ret_pop]
    pop edx
    add esp, ecx
    jmp edx

/* x64 import thunk (MS ABI in, SysV C out). stub: mov eax, idx ; mov r11, thunk64_common ; jmp r11 */
    .code64
    .globl thunk64_common
thunk64_common:
    push rbp
    mov rbp, rsp
    push rsi
    push rdi
    sub rsp, 144
    mov r10d, eax
    mov [rsp], rcx
    mov [rsp + 8], rdx
    mov [rsp + 16], r8
    mov [rsp + 24], r9
    xor r11d, r11d
1:  mov rax, [rbp + 48 + r11*8]
    mov [rsp + 32 + r11*8], rax
    inc r11d
    cmp r11d, 12
    jb 1b
    mov edi, r10d
    mov rsi, rsp
    cld
    call w64_dispatch
    lea rsp, [rbp - 16]
    pop rdi
    pop rsi
    pop rbp
    ret

/* exception / irq tables for C */
    .section .data
    .align 16
    .globl gdt64, exc_table
gdt64:
    .quad 0
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt64_end:
gdt64_ptr:
    .short gdt64_end - gdt64 - 1
    .long gdt64
    .align 8
pe_top_rsp: .quad 0
pe_ret_pop: .long 0
    .align 8
exc_table:
    .quad isr_exc0, isr_exc1, isr_exc2, isr_exc3, isr_exc4, isr_exc5, isr_exc6, isr_exc7
    .quad isr_exc8, isr_exc9, isr_exc10, isr_exc11, isr_exc12, isr_exc13, isr_exc14, isr_exc15
    .quad isr_exc16, isr_exc17, isr_exc18, isr_exc19, isr_exc20, isr_exc21, isr_exc22, isr_exc23
    .quad isr_exc24, isr_exc25, isr_exc26, isr_exc27, isr_exc28, isr_exc29, isr_exc30, isr_exc31

    .section .bss
    .align 4096
    .globl pml4, pdpt, pd
pml4: .skip 4096
pdpt: .skip 4096
pd:   .skip 4096*4
    .align 16
    .skip 262144
stack_top:
