#ifndef M_TOWN_LIFE_H
#define M_TOWN_LIFE_H

/*
 * Town life (ACGC_PORT, setting town_life): villagers with things to do.
 *
 * Wandering villagers sometimes start an activity instead of a walk
 * (aNPC_town_life_decide, ac_npc_town_life.c_inc): they pick one from
 * mTL_activity_table by personality, time of day, weather and season, walk to a
 * spot that suits it (the river bank, a tree, near home...), play its animation
 * with its prop for a while, then go back to wandering. Talking to them, a
 * greeting, rain or the schedule ending it stops the activity cleanly.
 *
 * Nothing here is saved: the state of each villager lives in memory only.
 */

#include "types.h"
#include "m_npc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ACGC_PORT

/* Where an activity happens */
enum {
    mTL_PLACE_ANYWHERE, /* where the villager stands */
    mTL_PLACE_OPEN,     /* any free tile in the villager's area */
    mTL_PLACE_PLAZA,    /* the wishing well acre */
    mTL_PLACE_RIVER,    /* beside the river or a pond, facing the water */
    mTL_PLACE_BEACH,    /* on the sand */
    mTL_PLACE_TREE,     /* beside a tree */
    mTL_PLACE_HOME,     /* near the villager's own house */

    mTL_PLACE_NUM
};

/* Weather bits */
#define mTL_WEATHER_CLEAR (1 << 0) /* clear, cherry blossoms, falling leaves */
#define mTL_WEATHER_RAIN (1 << 1)
#define mTL_WEATHER_SNOW (1 << 2)
#define mTL_WEATHER_ANY (mTL_WEATHER_CLEAR | mTL_WEATHER_RAIN | mTL_WEATHER_SNOW)
#define mTL_WEATHER_DRY (mTL_WEATHER_CLEAR | mTL_WEATHER_SNOW)

/* Season bits (1 << mTM_SEASON_*) */
#define mTL_SEASON_SPRING (1 << 0)
#define mTL_SEASON_SUMMER (1 << 1)
#define mTL_SEASON_AUTUMN (1 << 2)
#define mTL_SEASON_WINTER (1 << 3)
#define mTL_SEASON_ANY 0x0F

/* No prop */
#define mTL_PROP_NONE 0xFF

/* What a villager says when talked to mid-activity: the ACT_* message topics (tools/npc_life_msg.py), which
 * follow the gossip topics (mNL_TOPIC_NUM + line) */
enum {
    mTL_LINE_SIT,
    mTL_LINE_DRINK,
    mTL_LINE_EXERCISE,
    mTL_LINE_DANCE,
    mTL_LINE_FISH,
    mTL_LINE_DOZE,
    mTL_LINE_FAN,
    mTL_LINE_PRAY,
    mTL_LINE_PHONE,

    mTL_LINE_NUM
};
#define mTL_LINE_NONE 0xFF

typedef struct {
    const char* name;  /* for logs */
    u8 anim;           /* aNPC_ANIM_* */
    u8 sub_anim;       /* aNPC_SUB_ANIM_* (the fan) */
    u8 prop;           /* TOOL_* held in the right hand, or mTL_PROP_NONE */
    u8 place;          /* mTL_PLACE_* */
    u8 line;           /* mTL_LINE_*: what they say when talked to while doing it */
    u8 looks_weight[6]; /* how much each personality (mNpc_LOOKS_*) likes it; 0 never */
    u8 hour_from;      /* from this hour... */
    u8 hour_to;        /* ...until before this one (wraps past midnight when smaller) */
    u8 weather;        /* mTL_WEATHER_* bits */
    u8 seasons;        /* mTL_SEASON_* bits */
    u16 min_sec;       /* how long it lasts */
    u16 max_sec;
} mTL_activity_c;

/* Each activity (mTL_activity_table) */
enum {
    mTL_ACT_SIT,
    mTL_ACT_SIT_DRINK,
    mTL_ACT_SIT_LAUGH,
    mTL_ACT_SIT_FAN,
    mTL_ACT_DOZE,
    mTL_ACT_DANCE,
    mTL_ACT_EXERCISE,
    mTL_ACT_EXERCISE2,
    mTL_ACT_JOG,
    mTL_ACT_WARM_UP,
    mTL_ACT_FISH,
    mTL_ACT_FAN,
    mTL_ACT_PRAY,
    mTL_ACT_LOOK_AROUND,
    mTL_ACT_PHONE,
    mTL_ACT_CHEER,
    mTL_ACT_WAVE_FLAG,

    mTL_ACT_NUM,
    mTL_ACT_TEST = mTL_ACT_NUM /* mTL_test_entry, for tests */
};

/* Per-villager state (memory only) */
enum {
    mTL_STATE_IDLE,  /* wandering */
    mTL_STATE_GOING, /* walking to the activity's spot */
    mTL_STATE_DOING, /* playing the activity */
};

typedef struct {
    u8 state;
    u8 activity;
    u8 tries;      /* walks left to reach the spot */
    u8 oneshot;    /* the animation stopped and was started again */
    s16 spot_x;
    s16 spot_z;
    s16 angle;     /* facing while doing it */
    s16 cooldown;  /* decisions to skip before the next activity */
    int seconds;   /* how long it lasts */
    int start_sec; /* game time (Common time.now_sec) it started */
    int repeats;   /* animation loops played */
    void* prop;    /* the prop actor held, or NULL */
    u8 talk_activity; /* the activity + 1 the player's chat interrupted, or 0 */
    int talk_sec;     /* when */
} mTL_villager_c;

extern const mTL_activity_c mTL_activity_table[mTL_ACT_NUM];
/* An activity of the table, or mTL_test_entry for mTL_ACT_TEST */
extern const mTL_activity_c* mTL_GetActivity(int act);

extern int mTL_IsEnabled(void);

/* The state of the villager at Save animals index idx, or NULL */
extern mTL_villager_c* mTL_GetVillager(int idx);
extern void mTL_ClearVillager(int idx);

/* An activity that suits looks (mNpc_LOOKS_*) now, chosen by weight, or -1 */
extern int mTL_ChooseActivity(int looks);
/* Whether an activity may start now (time, weather, season) */
extern int mTL_ActivityAllowed(int act);

/* Seconds of game time since the villager's activity started */
extern int mTL_SecondsDoing(const mTL_villager_c* v);
/* Whether another villager is going to or using a spot near pos */
extern int mTL_SpotTaken(int idx, const xyz_t* pos);

/* Whether a spot suits place for the villager at Save animals index idx */
extern int mTL_CheckPlace(int place, int idx, const xyz_t* pos);
/* The angle a villager at pos should face for place */
extern s16 mTL_PlaceAngle(int place, const xyz_t* pos, s16 angle);

/* Percent of wander decisions that consider an activity (tests raise it) */
#define mTL_START_CHANCE 15
extern int mTL_start_chance;
/* Decisions to wait after an activity before another */
#define mTL_COOLDOWN 6

/* Tests: start only this activity (-1: any), in place, only for this villager (-1: any), for this many
 * seconds (0: the activity's own) */
extern int mTL_test_activity;
extern int mTL_test_villager;
extern int mTL_test_seconds;
extern mTL_activity_c mTL_test_entry;

/* Counts for tests and logs */
typedef struct {
    int started;
    int ended;
    int interrupted;
    int no_spot;
    int last_villager;
    int last_activity;
    int last_event; /* 1 started, 2 ended, 3 interrupted */
    int events;     /* increases with every start/end/interruption */
    int last_interrupt_act; /* the act (aNPC_ACT_*) that took over at the last interruption */
} mTL_stats_c;
extern mTL_stats_c mTL_stats;
extern void mTL_NoteEvent(int idx, int act, int event);

/* The player's chat interrupted the villager's activity */
extern void mTL_NoteTalk(int idx);
/* For a chat that interrupted an activity: a line about it (sets nothing else), or -1. Forgets the activity. */
#define mTL_TALK_CHANCE 70
extern int mTL_talk_chance;
extern int mTL_DecideTalkMsg(const Animal_c* animal, int looks);

/* Number of villagers busy with an activity right now */
extern int mTL_CountBusy(void);

#endif /* ACGC_PORT */

#ifdef __cplusplus
}
#endif

#endif /* M_TOWN_LIFE_H */
