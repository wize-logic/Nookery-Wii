#include "m_town_life.h"

#ifdef ACGC_PORT

#include "m_common_data.h"
#include "m_field_info.h"
#include "m_collision_bg.h"
#include "m_name_table.h"
#include "m_kankyo.h"
#include "m_time.h"
#include "m_lib.h"
#include "sys_math.h"
#include "ac_npc_anim_def.h"
#include "ac_npc.h"
#include "ac_tools.h"
#include "pc_settings.h"
#include "m_npc_life.h"

/*
 * The activities. Animations come from aNPC_animeTable (any villager can play
 * any of them); props are tools held in the right hand. Weights per
 * personality: normal, peppy, lazy, jock, cranky, snooty.
 */
// clang-format off
const mTL_activity_c mTL_activity_table[mTL_ACT_NUM] = {
    /* name          anim                      sub_anim             prop           place               line
                     weights                hours   weather            seasons            secs */
    { "sit",         aNPC_ANIM_SITDOWN_WAIT1,  aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_TREE,     mTL_LINE_SIT,
                     { 3, 1, 4, 0, 2, 2 },  8, 20,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,    20, 45 },
    { "sit_drink",   aNPC_ANIM_SITDOWN_DRINK1, aNPC_SUB_ANIM_NONE,  TOOL_TUMBLER,  mTL_PLACE_TREE,     mTL_LINE_DRINK,
                     { 2, 1, 2, 0, 3, 4 }, 10, 22,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,    20, 40 },
    { "sit_laugh",   aNPC_ANIM_SITDOWN_CLAP1,  aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_RIVER,    mTL_LINE_SIT,
                     { 1, 3, 1, 1, 0, 0 },  9, 19,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,    10, 20 },
    { "sit_fan",     aNPC_ANIM_SITDOWN_WAIT1,  aNPC_SUB_ANIM_UTIWA, TOOL_UTIWA,    mTL_PLACE_TREE,     mTL_LINE_FAN,
                     { 2, 1, 3, 0, 2, 3 }, 11, 20,  mTL_WEATHER_CLEAR, mTL_SEASON_SUMMER, 20, 40 },
    { "doze",        aNPC_ANIM_KOKKURI_D1,     aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_TREE,     mTL_LINE_DOZE,
                     { 1, 0, 5, 0, 2, 0 }, 12, 17,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,    30, 60 },
    { "dance",       aNPC_ANIM_DANCE1,         aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_DANCE,
                     { 1, 4, 0, 1, 0, 1 }, 10, 22,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,    10, 20 },
    { "exercise",    aNPC_ANIM_TAISOU1,        aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_EXERCISE,
                     { 1, 1, 0, 5, 1, 0 },  5, 12,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,    15, 30 },
    { "exercise2",   aNPC_ANIM_TAISOU2,        aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_EXERCISE,
                     { 0, 1, 0, 4, 0, 1 },  5, 20,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,    15, 30 },
    { "jog",         aNPC_ANIM_ASIHUMI1,       aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_EXERCISE,
                     { 0, 2, 0, 4, 0, 0 },  6, 21,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,     8, 15 },
    { "warm_up",     aNPC_ANIM_WARMUP1,        aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_EXERCISE,
                     { 0, 1, 0, 4, 1, 0 },  5, 19,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,     8, 15 },
    { "fish",        aNPC_ANIM_TURI_WAIT1,     aNPC_SUB_ANIM_NONE,  TOOL_NPC_SAO,  mTL_PLACE_RIVER,    mTL_LINE_FISH,
                     { 2, 1, 3, 1, 4, 0 },  6, 19,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,    30, 60 },
    { "fan",         aNPC_ANIM_WAIT1,          aNPC_SUB_ANIM_UTIWA, TOOL_UTIWA,    mTL_PLACE_ANYWHERE, mTL_LINE_FAN,
                     { 1, 1, 1, 1, 1, 1 }, 10, 21,  mTL_WEATHER_CLEAR, mTL_SEASON_SUMMER, 15, 30 },
    { "pray",        aNPC_ANIM_OMAIRI1,        aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_PLAZA,    mTL_LINE_PRAY,
                     { 2, 1, 1, 1, 1, 1 },  6, 20,  mTL_WEATHER_ANY,   mTL_SEASON_ANY,     6, 12 },
    { "look_around", aNPC_ANIM_KYORO1,         aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_ANYWHERE, mTL_LINE_NONE,
                     { 1, 1, 1, 1, 1, 1 },  0, 24,  mTL_WEATHER_ANY,   mTL_SEASON_ANY,     4,  8 },
    { "phone",       aNPC_ANIM_KEITAI_TALK1,   aNPC_SUB_ANIM_NONE,  TOOL_KEITAI,   mTL_PLACE_ANYWHERE, mTL_LINE_PHONE,
                     { 1, 3, 0, 1, 0, 3 },  9, 23,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,    10, 20 },
    { "cheer",       aNPC_ANIM_BANZAI1,        aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_ANYWHERE, mTL_LINE_DANCE,
                     { 0, 2, 0, 2, 0, 0 },  8, 20,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,     3,  6 },
    { "wave_flag",   aNPC_ANIM_HATAFURI1,      aNPC_SUB_ANIM_NONE,  TOOL_FLAG,     mTL_PLACE_OPEN,     mTL_LINE_DANCE,
                     { 0, 1, 0, 1, 0, 0 },  9, 18,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,     6, 12 },
};
// clang-format on

static mTL_villager_c mTL_villagers[ANIMAL_NUM_MAX];
mTL_group_c mTL_groups[mTL_GROUP_NUM];
int mTL_group_chance = mTL_GROUP_CHANCE;
int mTL_test_group_any_hour = FALSE;

/* Standing in a group, and walking a friend home: not in the table, never chosen at random */
static const mTL_activity_c mTL_chat_entry = {
    "chat", aNPC_ANIM_WAIT1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE, mTL_PLACE_ANYWHERE, mTL_LINE_NONE,
    { 0, 0, 0, 0, 0, 0 }, 0, 24, mTL_WEATHER_ANY, mTL_SEASON_ANY, 60, 180,
};
static const mTL_activity_c mTL_escort_entry = {
    "escort", aNPC_ANIM_WALK1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE, mTL_PLACE_ANYWHERE, mTL_LINE_NONE,
    { 0, 0, 0, 0, 0, 0 }, 0, 24, mTL_WEATHER_ANY, mTL_SEASON_ANY, 30, 45,
};

int mTL_start_chance = mTL_START_CHANCE;
int mTL_talk_chance = mTL_TALK_CHANCE;
int mTL_test_activity = -1;
int mTL_test_villager = -1;
int mTL_test_seconds = 0;
mTL_activity_c mTL_test_entry;
mTL_stats_c mTL_stats;

extern const mTL_activity_c* mTL_GetActivity(int act) {
    if (act == mTL_ACT_TEST) {
        return &mTL_test_entry;
    }
    if (act == mTL_ACT_CHAT) {
        return &mTL_chat_entry;
    }
    if (act == mTL_ACT_ESCORT) {
        return &mTL_escort_entry;
    }

    return &mTL_activity_table[(act >= 0 && act < mTL_ACT_NUM) ? act : 0];
}

extern int mTL_IsEnabled(void) {
    return g_pc_settings.town_life != 0;
}

extern mTL_villager_c* mTL_GetVillager(int idx) {
    if (idx < 0 || idx >= ANIMAL_NUM_MAX) {
        return NULL;
    }

    return &mTL_villagers[idx];
}

extern void mTL_ClearVillager(int idx) {
    mTL_villager_c* v = mTL_GetVillager(idx);

    if (v != NULL) {
        if (v->state != mTL_STATE_IDLE) {
            mTL_LeaveGroup(idx);
        }
        bzero(v, sizeof(*v));
        v->group = -1;
        v->escort = -1;
    }
}

extern void mTL_NoteEvent(int idx, int act, int event) {
    switch (event) {
        case 1:
            mTL_stats.started++;
            break;
        case 2:
            mTL_stats.ended++;
            break;
        case 3:
            mTL_stats.interrupted++;
            break;
    }

    mTL_stats.last_villager = idx;
    mTL_stats.last_activity = act;
    mTL_stats.last_event = event;
    mTL_stats.events++;
}

extern void mTL_NoteTalk(int idx) {
    mTL_villager_c* v = mTL_GetVillager(idx);

    if (v != NULL) {
        v->talk_activity = v->activity + 1;
        v->talk_sec = Common_Get(time.now_sec);
    }
}

extern int mTL_DecideTalkMsg(const Animal_c* animal, int looks) {
    mTL_villager_c* v = mTL_GetVillager(mNL_GetAnimalIdx(animal));
    const mTL_activity_c* a;
    int age;

    if (v == NULL || v->talk_activity == 0 || !mTL_IsEnabled()) {
        return -1;
    }

    a = mTL_GetActivity(v->talk_activity - 1);
    age = Common_Get(time.now_sec) - v->talk_sec;
    v->talk_activity = 0;
    if (age < 0) {
        age += 24 * 60 * 60;
    }
    /* (the chat comes right after the interruption) */
    if (age > 30 || a->line >= mTL_LINE_NUM || RANDOM(100) >= mTL_talk_chance) {
        return -1;
    }

    return mNL_TopicMsgNo(mNL_TOPIC_NUM + a->line, looks);
}

extern int mTL_CountBusy(void) {
    int n = 0;
    int i;

    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        n += mTL_villagers[i].state == mTL_STATE_DOING && mTL_villagers[i].activity != mTL_ACT_ESCORT;
    }

    return n;
}

extern int mTL_SecondsSince(int since_sec) {
    int d = Common_Get(time.now_sec) - since_sec;

    if (d < 0) {
        d += 24 * 60 * 60;
    }

    return d;
}

extern int mTL_SecondsDoing(const mTL_villager_c* v) {
    return mTL_SecondsSince(v->start_sec);
}

extern int mTL_GatherHour(void) {
    int hour = Common_Get(time.rtc_time).hour;

    /* morning, after lunch, evening */
    return mTL_test_group_any_hour || (hour >= 8 && hour < 10) || (hour >= 13 && hour < 15) ||
           (hour >= 17 && hour < 20);
}

extern int mTL_NewGroup(void) {
    int i;

    for (i = 0; i < mTL_GROUP_NUM; i++) {
        if (!mTL_groups[i].active) {
            bzero(&mTL_groups[i], sizeof(mTL_groups[i]));
            return i;
        }
    }

    return -1;
}

static void mTL_EndGroup(int group) {
    mTL_group_c* g = &mTL_groups[group];

    if (g->active) {
        g->active = FALSE;
        mTL_stats.groups_ended++;
    }
}

extern void mTL_LeaveGroup(int idx) {
    mTL_villager_c* v = mTL_GetVillager(idx);
    mTL_group_c* g;
    int left = 0;
    int i;

    if (v == NULL || v->group < 0 || v->group >= mTL_GROUP_NUM) {
        return;
    }

    g = &mTL_groups[v->group];
    v->group = -1;
    for (i = 0; i < g->count; i++) {
        if (g->members[i] == idx) {
            break;
        }
    }
    if (!g->active || i == g->count) {
        return;
    }

    g->members[i] = -1;
    g->arrived &= ~(1 << i);
    for (i = 0; i < g->count; i++) {
        left += g->members[i] >= 0;
    }

    if (left < 2) {
        mTL_EndGroup((int)(g - mTL_groups));
    }
}

extern int mTL_GroupTurn(int group) {
    mTL_group_c* g;
    int i;

    if (group < 0 || group >= mTL_GROUP_NUM || !mTL_groups[group].active) {
        return -1;
    }

    g = &mTL_groups[group];
    if (mTL_SecondsSince(g->start_sec) >= g->seconds) {
        mTL_EndGroup(group);
        return -1;
    }

    if (g->members[g->turn] < 0 || !(g->arrived & (1 << g->turn)) || mTL_SecondsSince(g->turn_sec) >= mTL_GROUP_TURN_SEC) {
        /* the next member at their place */
        for (i = 1; i <= g->count; i++) {
            int t = (g->turn + i) % g->count;

            if (g->members[t] >= 0 && (g->arrived & (1 << t))) {
                g->turn = t;
                g->turn_seq++;
                g->turn_sec = Common_Get(time.now_sec);
                return g->members[t];
            }
        }

        return -1;
    }

    return g->members[g->turn];
}

extern void mTL_GroupMeet(int a_idx, int b_idx) {
    Animal_c* animals = Save_Get(animals);
    int i;

    if (!mNL_IsEnabled() || a_idx < 0 || b_idx < 0 || a_idx >= ANIMAL_NUM_MAX || b_idx >= ANIMAL_NUM_MAX ||
        a_idx == b_idx) {
        return;
    }

    for (i = 0; i < 2; i++) {
        Animal_c* animal = &animals[i == 0 ? a_idx : b_idx];
        int other = i == 0 ? b_idx : a_idx;
        int rel = animal->animal_relations[other];

        /* a chat among friends: friends grow closer, acquaintances a little */
        rel += rel >= mNL_RELATION_FRIEND ? 3 : 1;
        animal->animal_relations[other] = MIN(255, rel);
    }

    if (animals[a_idx].animal_relations[b_idx] >= mNL_RELATION_FRIEND &&
        animals[b_idx].animal_relations[a_idx] >= mNL_RELATION_FRIEND &&
        (animals[a_idx].animal_relations[b_idx] < mNL_RELATION_FRIEND + 3 ||
         animals[b_idx].animal_relations[a_idx] < mNL_RELATION_FRIEND + 3)) {
        /* (one of them just crossed the line) */
        mNL_RecordPairEvent(mNL_EVENT_NEW_FRIENDS, a_idx, b_idx);
    }

    mNL_ShareNews(&animals[a_idx], &animals[b_idx]);
}

extern int mTL_SpotTaken(int idx, const xyz_t* pos) {
    int i;

    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        mTL_villager_c* v = &mTL_villagers[i];

        if (i != idx && v->state != mTL_STATE_IDLE) {
            f32 dx = pos->x - v->spot_x;
            f32 dz = pos->z - v->spot_z;

            if (dx * dx + dz * dz < SQ(1.5f * mFI_UT_WORLDSIZE_X_F)) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static int mTL_WeatherBit(void) {
    switch (Common_Get(weather)) {
        case mEnv_WEATHER_RAIN:
            return mTL_WEATHER_RAIN;
        case mEnv_WEATHER_SNOW:
            return mTL_WEATHER_SNOW;
        default:
            return mTL_WEATHER_CLEAR;
    }
}

extern int mTL_ActivityAllowed(int act) {
    const mTL_activity_c* a;
    int hour = Common_Get(time.rtc_time).hour;
    int season = Common_Get(time.season);

    if (act < 0 || act >= mTL_ACT_NUM) {
        return FALSE;
    }

    a = &mTL_activity_table[act];
    if ((a->weather & mTL_WeatherBit()) == 0) {
        return FALSE;
    }
    if (season >= 0 && season < mTM_SEASON_NUM && (a->seasons & (1 << season)) == 0) {
        return FALSE;
    }
    if (a->hour_from <= a->hour_to) {
        return hour >= a->hour_from && hour < a->hour_to;
    }

    return hour >= a->hour_from || hour < a->hour_to;
}

extern int mTL_ChooseActivity(int looks) {
    int total = 0;
    int roll;
    int act;

    if (mTL_test_activity >= 0 && mTL_test_activity <= mTL_ACT_TEST) {
        return mTL_test_activity;
    }

    if (looks < 0 || looks >= 6) {
        return -1;
    }

    for (act = 0; act < mTL_ACT_NUM; act++) {
        if (mTL_ActivityAllowed(act)) {
            total += mTL_activity_table[act].looks_weight[looks];
        }
    }

    if (total == 0) {
        return -1;
    }

    roll = RANDOM(total);
    for (act = 0; act < mTL_ACT_NUM; act++) {
        if (mTL_ActivityAllowed(act)) {
            roll -= mTL_activity_table[act].looks_weight[looks];
            if (roll < 0) {
                return act;
            }
        }
    }

    return -1;
}

/* The unit beside pos in direction (dx, dz), in units */
static xyz_t mTL_Beside(const xyz_t* pos, int dx, int dz) {
    xyz_t p = *pos;

    p.x += dx * mFI_UT_WORLDSIZE_X_F;
    p.z += dz * mFI_UT_WORLDSIZE_Z_F;
    return p;
}

static int mTL_IsWater(const xyz_t* pos) {
    return mCoBG_CheckWaterAttribute_OutOfSea(mCoBG_Wpos2Attribute(*pos, NULL));
}

static int mTL_IsTree(const xyz_t* pos) {
    mActor_name_t* fg = mFI_GetUnitFG(*pos);

    return fg != NULL && IS_ITEM_TREE(*fg) && !IS_ITEM_DEAD_SAPLING(*fg);
}

static const s8 mTL_around[8][2] = {
    { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 },
};

extern int mTL_CheckPlace(int place, int idx, const xyz_t* pos) {
    int bx;
    int bz;
    int i;

    switch (place) {
        case mTL_PLACE_ANYWHERE:
        case mTL_PLACE_OPEN:
            return TRUE;
        case mTL_PLACE_PLAZA:
            return mFI_Wpos2BlockNum(&bx, &bz, *pos) && mFI_CheckBlockKind(bx, bz, mRF_BLOCKKIND_SHRINE);
        case mTL_PLACE_RIVER:
            for (i = 0; i < 4; i++) {
                xyz_t p = mTL_Beside(pos, mTL_around[i][0], mTL_around[i][1]);

                if (mTL_IsWater(&p)) {
                    return TRUE;
                }
            }
            return FALSE;
        case mTL_PLACE_BEACH:
            return mCoBG_Wpos2Attribute(*pos, NULL) == mCoBG_ATTRIBUTE_SAND;
        case mTL_PLACE_TREE:
            for (i = 0; i < 4; i++) {
                xyz_t p = mTL_Beside(pos, mTL_around[i][0], mTL_around[i][1]);

                if (mTL_IsTree(&p)) {
                    return TRUE;
                }
            }
            return FALSE;
        case mTL_PLACE_HOME: {
            Animal_c* animal;
            xyz_t home;

            if (idx < 0 || idx >= ANIMAL_NUM_MAX) {
                return FALSE;
            }
            animal = &Save_Get(animals)[idx];
            if (animal->home_info.block_x == 0xFF) {
                return FALSE;
            }
            mFI_BkandUtNum2Wpos(&home, animal->home_info.block_x, animal->home_info.block_z, animal->home_info.ut_x,
                                animal->home_info.ut_z);
            return search_position_distanceXZ(&home, pos) < 4.0f * mFI_UT_WORLDSIZE_X_F;
        }
        default:
            return FALSE;
    }
}

extern s16 mTL_PlaceAngle(int place, const xyz_t* pos, s16 angle) {
    int i;

    switch (place) {
        case mTL_PLACE_RIVER:
            /* toward the water */
            for (i = 0; i < 4; i++) {
                xyz_t p = mTL_Beside(pos, mTL_around[i][0], mTL_around[i][1]);

                if (mTL_IsWater(&p)) {
                    return search_position_angleY(pos, &p);
                }
            }
            break;
        case mTL_PLACE_TREE:
            /* back to the tree */
            for (i = 0; i < 4; i++) {
                xyz_t p = mTL_Beside(pos, mTL_around[i][0], mTL_around[i][1]);

                if (mTL_IsTree(&p)) {
                    return search_position_angleY(&p, pos);
                }
            }
            break;
    }

    return angle;
}

#endif /* ACGC_PORT */
