/* The town's name in a town save (a .gci as slot A's folder has it), for the
 * online lobby's listing: the save's land_info, after the card's directory
 * entry and the banner/icon block. The game's letters are ASCII's for the
 * plain ones; others show as '?'. */

#include <stddef.h>
#include <string.h>

#include "m_card.h"
#include "m_common_data.h"

int nk_wii_town_name(const unsigned char* gci, unsigned int size, char* out, int out_size)
{
    unsigned int off = 0x40 + OTHERS_SIZE + offsetof(Save_t, land_info);
    int n = 0;
    if (out_size <= 0) {
        return -1;
    }
    out[0] = '\0';
    if (gci == NULL || size < off + sizeof(mLd_land_info_c)) {
        return -1;
    }
    const mLd_land_info_c* land = (const mLd_land_info_c*) (gci + off);
    for (int i = 0; i < LAND_NAME_SIZE && n < out_size - 1; i++) {
        unsigned char c = land->name[i];
        out[n++] = (c >= 0x20 && c < 0x7F) ? (char) c : '?';
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
    return 0;
}
