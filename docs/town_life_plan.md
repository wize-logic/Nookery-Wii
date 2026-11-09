# Town life plan

Make the 5×6 town feel busier and more lived-in: villagers with things to do,
places to gather, chatter you overhear, small changes they make to the town,
more of them outside, visitors, and weather and night moments.

Builds on villager friendships, gossip and news (`src/game/m_npc_life.c`).

## Principles

- **Port only:** everything goes under `ACGC_PORT`, behind one setting,
  `town_life` (PC: "Town Life"; Wii launcher: "Town life"), on by default.
  With it off, the game behaves as the original.
- **Saves:** no save format changes. The friendship and news memory use 22 of
  the 24 spare bytes per villager. Anything else that must persist goes in the
  padding at the end of `Save` (verify first that the game never writes it) or
  stays in memory only.
- **Online:** villagers run on each console, as now. Anything that changes the
  ground (flowers, gifts, snowmen) happens only on the host, and the existing
  ground sync carries it to guests. Purely visual behavior stays local.
- **Content:** reuse the game's own animations, effects and models. New text
  goes in `m_npc_life_msg.txt`-style files encoded by a tool, under message
  numbers past `MSG_MAX`.
- **Tests:** each phase extends the Wii test hook
  (`wii/src/netgame/nk_netgame.c`, `life_test`) so it can be checked
  unattended in Dolphin, with logs and screenshots.

## What we already know

- **Animations:** every villager animation is in one table, `aNPC_animeTable`
  (`src/actor/npc/ac_npc_data.c_inc:249`). The port links the data
  (`src/data/npc/*.c`) statically, so any villager can play any of them:
  - sitting: `SITDOWN`, `SITDOWN_DRINK` (0x49), `SITDOWN_CLAP`
  - festival moves: `DANCE` (0x4B), `TAISOU` exercises (0x53, plus variants),
    `TAMANAGE` ball throw (0x72), `TURI_WAIT` fishing pose (0x79), `BANZAI`,
    `HATAFURI` flag waving
  - quiet moments: `KOKKURI` nodding off, `UTIWA_WAIT` fanning, `OMAIRI`
    praying, `KYORO` looking around
  - already used: `ENSOU` instrument, `KEITAI` phone call
- **Props:** villagers can only hold an umbrella, a phone, an item they put
  away, or something in the left hand (`aNPC_ITEM_TYPE_*`). Fanning and the
  cane are "sub-animations" that draw their own prop. Whether the cup, rod and
  ball show up outside their festivals is the main unknown.
- **Behavior entry point:** wandering villagers decide their next action in
  `aNPC_think_wander_decide_next` (`ac_npc_think_wander.c_inc`); the
  friendship walking hook (`aNPC_life_decide`) already sits there.
- **Schedules:** per-personality tables in `src/game/m_npc_schedule.c`
  (asleep / at home / outside, by time of day).
- **Visitors:** event characters use the 5 event NPC slots
  (`mNpc_EVENT_NPC_NUM`, `ac_set_npc_manager.c`).

## Phase 0: groundwork (S)

1. **Animation and prop check:** a `life_test` mode that walks a villager
   through each candidate animation, with a screenshot of each. Record which
   loop, which show their prop, and which need a seat, a partner or a
   particular facing. This decides what Phase 1 can use cheaply.
2. **Activity framework:** a small table-driven system in `m_npc_life.c`:
   ```c
   typedef struct {
       u8 anim;           /* aNPC_ANIM_* */
       u8 place;          /* anywhere, plaza, river bank, beach, under a tree, near home */
       u8 looks_weight[6]; /* how much each personality likes it */
       u8 hour_from, hour_to;
       u8 weather;        /* bitmask: clear, rain, snow */
       u8 seasons;        /* bitmask */
       u16 min_sec, max_sec;
   } mNL_activity_c;
   ```
   plus a per-villager runtime state (current activity, timer, spot), kept in
   memory only.
3. **A spot finder:** free tiles near a place type (`mCoBG_Wpos2CheckNpc`,
   the villager's movement range), reusing the wander code's checks.

**Done when** the animation table is filled in from the check, and one
activity runs end to end in Dolphin.

**Result (done):** `src/game/m_town_life.c` (table, state, place checks) and
`ac_npc_act_town_life.c_inc` / `ac_npc_think_town_life.c_inc` (the act, the
wander hook, the spot finder). The check (`life_test = 4`, three screenshots
per animation):

| Animation | Loops | Prop / effect | Verdict |
|---|---|---|---|
| `SITDOWN_WAIT1` | yes | sits on the ground | sit (tree) |
| `SITDOWN_DRINK1` | one-shot, replayed | cup (`TOOL_TUMBLER`) shows | sit and drink |
| `SITDOWN_CLAP1` | yes | "Ha ha" effect | sit and laugh |
| `SITDOWN_WAIT1` + fan sub-animation | yes | fan (`TOOL_UTIWA`) shows | sit and fan (summer) |
| `KOKKURI_D1` | yes | eyes shut, nodding | doze |
| `DANCE1` | yes | music note | dance |
| `TAISOU1`, `TAISOU2`, `ASIHUMI1`, `WARMUP1` | yes | | exercise, jog, warm up |
| `TURI_WAIT1` | yes | rod (`TOOL_NPC_SAO`) | fish (river) |
| `WAIT1` + fan sub-animation | yes | fan | fan (summer) |
| `HATAFURI1` | yes | flag (`TOOL_FLAG`) | wave a flag |
| `BANZAI1`, `OMAIRI1`, `KYORO1` | `KYORO1` one-shot | | cheer, pray, look around |
| `KEITAI_TALK1` | one-shot, replayed | phone spawns | phone call |
| `TIRED1`, `WAIT_NEMU1`, `KIDUKU1` | | sweat, "Z", surprise | spare (groups, night) |
| `TAMAHIROI1`, `TAMANAGE1` | one-shot | ball spawns | Phase 4 (bend down), Phase 7 (snowball) |
| `SITDOWN_WAIT_D1`, `KOKKURI_D2`, `ENSOU_E1`, `CRACKER_WAIT1` | | deck pose, no instrument, festival prop | dropped |

Findings: villagers who like or dislike the player enough seek them out or
avoid them in their acre (`aNPC_chk_friendship`) and make no other decisions
meanwhile; a one-shot animation must be restarted from its first frame
(`draw.animation_id = -1`); and an activity right after another needs the act
index cleared, or the game skips the new act's init.

## Phase 1: everyday activities (M)

- In wander, villagers sometimes start an activity instead of a walk.
  Personality, time and weather choose which: jocks exercise in the morning,
  lazy villagers nod off in the afternoon, snooty ones sit and drink, anyone
  fans themselves in summer heat, and some play an instrument or take a phone
  call.
- **Lifecycle:** walk to a spot, turn, play the animation for its duration,
  then return to wandering.
- **Interruptions:** talking to the player, greetings, rain starting and the
  schedule changing all end the activity cleanly (end the animation, restore
  the movement state).
- **Talking mid-activity:** an occasional line about what they're doing ("I
  could do this all day…").
- **Test:** `life_test = 3` logs each activity started and ended, and how many
  villagers are busy each minute. Screenshots of each personality's favorite.

**Result (done):** 17 activities chosen by personality, hour, weather and
season; when the first choice needs a place the acre lacks (a river, a tree),
up to three are tried. Talked to mid-activity, a villager says what they were
doing 70% of the time: 54 new lines (`ACT_*` topics in
`m_npc_life_msg.txt`, one per kind of activity and personality, served after
the gossip topics). A villager with a full hand (an umbrella) skips
activities with a prop. `life_test = 3` also takes screenshots a few seconds
into activities near the player; `life_test = 5` interrupts a fanning
villager with a chat, rain and its schedule, and checks each time that the
prop, the fan pose and the act are gone: all clean in Dolphin, and the chat
showed the fan line. In 8 game minutes at a 50% start chance, 3 villagers did
19 activities.

## Phase 2: places to gather (M)

- **Gathering spots:** the plaza (the wishing well acre), the river bank, the
  beach and big trees, found from the town's acre types and features at game
  start.
- **Groups:**
  - At set times (morning, after lunch, evening) a villager may head to a
    spot, and friends within a few acres (opinion at or above friend level)
    follow.
  - Two to four villagers stand in a circle, facing the center, taking turns
    to emote (laugh, nod, clap).
  - Rivals don't join a group with someone they dislike.
  - The group breaks up after a few minutes, or when the schedule changes.
- **Evenings:** friends walk home together. The first to leave picks a friend
  heading the same way.
- **Friendships:** meetings in a group count like greetings (opinions, news
  exchange).
- **Test:** count groups and their members by game hour in watch mode, and
  measure how often friends end up within 100 units of each other.

**Result (done):** groups form in the villager's own acre (only loaded
villagers can be called over) on open ground or the plaza, with room for the
circle checked tile by tile. Members walking to or standing in a group don't
trigger the game's own greetings, which would break the group up; their
meeting counts instead (`mTL_GroupMeet`: opinions, news, "new friends").
Members keep walking while they make progress (walks pause for the game's
fatigue rests) and stop when close enough. In the evening, a free friend in
the same acre walks a villager who heads home (stays 43 to 58 units beside
them in Dolphin). `life_test = 6` makes the acre's villagers friends and two
of them rivals: groups of two and three formed and lasted their full time,
the rivals never shared one, and friends were within 100 units 41% of the
time against 35% with town life off (6 game minutes each).

## Phase 3: overheard chatter (M)

- **Speech bubbles:** a small balloon drawn above a villager:
  - project the world position to the screen
  - draw the window and the game's font, as the speech window does but smaller
  - fade in and out
  - at most one or two on screen at a time
- **Barks:** one-liners of at most 24 characters with a name filled in, per
  personality, for each gossip and news topic and each activity ("Heard about
  Boris and Belle?", "Morning stretches!"). They're encoded by the same tool;
  the bubble code fills in the names itself.
- **When:**
  - the player passes within a few tiles of a group or two greeting villagers
  - at most every ~20 s per villager
  - never during a conversation or a menu
- **Emotes:** reuse the existing emote effects over the speaker's head.
- **Test:** screenshots of bubbles at different camera distances, and a check
  that they never cover the speech window.

**Result (done):** `src/game/m_town_life_bubble.c` draws the game's own
balloon (`fki_win`, as `m_watch_my_step.c` does for item names) above the
speaker, kept on screen, at most two at once, each villager at most every 20
seconds, only within five tiles of the player, and never during a
conversation, a cutscene or a menu (bubbles vanish at once). 252 barks (21
topics: chat, laugh, like, dislike, pairs, news, each activity, weather,
night, planting, visitors and greeting the player; two per personality) in
`m_town_life_barks.txt`, encoded by `tools/town_life_barks.py`, names filled
in at run time. Speakers in a group say their line on their turn and
listeners sometimes laugh; busy villagers now and then say a word about their
activity; greeting villagers say something about each other; villagers
passing the player say hello. `life_test = 7`: 15 bubbles in 6 game minutes,
none while the speech window was open, screenshots with speakers from the top
to the bottom of the screen.

## Phase 4: villagers tend the town (M–L)

- **Actions:**
  - water flowers near home: animation only, but flowers they water count as
    watered
  - plant a flower: a free tile near their own home, a few times a week
  - pick fruit from a tree near home
  - build a snowman in winter
  - leave a gift for the player: the game's own villager mail with an item,
    rather than objects on the ground
- **Ground changes go through the game's field item functions:**
  - only on the host in online play
  - never on paths, plots players use, or near buried items
  - limited per day so the town rating isn't swamped
- **Gossip:** "I planted tulips by my house!" as a news event (new event type,
  same memory).
- **Test:** before/after ground diffs per day in watch mode, and that an
  online guest sees the change through the ground sync.

## Phase 5: more time outdoors (S)

- Rework the schedules in `m_npc_schedule.c` (port only) so more villagers
  are out at once and times are staggered, not all at noon. Keep each
  personality's character (cranky and snooty night owls, early jocks).
- **Weather:** fewer villagers out in heavy rain, apart from those who like it.
- **Measure:** villagers outside per hour, over a simulated day, before and
  after.

**Result (done):** town life's own schedule tables in `m_npc_schedule.c`
(shorter lunches indoors, longer evenings, each personality's shape kept),
each villager's day shifted by up to half an hour by its id, and a share of
each personality staying in when it rains (the same villagers all day, fewer
in light rain; jocks and peppy villagers like the rain). One of each
personality is out 13.5 h a day on average instead of 12.9 h, and 6 of 6 from
13:00 to 20:00. The test town's 10 villagers, on a rainy day: 137
villager-hours outside before, 148 after (`life_test = 3` logs both).

## Phase 6: visitors (M)

- A daily visitor: some days, a villager from the game's roster who doesn't
  live in town spends the day wandering, using an event NPC slot. Check
  `ac_set_npc_manager.c` and the summer camp code (a camper is the same
  idea).
- The game's travelling characters (Saharah, Gracie, Wendell and the rest)
  appear a little more often, within their existing rules.
- **Gossip:** "Did you see the visitor at the plaza?"
- **Test:** forced visitor days in `life_test`; check the event slots and
  existing events never clash.

## Phase 7: weather and night (S–M)

- Snowball throws in winter (`TAMANAGE` and a snow puff effect), between
  friends in a group.
- Rain: splashing in puddles and sheltering under trees.
- Night: night owls gather at the plaza and look up at the stars
  (`KYORO`, a slow turn).
- Summer evenings: fanning while sitting.

## Settings and performance

- One toggle, `town_life`, covers Phases 1–7. Sub-toggles only if testers ask.
- **Wii budget:** only villagers in the loaded acres run. Activities add no
  actors, apart from snowmen, which are already ground items.
- **Bubbles:** one display list each, at most two on screen.
- **Check:** measure frame time in Dolphin and on hardware with a busy plaza.

## Order and size

| Phase | Size | Depends on |
|---|---|---|
| 0 groundwork | S | none |
| 1 activities | M | 0 |
| 2 gatherings | M | 0, 1 |
| 3 bubbles | M | 2 (best with groups) |
| 5 more time outdoors | S | none (do it early, to measure) |
| 4 tend the town | M–L | 1 |
| 6 visitors | M | 1 |
| 7 weather and night | S–M | 1, 2 |

Suggested order: 0 → 5 → 1 → 2 → 3 → 4 → 6 → 7, one commit per phase, each
checked in Dolphin before the next.

## Risks and open questions

- **Props:** festival animations may look wrong without their prop (a drink
  without a cup). Phase 0 decides; drop any that don't work.
- **Sitting:** needs a sensible place. The ground near a tree or the plaza is
  fine; there are no benches in the stock town.
- **Interruptions:** the villager state machine is intricate. Every activity
  must end cleanly when talked to or when the schedule changes. Test those
  transitions specifically.
- **Town rating:** villager planting must not push the rating around much. Cap
  it, and plant only near their own homes.
- **Online:** only the host changes the ground. Visual-only behavior can
  differ between consoles, which is accepted, as villagers already do.
- **Save padding:** confirm the original game never writes the padding at the
  end of `Save` before using it.
- **Bubble readability on a 480i TV:** keep text large and short.

## Later

- Errands between villagers: carry an apology gift between rivals using the
  delivery quest system.
- Gossip about more of what the player does (furniture, house size, museum
  donations).
- Translations for PAL.
