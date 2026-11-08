/* The NES games' folders on the Wii (wii/src/nes_paths.c), for the PC port's
 * fixNES path in src/static/Famicom/famicom.cpp: the player's own ROMs, which
 * the game lists as cartridges, and those cartridges' saves. The PC's are in
 * its user folder (nes_roms, save/card_a/nes_roms). Each gives the folder
 * without a final '/' and returns 0 when it doesn't fit. */
#ifndef NOOKERY_WII_NES_PATHS_H
#define NOOKERY_WII_NES_PATHS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* sd:/apps/nookery/nes_roms */
int nk_wii_nes_roms_dir(char* out, size_t size);

/* <the memory card's folder>/nes_roms for card slot `chan` (0: your town's,
 * sd:/apps/nookery/card_a; 1: the town you visit) */
int nk_wii_nes_saves_dir(int chan, char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
