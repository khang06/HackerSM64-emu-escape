    ; Thanks to zero318 for fixing up my bad ASM
    BITS 64
    %define STACK_RVA           0x6E04230
    %define GFX_RVA             0x6DF41E0
    %define BLOCKS_START_RVA    0x6DF4CC0
    %define BLOCKS_END_RVA      0x75F4CC0
    %define INVALID_START_RVA   0x75F4CE0
    %define INVALID_END_RVA     0x76F4CE0
    %define REGS_RVA            0x6DF4BC0
    %define GFX_PLUGIN_RVA      0x5CAE120
    %define RESERVED_INSTR_RVA  0x628418  ; cached_interpreter_table (.rdata)
    %define RESERVED_INSTR2_RVA 0x6DF4B78 ; current_instruction_table
    %define VI_CONTROLLER_RVA   0x61F2ED0
    %define SAVE_RSP_RVA        0xBAD6B0

    ; These are all from retroarch.exe
    %define RETROARCH_HWND      0x1395D68
    %define MessageBoxA         0x139EB2C
    %define VirtualProtect      0x139E2D4
    %define GetForegroundWindow 0x139EA8C
    %define PostMessageA        0x139EB44
    %define SetWindowPos        0x139EBC4
    %define GetWindowLongPtrA   0x139EADC
    %define AdjustWindowRect    0x139E9E4
    %define GetModuleHandleA    0x139E10C
    %define GetProcAddress      0x139E11C
start:
    lea     rbx, [rel memblock]
    ; Save the DLL base
    lea     rcx, [rsp-STACK_RVA]
    mov     [rbx+(m64pBase-memblock)], rcx

    ; Clean up the mess made by the ROP chain
    add     rcx, BLOCKS_START_RVA
    lea     rax, [rcx+BLOCKS_END_RVA-BLOCKS_START_RVA]
    xorps   xmm0, xmm0
block_zero_loop:
    movaps  [rcx], xmm0
    add     rcx, 0x10
    cmp     rcx, rax
    jne     block_zero_loop

    add     rcx, INVALID_START_RVA-BLOCKS_END_RVA
    lea     rax, [rcx+INVALID_END_RVA-INVALID_START_RVA]
    movaps  xmm0, [rbx+(allOnes-memblock)]
block_invalidate_loop:
    movaps  [rcx], xmm0
    add     rcx, 0x10
    cmp     rcx, rax
    jnz     block_invalidate_loop

    ; Fix up the clobbered gfx plugin function pointers
    mov     eax, [rcx-INVALID_END_RVA+GFX_PLUGIN_RVA]
    mov     rdx, [rax*8+rbx+(updateScreen-memblock)]
    add     rdx, rcx
    mov     [rcx-INVALID_END_RVA+GFX_RVA+0x48], rdx
    mov     rdx, [rax*8+rbx+(processDList-memblock)]
    add     rdx, rcx
    mov     [rcx-INVALID_END_RVA+GFX_RVA+0x20], rdx

    ; Restore the stack
    ; Fortunately, the dynarec saves the stack pointer right before executing
    mov     rdx, [rcx-INVALID_END_RVA+SAVE_RSP_RVA]
    lea     rsp, [rdx-0x88]

    ; Patch the cached interpreter reserved instruction handler
    push    rbp
    sub     rcx, INVALID_END_RVA-RESERVED_INSTR_RVA
    mov     rbp, rcx
    mov     edx, 8
    mov     r8d, 4       ; PAGE_READWRITE
    lea     r9, [rsp-8]
    sub     rsp, 0x28
    call    [VirtualProtect]
    add     rsp, 0x28
    lea     rax, [rbx+(hypercall_handler-memblock)]
    mov     [rbp], rax
    mov     [rbp-RESERVED_INSTR_RVA+RESERVED_INSTR2_RVA], rax

    ; Import any additional functions that aren't imported by retroarch
    sub     rsp, 0x28
    lea     rcx, [rbx+(user32_str-memblock)]
    call    [GetModuleHandleA]
    mov     rcx, rax
    lea     rdx, [rbx+(CTS_str-memblock)]
    call    [GetProcAddress]
    mov     [rbx+(ClientToScreen-memblock)], rax
    add     rsp, 0x28
    
    ; Restore rbx
    lea     rbx, [rbp-RESERVED_INSTR_RVA+VI_CONTROLLER_RVA]
    pop     rbp

    ; Message box
    xor     ecx, ecx
    lea     rdx, [rel initMsg]
    xor     r8, r8
    xor     r9, r9
    sub     rsp, 0x28
    call    [MessageBoxA]
    add     rsp, 0x28

    ret

align 16
hypercall_handler:
    mov     rax, [rel m64pBase]
    mov     rcx, [rax+REGS_RVA+4*8]
    lea     rax, [rel start] ; shellcode base, needs to be rip-relative!
    add     rax, [hypercall_table+rax+rcx*8]
    jmp     rax

align 16
hypercall_msg:
    sub     rsp, 0x28
    xor     ecx, ecx
    lea     rdx, [rel dummyMsg]
    xor     r8, r8
    xor     r9, r9
    call    [MessageBoxA]
    add     rsp, 0x28
    ret

align 16
hypercall_minimize:
    sub     rsp, 0x28
    call    [GetForegroundWindow]
    mov     rcx, rax
    mov     edx, 274 ; WM_SYSCOMMAND
    mov     r8d, 61472 ; SC_MINIMIZE
    xor     r9d, r9d
    call    [PostMessageA]
    add     rsp, 0x28
    ret

align 16
hypercall_crash:
    ud2

; void lol() {
;     // TODO: support fullscreen mode
;     RECT adjusted = {
;         .left = (LONG)regs[6] >> 16,
;         .top = (LONG)regs[6] & 0xFFFF,
;         .right = ((LONG)regs[6] >> 16) + ((LONG)regs[7] >> 16),
;         .bottom = ((LONG)regs[6] & 0xFFFF) + ((LONG)regs[7] & 0xFFFF),
;     };
;     AdjustWindowRect(&adjusted, GetWindowLongPtrA((HWND)(uint32_t)regs[5], GWL_STYLE), TRUE);
;     SetWindowPos((HWND)(uint32_t)regs[5], 0, adjusted.left, adjusted.top, adjusted.right - adjusted.left, adjusted.bottom - adjusted.top, SWP_NOACTIVATE | SWP_NOZORDER);
; }
align 16
hypercall_set_win_pos:
    push    rsi
    push    rdi
    sub     rsp, 72
    mov     rdi, [rel m64pBase]
    mov     ecx, [rdi + REGS_RVA + 40]
    mov     eax, [rdi + REGS_RVA + 48]
    mov     edx, eax
    sar     edx, 16
    lea     rsi, [rsp + 56]
    mov     [rsi], edx
    mov     r8d, 65535
    and     eax, r8d
    mov     [rsi + 4], eax
    mov     r9d, [rdi + REGS_RVA + 56]
    mov     r10d, r9d
    sar     r10d, 16
    add     r10d, edx
    mov     [rsi + 8], r10d
    and     r9d, r8d
    add     r9d, eax
    mov     [rsi + 12], r9d
    mov     edx, -16
    call    [GetWindowLongPtrA]
    mov     rcx, rsi
    mov     edx, eax
    mov     r8d, 1
    call    [AdjustWindowRect]
    mov     ecx, [rdi + REGS_RVA + 40]
    mov     r8d, [rsi]
    mov     r9d, [rsi + 4]
    mov     eax, [rsi + 8]
    sub     eax, r8d
    mov     edx, [rsi + 12]
    sub     edx, r9d
    mov     [rsp + 40], edx
    mov     [rsp + 32], eax
    mov     dword [rsp + 48], 0x14
    xor     edx, edx
    call    [SetWindowPos]
    add     rsp, 72
    pop     rdi
    pop     rsi
    ret

align 16
hypercall_shellcode_base:
    mov     rax, [rel m64pBase]
    lea     rcx, [rel start]
    mov     [rax+REGS_RVA+3*8], ecx
    shr     rcx, 32
    mov     [rax+REGS_RVA+2*8], ecx
    ret

align 16
hypercall_get_window:
    sub     rsp, 0x28
    mov     rax, [RETROARCH_HWND]
    mov     rcx, [rel m64pBase]
    mov     [rcx+REGS_RVA+3*8], eax
    shr     rax, 32
    mov     [rcx+REGS_RVA+2*8], eax
    add     rsp, 0x28
    ret

; void lol() {
;     POINT ret = {};
;     ClientToScreen((HWND)(uint32_t)regs[5], &ret);
;     regs[2] = ret.x;
;     regs[3] = ret.y;
; }
align 16
hypercall_get_win_pos:
    push    rsi
    push    rdi
    sub     rsp, 40
    lea     rsi, [rsp + 32]
    mov     qword [rsi], 0
    mov     rdi, [rel m64pBase]
    mov     ecx, [rdi + REGS_RVA + 40]
    mov     rdx, rsi
    call    [rel ClientToScreen]
    mov     eax, [rsi]
    mov     [rdi + REGS_RVA + 16], eax
    mov     eax, [rsi + 4]
    mov     [rdi + REGS_RVA + 24], eax
    add     rsp, 40
    pop     rdi
    pop     rsi
    ret

align 16
memblock:
allOnes:        dq  0x0101010101010101, 0x0101010101010101
updateScreen:   dq  0x2AD240-INVALID_END_RVA, 0x3E240-INVALID_END_RVA, 0x291A90-INVALID_END_RVA,
                dq  0x3062A0-INVALID_END_RVA, 0x1AAAB0-INVALID_END_RVA, 0x3FE90-INVALID_END_RVA
processDList:   dq  0x2F82F0-INVALID_END_RVA, 0x3E550-INVALID_END_RVA, 0x2919F0-INVALID_END_RVA,
                dq  0x3061B0-INVALID_END_RVA, 0x1AAA70-INVALID_END_RVA, 0x2F82F0-INVALID_END_RVA
m64pBase:       dq  0
ClientToScreen  dq  0
hypercall_table dq  hypercall_msg-start, hypercall_minimize-start, hypercall_crash-start, hypercall_set_win_pos-start
                dq  hypercall_shellcode_base-start, hypercall_get_window-start, hypercall_get_win_pos-start
initMsg:        dd  `Shellcode ran!`, 0
dummyMsg        dd  `Hypercall worked!`, 0
user32_str      dd  `user32.dll`
CTS_str         dd  `ClientToScreen`, 0
