/* The SDK calls the game makes for hardware the Wii doesn't have or the port
 * leaves to libogc: the GBA link cable (Kapp'n's island goes without it,
 * pc/... ACGC_PORT paths), the boot mode's SRAM, the debugger's protections,
 * the IPL font's encoding, and libultra's leftovers (src/static/libultra's
 * ultra.c and printf, which newlib provides). */

#include "nk_wii.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/gba.h>
#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>

/* gba.h: no GBA is ever plugged in */
void GBAInit(void) {}
s32 GBAGetStatus(s32 chan, u8* status) { (void) chan; if (status) *status = 0; return GBA_NOT_READY; }
s32 GBAGetProcessStatus(s32 chan, u8* percentp) { (void) chan; if (percentp) *percentp = 0; return GBA_NOT_READY; }
s32 GBAReset(s32 chan, u8* status) { (void) chan; (void) status; return GBA_NOT_READY; }
s32 GBARead(s32 chan, u8* dst, u8* status) { (void) chan; (void) dst; (void) status; return GBA_NOT_READY; }
s32 GBAWrite(s32 chan, u8* src, u8* status) { (void) chan; (void) src; (void) status; return GBA_NOT_READY; }
s32 GBAJoyBootAsync(s32 chan, s32 palette_color, s32 palette_speed, u8* programp, s32 length,
                    u8* status, GBACallback callback)
{
    (void) chan; (void) palette_color; (void) palette_speed; (void) programp; (void) length;
    (void) status; (void) callback;
    return GBA_NOT_READY;
}

/* OSRtc.c: the boot mode lives in the GameCube's SRAM */
void __OSSetBootMode(u8 mode) { (void) mode; }
BOOL __OSSyncSram(void) { return TRUE; }

/* OS.c, OSProtect.c, OSFont.c */
u32 OSGetStackPointer(void)
{
    u32 sp;
    __asm__ volatile("mr %0, 1" : "=r"(sp));
    return sp;
}

void OSGetSaveRegion(void** start, void** end)
{
    *start = NULL;
    *end = NULL;
}

void OSProtectRange(u32 chan, void* addr, u32 nBytes, u32 control)
{
    (void) chan; (void) addr; (void) nBytes; (void) control;
}

void OSFillFPUContext(OSContext* context) { (void) context; }
void OSSetStringTable(void* stringTable) { (void) stringTable; }
u16 OSGetFontEncode(void) { return 0; /* OS_FONT_ENCODE_ANSI */ }

/* pad.h, vi.h */
void PADControlAllMotors(const u32* cmdArr)
{
    for (u32 i = 0; i < 4; i++) {
        PADControlMotor(i, cmdArr[i]);
    }
}
void PADSetAnalogMode(u32 mode) { (void) mode; }
void VIConfigurePan(u16 xOrg, u16 yOrg, u16 width, u16 height)
{
    (void) xOrg; (void) yOrg; (void) width; (void) height;
}

/* libultra's ultra.c */
s32 osAppNMIBuffer[16];

void osSyncPrintf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    OSVReport(format, args);
    va_end(args);
}

void osWritebackDCache(void* buf, u32 len)
{
    DCStoreRange(buf, len);
}

u32 osGetCount(void)
{
    return OSGetTick();
}

/* aurora_platform.cpp's: the N64 C library's helpers */
int __abs(int value)
{
    return value < 0 ? -value : value;
}

/* libc64/malloc.c's arena (newlib's malloc serves the game, as on PC) */
static size_t s_malloc_arena;
void MallocInit(void* base, size_t size) { (void) base; s_malloc_arena = size; }
void MallocCleanup(void) { s_malloc_arena = 0; }
void GetFreeArena(size_t* max_size, size_t* free_size, size_t* alloc_size)
{
    if (max_size) *max_size = s_malloc_arena;
    if (free_size) *free_size = s_malloc_arena;
    if (alloc_size) *alloc_size = 0;
}
void DisplayArena(void) {}

/* libforest/ReconfigBATs.c: libogc maps memory itself */
void ReconfigBATs(void) {}
