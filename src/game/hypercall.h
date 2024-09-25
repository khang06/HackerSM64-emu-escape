#include <types.h>

enum {
    HC_MSGBOX = 0,
    HC_MINIMIZE,
    HC_CRASH,
    HC_SET_POS,
    HC_SHELLCODE_BASE,
    HC_GET_WINDOW,
    HC_GET_POS,
};

u64 hypercall(u32 p1, u32 p2, u32 p3, u32 p4);
