/* Widescreen, as the PC port has it (pc/src/aurora_widescreen.cpp, the same
 * functions): on a Wii set to 16:9 in Wii Settings, the game's 640x480
 * picture is drawn anamorphic, so the TV's stretch makes it right, and hor+:
 * the 3D view's projection narrowed by (4:3)/(16:9) = 0.75 shows more at
 * the sides. The game's markers (PC_NOOP_WIDESCREEN_STRETCH*, emu64.c) leave
 * full-screen pictures stretched (1) or keep 2D menus 4:3 in the middle (2);
 * the actors' and the weather's culling widen with it (m_actor.c, ...), and
 * with settings.ini hud_at_edges = 1 the corner HUD moves to the edges.
 * settings.ini widescreen = 0 keeps the game 4:3 whatever Wii Settings say. */

#include "nk_wii.h"
#include "ogc_bridge.h"

#include <string.h>

#include <dolphin/gx.h>
#include <dolphin/mtx.h>

#include "pc_settings.h"

#define GC_WIDTH 640.0f
#define GC_HEIGHT 480.0f

int g_pc_widescreen_stretch = 0;
extern int g_nk_widescreen; /* settings.c */

float pc_widescreen_get_aspect_factor(void)
{
    static int wide = -1;
    if (wide < 0) {
        wide = CONF_GetAspectRatio() == 1 ? 1 : 0;
    }
    return (wide && g_nk_widescreen) ? (4.0f / 3.0f) / (16.0f / 9.0f) : 1.0f;
}

float pc_widescreen_get_hud_edge_offset(void)
{
    if (!g_pc_settings.hud_at_edges) {
        return 0.0f;
    }
    return GC_WIDTH * 0.25f * (1.0f / pc_widescreen_get_aspect_factor() - 1.0f);
}

void pc_widescreen_set_projection(const void* mtx, unsigned long type)
{
    Mtx44 adjusted;
    memcpy(adjusted, mtx, sizeof(adjusted));
    float factor = pc_widescreen_get_aspect_factor();
    int correct = g_pc_widescreen_stretch == 0 || (g_pc_widescreen_stretch == 2 && type == GX_ORTHOGRAPHIC);
    if (correct && factor < 1.0f) {
        adjusted[0][0] *= factor;
    }
    GXSetProjection(adjusted, (GXProjectionType) type);
}

void pc_widescreen_set_viewport(float left, float top, float width, float height, float nearz, float farz)
{
    float factor = pc_widescreen_get_aspect_factor();
    if (g_pc_widescreen_stretch == 2 && factor < 1.0f) {
        int full = left < 1.0f && top < 1.0f && width > GC_WIDTH - 1.0f && height > GC_HEIGHT - 1.0f;
        if (!full) {
            left = (1.0f - factor) * 0.5f * GC_WIDTH + left * factor;
            width *= factor;
        }
    }
    GXSetViewport(left, top, width, height, nearz, farz);
}
