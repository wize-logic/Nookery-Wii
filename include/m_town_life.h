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
    mTL_PLACE_FLOWERS,  /* beside flowers near home, facing them */
    mTL_PLACE_PLANT,    /* beside a free patch of grass near home where a flower may go, facing it */
    mTL_PLACE_FRUIT,    /* beside a fruit tree with fruit near home, facing it */

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

/* What an activity changes in town (only where this console may change the ground) */
enum {
    mTL_EFFECT_NONE,
    mTL_EFFECT_PLANT,   /* at the end: a flower on the tile in front */
    mTL_EFFECT_PICK,    /* at the start: the fruit off the tree in front (then eaten) */
    mTL_EFFECT_SNOWMAN, /* at the end: a snowman on the tile in front */

    mTL_EFFECT_NUM
};

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
    u8 effect;         /* mTL_EFFECT_* */
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
    mTL_ACT_TEND_FLOWERS,
    mTL_ACT_PLANT,
    mTL_ACT_PICK_FRUIT,
    mTL_ACT_SNOWMAN,

    mTL_ACT_NUM,
    mTL_ACT_TEST = mTL_ACT_NUM, /* mTL_test_entry, for tests */
    mTL_ACT_CHAT,               /* standing in a group (mTL_group_c), taking turns to talk */
    mTL_ACT_ESCORT,             /* walking a friend home */
};

/* Per-villager state (memory only) */
enum {
    mTL_STATE_IDLE,  /* wandering */
    mTL_STATE_GOING, /* walking to the activity's spot */
    mTL_STATE_DOING, /* playing the activity */
    mTL_STATE_ESCORT, /* walking beside a friend who is going home */
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
    s8 group;         /* mTL_groups index while in a group, else -1 (only meaningful when not idle) */
    u8 turn_seen;     /* the group's turn this villager last reacted to */
    s8 escort;        /* the friend (Save animals index) being walked home, for mTL_ACT_ESCORT */
    s16 last_dist;    /* distance to the spot at the last walk toward it */
} mTL_villager_c;

/*
 * Gatherings: at set times a villager may call friends in the same acre over to a spot (the plaza, a tree, the
 * river bank, open ground). Two to four stand in a circle facing the center and take turns to talk and react.
 * Rivals of anyone in the group don't join. Meeting counts like a greeting (opinions, news), and the group
 * breaks up after a few minutes or when its members' schedules change.
 */
#define mTL_GROUP_NUM 4
#define mTL_GROUP_MAX 4
#define mTL_GROUP_TURN_SEC 4 /* how long each member talks */

typedef struct {
    u8 active;
    u8 count;
    s8 members[mTL_GROUP_MAX]; /* Save animals indices, -1 for a member who left */
    u8 arrived;                /* bits of members at their places */
    u8 met;                    /* bits of members whose meeting with the others was counted */
    u8 turn;                   /* the member talking */
    u8 turn_seq;               /* increases with each turn */
    s16 center_x;
    s16 center_z;
    int start_sec;
    int seconds;
    int turn_sec;
} mTL_group_c;

extern mTL_group_c mTL_groups[mTL_GROUP_NUM];

/* Hours villagers gather in, and the percent of wander decisions in them that try to start a group */
#define mTL_GROUP_CHANCE 20
extern int mTL_group_chance;
extern int mTL_GatherHour(void);
/* A free group, or -1 */
extern int mTL_NewGroup(void);
/* A member leaves (interrupted, or the group's time is up); the group ends below two members */
extern void mTL_LeaveGroup(int idx);
/* Advances the turns; returns the talking member's Save animals index, or -1 */
extern int mTL_GroupTurn(int group);
/* Two members meet: like a greeting (opinions and news), once per pair and group */
extern void mTL_GroupMeet(int a_idx, int b_idx);
/* Seconds of game time since since_sec */
extern int mTL_SecondsSince(int since_sec);

extern const mTL_activity_c mTL_activity_table[mTL_ACT_NUM];
/* An activity of the table, or mTL_test_entry for mTL_ACT_TEST */
extern const mTL_activity_c* mTL_GetActivity(int act);

extern int mTL_IsEnabled(void);

/* The state of the villager at Save animals index idx, or NULL */
extern mTL_villager_c* mTL_GetVillager(int idx);
extern void mTL_ClearVillager(int idx);

/* An activity that suits looks (mNpc_LOOKS_*) now for the villager at Save animals index idx, chosen by weight,
 * or -1 */
extern int mTL_ChooseActivity(int looks, int idx);
/* Whether an activity may start now (time, weather, season, and for ones that change the town, its limits) */
extern int mTL_ActivityAllowed(int act, int idx);

/*
 * Tending the town. Plantings are limited (mTL_PLANTS_PER_DAY in town, one per villager every
 * mTL_PLANT_DAYS days, none near a house already with mTL_PLANT_MAX_NEAR flowers) so the town rating isn't swamped;
 * fruit is picked once a day per villager; a snowman only when the town has none. The online play code clears
 * mTL_ground_changes_allowed on a guest, whose ground comes from the host.
 */
#define mTL_PLANTS_PER_DAY 2
#define mTL_PLANT_DAYS 2
#define mTL_PLANT_MAX_NEAR 6
extern int mTL_ground_changes_allowed;
/* Does an activity's effect now, at its start or end; the spot and angle are where the villager stands */
extern int mTL_DoEffect(int idx, int act, const xyz_t* spot, s16 angle, int at_end);
/* At game start: villagers fond of a player sometimes send them a present by mail */
extern void mTL_DailyGifts(void);
/* Tests: no limits on plantings, fruit, snowmen and gifts; the snowman season */
extern int mTL_test_no_limits;
extern int mTL_test_winter;

/* Seconds of game time since the villager's activity started */
extern int mTL_SecondsDoing(const mTL_villager_c* v);
/* Whether another villager is going to or using a spot near pos */
extern int mTL_SpotTaken(int idx, const xyz_t* pos);

/* The middle of a villager's house (Save animals index); FALSE when they have none */
extern int mTL_GetHomePos(int idx, xyz_t* pos);

/* Whether a spot suits place for the villager at Save animals index idx */
extern int mTL_CheckPlace(int place, int idx, const xyz_t* pos);
/* The angle a villager at pos should face for place */
extern s16 mTL_PlaceAngle(int place, const xyz_t* pos, s16 angle);

/* Percent of wander decisions that consider an activity (tests raise it) */
#define mTL_START_CHANCE 15
extern int mTL_start_chance;
/* Decisions to wait after an activity before another */
#define mTL_COOLDOWN 6

/* Tests: groups form at any hour */
extern int mTL_test_group_any_hour;

/* Tests: start only this activity (-1: any), in place, only for this villager (-1: any), for this many
 * seconds (0: the activity's own) */
extern int mTL_test_activity;
extern int mTL_test_villager;
extern int mTL_test_seconds;
extern int mTL_test_keep_place; /* the forced activity still goes to its own kind of place */
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
    int visitor_days;
    int visitor_spawns;
    int visitor_step; /* where the visitor's appearing got to: 1 their acre, 2 mask record, 3 a tile, 4 made */
    int planted;
    int picked;
    int snowmen;
    int gifts;
    int groups_formed;
    int groups_ended;
    int group_members; /* members of all groups formed */
    int escorts;
} mTL_stats_c;
extern mTL_stats_c mTL_stats;
extern void mTL_NoteEvent(int idx, int act, int event);

/* The player's chat interrupted the villager's activity */
extern void mTL_NoteTalk(int idx);
/* For a chat that interrupted an activity: a line about it (sets nothing else), or -1. Forgets the activity. */
#define mTL_TALK_CHANCE 70
extern int mTL_talk_chance;
extern int mTL_DecideTalkMsg(const Animal_c* animal, int looks);

/*
 * Speech bubbles (m_town_life_bubble.c): short lines villagers say to each other near the player. Topics, in the
 * order of tools/town_life_barks.py.
 */
enum {
    mTL_BARK_CHAT,
    mTL_BARK_LAUGH,
    mTL_BARK_LIKE,    /* {A}: a friend */
    mTL_BARK_DISLIKE, /* {A}: a rival */
    mTL_BARK_PAIR,    /* {A} and {B} */
    mTL_BARK_NEWS,
    mTL_BARK_EXERCISE,
    mTL_BARK_SIT,
    mTL_BARK_FISH,
    mTL_BARK_DANCE,
    mTL_BARK_DRINK,
    mTL_BARK_DOZE,
    mTL_BARK_FAN,
    mTL_BARK_PRAY,
    mTL_BARK_PHONE,
    mTL_BARK_RAIN,
    mTL_BARK_SNOW,
    mTL_BARK_STARS,
    mTL_BARK_PLANTED,
    mTL_BARK_VISITOR, /* {A}: the visitor */
    mTL_BARK_GREET,

    mTL_BARK_NUM
};

/* The villager (Save animals index) says a line of topic, with {A} and {B} as these villagers: shown when the
 * player is near, nothing else is being said and it's been a while since they last spoke. TRUE when shown. */
extern int mTL_Bark(int idx, int topic, int a_idx, int b_idx);
/* The bark topic for an activity's line (mTL_LINE_*), or -1 */
extern int mTL_LineBark(int line);
/* A group chat line for the speaker: a topic, with its villagers in *a_idx and *b_idx */
extern int mTL_ChooseChatBark(int idx, int* a_idx, int* b_idx);
/* Each frame, from Game_play_move / Game_play_draw */
struct game_play_s;
extern void mTL_BubbleMove(struct game_play_s* play);
extern void mTL_BubbleDraw(struct game_play_s* play);
extern void mTL_BubbleClear(void);
/* Bubbles on screen now */
extern int mTL_BubblesShowing(void);

typedef struct {
    int shown;
    int last_villager;
    int last_topic;
    int screen_y; /* where the last bubble's speaker is on screen (0-240) */
} mTL_bubble_stats_c;
extern mTL_bubble_stats_c mTL_bubble_stats;

/*
 * Visitors (m_town_life_visitor.c): some days a villager who doesn't live in town spends the day here.
 */
#define mTL_VISITOR_CHANCE 30      /* percent of days */
#define mTL_VISITOR_MOVE_SEC (8 * 60) /* game seconds between their moves to another acre */
#define mTL_VISITOR_IDX 100        /* stands for the visitor where a Save animals index is expected (barks) */
extern int mTL_visitor_chance;
extern int mTL_test_visitor; /* tests: a visitor today, at any hour */
/* Tests: decide today's visitor again, in this acre (field block numbers) */
extern void mTL_TestVisitorHere(int bx, int bz);
extern void mTL_VisitorMove(struct game_play_s* play);
/* Today's visitor (an NPC id), or EMPTY_NO */
extern mActor_name_t mTL_GetVisitor(void);
/* Whether today's visitor has been about town near the player (worth gossiping about) */
extern int mTL_VisitorSeen(void);
/* The visitor's npclist entry, for mNpc_SetNpcinfo (the game's talk code reads one), or NULL when npc_id isn't
 * today's visitor */
extern mNpc_NpcList_c* mTL_VisitorList(mActor_name_t npc_id);
/* The visitor's name (ANIMAL_NAME_LEN characters) */
extern const u8* mTL_VisitorName(void);

/* Number of villagers busy with an activity right now */
extern int mTL_CountBusy(void);

#endif /* ACGC_PORT */

#ifdef __cplusplus
}
#endif

#endif /* M_TOWN_LIFE_H */
