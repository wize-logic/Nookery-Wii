#ifndef M_NPC_LIFE_H
#define M_NPC_LIFE_H

/*
 * Villager friendships (ACGC_PORT, setting villager_life).
 *
 * Every villager keeps a 0-255 opinion of each other villager
 * (Animal_c.animal_relations, 128 = neutral). The original game changes it when
 * two villagers greet each other but never reads it. This module reads it:
 * villagers walk over to friends and away from rivals (ac_npc_think_wander.c_inc),
 * friendships and grudges deepen when they meet (ac_npc_act_greeting.c_inc), and
 * villagers gossip about it when the player talks to them
 * (ac_quest_talk_normal_init.c).
 *
 * The gossip lines are new messages, served from mNL_MSG_BASE up by
 * mMsg_LoadMsgData (m_npc_life_msg.txt, encoded by tools/npc_life_msg.py).
 *
 * Opinions drift back toward neutral each day (mNL_DriftRelations at game
 * start), and rivals who meet can make up, so friendships need keeping up and
 * feuds fade.
 */

#include "types.h"
#include "m_npc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ACGC_PORT

/* Message numbers past the game's own (MSG_MAX) */
#define mNL_MSG_BASE 0x4000

/* Opinion thresholds (animal_relations) */
#define mNL_RELATION_FRIEND 152 /* at or above: a friend */
#define mNL_RELATION_RIVAL 104  /* at or below: a rival */

/* Daily drift toward neutral, for up to mNL_DRIFT_MAX_DAYS days since the town was last played */
#define mNL_DRIFT_PER_DAY 2
#define mNL_DRIFT_MAX_DAYS 30

/* Rivals who meet make up this often (percent), back to this opinion */
#define mNL_MAKE_UP_CHANCE 20
#define mNL_MAKE_UP_RELATION 120

/* A villager this fond of the player (Anmmem_c.friendship) is a fan */
#define mNL_PLAYER_FAN_FRIENDSHIP 60

/* Percent of ordinary chats that become gossip, when there is something to say
 * (a variable for tests: settings life_test on the Wii raises it to 100) */
#define mNL_GOSSIP_CHANCE 30
extern int mNL_gossip_chance;

enum {
    mNL_TOPIC_LIKE,       /* the speaker likes STR_FREE13 */
    mNL_TOPIC_DISLIKE,    /* the speaker dislikes STR_FREE13 */
    mNL_TOPIC_PAIR_CLOSE, /* STR_FREE13 and STR_FREE14 like each other */
    mNL_TOPIC_PAIR_FEUD,  /* STR_FREE13 and STR_FREE14 don't get along */
    mNL_TOPIC_NEUTRAL,    /* the speaker barely knows STR_FREE13 */
    mNL_TOPIC_PLAYER_FAN, /* STR_FREE13 is fond of the player */
    mNL_TOPIC_MOVING_SAD, /* STR_FREE13, a friend, is thinking of moving away */
    mNL_TOPIC_MOVING_GLAD, /* STR_FREE13, a rival, is thinking of moving away */

    mNL_TOPIC_NUM
};

extern int mNL_IsEnabled(void);

/* Index of a town villager in Save animals, or -1 (islander, event NPC, NULL) */
extern int mNL_GetAnimalIdx(const Animal_c* animal);

/* animal's opinion of the villager at other_idx (128 when unknown) */
extern int mNL_GetRelation(const Animal_c* animal, int other_idx);

/* At game start: drifts every opinion toward neutral for the days since the town was last played */
extern void mNL_DriftRelations(void);

/* Picks gossip for speaker and sets STR_FREE13/14. Returns a message number or -1. */
extern int mNL_DecideGossipMsg(const Animal_c* speaker, int looks);

/* Gossip message lookup for mMsg_LoadMsgData */
extern int mNL_IsExtMsg(int msg_no);
extern int mNL_GetMsgData(int msg_no, const u8** data, u32* size);

/* A message number the message window will load: the game's own or a gossip line */
#define mNL_MSG_NO_VALID(msg_no) (((msg_no) >= 0 && (msg_no) < MSG_MAX) || mNL_IsExtMsg(msg_no))

#endif /* ACGC_PORT */

#ifdef __cplusplus
}
#endif

#endif /* M_NPC_LIFE_H */
