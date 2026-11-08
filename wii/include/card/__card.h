/* The CARD internals m_card.c calls (the SDK's __card.h): the directory
 * entry of an open file, read and written whole (wii/src/card.c) */
#ifndef NK_WII_CARD_INTERNAL_H
#define NK_WII_CARD_INTERNAL_H
#include <dolphin/card.h>
#ifdef __cplusplus
extern "C" {
#endif
s32 __CARDGetStatusEx(s32 chan, s32 fileNo, CARDDir* dirent);
s32 __CARDSetStatusEx(s32 chan, s32 fileNo, CARDDir* dirent);
s32 __CARDSetStatusExAsync(s32 chan, s32 fileNo, CARDDir* dirent, CARDCallback callback);
#ifdef __cplusplus
}
#endif
#endif
