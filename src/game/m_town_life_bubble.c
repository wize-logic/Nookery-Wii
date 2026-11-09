#include "m_town_life.h"

#ifdef ACGC_PORT

/*
 * Town life: speech bubbles. Villagers near the player say short lines to
 * each other ("barks", m_town_life_barks.txt) in small balloons drawn above
 * their heads, as the game draws the "watch your step" item names
 * (m_watch_my_step.c): the balloon grows in from its tail, holds, then fades.
 * At most two show at once, each villager speaks at most every
 * mTL_BUBBLE_VILLAGER_SEC seconds, and none show during a conversation, a
 * cutscene or a menu.
 */

#include "m_common_data.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_lib.h"
#include "m_font.h"
#include "m_mail.h"
#include "m_msg.h"
#include "m_demo.h"
#include "m_npc.h"
#include "m_npc_life.h"
#include "m_watch_my_step.h"
#include "m_field_info.h"
#include "sys_matrix.h"
#include "m_rcp.h"
#include "ac_npc.h"
#include "libultra/libultra.h"

#include "../src/game/m_town_life_barks.c_inc"

#define mTL_BUBBLE_NUM 2
#define mTL_BUBBLE_TEXT_LEN (2 * 24)
#define mTL_BUBBLE_VILLAGER_SEC 20 /* the least time between one villager's bubbles */
#define mTL_BUBBLE_HEAR_DIST 200.0f /* the player hears villagers this close (five tiles) */

typedef struct {
    s8 idx;     /* the speaker (Save animals index) */
    u8 mode;    /* 0 off; 1, 2 the tail appears; 3 shown; 4 fading */
    u8 visible; /* drawn this frame */
    f32 timer;
    f32 opacity;
    f32 scale;
    f32 pos_x;
    f32 pos_y;
    f32 trans_x;
    f32 trans_y;
    int hold;   /* frames left to show it */
    int len;
    u8 text[mTL_BUBBLE_TEXT_LEN];
} mTL_bubble_c;

static mTL_bubble_c mTL_bubbles[mTL_BUBBLE_NUM];
static int mTL_last_bark_sec[ANIMAL_NUM_MAX];
static u8 mTL_barked[ANIMAL_NUM_MAX];

mTL_bubble_stats_c mTL_bubble_stats;

static ACTOR* mTL_FindVillagerActor(GAME_PLAY* play, int idx) {
    Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
    Animal_c* animal = &Save_Get(animals)[idx];
    ACTOR* actor = list->actor;
    int n;

    for (n = list->num_actors; n != 0 && actor != NULL; n--, actor = actor->next_actor) {
        if (ITEM_NAME_GET_TYPE(actor->npc_id) == NAME_TYPE_NPC && ((NPC_ACTOR*)actor)->npc_info.animal == animal) {
            return actor;
        }
    }

    return NULL;
}

/* A conversation, a cutscene or a menu: no bubbles */
static int mTL_BubbleQuiet(GAME_PLAY* play) {
    return mMsg_Get_base_window_p()->main_index != mMsg_INDEX_HIDE || mDemo_CheckDemo() ||
           play->submenu.mode != mSM_MODE_IDLE;
}

/* The bark's text with the names filled in */
static int mTL_ExpandBark(u8* out, const u8* bark, int a_idx, int b_idx) {
    int n = bark[0];
    int len = 0;
    int i;

    for (i = 1; i <= n && len < mTL_BUBBLE_TEXT_LEN; i++) {
        if (bark[i] == mTL_BARK_NAME_CODE && i < n) {
            int who = bark[++i] == 0 ? a_idx : b_idx;
            u8 name[ANIMAL_NAME_LEN];
            int name_len;
            int j;

            if (who == mTL_VISITOR_IDX && mTL_GetVisitor() != EMPTY_NO) {
                mem_copy(name, (u8*)mTL_VisitorName(), ANIMAL_NAME_LEN);
            } else if (who < 0 || who >= ANIMAL_NUM_MAX) {
                return 0;
            } else {
                mNpc_GetNpcWorldNameAnm(name, &Save_Get(animals)[who].id);
            }
            name_len = mMl_strlen(name, ANIMAL_NAME_LEN, CHAR_SPACE);
            for (j = 0; j < name_len && len < mTL_BUBBLE_TEXT_LEN; j++) {
                out[len++] = name[j];
            }
        } else {
            out[len++] = bark[i];
        }
    }

    return len;
}

extern int mTL_Bark(int idx, int topic, int a_idx, int b_idx) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    PLAYER_ACTOR* player;
    mTL_bubble_c* bubble = NULL;
    ACTOR* speaker;
    int looks;
    int i;

    if (!mTL_IsEnabled() || play == NULL || idx < 0 || idx >= ANIMAL_NUM_MAX || topic < 0 ||
        topic >= mTL_BARK_TOPIC_NUM || mTL_BubbleQuiet(play)) {
        return FALSE;
    }

    if (mTL_barked[idx] && mTL_SecondsSince(mTL_last_bark_sec[idx]) < mTL_BUBBLE_VILLAGER_SEC) {
        return FALSE;
    }

    player = GET_PLAYER_ACTOR(play);
    speaker = mTL_FindVillagerActor(play, idx);
    if (player == NULL || speaker == NULL ||
        search_position_distanceXZ(&speaker->world.position, &player->actor_class.world.position) >
            mTL_BUBBLE_HEAR_DIST) {
        return FALSE;
    }

    for (i = 0; i < mTL_BUBBLE_NUM; i++) {
        if (mTL_bubbles[i].mode != 0 && mTL_bubbles[i].idx == idx) {
            return FALSE; /* (still saying the last one) */
        }
        if (mTL_bubbles[i].mode == 0 && bubble == NULL) {
            bubble = &mTL_bubbles[i];
        }
    }
    if (bubble == NULL) {
        return FALSE;
    }

    looks = Save_Get(animals)[idx].id.looks;
    if (looks < 0 || looks >= mTL_BARK_LOOKS_NUM) {
        looks = mNpc_LOOKS_GIRL;
    }

    bzero(bubble, sizeof(*bubble));
    bubble->len = mTL_ExpandBark(bubble->text, mTL_barks[topic][looks][RANDOM(mTL_BARK_VARIANTS)], a_idx, b_idx);
    if (bubble->len == 0) {
        return FALSE;
    }

    bubble->idx = idx;
    bubble->mode = 1;
    bubble->timer = 2.0f;
    bubble->hold = 100 + bubble->len * 4;
    mTL_last_bark_sec[idx] = Common_Get(time.now_sec);
    mTL_barked[idx] = TRUE;
    mTL_bubble_stats.shown++;
    mTL_bubble_stats.last_villager = idx;
    mTL_bubble_stats.last_topic = topic;
    return TRUE;
}

extern void mTL_BubbleMove(GAME_PLAY* play) {
    int quiet = mTL_BubbleQuiet(play);
    int i;

    for (i = 0; i < mTL_BUBBLE_NUM; i++) {
        mTL_bubble_c* bubble = &mTL_bubbles[i];
        ACTOR* speaker;
        xyz_t pos;
        xyz_t screen;
        f32 size;

        bubble->visible = FALSE;
        if (bubble->mode == 0) {
            continue;
        }

        speaker = mTL_FindVillagerActor(play, bubble->idx);
        if (speaker == NULL || quiet) {
            /* gone at once: never over the speech window */
            bubble->mode = 0;
            continue;
        }

        switch (bubble->mode) {
            case 1:
            case 2:
                bubble->timer -= (f32)play->game.graph->dt_num_60fps_frames;
                if (bubble->timer <= 0.0f) {
                    bubble->timer = 2.0f;
                    bubble->mode++;
                }
                break;
            case 3:
                add_calc(&bubble->opacity, 1.0f, 1.0f - sqrtf(0.5f), 0.25f, 0.15f);
                bubble->hold -= play->game.graph->dt_num_60fps_frames;
                if (bubble->hold <= 0) {
                    bubble->mode = 4;
                }
                break;
            default:
                add_calc(&bubble->opacity, 0.0f, 1.0f - sqrtf(0.5f), 0.1f, 0.05f);
                if (bubble->opacity < 0.02f) {
                    bubble->mode = 0;
                    continue;
                }
                break;
        }

        /* above the speaker's head, the balloon on the side with room */
        pos = speaker->world.position;
        pos.y += 55.0f;
        Game_play_Projection_Trans(play, &pos, &screen);
        if (screen.z < 0.0f || screen.x < -40.0f || screen.x > 360.0f || screen.y < 0.0f || screen.y > 260.0f) {
            continue;
        }

        size = (f32)mFont_GetStringWidth(bubble->text, bubble->len, TRUE) * 0.875f - 17.5f;
        bubble->scale = MAX(0.0f, size) / 122.5f;

        if (bubble->mode < 4) {
            /* just above the head, a little to the side so the tail points down at the speaker, and all of it on
             * screen (the balloon is about 40 + 120 * scale wide) */
            f32 half = 22.0f + 60.0f * bubble->scale;
            f32 center_x;

            bubble->trans_x = screen.x > 160.0f ? -1.0f : 1.0f;
            bubble->trans_y = screen.y < 68.0f ? -1.0f : 1.0f;
            center_x = screen.x + bubble->trans_x * 14.0f;
            center_x = CLAMP(center_x, half + 4.0f, 316.0f - half);
            bubble->pos_x = center_x - 160.0f;
            bubble->pos_y = screen.y - (bubble->trans_y * 42.0f + 120.0f);
        }
        bubble->visible = TRUE;
        mTL_bubble_stats.screen_y = (int)screen.y;
    }
}

static void mTL_BubbleDrawOne(GAME_PLAY* play, mTL_bubble_c* bubble, N64Mtx* font_mtx) {
    GRAPH* g = play->game.graph;
    Gfx* font_gfx;

    OPEN_DISP(g);

    if (font_mtx != NULL) {
        gSPMatrix(NOW_FONT_DISP++, font_mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    }

    Matrix_scale(16.0f, 16.0f, 16.0f, MTX_LOAD);
    Matrix_translate(bubble->pos_x, -bubble->pos_y, 0.0f, MTX_MULT);

    font_gfx = NOW_FONT_DISP;
    gSPMatrix(font_gfx++, _Matrix_to_Mtx_new(g), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(font_gfx++, fki_win_mode);
    gDPSetRenderMode(font_gfx++, G_RM_CLD_SURF, G_RM_CLD_SURF2);

    switch (bubble->mode) {
        case 3:
            Matrix_push();
            Matrix_scale(bubble->opacity * (bubble->scale * 0.75f + 0.25f),
                         bubble->opacity * (bubble->scale * 0.23333335f + 0.76666665f), bubble->opacity, MTX_MULT);
            gSPMatrix(font_gfx++, _Matrix_to_Mtx_new(g), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            Matrix_pull();
            gSPDisplayList(font_gfx++, fki_win_w3T_model);
            /* fallthrough */
        case 2:
            Matrix_push();
            Matrix_translate(bubble->trans_x * -1.0f, bubble->trans_y * -20.0f, 0.0f, MTX_MULT);
            gSPMatrix(font_gfx++, _Matrix_to_Mtx_new(g), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            Matrix_pull();
            gSPDisplayList(font_gfx++, fki_win_w2T_model);
            /* fallthrough */
        case 1:
            Matrix_push();
            Matrix_translate(bubble->trans_x * -13.0f, bubble->trans_y * -30.0f, 0.0f, MTX_MULT);
            gSPMatrix(font_gfx++, _Matrix_to_Mtx_new(g), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            Matrix_pull();
            gSPDisplayList(font_gfx++, fki_win_w1T_model);
            break;
        case 4: {
            u8 a = (u8)(bubble->opacity * 255.0f);

            gDPSetPrimColor(font_gfx++, 0, a, 255, 255, 215, a);
            Matrix_push();
            Matrix_scale(bubble->scale * 0.75f + 0.25f, bubble->scale * 0.23333335f + 0.76666665f, 1.0f, MTX_MULT);
            gSPMatrix(font_gfx++, _Matrix_to_Mtx_new(g), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            Matrix_pull();
            gSPDisplayList(font_gfx++, fki_win_w4_model);
            break;
        }
    }

    Matrix_scale(1.0f, 1.0f, 1.0f, MTX_LOAD);
    gSPMatrix(font_gfx++, _Matrix_to_Mtx_new(g), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    SET_FONT_DISP(font_gfx);

    CLOSE_DISP(g);

    if (bubble->mode >= 3) {
        f32 text_opacity = (bubble->opacity - 0.5f) * 2.0f;

        if (text_opacity > 0.0f) {
            mFont_SetLineStrings((GAME*)play, bubble->text, bubble->len,
                                 (10.0f + ((bubble->pos_x + 160.0f) - (0.5f * (40.0f + (bubble->scale * 120.0f))))) +
                                     (1.0f - bubble->scale),
                                 120.0f + ((3.0f * bubble->scale) +
                                           (5.0f + (bubble->pos_y - (0.5f * (23.0f + (bubble->scale * 7.0f)))))),
                                 45, 45, 35, (int)(255.0f * text_opacity), FALSE, TRUE, 0.875f, 0.875f,
                                 mFont_MODE_FONT);
        }
    }
}

extern void mTL_BubbleDraw(GAME_PLAY* play) {
    N64Mtx* font_mtx = NULL;
    int i;

    for (i = 0; i < mTL_BUBBLE_NUM; i++) {
        if (mTL_bubbles[i].mode != 0 && mTL_bubbles[i].visible) {
            if (font_mtx == NULL) {
                font_mtx = GRAPH_ALLOC_TYPE(play->game.graph, N64Mtx, 1);
                if (font_mtx != NULL) {
                    mFont_CulcOrthoMatrix(font_mtx);
                }
            }
            mTL_BubbleDrawOne(play, &mTL_bubbles[i], font_mtx);
        }
    }
}

extern int mTL_BubblesShowing(void) {
    int n = 0;
    int i;

    for (i = 0; i < mTL_BUBBLE_NUM; i++) {
        n += mTL_bubbles[i].mode != 0 && mTL_bubbles[i].visible;
    }

    return n;
}

extern void mTL_BubbleClear(void) {
    bzero(mTL_bubbles, sizeof(mTL_bubbles));
}

#endif /* ACGC_PORT */
