/* Included before every game and port source (cmake/Wii.cmake): names the
 * game's console branches use that Aurora's SDK headers spell otherwise */
#ifndef NOOKERY_WII_GAME_COMPAT_H
#define NOOKERY_WII_GAME_COMPAT_H
#define OS_INTR_DSP_DSP __OS_INTERRUPT_DSP_DSP
#define OS_BOOT_MODE_RETAIL 0
#define OS_BOOT_MODE_DEBUG 1
/* MSL's stddef.h (include/libc), which the hosted build doesn't use */
#ifndef membersize
#define membersize(type, member) (sizeof(((type*)0)->member))
#endif
/* The PC port's bring-up traces (pc/include/pc_diag.h): off */
#ifndef PC_DIAG
#define PC_DIAG(limit, fmt, ...) ((void)0)
#endif
/* The PC's profiler zones, which some shared sources use outside its
 * conditionals */
#ifndef ZoneScoped
#define ZoneScoped
#define ZoneScopedN(name)
#endif
#ifdef __cplusplus
extern "C" void OSDVDFatalError(void);
#else
void OSDVDFatalError(void);
#endif
/* The PC port's widescreen (pc/include/pc_platform.h, after
 * pc/src/aurora_widescreen.cpp): on a Wii set to 16:9, the game's 640x480
 * picture drawn anamorphic and hor+ (wii/src/widescreen.c) */
#ifdef __cplusplus
extern "C" {
#endif
float pc_widescreen_get_aspect_factor(void);
void pc_widescreen_set_projection(const void* mtx, unsigned long type); /* (u32 here) */
void pc_widescreen_set_viewport(float left, float top, float width, float height, float nearz, float farz);
float pc_widescreen_get_hud_edge_offset(void);
extern int g_pc_widescreen_stretch;
#ifdef __cplusplus
}
#endif
#define PC_NOOP_WIDESCREEN_STRETCH 0xAC5701u
#define PC_NOOP_WIDESCREEN_STRETCH_OFF 0xAC5700u
#endif
