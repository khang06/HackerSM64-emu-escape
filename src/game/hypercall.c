#include <types.h>

u64 hypercall(u32 p1, u32 p2, u32 p3, u32 p4) {
    u64 res;
    __asm__ volatile("nop;"
                     //"move $a0, %1;"
                     //"move $a1, %2;"
                     //"move $a2, %3;"
                     //"move $a3, %4;"
                     ".byte 0x48, 0x00, 0x00, 0x00;"
                     "move $a0, $v0;" // DO NOT DO THIS EVER
                     "move $a1, $v1;"
                     : "=r"(res)
                     : "r"(p1), "r"(p2), "r"(p3), "r"(p4)
                     : "v0");
    return res;
}
