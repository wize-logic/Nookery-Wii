#include "m_town_life.h"

#ifdef ACGC_PORT

/*
 * Town life: visitors. Some days a villager from the game's roster who doesn't
 * live in town spends the day here (9:00 to 18:00), drifting from acre to acre
 * and wandering like a resident wherever the player meets them.
 *
 * The visitor is a normal villager actor whose villager data comes from one of
 * the game's mask NPC records (mNpc_RegistMaskNpc with the visitor's own id as
 * the mask id, the way event villagers are set up), so it needs no house, no
 * save data and no schedule entry: without one, the game gives an actor the
 * wander schedule. The game clears mask records with every scene, so the
 * record is made again when the visitor appears, and only when two of the
 * three are free, so one is always left for an event.
 */

#include "m_common_data.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_lib.h"
#include "m_npc.h"
#include "m_field_info.h"
#include "m_collision_bg.h"
#include "m_name_table.h"
#include "m_event.h"
#include "m_scene_table.h"
#include "sys_math.h"
#include "ac_npc.h"

extern s8 npc_grow_list[];
extern mNpc_Default_Data_c npc_def_list[];

typedef struct {
    int stamp;            /* the day decided (day stamp + 1) */
    mActor_name_t npc_id; /* today's visitor, or EMPTY_NO */
    s8 bx;                /* the acre they're in (field block numbers) */
    s8 bz;
    int move_sec;         /* when they last moved acre */
    u8 seen;              /* they have been on the streets near the player */
    u8 name[ANIMAL_NAME_LEN];
} mTL_visitor_c;

static mTL_visitor_c mTL_visitor;
static mNpc_NpcList_c mTL_visitor_list;

int mTL_visitor_chance = mTL_VISITOR_CHANCE;
int mTL_test_visitor = FALSE;
static int mTL_visitor_test_bx = -1;
static int mTL_visitor_test_bz = -1;

static int mTL_VisitorStamp(void) {
    lbRTC_time_c* now = Common_GetPointer(time.rtc_time);

    return now->year * 372 + (now->month - 1) * 31 + now->day;
}

/* A villager from the roster who could move in, isn't living here and isn't the islander */
static mActor_name_t mTL_PickVisitor(void) {
    int start = RANDOM(NPC_NUM);
    int i;

    for (i = 0; i < NPC_NUM; i++) {
        int idx = (start + i) % NPC_NUM;
        mActor_name_t npc_id = NPC_START | idx;

        if ((npc_grow_list[idx] == mNpc_GROW_STARTER || npc_grow_list[idx] == mNpc_GROW_MOVE_IN) &&
            mNpc_SearchAnimalinfo(Save_Get(animals), npc_id, ANIMAL_NUM_MAX) == -1 &&
            Save_Get(island).animal.id.npc_id != npc_id) {
            return npc_id;
        }
    }

    return EMPTY_NO;
}

static ACTOR* mTL_VisitorActor(GAME_PLAY* play) {
    Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
    ACTOR* actor = list->actor;
    int n;

    for (n = list->num_actors; n != 0 && actor != NULL; n--, actor = actor->next_actor) {
        if (actor->npc_id == mTL_visitor.npc_id) {
            return actor;
        }
    }

    return NULL;
}

/* Enough mask records free that one is left over for an event after the visitor takes one */
static int mTL_MasksFree(void) {
    int n = 0;
    int i;

    for (i = 0; i < mNpc_MASK_NPC_NUM; i++) {
        n += !Common_Get(mask_npc[i]).in_use;
    }

    return n >= 2;
}

extern void mTL_TestVisitorHere(int bx, int bz) {
    mTL_test_visitor = TRUE;
    mTL_visitor.stamp = 0;
    mTL_visitor_test_bx = bx;
    mTL_visitor_test_bz = bz;
}

extern mActor_name_t mTL_GetVisitor(void) {
    return mTL_visitor.stamp == mTL_VisitorStamp() + 1 ? mTL_visitor.npc_id : EMPTY_NO;
}

extern mNpc_NpcList_c* mTL_VisitorList(mActor_name_t npc_id) {
    if (npc_id == EMPTY_NO || npc_id != mTL_GetVisitor()) {
        return NULL;
    }

    if (mTL_visitor_list.name != npc_id) {
        bzero(&mTL_visitor_list, sizeof(mTL_visitor_list));
        mTL_visitor_list.name = npc_id;
    }

    return &mTL_visitor_list;
}

extern int mTL_VisitorSeen(void) {
    return mTL_GetVisitor() != EMPTY_NO && mTL_visitor.seen;
}

extern const u8* mTL_VisitorName(void) {
    return mTL_visitor.name;
}

extern void mTL_VisitorMove(GAME_PLAY* play) {
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    int hour = Common_Get(time.rtc_time).hour;
    ACTOR* actor;
    int pbx;
    int pbz;

    if (!mTL_IsEnabled() || player == NULL || Save_Get(scene_no) != SCENE_FG || !mEv_IsNotTitleDemo() ||
        Common_Get(time.rtc_time).year == 0) {
        return;
    }

    /* today's visitor, if any */
    if (mTL_visitor.stamp != mTL_VisitorStamp() + 1) {
        mTL_visitor.stamp = mTL_VisitorStamp() + 1;
        mTL_visitor.npc_id = EMPTY_NO;
        mTL_visitor.seen = FALSE;
        if (mTL_test_visitor || RANDOM(100) < mTL_visitor_chance) {
            mTL_visitor.npc_id = mTL_PickVisitor();
            mTL_visitor.bx = mTL_visitor_test_bx >= 0 ? mTL_visitor_test_bx : 1 + RANDOM(FG_BLOCK_X_NUM);
            mTL_visitor.bz = mTL_visitor_test_bz >= 0 ? mTL_visitor_test_bz : 1 + RANDOM(FG_BLOCK_Z_NUM);
            mTL_visitor.move_sec = Common_Get(time.now_sec);
            if (mTL_visitor.npc_id != EMPTY_NO) {
                Animal_c animal;

                mNpc_ClearAnimalInfo(&animal);
                mNpc_SetDefAnimal(&animal, mTL_visitor.npc_id, npc_def_list);
                mNpc_GetNpcWorldNameAnm(mTL_visitor.name, &animal.id);
                mTL_stats.visitor_days++;
            }
        }
    }

    if (mTL_visitor.npc_id == EMPTY_NO) {
        return;
    }

    actor = mTL_VisitorActor(play);
    if (!mTL_test_visitor && (hour < 9 || hour >= 18)) {
        /* gone home */
        if (actor != NULL && actor->player_distance_xz > 200.0f) {
            Actor_delete(actor);
        }
        return;
    }

    if (actor != NULL) {
        mTL_visitor.bx = actor->block_x;
        mTL_visitor.bz = actor->block_z;
        return;
    }

    mFI_Wpos2BlockNum(&pbx, &pbz, player->actor_class.world.position);

    /* away from the player, they drift to a neighboring acre every few minutes */
    if (mTL_SecondsSince(mTL_visitor.move_sec) >= mTL_VISITOR_MOVE_SEC && mTL_visitor_test_bx < 0) {
        int bx = mTL_visitor.bx + RANDOM(3) - 1;
        int bz = mTL_visitor.bz + RANDOM(3) - 1;

        mTL_visitor.move_sec = Common_Get(time.now_sec);
        if (bx >= 1 && bx <= FG_BLOCK_X_NUM && bz >= 1 && bz <= FG_BLOCK_Z_NUM) {
            mTL_visitor.bx = bx;
            mTL_visitor.bz = bz;
        }
    }

    /* the player comes to their acre: they're somewhere about it */
    if (pbx == mTL_visitor.bx && pbz == mTL_visitor.bz && play->actor_info.list[ACTOR_PART_NPC].num_actors < 12) {
        int i;

        mTL_stats.visitor_step = 1;
        if (mNpc_GetSameMaskNpc(mTL_visitor.npc_id) == NULL &&
            (!mTL_MasksFree() || !mNpc_RegistMaskNpc(mTL_visitor.npc_id, mTL_visitor.npc_id, EMPTY_NO))) {
            return;
        }
        mTL_stats.visitor_step = 2;
        {
            /* (a villager's defaults leave them in a mood, often sleepy: a visitor is out to enjoy the day) */
            mNpc_MaskNpc_c* mask = mNpc_GetSameMaskNpc(mTL_visitor.npc_id);

            if (mask != NULL && mask->exists == FALSE) {
                mask->animal_data.mood = mNpc_FEEL_NORMAL;
                mask->animal_data.mood_time = 0;
            }
        }

        for (i = 0; i < 32; i++) {
            xyz_t pos;
            mActor_name_t* fg;

            mFI_BkandUtNum2CenterWpos(&pos, pbx, pbz, 2 + RANDOM(UT_X_NUM - 4), 2 + RANDOM(UT_Z_NUM - 4));
            fg = mFI_GetUnitFG(pos);
            if (fg == NULL || *fg != EMPTY_NO || mCoBG_Wpos2CheckNpc(pos) != TRUE ||
                search_position_distanceXZ(&pos, &player->actor_class.world.position) < 160.0f) {
                continue;
            }

            pos.y = mCoBG_GetBgY_OnlyCenter_FromWpos(pos, 0.0f);
            mTL_stats.visitor_step = 3;
            if (Actor_info_make_actor(&play->actor_info, (GAME*)play, mAc_PROFILE_NORMAL_NPC, pos.x, pos.y, pos.z, 0,
                                      0, 0, pbx, pbz, -1, mTL_visitor.npc_id, -1, -1, -1) != NULL) {
                mTL_visitor.seen = TRUE;
                mTL_stats.visitor_spawns++;
                mTL_stats.visitor_step = 4;
            }
            break;
        }
    }
}

#endif /* ACGC_PORT */
