#include "m_npc_schedule.h"

#include "m_common_data.h"
#ifdef ACGC_PORT
#include "m_town_life.h"
#include "m_kankyo.h"
#endif

#define mNPS_TIME_2_SEC(hour, min, sec) ((hour) * 3600 + (min) * 60 + (sec))
#define mNPS_MAKE_SCHEDULE_TABLE(sched_data) { ARRAY_SIZE(sched_data, mNPS_schedule_data_c), sched_data }

static mNPS_schedule_data_c girl_schedule_dt[] = {
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(5, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(6, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(12, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(13, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(18, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(21, 0, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_table_c girl_schedule = mNPS_MAKE_SCHEDULE_TABLE(girl_schedule_dt);

static mNPS_schedule_data_c ko_girl_schedule_dt[] = {
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(7, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(8, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(13, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(14, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(22, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(23, 30, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_table_c ko_girl_schedule = mNPS_MAKE_SCHEDULE_TABLE(ko_girl_schedule_dt);

static mNPS_schedule_data_c boy_schedule_dt[] = {
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(8, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(9, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(12, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(14, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(19, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(22, 0, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_table_c boy_schedule = mNPS_MAKE_SCHEDULE_TABLE(boy_schedule_dt);

static mNPS_schedule_data_c sport_man_schedule_dt[] = {
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(1, 0, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(5, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(6, 30, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(12, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(12, 30, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(23, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_table_c sport_man_schedule = mNPS_MAKE_SCHEDULE_TABLE(sport_man_schedule_dt);

static mNPS_schedule_data_c grim_man_schedule_dt[] = {
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(4, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(5, 0, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(10, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(11, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(15, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(16, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(22, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(23, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_table_c grim_man_schedule = mNPS_MAKE_SCHEDULE_TABLE(grim_man_schedule_dt);

static mNPS_schedule_data_c naniwa_lady_schedule_dt[] = {
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(1, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(2, 30, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(9, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(10, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(13, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(14, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(21, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(22, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_table_c naniwa_lady_schedule = mNPS_MAKE_SCHEDULE_TABLE(naniwa_lady_schedule_dt);

static mNPS_schedule_data_table_c* mNPS_schedule[mNpc_LOOKS_NUM] = {
  &girl_schedule,
  &ko_girl_schedule,
  &boy_schedule,
  &sport_man_schedule,
  &grim_man_schedule,
  &naniwa_lady_schedule
};

#ifdef ACGC_PORT
/*
 * Town life (setting town_life): longer days outdoors. Each personality keeps
 * its character (early jocks, cranky and snooty night owls, lazy late risers),
 * but lunch indoors is short, evenings outside are longer, and each villager's
 * day is shifted by up to half an hour either way (mNPS_town_life_shift), so
 * they don't all go in and come out at the same moment.
 */
static mNPS_schedule_data_c tl_girl_schedule_dt[] = {
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(5, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(6, 30, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(12, 15, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(12, 45, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(20, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(22, 0, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_c tl_ko_girl_schedule_dt[] = {
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(7, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(7, 45, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(13, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(13, 30, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(22, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(23, 30, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_c tl_boy_schedule_dt[] = {
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(8, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(9, 15, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(12, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(13, 30, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(21, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(22, 30, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_c tl_sport_man_schedule_dt[] = {
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(0, 30, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(5, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(5, 45, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(12, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(12, 30, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(23, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_c tl_grim_man_schedule_dt[] = {
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(4, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(5, 0, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(10, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(10, 45, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(15, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(15, 30, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(22, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(23, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_c tl_naniwa_lady_schedule_dt[] = {
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(1, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(2, 30, 0) },
  { mNPS_SCHED_SLEEP, mNPS_TIME_2_SEC(9, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(9, 45, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(13, 0, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(13, 30, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(21, 30, 0) },
  { mNPS_SCHED_IN_HOUSE, mNPS_TIME_2_SEC(22, 0, 0) },
  { mNPS_SCHED_FIELD, mNPS_TIME_2_SEC(24, 0, 0) }
};

static mNPS_schedule_data_table_c tl_girl_schedule = mNPS_MAKE_SCHEDULE_TABLE(tl_girl_schedule_dt);
static mNPS_schedule_data_table_c tl_ko_girl_schedule = mNPS_MAKE_SCHEDULE_TABLE(tl_ko_girl_schedule_dt);
static mNPS_schedule_data_table_c tl_boy_schedule = mNPS_MAKE_SCHEDULE_TABLE(tl_boy_schedule_dt);
static mNPS_schedule_data_table_c tl_sport_man_schedule = mNPS_MAKE_SCHEDULE_TABLE(tl_sport_man_schedule_dt);
static mNPS_schedule_data_table_c tl_grim_man_schedule = mNPS_MAKE_SCHEDULE_TABLE(tl_grim_man_schedule_dt);
static mNPS_schedule_data_table_c tl_naniwa_lady_schedule = mNPS_MAKE_SCHEDULE_TABLE(tl_naniwa_lady_schedule_dt);

static mNPS_schedule_data_table_c* mNPS_town_life_schedule[mNpc_LOOKS_NUM] = {
  &tl_girl_schedule,
  &tl_ko_girl_schedule,
  &tl_boy_schedule,
  &tl_sport_man_schedule,
  &tl_grim_man_schedule,
  &tl_naniwa_lady_schedule
};

/* Percent of each personality who stay in when it rains (half that in light rain); the rest like it */
static const u8 mNPS_rain_stay_in[mNpc_LOOKS_NUM] = { 40, 15, 55, 10, 30, 65 };

/* A villager's own shift of the day, -30 to +30 minutes, from their id */
static int mNPS_town_life_shift(const AnmPersonalID_c* id) {
  u32 h = (u32)id->npc_id * 2654435761u + id->name_id * 40503u;

  return ((int)((h >> 16) % 61) - 30) * 60;
}

/* Whether a villager stays in today in this rain: the same answer all day */
static int mNPS_town_life_rain_stay_in(const AnmPersonalID_c* id) {
  const lbRTC_time_c* now = Common_GetPointer(time.rtc_time);
  u32 h;
  int percent;

  if (Common_Get(weather) != mEnv_WEATHER_RAIN || id->looks >= mNpc_LOOKS_NUM) {
    return FALSE;
  }

  percent = mNPS_rain_stay_in[id->looks];
  if (Common_Get(weather_intensity) <= mEnv_WEATHER_INTENSITY_LIGHT) {
    percent /= 2;
  }

  h = ((u32)id->npc_id * 2246822519u) ^ ((u32)(now->year * 372 + now->month * 31 + now->day) * 3266489917u);
  return (int)((h >> 8) % 100) < percent;
}

/* The schedule entry type for id at now_sec in table */
static u32 mNPS_lookup(mNPS_schedule_data_table_c* table, int now_sec) {
  mNPS_schedule_data_c* entry = table->sched_data;
  int count;

  for (count = table->count; count != 0; count--) {
    if (entry->end_time > now_sec) {
      break;
    }

    entry++;
  }

  if (count == 0) {
    entry--;
  }

  return entry->type;
}

/* For tests: whether a villager is outside at now_sec, on the town life schedule or the original */
extern int mNPS_test_outside(const AnmPersonalID_c* id, int now_sec, int town_life) {
  if (id->looks >= mNpc_LOOKS_NUM) {
    return FALSE;
  }

  if (town_life) {
    now_sec = (now_sec - mNPS_town_life_shift(id) + 24 * 60 * 60) % (24 * 60 * 60);
    return mNPS_lookup(mNPS_town_life_schedule[id->looks], now_sec) == mNPS_SCHED_FIELD &&
           !mNPS_town_life_rain_stay_in(id);
  }

  return mNPS_lookup(mNPS_schedule[id->looks], now_sec) == mNPS_SCHED_FIELD;
}
#endif

extern mNPS_schedule_c* mNPS_get_schedule_area(AnmPersonalID_c* anm_id) {
  mNPS_schedule_c* schedule = Common_Get(npc_schedule);
  int i;

  for (i = 0; i < SCHEDULE_NUM; i++) {
    if (schedule->id == anm_id) {
      return schedule;
    }

    schedule++;
  }

  return NULL;
}

static void mNPS_set_schedule_area(AnmPersonalID_c* anm_id) {
  mNPS_schedule_c* schedule;

#ifdef ACGC_PORT
  /* Guard against invalid looks (e.g. mNpc_LOOKS_UNSET=6 on free animal slots
   * where CheckFreeAnimalInfo didn't catch it due to home_info not being 0xFF) */
  if (anm_id->looks >= mNpc_LOOKS_NUM) {
    return;
  }
#endif

  schedule = mNPS_get_schedule_area(NULL); /* try to get an unused schedule */

  if (schedule != NULL) {
    schedule->id = anm_id;
    schedule->data_table = mNPS_schedule[anm_id->looks];
    schedule->forced_timer = 0;
  }
}

extern void mNPS_set_island_schedule_area(AnmPersonalID_c* anm_id) {
  if (Common_Get(npc_schedule[ANIMAL_NUM_MAX]).id != NULL) {
    return;
  }

#ifdef ACGC_PORT
  if (anm_id->looks >= mNpc_LOOKS_NUM) {
    return;
  }
#endif

  Common_Set(npc_schedule[ANIMAL_NUM_MAX].id, anm_id);
  Common_Set(npc_schedule[ANIMAL_NUM_MAX].data_table, mNPS_schedule[anm_id->looks]);
  Common_Set(npc_schedule[ANIMAL_NUM_MAX].forced_timer, 0);
}

extern void mNPS_reset_schedule_area(AnmPersonalID_c* anm_id) {
  mNPS_schedule_c* schedule = mNPS_get_schedule_area(anm_id);

  if (schedule != NULL) {
    schedule->id = NULL;
  }
}

static void mNPS_schedule_manager_sub(mNPS_schedule_c* schedule, int forced_ticks) {
  int now_sec = Common_Get(time.now_sec);
  int count = schedule->data_table->count;
  mNPS_schedule_data_c* schedule_entry = schedule->data_table->sched_data;

#ifdef ACGC_PORT
  /* Town life: the longer, staggered days, and some stay in out of the rain */
  if (mTL_IsEnabled() && schedule->id != NULL && schedule->id->looks < mNpc_LOOKS_NUM &&
      schedule->data_table == mNPS_schedule[schedule->id->looks]) {
    u32 type;

    now_sec = (now_sec - mNPS_town_life_shift(schedule->id) + 24 * 60 * 60) % (24 * 60 * 60);
    type = mNPS_lookup(mNPS_town_life_schedule[schedule->id->looks], now_sec);
    if (type == mNPS_SCHED_FIELD && mNPS_town_life_rain_stay_in(schedule->id)) {
      type = mNPS_SCHED_IN_HOUSE;
    }

    schedule->saved_type = type;
    if (schedule->forced_timer > 0) {
      schedule->current_type = schedule->forced_type;
      schedule->forced_timer -= forced_ticks;
      if (schedule->forced_timer < 0) {
        schedule->forced_timer = 0;
      }
    } else {
      schedule->current_type = schedule->saved_type;
    }
    return;
  }
#endif

  /* Locate our current scehdule entry */
  for (count; count != 0; count--) {
    if (schedule_entry->end_time > now_sec) {
      break;
    }
    
    schedule_entry++;
  }

  schedule->saved_type = schedule_entry->type;
  if (schedule->forced_timer > 0) {
    schedule->current_type = schedule->forced_type;
    schedule->forced_timer -= forced_ticks;
    if (schedule->forced_timer < 0) {
      schedule->forced_timer = 0;
    }
  }
  else {
    schedule->current_type = schedule->saved_type;
  }
}

static void mNPS_schedule_manager_sub0() {
  mNPS_schedule_c* schedule = Common_Get(npc_schedule);
  int i;

  /* Set all town animals to go outside in their home block */
  for (i = 0; i < ANIMAL_NUM_MAX; i++) {
    if (schedule->id != NULL) {
      schedule->current_type = mNPS_SCHED_FIELD;
    }

    schedule++;
  }
}

static void mNPS_schedule_manager_sub1(int forced_ticks) {
  mNPS_schedule_c* schedule = Common_Get(npc_schedule);
  int i;

  /* Set all town animals to go about their intended schedule */
  for (i = 0; i < ANIMAL_NUM_MAX; i++) {
    if (schedule->id != NULL) {
      mNPS_schedule_manager_sub(schedule, forced_ticks);
    }

    schedule++;
  }
}

static void mNPS_island_schedule_manager(int forced_ticks) {
  mNPS_schedule_c* schedule = Common_GetPointer(npc_schedule[ANIMAL_NUM_MAX]);

  if (schedule->id != NULL) {
    mNPS_schedule_manager_sub(schedule, forced_ticks);
  }
}

extern void mNPS_schedule_manager() {
  static float forced_accum = 0.0f;
  int forced_ticks = graph_dt_60hz_ticks(gamePT, &forced_accum);
  int force_outside = FALSE;

  if (mEv_CheckFirstJob() == TRUE || mEv_check_status(mEv_EVENT_HALLOWEEN, mEv_STATUS_ACTIVE)) {
    force_outside = TRUE;
  }

  if (force_outside == TRUE) {
    mNPS_schedule_manager_sub0();
  }
  else {
    mNPS_schedule_manager_sub1(forced_ticks);
  }

  mNPS_island_schedule_manager(forced_ticks);
}

extern void mNPS_set_all_schedule_area() {
  Animal_c* animal = Save_Get(animals);
  int i;

  for (i = 0; i < ANIMAL_NUM_MAX; i++) {
    if (mNpc_CheckFreeAnimalInfo(animal) == FALSE) {
      mNPS_set_schedule_area(&animal->id);
    }

    animal++;
  }

  mNPS_schedule_manager();
}
