/* The NES games' folders (nookery_wii/nes_paths.h). The PC port finds them
 * in its user folder (pc_platform_user_path: nes_roms, and save/card_a or
 * save/card_b's nes_roms for the cartridges' saves); here they are beside
 * the program's other files on the SD card, the saves beside the memory
 * card's (wii/src/card.c) as the PC's are beside its card's. */

#include "nk_wii.h"
#include "nookery_wii/nes_paths.h"

#include <stdio.h>

static int put(char* out, size_t size, const char* dir, const char* name)
{
    int length;
    if (out == NULL || size == 0 || dir == NULL || dir[0] == '\0') {
        return 0;
    }
    length = snprintf(out, size, "%s/%s", dir, name);
    return length >= 0 && (size_t) length < size;
}

int nk_wii_nes_roms_dir(char* out, size_t size)
{
    return put(out, size, NK_WII_DIR, "nes_roms");
}

int nk_wii_nes_saves_dir(int chan, char* out, size_t size)
{
    return put(out, size, nk_wii_card_get_dir(chan), "nes_roms");
}
