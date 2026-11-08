/* Aurora's GXVert.h, with the vertex writers its console branch leaves out
 * (the GameCube SDK's inlines: each a write to the write-gather pipe) */
#ifndef NK_WII_GXVERT_H
#define NK_WII_GXVERT_H
#include_next <dolphin/gx/GXVert.h>
#ifndef TARGET_PC
#ifdef __cplusplus
extern "C" {
#endif
static inline void GXPosition3u16(u16 x, u16 y, u16 z) {
  GXWGFifo.u16 = x;
  GXWGFifo.u16 = y;
  GXWGFifo.u16 = z;
}
static inline void GXPosition3u8(u8 x, u8 y, u8 z) {
  GXWGFifo.u8 = x;
  GXWGFifo.u8 = y;
  GXWGFifo.u8 = z;
}
static inline void GXPosition3s8(s8 x, s8 y, s8 z) {
  GXWGFifo.s8 = x;
  GXWGFifo.s8 = y;
  GXWGFifo.s8 = z;
}
static inline void GXPosition2u16(u16 x, u16 y) {
  GXWGFifo.u16 = x;
  GXWGFifo.u16 = y;
}
static inline void GXPosition2s16(s16 x, s16 y) {
  GXWGFifo.s16 = x;
  GXWGFifo.s16 = y;
}
static inline void GXPosition2u8(u8 x, u8 y) {
  GXWGFifo.u8 = x;
  GXWGFifo.u8 = y;
}
static inline void GXPosition2s8(s8 x, s8 y) {
  GXWGFifo.s8 = x;
  GXWGFifo.s8 = y;
}
static inline void GXPosition1x16(u16 i) {
  GXWGFifo.u16 = i;
}
static inline void GXPosition1x8(u8 i) {
  GXWGFifo.u8 = i;
}
static inline void GXNormal3s16(s16 x, s16 y, s16 z) {
  GXWGFifo.s16 = x;
  GXWGFifo.s16 = y;
  GXWGFifo.s16 = z;
}
static inline void GXNormal3s8(s8 x, s8 y, s8 z) {
  GXWGFifo.s8 = x;
  GXWGFifo.s8 = y;
  GXWGFifo.s8 = z;
}
static inline void GXNormal1x16(u16 i) {
  GXWGFifo.u16 = i;
}
static inline void GXNormal1x8(u8 i) {
  GXWGFifo.u8 = i;
}
static inline void GXColor3u8(u8 r, u8 g, u8 b) {
  GXWGFifo.u8 = r;
  GXWGFifo.u8 = g;
  GXWGFifo.u8 = b;
}
static inline void GXColor1u32(u32 c) {
  GXWGFifo.u32 = c;
}
static inline void GXColor1u16(u16 c) {
  GXWGFifo.u16 = c;
}
static inline void GXColor1x16(u16 i) {
  GXWGFifo.u16 = i;
}
static inline void GXColor1x8(u8 i) {
  GXWGFifo.u8 = i;
}
static inline void GXTexCoord2u16(u16 s, u16 t) {
  GXWGFifo.u16 = s;
  GXWGFifo.u16 = t;
}
static inline void GXTexCoord2u8(u8 s, u8 t) {
  GXWGFifo.u8 = s;
  GXWGFifo.u8 = t;
}
static inline void GXTexCoord2s8(s8 s, s8 t) {
  GXWGFifo.s8 = s;
  GXWGFifo.s8 = t;
}
static inline void GXTexCoord1f32(f32 s) {
  GXWGFifo.f32 = s;
}
static inline void GXTexCoord1u16(u16 s) {
  GXWGFifo.u16 = s;
}
static inline void GXTexCoord1s16(s16 s) {
  GXWGFifo.s16 = s;
}
static inline void GXTexCoord1u8(u8 s) {
  GXWGFifo.u8 = s;
}
static inline void GXTexCoord1s8(s8 s) {
  GXWGFifo.s8 = s;
}
static inline void GXTexCoord1x16(u16 i) {
  GXWGFifo.u16 = i;
}
static inline void GXTexCoord1x8(u8 i) {
  GXWGFifo.u8 = i;
}
#ifdef __cplusplus
}
#endif
#endif
#endif
