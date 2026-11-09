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
#include "m_snowman.h"
#include "m_event.h"
#include "m_mail.h"
#include "m_font.h"
#include "m_shop.h"
#include "m_house.h"
#include "m_post_office.h"
#include "m_private.h"
#include "m_land.h"
#include "lb_rtc.h"

/*
 * The activities. Animations come from aNPC_animeTable (any villager can play
 * any of them); props are tools held in the right hand. Weights per
 * personality: normal, peppy, lazy, jock, cranky, snooty.
 */
// clang-format off
const mTL_activity_c mTL_activity_table[mTL_ACT_NUM] = {
    /* name          anim                      sub_anim             prop           place               line
                     weights                hours   weather            seasons            secs */
    { "sit",         aNPC_ANIM_SITDOWN_WAIT1,  aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_TREE,     mTL_LINE_SIT, mTL_EFFECT_NONE,
                     { 3, 1, 4, 0, 2, 2 },  8, 20,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,    20, 45 },
    { "sit_drink",   aNPC_ANIM_SITDOWN_DRINK1, aNPC_SUB_ANIM_NONE,  TOOL_TUMBLER,  mTL_PLACE_TREE,     mTL_LINE_DRINK, mTL_EFFECT_NONE,
                     { 2, 1, 2, 0, 3, 4 }, 10, 22,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,    20, 40 },
    { "sit_laugh",   aNPC_ANIM_SITDOWN_CLAP1,  aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_RIVER,    mTL_LINE_SIT, mTL_EFFECT_NONE,
                     { 1, 3, 1, 1, 0, 0 },  9, 19,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,    10, 20 },
    { "sit_fan",     aNPC_ANIM_SITDOWN_WAIT1,  aNPC_SUB_ANIM_UTIWA, TOOL_UTIWA,    mTL_PLACE_TREE,     mTL_LINE_FAN, mTL_EFFECT_NONE,
                     { 2, 1, 3, 0, 2, 3 }, 11, 22,  mTL_WEATHER_CLEAR, mTL_SEASON_SUMMER, 20, 40 },
    { "doze",        aNPC_ANIM_KOKKURI_D1,     aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_TREE,     mTL_LINE_DOZE, mTL_EFFECT_NONE,
                     { 1, 0, 5, 0, 2, 0 }, 12, 17,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,    30, 60 },
    { "dance",       aNPC_ANIM_DANCE1,         aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_DANCE, mTL_EFFECT_NONE,
                     { 1, 4, 0, 1, 0, 1 }, 10, 22,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,    10, 20 },
    { "exercise",    aNPC_ANIM_TAISOU1,        aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_EXERCISE, mTL_EFFECT_NONE,
                     { 1, 1, 0, 5, 1, 0 },  5, 12,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,    15, 30 },
    { "exercise2",   aNPC_ANIM_TAISOU2,        aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_EXERCISE, mTL_EFFECT_NONE,
                     { 0, 1, 0, 4, 0, 1 },  5, 20,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,    15, 30 },
    { "jog",         aNPC_ANIM_ASIHUMI1,       aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_EXERCISE, mTL_EFFECT_NONE,
                     { 0, 2, 0, 4, 0, 0 },  6, 21,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,     8, 15 },
    { "warm_up",     aNPC_ANIM_WARMUP1,        aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_EXERCISE, mTL_EFFECT_NONE,
                     { 0, 1, 0, 4, 1, 0 },  5, 19,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,     8, 15 },
    { "fish",        aNPC_ANIM_TURI_WAIT1,     aNPC_SUB_ANIM_NONE,  TOOL_NPC_SAO,  mTL_PLACE_RIVER,    mTL_LINE_FISH, mTL_EFFECT_NONE,
                     { 2, 1, 3, 1, 4, 0 },  6, 19,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,    30, 60 },
    { "fan",         aNPC_ANIM_WAIT1,          aNPC_SUB_ANIM_UTIWA, TOOL_UTIWA,    mTL_PLACE_ANYWHERE, mTL_LINE_FAN, mTL_EFFECT_NONE,
                     { 1, 1, 1, 1, 1, 1 }, 10, 21,  mTL_WEATHER_CLEAR, mTL_SEASON_SUMMER, 15, 30 },
    { "pray",        aNPC_ANIM_OMAIRI1,        aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_PLAZA,    mTL_LINE_PRAY, mTL_EFFECT_NONE,
                     { 2, 1, 1, 1, 1, 1 },  6, 20,  mTL_WEATHER_ANY,   mTL_SEASON_ANY,     6, 12 },
    { "look_around", aNPC_ANIM_KYORO1,         aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_ANYWHERE, mTL_LINE_NONE, mTL_EFFECT_NONE,
                     { 1, 1, 1, 1, 1, 1 },  0, 24,  mTL_WEATHER_ANY,   mTL_SEASON_ANY,     4,  8 },
    { "phone",       aNPC_ANIM_KEITAI_TALK1,   aNPC_SUB_ANIM_NONE,  TOOL_KEITAI,   mTL_PLACE_ANYWHERE, mTL_LINE_PHONE, mTL_EFFECT_NONE,
                     { 1, 3, 0, 1, 0, 3 },  9, 23,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,    10, 20 },
    { "cheer",       aNPC_ANIM_BANZAI1,        aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_ANYWHERE, mTL_LINE_DANCE, mTL_EFFECT_NONE,
                     { 0, 2, 0, 2, 0, 0 },  8, 20,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,     3,  6 },
    { "wave_flag",   aNPC_ANIM_HATAFURI1,      aNPC_SUB_ANIM_NONE,  TOOL_FLAG,     mTL_PLACE_OPEN,     mTL_LINE_DANCE, mTL_EFFECT_NONE,
                     { 0, 1, 0, 1, 0, 0 },  9, 18,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,     6, 12 },
    /* tending the town */
    { "tend_flowers", aNPC_ANIM_TAMAHIROI1,    aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_FLOWERS,  mTL_LINE_NONE, mTL_EFFECT_NONE,
                     { 3, 2, 1, 1, 2, 3 },  7, 18,  mTL_WEATHER_DRY,   mTL_SEASON_SPRING | mTL_SEASON_SUMMER | mTL_SEASON_AUTUMN, 10, 20 },
    { "plant",       aNPC_ANIM_TAMAHIROI1,     aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_PLANT,    mTL_LINE_NONE, mTL_EFFECT_PLANT,
                     { 3, 3, 1, 2, 2, 3 },  7, 18,  mTL_WEATHER_CLEAR, mTL_SEASON_SPRING | mTL_SEASON_SUMMER | mTL_SEASON_AUTUMN,  8, 12 },
    { "pick_fruit",  aNPC_ANIM_GET_EAT1,       aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_FRUIT,    mTL_LINE_NONE, mTL_EFFECT_PICK,
                     { 2, 2, 4, 2, 1, 1 },  8, 19,  mTL_WEATHER_DRY,   mTL_SEASON_ANY,     6, 10 },
    { "snowman",     aNPC_ANIM_TAMAHIROI1,     aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_NONE, mTL_EFFECT_SNOWMAN,
                     { 2, 4, 1, 3, 1, 1 },  9, 17,  mTL_WEATHER_DRY,   mTL_SEASON_WINTER, 15, 25 },
    /* weather and night */
    { "splash",      aNPC_ANIM_ASIHUMI1,       aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_NONE, mTL_EFFECT_SPLASH,
                     { 1, 4, 0, 3, 0, 0 },  7, 20,  mTL_WEATHER_RAIN,  mTL_SEASON_ANY,     8, 15 },
    { "shelter",     aNPC_ANIM_KYORO1,         aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_TREE,     mTL_LINE_NONE, mTL_EFFECT_NONE,
                     { 2, 0, 3, 0, 2, 3 },  6, 22,  mTL_WEATHER_RAIN,  mTL_SEASON_ANY,    20, 40 },
    { "stargaze",    aNPC_ANIM_KYORO1,         aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_PLAZA,    mTL_LINE_NONE, mTL_EFFECT_STARS,
                     { 1, 1, 0, 2, 4, 4 }, 20,  4,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,    20, 40 },
    { "stargaze2",   aNPC_ANIM_KYORO1,         aNPC_SUB_ANIM_NONE,  mTL_PROP_NONE, mTL_PLACE_OPEN,     mTL_LINE_NONE, mTL_EFFECT_STARS,
                     { 0, 1, 0, 1, 3, 3 }, 20,  4,  mTL_WEATHER_CLEAR, mTL_SEASON_ANY,    20, 40 },
};
// clang-format on

static mTL_villager_c mTL_villagers[ANIMAL_NUM_MAX];
mTL_group_c mTL_groups[mTL_GROUP_NUM];
int mTL_group_chance = mTL_GROUP_CHANCE;
int mTL_test_group_any_hour = FALSE;
int mTL_test_snow = FALSE;

/* Standing in a group, and walking a friend home: not in the table, never chosen at random */
static const mTL_activity_c mTL_chat_entry = {
    "chat", aNPC_ANIM_WAIT1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE, mTL_PLACE_ANYWHERE, mTL_LINE_NONE, mTL_EFFECT_NONE, mTL_EFFECT_NONE,
    { 0, 0, 0, 0, 0, 0 }, 0, 24, mTL_WEATHER_ANY, mTL_SEASON_ANY, 60, 180,
};
static const mTL_activity_c mTL_escort_entry = {
    "escort", aNPC_ANIM_WALK1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE, mTL_PLACE_ANYWHERE, mTL_LINE_NONE, mTL_EFFECT_NONE, mTL_EFFECT_NONE,
    { 0, 0, 0, 0, 0, 0 }, 0, 24, mTL_WEATHER_ANY, mTL_SEASON_ANY, 30, 45,
};

int mTL_start_chance = mTL_START_CHANCE;
int mTL_ground_changes_allowed = TRUE;
int mTL_test_no_limits = FALSE;
int mTL_test_winter = FALSE;

/* Plantings today in town, and fruit picked today per villager (memory only) */
static int mTL_plant_stamp = -1;
static int mTL_plants_today;
static int mTL_pick_stamp[ANIMAL_NUM_MAX];
int mTL_talk_chance = mTL_TALK_CHANCE;
int mTL_test_activity = -1;
int mTL_test_villager = -1;
int mTL_test_seconds = 0;
int mTL_test_keep_place = FALSE;
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

extern int mTL_LineBark(int line) {
    static const s8 bark[mTL_LINE_NUM] = {
        mTL_BARK_SIT, mTL_BARK_DRINK, mTL_BARK_EXERCISE, mTL_BARK_DANCE, mTL_BARK_FISH,
        mTL_BARK_DOZE, mTL_BARK_FAN, mTL_BARK_PRAY, mTL_BARK_PHONE,
    };

    return line >= 0 && line < mTL_LINE_NUM ? bark[line] : -1;
}

extern int mTL_ChooseChatBark(int idx, int* a_idx, int* b_idx) {
    Animal_c* animals = Save_Get(animals);
    int friend_idx = -1;
    int rival_idx = -1;
    int pair_a = -1;
    int pair_b = -1;
    int seen = 0;
    int roll;
    int i;
    int j;

    *a_idx = -1;
    *b_idx = -1;
    if (idx < 0 || idx >= ANIMAL_NUM_MAX || !mNL_IsEnabled()) {
        return RANDOM(4) == 0 ? mTL_BARK_LAUGH : mTL_BARK_CHAT;
    }

    /* someone they like or dislike, and two others who are close, each picked at random */
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        int rel;

        if (i == idx || mNpc_CheckFreeAnimalPersonalID(&animals[i].id)) {
            continue;
        }
        rel = animals[idx].animal_relations[i];
        if (rel >= mNL_RELATION_FRIEND && (friend_idx < 0 || RANDOM(2))) {
            friend_idx = i;
        } else if (rel <= mNL_RELATION_RIVAL && (rival_idx < 0 || RANDOM(2))) {
            rival_idx = i;
        }
        for (j = i + 1; j < ANIMAL_NUM_MAX; j++) {
            if (j != idx && !mNpc_CheckFreeAnimalPersonalID(&animals[j].id) &&
                animals[i].animal_relations[j] >= mNL_RELATION_FRIEND &&
                animals[j].animal_relations[i] >= mNL_RELATION_FRIEND && RANDOM(++seen) == 0) {
                pair_a = i;
                pair_b = j;
            }
        }
    }

    if (mTL_VisitorSeen() && RANDOM(4) == 0) {
        *a_idx = mTL_VISITOR_IDX;
        return mTL_BARK_VISITOR;
    }

    roll = RANDOM(10);
    if (roll < 2 && friend_idx >= 0) {
        *a_idx = friend_idx;
        return mTL_BARK_LIKE;
    }
    if (roll < 3 && rival_idx >= 0) {
        *a_idx = rival_idx;
        return mTL_BARK_DISLIKE;
    }
    if (roll < 5 && pair_a >= 0) {
        *a_idx = pair_a;
        *b_idx = pair_b;
        return mTL_BARK_PAIR;
    }
    if (roll < 7 && mNL_CountEvents(&animals[idx]) > 0) {
        return mTL_BARK_NEWS;
    }

    return mTL_BARK_CHAT;
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

extern int mTL_Night(void) {
    int hour = Common_Get(time.rtc_time).hour;

    return hour >= 20 || hour < 4;
}

extern int mTL_NightOwl(int looks) {
    return looks == mNpc_LOOKS_GRIM_MAN || looks == mNpc_LOOKS_NANIWA_LADY || looks == mNpc_LOOKS_SPORT_MAN;
}

extern int mTL_GatherHour(void) {
    int hour = Common_Get(time.rtc_time).hour;

    /* morning, after lunch, evening; and at night, for the night owls (try_group checks who) */
    return mTL_test_group_any_hour || (hour >= 8 && hour < 10) || (hour >= 13 && hour < 15) ||
           (hour >= 17 && hour < 20) || (hour >= 21 && hour < 24);
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

/* Today, as a number that changes each day (and wraps) */
static int mTL_DayStamp(void) {
    lbRTC_time_c* now = Common_GetPointer(time.rtc_time);

    return (now->year * 372 + (now->month - 1) * 31 + (now->day - 1)) & 0xFF;
}

/* Days since a stamp (0-255) */
static int mTL_DaysSince(int stamp) {
    return (mTL_DayStamp() - stamp) & 0xFF;
}

/*
 * Two bytes per villager that persist: the last bytes of Animal_c.unused, after the friendship news memory
 * (m_npc_life.c, 22 bytes). [0] the day they last planted (+1, 0: never), [1] the day they last sent a gift.
 */
#define mTL_SPARE_OFS 22
static u8* mTL_Spare(Animal_c* animal) {
    return &animal->unused[mTL_SPARE_OFS];
}

typedef char mTL_spare_fits[(mTL_SPARE_OFS + 2 <= sizeof(((Animal_c*)0)->unused)) ? 1 : -1];

static int mTL_SnowmanSeason(void) {
    return mTL_test_winter || mEv_check_status(mEv_EVENT_SNOWMAN_SEASON, mEv_STATUS_ACTIVE);
}

extern int mTL_Snowy(void) {
    return mTL_test_snow || Common_Get(weather) == mEnv_WEATHER_SNOW || mTL_SnowmanSeason();
}

extern int mTL_ActivityBark(int act) {
    const mTL_activity_c* a = mTL_GetActivity(act);

    if (a->effect == mTL_EFFECT_STARS || (mTL_Night() && RANDOM(3) == 0)) {
        return mTL_BARK_STARS;
    }
    if (Common_Get(weather) == mEnv_WEATHER_RAIN && (a->effect == mTL_EFFECT_SPLASH || RANDOM(2) == 0)) {
        return mTL_BARK_RAIN;
    }
    if (mTL_Snowy() && RANDOM(3) == 0) {
        return mTL_BARK_SNOW;
    }

    return mTL_LineBark(a->line);
}

static int mTL_SnowmenInTown(void) {
    int n = 0;
    int i;

    for (i = 0; i < mSN_SAVE_COUNT; i++) {
        n += Save_Get(snowmen).snowmen_data[i].exists != 0;
    }

    return n;
}

/* The limits on changing the town, for the villager at idx */
static int mTL_EffectAllowed(int effect, int idx) {
    if (effect == mTL_EFFECT_NONE) {
        return TRUE;
    }
    if (!mTL_ground_changes_allowed || idx < 0 || idx >= ANIMAL_NUM_MAX) {
        return FALSE;
    }
    if (mTL_test_no_limits) {
        return effect != mTL_EFFECT_SNOWMAN || mTL_SnowmanSeason();
    }

    switch (effect) {
        case mTL_EFFECT_PLANT: {
            u8* spare = mTL_Spare(&Save_Get(animals)[idx]);

            if (mTL_plant_stamp == mTL_DayStamp() && mTL_plants_today >= mTL_PLANTS_PER_DAY) {
                return FALSE;
            }
            return spare[0] == 0 || mTL_DaysSince(spare[0] - 1) >= mTL_PLANT_DAYS;
        }
        case mTL_EFFECT_PICK:
            return mTL_pick_stamp[idx] != mTL_DayStamp() + 1;
        case mTL_EFFECT_SNOWMAN:
            return mTL_SnowmanSeason() && mTL_SnowmenInTown() == 0;
        default:
            return FALSE;
    }
}

extern int mTL_ActivityAllowed(int act, int idx) {
    const mTL_activity_c* a;
    int hour = Common_Get(time.rtc_time).hour;
    int season = Common_Get(time.season);

    if (act < 0 || act >= mTL_ACT_NUM) {
        return FALSE;
    }

    a = &mTL_activity_table[act];
    if (!mTL_EffectAllowed(a->effect, idx)) {
        return FALSE;
    }
    if (a->effect == mTL_EFFECT_SNOWMAN) {
        /* (the snowman season, not the calendar's winter) */
        season = mTL_SnowmanSeason() ? mTM_SEASON_WINTER : mTM_SEASON_SPRING;
    }
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

extern int mTL_ChooseActivity(int looks, int idx) {
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
        if (mTL_ActivityAllowed(act, idx)) {
            total += mTL_activity_table[act].looks_weight[looks];
        }
    }

    if (total == 0) {
        return -1;
    }

    roll = RANDOM(total);
    for (act = 0; act < mTL_ACT_NUM; act++) {
        if (mTL_ActivityAllowed(act, idx)) {
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

static int mTL_IsFruitTree(mActor_name_t item) {
    return item == TREE_APPLE_FRUIT || item == TREE_ORANGE_FRUIT || item == TREE_PEACH_FRUIT ||
           item == TREE_PEAR_FRUIT || item == TREE_CHERRY_FRUIT || item == TREE_PALM_FRUIT;
}

extern int mTL_GetHomePos(int idx, xyz_t* pos) {
    Animal_c* animal;

    if (idx < 0 || idx >= ANIMAL_NUM_MAX) {
        return FALSE;
    }
    animal = &Save_Get(animals)[idx];
    if (animal->home_info.block_x == 0xFF) {
        return FALSE;
    }
    mFI_BkandUtNum2CenterWpos(pos, animal->home_info.block_x, animal->home_info.block_z, animal->home_info.ut_x,
                              animal->home_info.ut_z);
    return TRUE;
}

/* Within this many tiles of the villager's house */
static int mTL_NearHome(int idx, const xyz_t* pos, f32 tiles) {
    Animal_c* animal;
    xyz_t home;

    if (idx < 0 || idx >= ANIMAL_NUM_MAX) {
        return FALSE;
    }
    animal = &Save_Get(animals)[idx];
    if (animal->home_info.block_x == 0xFF) {
        return FALSE;
    }
    mFI_BkandUtNum2CenterWpos(&home, animal->home_info.block_x, animal->home_info.block_z, animal->home_info.ut_x,
                              animal->home_info.ut_z);
    return search_position_distanceXZ(&home, pos) < tiles * mFI_UT_WORLDSIZE_X_F;
}

/* A building, or the tiles it stands on */
static int mTL_IsHouse(mActor_name_t item) {
    return ITEM_IS_NPC_HOUSE(item) || ITEM_IS_DUMMY_NPC_HOUSE(item) || ITEM_NAME_GET_TYPE(item) == NAME_TYPE_STRUCT ||
           (item >= DUMMY_START && item != 0xFFFF);
}

/*
 * A flower may go here: a free patch of grass (not a path worn into the ground, sand or a plot's soil), nothing
 * buried on or beside it, not just in front of a door, and not where the house already has its share of flowers.
 */
static int mTL_CanPlant(int idx, const xyz_t* pos) {
    mActor_name_t* fg = mFI_GetUnitFG(*pos);
    u32 attr = mCoBG_Wpos2Attribute(*pos, NULL);
    int flowers = 0;
    int dx;
    int dz;

    /* (mCoBG_CheckPlant: how well things grow here, or -1 where they die) */
    if (fg == NULL || *fg != EMPTY_NO || attr > mCoBG_ATTRIBUTE_GRASS3 || mCoBG_CheckPlant(*pos) < 0) {
        return FALSE;
    }

    for (dz = -1; dz <= 1; dz++) {
        for (dx = -1; dx <= 1; dx++) {
            xyz_t p = mTL_Beside(pos, dx, dz);

            if (mFI_Wpos2DepositGet(p)) {
                return FALSE;
            }
        }
    }

    /* doors face south: the two tiles below a house stay clear */
    for (dz = 1; dz <= 2; dz++) {
        xyz_t p = mTL_Beside(pos, 0, -dz);
        mActor_name_t* up = mFI_GetUnitFG(p);

        if (up != NULL && mTL_IsHouse(*up)) {
            return FALSE;
        }
    }

    for (dz = -3; dz <= 3; dz++) {
        for (dx = -3; dx <= 3; dx++) {
            xyz_t p = mTL_Beside(pos, dx, dz);
            mActor_name_t* f = mFI_GetUnitFG(p);

            flowers += f != NULL && IS_ITEM_FLOWER(*f);
        }
    }

    return flowers < mTL_PLANT_MAX_NEAR || mTL_test_no_limits;
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
        case mTL_PLACE_FLOWERS:
        case mTL_PLACE_PLANT:
        case mTL_PLACE_FRUIT:
            if (!mTL_NearHome(idx, pos, 5.0f)) {
                return FALSE;
            }
            for (i = 0; i < 4; i++) {
                xyz_t p = mTL_Beside(pos, mTL_around[i][0], mTL_around[i][1]);
                mActor_name_t* fg = mFI_GetUnitFG(p);

                if (fg == NULL) {
                    continue;
                }
                if ((place == mTL_PLACE_FLOWERS && IS_ITEM_FLOWER(*fg)) ||
                    (place == mTL_PLACE_FRUIT && mTL_IsFruitTree(*fg)) ||
                    (place == mTL_PLACE_PLANT && mTL_CanPlant(idx, &p))) {
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
        case mTL_PLACE_FLOWERS:
        case mTL_PLACE_PLANT:
        case mTL_PLACE_FRUIT:
            /* toward the flowers, the patch, the tree (with idx unknown here, any patch that's free grass) */
            for (i = 0; i < 4; i++) {
                xyz_t p = mTL_Beside(pos, mTL_around[i][0], mTL_around[i][1]);
                mActor_name_t* fg = mFI_GetUnitFG(p);

                if (fg != NULL && ((place == mTL_PLACE_FLOWERS && IS_ITEM_FLOWER(*fg)) ||
                                   (place == mTL_PLACE_FRUIT && mTL_IsFruitTree(*fg)) ||
                                   (place == mTL_PLACE_PLANT && *fg == EMPTY_NO &&
                                    mCoBG_Wpos2Attribute(p, NULL) <= mCoBG_ATTRIBUTE_GRASS3))) {
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

/* The tile in front of a villager standing at spot facing angle (the four directions) */
static xyz_t mTL_InFront(const xyz_t* spot, s16 angle) {
    xyz_t p = *spot;

    p.x += sin_s(angle) * mFI_UT_WORLDSIZE_X_F;
    p.z += cos_s(angle) * mFI_UT_WORLDSIZE_Z_F;
    mFI_Wpos2UtCenterWpos(&p, p);
    return p;
}

extern int mTL_DoEffect(int idx, int act, const xyz_t* spot, s16 angle, int at_end) {
    const mTL_activity_c* a = mTL_GetActivity(act);
    xyz_t front = mTL_InFront(spot, angle);
    mActor_name_t* fg = mFI_GetUnitFG(front);

    if (fg == NULL || !mTL_EffectAllowed(a->effect, idx)) {
        return FALSE;
    }

    switch (a->effect) {
        case mTL_EFFECT_PLANT: {
            static const mActor_name_t flowers[] = {
                FLOWER_PANSIES0, FLOWER_PANSIES1, FLOWER_PANSIES2, FLOWER_COSMOS0, FLOWER_COSMOS1,
                FLOWER_COSMOS2,  FLOWER_TULIP0,   FLOWER_TULIP1,   FLOWER_TULIP2,
            };
            mActor_name_t flower;

            if (!at_end || !mTL_CanPlant(idx, &front)) {
                return FALSE;
            }

            /* a villager keeps to one kind of flower */
            flower = flowers[(Save_Get(animals)[idx].id.npc_id + RANDOM(3)) % ARRAY_COUNT(flowers)];
            if (!mFI_SetFG_common(flower, front, TRUE)) {
                return FALSE;
            }

            if (mTL_plant_stamp != mTL_DayStamp()) {
                mTL_plant_stamp = mTL_DayStamp();
                mTL_plants_today = 0;
            }
            mTL_plants_today++;
            mTL_Spare(&Save_Get(animals)[idx])[0] = mTL_DayStamp() + 1;
            mNL_RecordPlanted(idx, flower);
            mTL_stats.planted++;
            mTL_Bark(idx, mTL_BARK_PLANTED, -1, -1);
            return TRUE;
        }
        case mTL_EFFECT_PICK:
            if (at_end || !mTL_IsFruitTree(*fg)) {
                return FALSE;
            }
            /* the full tree becomes the same tree without fruit (FRUIT is NOFRUIT_0 + 3), which fruits again */
            if (!mFI_SetFG_common(*fg - 3, front, TRUE)) {
                return FALSE;
            }
            mTL_pick_stamp[idx] = mTL_DayStamp() + 1;
            mTL_stats.picked++;
            return TRUE;
        case mTL_EFFECT_SNOWMAN: {
            mSN_snowman_info_c info;

            if (!at_end || *fg != EMPTY_NO || mFI_Wpos2DepositGet(front) || mSN_get_free_space() < 0) {
                return FALSE;
            }

            /* a well made one: the head a little smaller than the body */
            bzero(&info, sizeof(info));
            info.data.exists = TRUE;
            info.data.body_size = 160 + RANDOM(60);
            info.data.head_size = info.data.body_size * 3 / 4 + RANDOM(10);
            info.data.score = mSN_RESULT_GOOD;
            info.pos = front;
            mSN_regist_snowman_society(&info);
            mTL_stats.snowmen++;
            mTL_Bark(idx, mTL_BARK_SNOW, -1, -1);
            return TRUE;
        }
        default:
            return FALSE;
    }
}

/*
 * Gifts: at game start, each villager who is a good friend of a player of this town (Anmmem_c.friendship
 * mTL_GIFT_FRIENDSHIP or more), and hasn't sent a gift for a week, may send them a present in the mail; at most
 * one villager a day. The letter is the port's own text.
 */
#define mTL_GIFT_FRIENDSHIP 70
#define mTL_GIFT_DAYS 7
#define mTL_GIFT_CHANCE 25

static void mTL_CopyText(u8* dst, int size, const char* text) {
    int i;

    for (i = 0; i < size; i++) {
        dst[i] = CHAR_SPACE;
    }
    for (i = 0; i < size && text[i] != '\0'; i++) {
        dst[i] = text[i] == '\n' ? CHAR_NEW_LINE : (u8)text[i];
    }
}

static int mTL_SendGift(Animal_c* animal, int mem_idx) {
    static const char* const body[mNpc_LOOKS_NUM] = {
        "I saw this and thought of\nyou right away. I hope you\nlike it! Thanks for being\nsuch a good friend.",
        "OMG, I found the CUTEST\nthing and it just screamed\nyour name! It's yours now.\nBFFs forever!",
        "I was gonna keep this, but\nthen I thought, nah, you'd\nlike it more. Don't say I\nnever give you anything.",
        "Hey! I got you something.\nKeep up the hard work and\nstay strong out there!\nYou're a champ.",
        "Found this lying around.\nThought you could use it.\nDon't make a fuss about it.",
        "A little something for you,\ndarling. I do have such\ngood taste, don't I?\nYou may thank me later.",
    };
    PersonalID_c* pid = &animal->memories[mem_idx].memory_player_id;
    int player_no = mPr_GetPrivateIdx(pid);
    Mail_c mail;
    mHm_hs_c* home;
    mActor_name_t present;
    u8 name[ANIMAL_NAME_LEN];
    int looks = animal->id.looks < mNpc_LOOKS_NUM ? animal->id.looks : mNpc_LOOKS_GIRL;
    int free_idx;
    int name_len;

    if (player_no < 0 || player_no >= PLAYER_NUM) {
        return FALSE;
    }

    home = Save_GetPointer(homes[mHS_get_arrange_idx(player_no)]);
    if (!mPr_CheckCmpPersonalID(pid, &home->ownerID)) {
        return FALSE;
    }

    mSP_SelectRandomItem_New(NULL, &present, 1, NULL, 0, RANDOM(2) ? mSP_KIND_FURNITURE : mSP_KIND_CLOTH,
                             mSP_LISTTYPE_COMMON, FALSE);
    if (present == EMPTY_NO) {
        return FALSE;
    }

    mMl_clear_mail(&mail);
    mTL_CopyText(mail.content.header, MAIL_HEADER_LEN, "Dear ,");
    mail.content.header_back_start = 5;
    mTL_CopyText(mail.content.body, MAIL_BODY_LEN, body[looks]);
    mNpc_GetNpcWorldNameAnm(name, &animal->id);
    mTL_CopyText(mail.content.footer, MAIL_FOOTER_LEN, "");
    name_len = mMl_strlen(name, ANIMAL_NAME_LEN, CHAR_SPACE);
    mem_copy(mail.content.footer, name, name_len);
    mail.content.font = mMl_FONT_RECV;
    mail.content.mail_type = mMl_TYPE_MAIL;
    mail.content.paper_type = mNpc_GetPaperType();
    mPr_CopyPersonalID(&mail.header.recipient.personalID, pid);
    mail.header.recipient.type = mMl_NAME_TYPE_PLAYER;
    mMl_set_mail_name_npcinfo(&mail.header.sender, &animal->id);
    mail.present = present;

    free_idx = mMl_chk_mail_free_space(home->mailbox, HOME_MAILBOX_SIZE);
    if (free_idx != -1) {
        mMl_copy_mail(home->mailbox + free_idx, &mail);
        return TRUE;
    }
    if (mPO_get_keep_mail_sum() < mPO_MAIL_STORAGE_SIZE) {
        return mPO_receipt_proc(&mail, mPO_SENDTYPE_MAIL);
    }

    return FALSE;
}

extern void mTL_DailyGifts(void) {
    Animal_c* animals = Save_Get(animals);
    int start = RANDOM(ANIMAL_NUM_MAX);
    int n;

    if (!mTL_IsEnabled() || !mTL_ground_changes_allowed || Common_Get(time.rtc_time).year == 0) {
        return;
    }

    for (n = 0; n < ANIMAL_NUM_MAX; n++) {
        int i = (start + n) % ANIMAL_NUM_MAX;
        Animal_c* animal = &animals[i];
        u8* spare = mTL_Spare(animal);
        int m;

        if (mNpc_CheckFreeAnimalPersonalID(&animal->id) ||
            (!mTL_test_no_limits && ((spare[1] != 0 && mTL_DaysSince(spare[1] - 1) < mTL_GIFT_DAYS) ||
                                     RANDOM(100) >= mTL_GIFT_CHANCE))) {
            continue;
        }

        for (m = 0; m < ANIMAL_MEMORY_NUM; m++) {
            Anmmem_c* memory = &animal->memories[m];

            if (memory->friendship >= mTL_GIFT_FRIENDSHIP && mLd_CheckThisLand(memory->memory_player_id.land_name,
                                                                               memory->memory_player_id.land_id) &&
                mTL_SendGift(animal, m)) {
                spare[1] = mTL_DayStamp() + 1;
                mTL_stats.gifts++;
                mTL_stats.last_villager = i;
                return; /* one a day */
            }
        }
    }
}

#endif /* ACGC_PORT */
