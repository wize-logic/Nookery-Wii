#include "m_npc_life.h"

#ifdef ACGC_PORT

#include "m_common_data.h"
#include "m_msg.h"
#include "m_msg_data.h"
#include "sys_math.h"
#include "m_time.h"
#include "lb_rtc.h"
#include "pc_settings.h"

#include "../src/game/m_npc_life_msg.c_inc"

/* How much a topic is preferred over the others when several apply */
static const int mNL_topic_weight[mNL_TOPIC_NUM] = { 3, 3, 2, 2, 1, 2, 3, 3 };

/* Opinion at or above which a villager is sorry to see another move away */
#define mNL_RELATION_MISS 140

/* Opinions close to neutral: the speaker has met the other villager but has no view yet */
#define mNL_RELATION_NEUTRAL_MIN 116
#define mNL_RELATION_NEUTRAL_MAX 140

static u8 mNL_name_str[2][ANIMAL_NAME_LEN];

int mNL_gossip_chance = mNL_GOSSIP_CHANCE;

extern int mNL_IsEnabled(void) {
    return g_pc_settings.villager_life != 0;
}

static int mNL_IsTownAnimal(const Animal_c* animal) {
    return mNpc_CheckFreeAnimalPersonalID((AnmPersonalID_c*)&animal->id) == FALSE;
}

extern int mNL_GetAnimalIdx(const Animal_c* animal) {
    const Animal_c* animals = Save_Get(animals);

    if (animal != NULL && animal >= animals && animal < animals + ANIMAL_NUM_MAX) {
        return (int)(animal - animals);
    }

    return -1;
}

extern int mNL_GetRelation(const Animal_c* animal, int other_idx) {
    if (animal == NULL || other_idx < 0 || other_idx >= ANIMAL_NUM_MAX) {
        return 128;
    }

    return animal->animal_relations[other_idx];
}

extern void mNL_DriftRelations(void) {
    Animal_c* animals = Save_Get(animals);
    lbRTC_time_c* now = Common_GetPointer(time.rtc_time);
    lbRTC_time_c last;
    int days;
    int step;
    int i;
    int j;

    if (!mNL_IsEnabled() || now->year == 0 || Save_Get(renew_time).year == 0) {
        return;
    }

    /* renew_time: the day the town was last played (mTM_renewal_renew_time moves it on afterwards) */
    mTM_ymd_2_time(&last, Save_GetPointer(renew_time));
    days = lbRTC_GetIntervalDays(&last, now);
    if (days <= 0) {
        return;
    }
    if (days > mNL_DRIFT_MAX_DAYS) {
        days = mNL_DRIFT_MAX_DAYS;
    }

    step = days * mNL_DRIFT_PER_DAY;
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        u8* relations = animals[i].animal_relations;

        if (!mNL_IsTownAnimal(&animals[i])) {
            continue;
        }

        for (j = 0; j < ANIMAL_NUM_MAX; j++) {
            int rel = relations[j];

            if (rel > 128) {
                rel = MAX(128, rel - step);
            } else if (rel < 128) {
                rel = MIN(128, rel + step);
            }
            relations[j] = rel;
        }
    }
}

extern int mNL_IsExtMsg(int msg_no) {
    return msg_no >= mNL_MSG_BASE && msg_no < mNL_MSG_BASE + mNL_MSG_NUM;
}

extern int mNL_GetMsgData(int msg_no, const u8** data, u32* size) {
    if (!mNL_IsExtMsg(msg_no)) {
        return FALSE;
    }

    *data = mNL_msg_data[msg_no - mNL_MSG_BASE];
    *size = mNL_msg_size[msg_no - mNL_MSG_BASE];
    return TRUE;
}

static void mNL_SetNameFreeStr(int slot, int free_str_no, int animal_idx) {
    Animal_c* animal = &Save_Get(animals)[animal_idx];

    mNpc_GetNpcWorldNameAnm(mNL_name_str[slot], &animal->id);
    mMsg_SET_FREE_STR(free_str_no, mNL_name_str[slot], ANIMAL_NAME_LEN);
}

typedef struct {
    int found;
    int a;
    int b;
    int score;
} mNL_topic_pick_c;

/* Keeps the strongest candidate, breaking ties at random */
static void mNL_Consider(mNL_topic_pick_c* pick, int a, int b, int score, int* ties) {
    if (!pick->found || score > pick->score) {
        pick->found = TRUE;
        pick->a = a;
        pick->b = b;
        pick->score = score;
        *ties = 1;
    } else if (score == pick->score) {
        (*ties)++;
        if (RANDOM(*ties) == 0) {
            pick->a = a;
            pick->b = b;
        }
    }
}

extern int mNL_DecideGossipMsg(const Animal_c* speaker, int looks) {
    Animal_c* animals = Save_Get(animals);
    mNL_topic_pick_c pick[mNL_TOPIC_NUM];
    int ties[mNL_TOPIC_NUM];
    int self = mNL_GetAnimalIdx(speaker);
    int total_weight = 0;
    int topic;
    int roll;
    int i;
    int j;

    if (self < 0 || looks < 0 || looks >= mNL_MSG_LOOKS_NUM || mNL_MSG_TOPIC_NUM != mNL_TOPIC_NUM) {
        return -1;
    }

    bzero(pick, sizeof(pick));
    bzero(ties, sizeof(ties));

    /* The villager thinking of moving away (they tell the player so themselves) */
    i = Save_Get(remove_animal_idx);
    if (i != self && i >= 0 && i < ANIMAL_NUM_MAX && mNL_IsTownAnimal(&animals[i])) {
        int rel = speaker->animal_relations[i];

        if (rel >= mNL_RELATION_MISS) {
            mNL_Consider(&pick[mNL_TOPIC_MOVING_SAD], i, -1, rel, &ties[mNL_TOPIC_MOVING_SAD]);
        } else if (rel <= mNL_RELATION_RIVAL) {
            mNL_Consider(&pick[mNL_TOPIC_MOVING_GLAD], i, -1, 255 - rel, &ties[mNL_TOPIC_MOVING_GLAD]);
        }
    }

    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        int rel;

        if (i == self || !mNL_IsTownAnimal(&animals[i])) {
            continue;
        }

        /* The player's biggest fan, other than the speaker */
        if (Now_Private != NULL) {
            int mem_idx = mNpc_GetAnimalMemoryIdx(&Now_Private->player_ID, animals[i].memories, ANIMAL_MEMORY_NUM);

            if (mem_idx != -1 && animals[i].memories[mem_idx].friendship >= mNL_PLAYER_FAN_FRIENDSHIP) {
                mNL_Consider(&pick[mNL_TOPIC_PLAYER_FAN], i, -1, animals[i].memories[mem_idx].friendship,
                             &ties[mNL_TOPIC_PLAYER_FAN]);
            }
        }

        rel = speaker->animal_relations[i];
        if (rel >= mNL_RELATION_FRIEND) {
            mNL_Consider(&pick[mNL_TOPIC_LIKE], i, -1, rel, &ties[mNL_TOPIC_LIKE]);
        } else if (rel <= mNL_RELATION_RIVAL) {
            mNL_Consider(&pick[mNL_TOPIC_DISLIKE], i, -1, 255 - rel, &ties[mNL_TOPIC_DISLIKE]);
        } else if (rel >= mNL_RELATION_NEUTRAL_MIN && rel <= mNL_RELATION_NEUTRAL_MAX) {
            /* Any of them will do */
            mNL_Consider(&pick[mNL_TOPIC_NEUTRAL], i, -1, 0, &ties[mNL_TOPIC_NEUTRAL]);
        }

        /* What the speaker has noticed about two other villagers */
        for (j = i + 1; j < ANIMAL_NUM_MAX; j++) {
            int ab;
            int ba;

            if (j == self || !mNL_IsTownAnimal(&animals[j])) {
                continue;
            }

            ab = animals[i].animal_relations[j];
            ba = animals[j].animal_relations[i];
            if (ab >= mNL_RELATION_FRIEND && ba >= mNL_RELATION_FRIEND) {
                mNL_Consider(&pick[mNL_TOPIC_PAIR_CLOSE], i, j, ab + ba, &ties[mNL_TOPIC_PAIR_CLOSE]);
            } else if (ab <= mNL_RELATION_RIVAL || ba <= mNL_RELATION_RIVAL) {
                /* One grudge is enough for a feud */
                mNL_Consider(&pick[mNL_TOPIC_PAIR_FEUD], i, j, 510 - ab - ba, &ties[mNL_TOPIC_PAIR_FEUD]);
            }
        }
    }

    for (topic = 0; topic < mNL_TOPIC_NUM; topic++) {
        if (pick[topic].found) {
            total_weight += mNL_topic_weight[topic];
        }
    }

    if (total_weight == 0) {
        return -1;
    }

    roll = RANDOM(total_weight);
    for (topic = 0; topic < mNL_TOPIC_NUM; topic++) {
        if (pick[topic].found) {
            roll -= mNL_topic_weight[topic];
            if (roll < 0) {
                break;
            }
        }
    }

    if (topic >= mNL_TOPIC_NUM) {
        return -1;
    }

    mNL_SetNameFreeStr(0, mMsg_FREE_STR13, pick[topic].a);
    if (pick[topic].b >= 0) {
        mNL_SetNameFreeStr(1, mMsg_FREE_STR14, pick[topic].b);
    }

    return mNL_MSG_BASE + mNL_msg_topic_first[topic] + looks * mNL_msg_variants[topic] +
           RANDOM(mNL_msg_variants[topic]);
}

#endif /* ACGC_PORT */
