#include "m_npc_life.h"

#ifdef ACGC_PORT

#include "m_common_data.h"
#include "m_msg.h"
#include "m_msg_data.h"
#include "m_item_name.h"
#include "m_land.h"
#include "sys_math.h"
#include "m_time.h"
#include "lb_rtc.h"
#include "pc_settings.h"

#include "../src/game/m_npc_life_msg.c_inc"

/* How much a topic is preferred over the others when several apply */
static const int mNL_topic_weight[mNL_TOPIC_NUM] = {
    3, 3, 2, 2, 1, /* like, dislike, pair close, pair feud, neutral */
    2, 3, 3,       /* fan, moving sad, moving glad */
    5, 5, 4,       /* news: made up, fell out, new friends */
    5, 5, 5,       /* the speaker made up, fell out; the player's catch */
    4, 3,          /* planted flowers: the speaker, another villager */
};

/* Opinion at or above which a villager is sorry to see another move away */
#define mNL_RELATION_MISS 140

/* Opinions close to neutral: the speaker has met the other villager but has no view yet */
#define mNL_RELATION_NEUTRAL_MIN 116
#define mNL_RELATION_NEUTRAL_MAX 140

/* A catch is witnessed by villagers who clap within this many seconds of it */
#define mNL_CATCH_WITNESS_SECONDS 30

static u8 mNL_name_str[2][ANIMAL_NAME_LEN];
static u8 mNL_item_str[mIN_ITEM_NAME_LEN];

int mNL_gossip_chance = mNL_GOSSIP_CHANCE;
int mNL_test_topic = -1;
int mNL_make_up_chance = mNL_MAKE_UP_CHANCE;

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

/*
 * Event memory, in Animal_c.unused: a marker, then three events of 7 bytes.
 * Pair events hold both villagers' Save animals indices and the low bytes of
 * their ids (so an event about a villager who has since moved out is dropped);
 * a catch holds the player's number and the item. All bytes, so the PC's save
 * byte swapping leaves them alone.
 */

#define mNL_MEMORY_MAGIC 0xA7
#define mNL_MEMORY_EVENTS 3

#define mNL_EVENT_TYPE_MASK 0x0F
#define mNL_EVENT_HEARD 0x80 /* heard from another villager */
#define mNL_EVENT_TOLD 0x40  /* already told to the player */

typedef struct {
    u8 type;
    u8 a;
    u8 b;
    u8 data0;
    u8 data1;
    u8 date[2]; /* year % 64, month, day */
} mNL_event_c;

typedef struct {
    u8 magic;
    mNL_event_c events[mNL_MEMORY_EVENTS];
} mNL_memory_c;

typedef char mNL_memory_fits[(sizeof(mNL_memory_c) <= sizeof(((Animal_c*)0)->unused)) ? 1 : -1];

static mActor_name_t mNL_catch_item = EMPTY_NO;
static lbRTC_time_c mNL_catch_time;

static mNL_memory_c* mNL_GetMemory(Animal_c* animal, int create) {
    mNL_memory_c* memory = (mNL_memory_c*)animal->unused;

    if (memory->magic != mNL_MEMORY_MAGIC) {
        if (!create) {
            return NULL;
        }
        bzero(animal->unused, sizeof(animal->unused));
        memory->magic = mNL_MEMORY_MAGIC;
    }

    return memory;
}

static void mNL_SetEventDate(mNL_event_c* event) {
    lbRTC_time_c* now = Common_GetPointer(time.rtc_time);
    u16 date = ((now->year & 63) << 9) | ((now->month & 15) << 5) | (now->day & 31);

    event->date[0] = date >> 8;
    event->date[1] = date & 0xFF;
}

/* Days since the event, or a large number when its date is unreadable */
static int mNL_EventAge(const mNL_event_c* event) {
    lbRTC_time_c* now = Common_GetPointer(time.rtc_time);
    lbRTC_time_c then;
    u16 date = (event->date[0] << 8) | event->date[1];
    int month = (date >> 5) & 15;
    int day = date & 31;

    if (month < 1 || month > 12 || day < 1) {
        return 999;
    }

    lbRTC_TimeCopy(&then, now);
    then.year = now->year - ((now->year - (date >> 9)) & 63);
    then.month = month;
    then.day = day;
    return lbRTC_GetIntervalDays(&then, now);
}

static int mNL_PairValid(const mNL_event_c* event) {
    Animal_c* animals = Save_Get(animals);

    return event->a < ANIMAL_NUM_MAX && event->b < ANIMAL_NUM_MAX && event->a != event->b &&
           mNL_IsTownAnimal(&animals[event->a]) && mNL_IsTownAnimal(&animals[event->b]) &&
           animals[event->a].id.name_id == event->data0 && animals[event->b].id.name_id == event->data1;
}

/* Still worth telling: the villagers or player it's about are still here, and it's recent */
static int mNL_EventFresh(const mNL_event_c* event) {
    int type = event->type & mNL_EVENT_TYPE_MASK;

    switch (type) {
        case mNL_EVENT_MADE_UP:
        case mNL_EVENT_FELL_OUT:
        case mNL_EVENT_NEW_FRIENDS:
            return mNL_PairValid(event) && mNL_EventAge(event) <= mNL_NEWS_DAYS;
        case mNL_EVENT_PLAYER_CATCH:
            return event->a < PLAYER_NUM && mNL_EventAge(event) <= mNL_CATCH_DAYS;
        case mNL_EVENT_PLANTED:
            return event->a < ANIMAL_NUM_MAX && mNL_IsTownAnimal(&Save_Get(animals)[event->a]) &&
                   Save_Get(animals)[event->a].id.name_id == event->b && mNL_EventAge(event) <= mNL_CATCH_DAYS;
        default:
            return FALSE;
    }
}

static int mNL_SameEvent(const mNL_event_c* e0, const mNL_event_c* e1) {
    int type = e0->type & mNL_EVENT_TYPE_MASK;

    if (type != (e1->type & mNL_EVENT_TYPE_MASK)) {
        return FALSE;
    }
    if (type == mNL_EVENT_PLAYER_CATCH) {
        return e0->a == e1->a && e0->data0 == e1->data0 && e0->data1 == e1->data1;
    }
    if (type == mNL_EVENT_PLANTED) {
        return e0->a == e1->a;
    }

    return (e0->a == e1->a && e0->b == e1->b) || (e0->a == e1->b && e0->b == e1->a);
}

/* Remembers an event: refreshes the same one, else takes a free or stale slot, else the oldest */
static void mNL_AddEvent(Animal_c* animal, const mNL_event_c* event) {
    mNL_memory_c* memory;
    mNL_event_c* slot = NULL;
    int oldest = -1;
    int i;

    if (mNL_GetAnimalIdx(animal) < 0 || !mNL_IsTownAnimal(animal)) {
        return;
    }

    memory = mNL_GetMemory(animal, TRUE);
    for (i = 0; i < mNL_MEMORY_EVENTS; i++) {
        mNL_event_c* e = &memory->events[i];

        if (mNL_EventFresh(e) && mNL_SameEvent(e, event)) {
            /* known already: firsthand news stays firsthand */
            u8 heard = e->type & event->type & mNL_EVENT_HEARD;

            *e = *event;
            e->type = (event->type & ~mNL_EVENT_HEARD) | heard;
            return;
        }
    }

    for (i = 0; i < mNL_MEMORY_EVENTS; i++) {
        mNL_event_c* e = &memory->events[i];
        int age;

        if (!mNL_EventFresh(e)) {
            slot = e;
            break;
        }

        age = mNL_EventAge(e);
        if (age > oldest) {
            oldest = age;
            slot = e;
        }
    }

    *slot = *event;
}

extern void mNL_RecordPairEvent(int type, int a_idx, int b_idx) {
    Animal_c* animals = Save_Get(animals);
    mNL_event_c event;

    if (!mNL_IsEnabled() || a_idx < 0 || a_idx >= ANIMAL_NUM_MAX || b_idx < 0 || b_idx >= ANIMAL_NUM_MAX ||
        a_idx == b_idx) {
        return;
    }

    bzero(&event, sizeof(event));
    event.type = type;
    event.a = a_idx;
    event.b = b_idx;
    event.data0 = animals[a_idx].id.name_id;
    event.data1 = animals[b_idx].id.name_id;
    mNL_SetEventDate(&event);
    mNL_AddEvent(&animals[a_idx], &event);
    mNL_AddEvent(&animals[b_idx], &event);
}

extern void mNL_SetPlayerCatch(mActor_name_t item) {
    mNL_catch_item = item;
    lbRTC_TimeCopy(&mNL_catch_time, Common_GetPointer(time.rtc_time));
}

extern void mNL_SawPlayerCatch(Animal_c* witness) {
    lbRTC_time_c* now = Common_GetPointer(time.rtc_time);
    int player_no = Common_Get(player_no);
    int seconds;
    mNL_event_c event;

    if (!mNL_IsEnabled() || mNL_catch_item == EMPTY_NO || player_no >= PLAYER_NUM || mLd_PlayerManKindCheck() ||
        now->year != mNL_catch_time.year || now->month != mNL_catch_time.month || now->day != mNL_catch_time.day) {
        return;
    }

    seconds = (now->hour - mNL_catch_time.hour) * 3600 + (now->min - mNL_catch_time.min) * 60 +
              (now->sec - mNL_catch_time.sec);
    if (seconds < 0 || seconds > mNL_CATCH_WITNESS_SECONDS) {
        return;
    }

    bzero(&event, sizeof(event));
    event.type = mNL_EVENT_PLAYER_CATCH;
    event.a = player_no;
    event.data0 = mNL_catch_item >> 8;
    event.data1 = mNL_catch_item & 0xFF;
    mNL_SetEventDate(&event);
    mNL_AddEvent(witness, &event);
}

extern void mNL_RecordPlanted(int idx, mActor_name_t flower) {
    Animal_c* animals = Save_Get(animals);
    mNL_event_c event;

    if (!mNL_IsEnabled() || idx < 0 || idx >= ANIMAL_NUM_MAX) {
        return;
    }

    bzero(&event, sizeof(event));
    event.type = mNL_EVENT_PLANTED;
    event.a = idx;
    event.b = animals[idx].id.name_id;
    event.data0 = flower >> 8;
    event.data1 = flower & 0xFF;
    mNL_SetEventDate(&event);
    mNL_AddEvent(&animals[idx], &event);
}

/* The freshest news teller has that listener hasn't heard and isn't part of */
extern void mNL_TellNews(Animal_c* teller, Animal_c* listener) {
    mNL_memory_c* memory = mNL_GetMemory(teller, FALSE);
    mNL_memory_c* known = mNL_GetMemory(listener, FALSE);
    int listener_idx = mNL_GetAnimalIdx(listener);
    mNL_event_c* best = NULL;
    int best_age = 999;
    int i;
    int j;

    if (memory == NULL || listener_idx < 0) {
        return;
    }

    for (i = 0; i < mNL_MEMORY_EVENTS; i++) {
        mNL_event_c* e = &memory->events[i];
        int type = e->type & mNL_EVENT_TYPE_MASK;
        int is_pair = type == mNL_EVENT_MADE_UP || type == mNL_EVENT_FELL_OUT || type == mNL_EVENT_NEW_FRIENDS;
        int age;

        /* (news about the listener isn't news to them) */
        if (!mNL_EventFresh(e) || (is_pair && (e->a == listener_idx || e->b == listener_idx)) ||
            (type == mNL_EVENT_PLANTED && e->a == listener_idx)) {
            continue;
        }

        if (known != NULL) {
            for (j = 0; j < mNL_MEMORY_EVENTS; j++) {
                if (mNL_EventFresh(&known->events[j]) && mNL_SameEvent(&known->events[j], e)) {
                    break;
                }
            }
            if (j < mNL_MEMORY_EVENTS) {
                continue;
            }
        }

        age = mNL_EventAge(e);
        if (age < best_age) {
            best_age = age;
            best = e;
        }
    }

    if (best != NULL) {
        mNL_event_c heard = *best;

        heard.type = (best->type & mNL_EVENT_TYPE_MASK) | mNL_EVENT_HEARD;
        mNL_AddEvent(listener, &heard);
    }
}

extern void mNL_ShareNews(Animal_c* animal0, Animal_c* animal1) {
    if (!mNL_IsEnabled() || animal0 == NULL || animal1 == NULL) {
        return;
    }

    /* (news the listener already knows is skipped, so the second doesn't just tell back the first's) */
    mNL_TellNews(animal0, animal1);
    mNL_TellNews(animal1, animal0);
}

extern int mNL_CountEvents(const Animal_c* animal) {
    mNL_memory_c* memory = mNL_GetMemory((Animal_c*)animal, FALSE);
    int count = 0;
    int i;

    if (memory != NULL) {
        for (i = 0; i < mNL_MEMORY_EVENTS; i++) {
            count += mNL_EventFresh(&memory->events[i]);
        }
    }

    return count;
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

extern int mNL_GetMsgTopic(int msg_no) {
    int topic;

    if (!mNL_IsExtMsg(msg_no)) {
        return -1;
    }

    for (topic = mNL_MSG_TOPIC_NUM - 1; topic > 0; topic--) {
        if (msg_no - mNL_MSG_BASE >= mNL_msg_topic_first[topic]) {
            break;
        }
    }

    return topic;
}

extern int mNL_TopicMsgNo(int topic, int looks) {
    if (topic < 0 || topic >= mNL_MSG_TOPIC_NUM || looks < 0 || looks >= mNL_MSG_LOOKS_NUM) {
        return -1;
    }

    return mNL_MSG_BASE + mNL_msg_topic_first[topic] + looks * mNL_msg_variants[topic] +
           RANDOM(mNL_msg_variants[topic]);
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
    mNL_event_c* event;
    int total; /* the weights of every candidate so far */
} mNL_topic_pick_c;

/* Picks among a topic's candidates at random, the stronger ones more often */
static void mNL_Consider(mNL_topic_pick_c* pick, int a, int b, mNL_event_c* event, int weight) {
    if (weight < 1) {
        weight = 1;
    }

    pick->total += weight;
    if (!pick->found || RANDOM(pick->total) < weight) {
        pick->found = TRUE;
        pick->a = a;
        pick->b = b;
        pick->event = event;
    }
}

static int mNL_DecideGossipMsgSub(const Animal_c* speaker, int looks) {
    Animal_c* animals = Save_Get(animals);
    mNL_topic_pick_c pick[mNL_TOPIC_NUM];
    mNL_memory_c* memory = mNL_GetMemory((Animal_c*)speaker, FALSE);
    int self = mNL_GetAnimalIdx(speaker);
    int total_weight = 0;
    int topic;
    int roll;
    int i;
    int j;

    if (self < 0 || looks < 0 || looks >= mNL_MSG_LOOKS_NUM || mNL_MSG_TOPIC_NUM < mNL_TOPIC_NUM) {
        return -1;
    }

    bzero(pick, sizeof(pick));

    /* News the speaker remembers and hasn't told yet */
    if (memory != NULL) {
        for (i = 0; i < mNL_MEMORY_EVENTS; i++) {
            mNL_event_c* e = &memory->events[i];
            int type = e->type & mNL_EVENT_TYPE_MASK;
            int weight;

            if ((e->type & mNL_EVENT_TOLD) || !mNL_EventFresh(e)) {
                continue;
            }

            weight = 1 + mNL_NEWS_DAYS - mNL_EventAge(e);
            if (type == mNL_EVENT_PLAYER_CATCH) {
                if (e->a == Common_Get(player_no) && !mLd_PlayerManKindCheck()) {
                    mNL_Consider(&pick[mNL_TOPIC_PLAYER_CATCH], -1, -1, e, weight);
                }
            } else if (type == mNL_EVENT_PLANTED) {
                mNL_Consider(&pick[e->a == self ? mNL_TOPIC_SELF_PLANTED : mNL_TOPIC_NEWS_PLANTED], e->a, -1, e,
                             weight);
            } else if (e->a == self || e->b == self) {
                int other = e->a == self ? e->b : e->a;

                if (type == mNL_EVENT_MADE_UP) {
                    mNL_Consider(&pick[mNL_TOPIC_SELF_MADE_UP], other, -1, e, weight);
                } else if (type == mNL_EVENT_FELL_OUT) {
                    mNL_Consider(&pick[mNL_TOPIC_SELF_FELL_OUT], other, -1, e, weight);
                }
            } else {
                static const int news_topic[] = { -1, mNL_TOPIC_NEWS_MADE_UP, mNL_TOPIC_NEWS_FELL_OUT,
                                                  mNL_TOPIC_NEWS_NEW_FRIENDS };

                mNL_Consider(&pick[news_topic[type]], e->a, e->b, e, weight);
            }
        }
    }

    /* The villager thinking of moving away (they tell the player so themselves) */
    i = Save_Get(remove_animal_idx);
    if (i != self && i >= 0 && i < ANIMAL_NUM_MAX && mNL_IsTownAnimal(&animals[i])) {
        int rel = speaker->animal_relations[i];

        if (rel >= mNL_RELATION_MISS) {
            mNL_Consider(&pick[mNL_TOPIC_MOVING_SAD], i, -1, NULL, 1);
        } else if (rel <= mNL_RELATION_RIVAL) {
            mNL_Consider(&pick[mNL_TOPIC_MOVING_GLAD], i, -1, NULL, 1);
        }
    }

    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        int rel;

        if (i == self || !mNL_IsTownAnimal(&animals[i])) {
            continue;
        }

        /* Fans of the player, other than the speaker */
        if (Now_Private != NULL) {
            int mem_idx = mNpc_GetAnimalMemoryIdx(&Now_Private->player_ID, animals[i].memories, ANIMAL_MEMORY_NUM);

            if (mem_idx != -1 && animals[i].memories[mem_idx].friendship >= mNL_PLAYER_FAN_FRIENDSHIP) {
                mNL_Consider(&pick[mNL_TOPIC_PLAYER_FAN], i, -1, NULL,
                             1 + animals[i].memories[mem_idx].friendship - mNL_PLAYER_FAN_FRIENDSHIP);
            }
        }

        rel = speaker->animal_relations[i];
        if (rel >= mNL_RELATION_FRIEND) {
            mNL_Consider(&pick[mNL_TOPIC_LIKE], i, -1, NULL, 1 + rel - mNL_RELATION_FRIEND);
        } else if (rel <= mNL_RELATION_RIVAL) {
            mNL_Consider(&pick[mNL_TOPIC_DISLIKE], i, -1, NULL, 1 + mNL_RELATION_RIVAL - rel);
        } else if (rel >= mNL_RELATION_NEUTRAL_MIN && rel <= mNL_RELATION_NEUTRAL_MAX) {
            mNL_Consider(&pick[mNL_TOPIC_NEUTRAL], i, -1, NULL, 1);
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
                mNL_Consider(&pick[mNL_TOPIC_PAIR_CLOSE], i, j, NULL, 1 + ab + ba - 2 * mNL_RELATION_FRIEND);
            } else if (ab <= mNL_RELATION_RIVAL || ba <= mNL_RELATION_RIVAL) {
                /* One grudge is enough for a feud */
                mNL_Consider(&pick[mNL_TOPIC_PAIR_FEUD], i, j, NULL, 1 + mNL_RELATION_RIVAL - MIN(ab, ba));
            }
        }
    }

    for (topic = 0; topic < mNL_TOPIC_NUM; topic++) {
        if (mNL_test_topic >= 0 && topic != mNL_test_topic) {
            pick[topic].found = FALSE;
        }
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

    if (pick[topic].event != NULL) {
        /* told once */
        pick[topic].event->type |= mNL_EVENT_TOLD;
    }

    if (topic == mNL_TOPIC_PLAYER_CATCH || topic == mNL_TOPIC_SELF_PLANTED || topic == mNL_TOPIC_NEWS_PLANTED) {
        mActor_name_t item = (pick[topic].event->data0 << 8) | pick[topic].event->data1;

        mIN_copy_name_str(mNL_item_str, item);
        mMsg_SET_ITEM_STR_ART(mMsg_ITEM_STR0, mNL_item_str, mIN_ITEM_NAME_LEN, item);
        if (topic == mNL_TOPIC_NEWS_PLANTED) {
            mNL_SetNameFreeStr(0, mMsg_FREE_STR13, pick[topic].a);
        }
    } else {
        mNL_SetNameFreeStr(0, mMsg_FREE_STR13, pick[topic].a);
        if (pick[topic].b >= 0) {
            mNL_SetNameFreeStr(1, mMsg_FREE_STR14, pick[topic].b);
        }
    }

    return mNL_MSG_BASE + mNL_msg_topic_first[topic] + looks * mNL_msg_variants[topic] +
           RANDOM(mNL_msg_variants[topic]);
}

int mNL_decide_count;
int mNL_decide_last[3];

extern int mNL_DecideGossipMsg(const Animal_c* speaker, int looks) {
    int msg_no = mNL_DecideGossipMsgSub(speaker, looks);

    /* (for tests: who asked, as what personality, and the answer) */
    mNL_decide_count++;
    mNL_decide_last[0] = mNL_GetAnimalIdx(speaker);
    mNL_decide_last[1] = looks;
    mNL_decide_last[2] = msg_no;
    return msg_no;
}

#endif /* ACGC_PORT */
