/* The online play's game side (built with the game's headers): each frame of
 * the play scene (src/game/m_play.c calls nk_netgame_play_frame), it
 *   - publishes the local player's state and looks (nk_net.h),
 *   - shows the other players who are on the same town's field as puppets
 *     (an actor of its own: the player's skeleton and animations, the
 *     remote player's face and clothes, after the inventory screen's model
 *     and ACGC-PC-Multiplayer's remote players),
 *   - keeps the town's ground items the same on every console: a copy of the
 *     town's item grid is compared with the live one, an acre a frame, and
 *     what changed is sent; what comes from the others is written into both
 *     (the host's town has the last word: a guest's change goes to the host,
 *     which takes it and sends it back to everyone). */

#include "nk_net.h"

#include <string.h>

#include "dolphin/os.h"
#include "game.h"
#include "m_actor.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_lib.h"
#include "m_malloc.h"
#include "m_needlework.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_private.h"
#include "m_rcp.h"
#include "m_scene.h"
#include "lb_rtc.h"
#include "m_bgm.h"
#include "m_event.h"
#include "m_card.h"
#include "m_scene_table.h"
#include "c_keyframe.h"
#include "sys_matrix.h"
#include "ac_npc.h"
#include "m_msg.h"
#include "m_npc_life.h"
#include "m_town_life.h"
#include "m_kankyo.h"
#include "m_npc_schedule.h"
#include "m_house.h"
#include "m_mail.h"
#include "ac_npc_anim_def.h"
#include "ac_tools.h"
#include "m_choice.h"
#include "m_time.h"

/* src/game/m_actor.c, src/game/m_player_lib.c (ACGC_PORT) */
extern ACTOR* Actor_info_make_actor_from_profile(Actor_info* actor_info, GAME* game, ACTOR_PROFILE* profile,
                                                 ACTOR_DLFTBL* dlftbl, f32 x, f32 y, f32 z, s16 rot_x, s16 rot_y,
                                                 s16 rot_z, mActor_name_t name_id, s16 arg);
extern void mPlib_Load_FaceTexAndPallet(void* face_tex_p, void* face_pallet_p, int sex, int face_type,
                                        int sunburn_rank, int swell, int decoy);
extern cKF_Skeleton_R_c cKF_bs_r_boy_1;
extern cKF_Skeleton_R_c cKF_bs_r_grl_1;

/* wii/src/os.c */
void nk_wii_log(const char* msg, ...);

/* settings.c: the echo test (the local player as remote slot 1) */
extern int g_nk_net_echo;
static NkPlayerState s_echo_state;
static NkAppearance s_echo_look;
static uint32_t s_echo_seq;

static int remote_get(int slot, NkPlayerState* st, NkAppearance* look, uint32_t* age, uint32_t* look_seq)
{
    if (g_nk_net_echo && nk_net_mode() == NK_NET_OFF) {
        if (slot != 1 || s_echo_seq == 0) {
            return 0;
        }
        *st = s_echo_state;
        st->x += 50.0f;
        *look = s_echo_look;
        *age = 0;
        *look_seq = 1;
        return 1;
    }
    return nk_net_remote(slot, st, look, age, look_seq);
}

#define STALE_MS 3000
#define LOOK_RESEND_FRAMES 180
/* the puppets' actor names: never an item the game looks up */
#define PUPPET_NAME(slot) ((mActor_name_t) (0xF0E0 | (slot)))

/* ---- the puppets ----------------------------------------------------------- */

typedef struct NkPuppet {
    ACTOR actor;
    int slot;
    cKF_SkeletonInfo_R_c keyframe;
    s_xyz joint[mPlayer_JOINT_NUM + 1];
    s_xyz morph[mPlayer_JOINT_NUM + 1];
    int anim;
    int ready;
    xyz_t target;
    s16 target_angle;
} NkPuppet;

/* A remote player's looks, resolved into textures (32-byte aligned for the
 * GPU); kept per slot, across scenes */
typedef struct NkLooks {
    u8 face_tex[0xE00] ATTRIBUTE_ALIGN(32);
    u16 face_pal[16] ATTRIBUTE_ALIGN(32);
    u8 cloth_tex[mNW_DESIGN_TEX_SIZE] ATTRIBUTE_ALIGN(32);
    u16 cloth_pal[16] ATTRIBUTE_ALIGN(32);
    uint32_t seq;  /* the look's sequence resolved */
    int ready;
    int sex;
} NkLooks;

static NkLooks s_looks[NK_NET_MAX_PLAYERS];
static NkPuppet* s_puppets[NK_NET_MAX_PLAYERS];
static GAME_PLAY* s_play;      /* the play scene the puppets belong to */
static u32 s_play_frame;

static ACTOR_PROFILE s_profile;
static ACTOR_DLFTBL s_dlftbl;

static void puppet_ct(ACTOR* actorx, GAME* game);
static void puppet_dt(ACTOR* actorx, GAME* game);
static void puppet_mv(ACTOR* actorx, GAME* game);
static void puppet_dw(ACTOR* actorx, GAME* game);

static void profile_init(void)
{
    if (s_dlftbl.profile != NULL) {
        return;
    }
    memset(&s_profile, 0, sizeof(s_profile));
    s_profile.id = -1;
    /* (a list no game code walks for villagers or items) */
    s_profile.part = ACTOR_PART_UNUSED;
    s_profile.initial_flags_state = ACTOR_STATE_NO_MOVE_WHILE_CULLED | ACTOR_STATE_NO_DRAW_WHILE_CULLED;
    s_profile.npc_id = 0;
    s_profile.obj_bank_id = -1;
    s_profile.class_size = sizeof(NkPuppet);
    s_profile.ct_proc = puppet_ct;
    s_profile.dt_proc = puppet_dt;
    s_profile.mv_proc = puppet_mv;
    s_profile.dw_proc = puppet_dw;
    s_profile.sv_proc = NULL;
    memset(&s_dlftbl, 0, sizeof(s_dlftbl));
    s_dlftbl.profile = &s_profile;
}

static void resolve_looks(int slot, const NkAppearance* look, uint32_t seq)
{
    NkLooks* l = &s_looks[slot];
    int face = look->face < mPr_FACE_TYPE_NUM ? look->face : 0;
    int sunburn = look->sunburn <= mPr_SUNBURN_RANK8 ? look->sunburn : 0;
    mPr_cloth_c cloth;

    mPlib_Load_FaceTexAndPallet(l->face_tex, l->face_pal, look->sex, face, sunburn, FALSE, FALSE);
    memset(&cloth, 0, sizeof(cloth));
    mPlib_change_player_cloth_info(&cloth, (mActor_name_t) look->cloth);
    mPlib_Load_PlayerTexAndPallet(l->cloth_tex, l->cloth_pal, cloth.idx);
    l->sex = look->sex;
    l->seq = seq;
    l->ready = TRUE;
}

static void puppet_set_anim(NkPuppet* p, int anim)
{
    cKF_Animation_R_c* a = mPlib_Get_Pointer_Animation(anim);
    if (a == NULL) {
        anim = mPlayer_ANIM_WAIT1;
        a = mPlib_Get_Pointer_Animation(anim);
    }
    cKF_SkeletonInfo_R_init_standard_repeat_setframeandspeedandmorph(&p->keyframe, a, NULL, 1.0f, 0.5f, -5.0f);
    p->anim = anim;
}

static void puppet_ct(ACTOR* actorx, GAME* game)
{
    NkPuppet* p = (NkPuppet*) actorx;
    (void) game;
    p->ready = FALSE;
    p->anim = -1;
    actorx->scale.x = actorx->scale.y = actorx->scale.z = 0.01f;
    actorx->shape_info.draw_shadow = TRUE;
    actorx->shape_info.shadow_size_x = 18.0f;
    actorx->shape_info.shadow_size_z = 18.0f;
    actorx->shape_info.shadow_alpha_change_rate = 0.4f;
}

static void puppet_dt(ACTOR* actorx, GAME* game)
{
    NkPuppet* p = (NkPuppet*) actorx;
    (void) game;
    if (p->slot >= 0 && p->slot < NK_NET_MAX_PLAYERS && s_puppets[p->slot] == p) {
        s_puppets[p->slot] = NULL;
    }
    if (p->ready) {
        cKF_SkeletonInfo_R_dt(&p->keyframe);
    }
}

static void puppet_mv(ACTOR* actorx, GAME* game)
{
    NkPuppet* p = (NkPuppet*) actorx;
    NkLooks* l = &s_looks[p->slot];
    (void) game;
    if (!l->ready) {
        return;
    }
    if (!p->ready) {
        cKF_Skeleton_R_c* skel = l->sex == mPr_SEX_MALE ? &cKF_bs_r_boy_1 : &cKF_bs_r_grl_1;
        cKF_SkeletonInfo_R_ct(&p->keyframe, skel, NULL, p->joint, p->morph);
        p->ready = TRUE;
        puppet_set_anim(p, p->anim >= 0 ? p->anim : mPlayer_ANIM_WAIT1);
    }
    /* toward the last state, a little each frame (states come 10 times a second) */
    actorx->world.position.x += (p->target.x - actorx->world.position.x) * 0.35f;
    actorx->world.position.y += (p->target.y - actorx->world.position.y) * 0.35f;
    actorx->world.position.z += (p->target.z - actorx->world.position.z) * 0.35f;
    actorx->shape_info.rotation.y += (s16) ((s16) (p->target_angle - actorx->shape_info.rotation.y) * 0.4f);
    actorx->world.angle.y = actorx->shape_info.rotation.y;
    cKF_SkeletonInfo_R_play(&p->keyframe);
}

static void puppet_dw(ACTOR* actorx, GAME* game)
{
    NkPuppet* p = (NkPuppet*) actorx;
    NkLooks* l = &s_looks[p->slot];
    GRAPH* graph = game->graph;
    N64Mtx* mtx;

    if (!p->ready || !l->ready) {
        return;
    }
    mtx = (N64Mtx*) GRAPH_ALLOC_TYPE(graph, N64Mtx, p->keyframe.skeleton->num_shown_joints);
    if (mtx == NULL) {
        return;
    }
    _texture_z_light_fog_prim(graph);
    OPEN_DISP(graph);
    gSPSegment(NEXT_POLY_OPA_DISP, G_MWO_SEGMENT_8, l->face_tex);              /* the eyes */
    gSPSegment(NEXT_POLY_OPA_DISP, G_MWO_SEGMENT_9, l->face_tex + 8 * 0x100);  /* the mouth */
    gSPSegment(NEXT_POLY_OPA_DISP, G_MWO_SEGMENT_A, l->cloth_tex);
    gSPSegment(NEXT_POLY_OPA_DISP, G_MWO_SEGMENT_B, l->cloth_pal);
    gSPSegment(NEXT_POLY_OPA_DISP, G_MWO_SEGMENT_C, l->face_pal);
    CLOSE_DISP(graph);
    cKF_Si3_draw_R_SV(game, &p->keyframe, mtx, NULL, NULL, p);
}

/* ---- the town's identity, the local player ---------------------------------- */

static uint32_t land_id(void)
{
    uint32_t h = 2166136261u;
    const u8* name = Save_Get(land_info).name;
    for (int i = 0; i < LAND_NAME_SIZE; i++) {
        h = (h ^ name[i]) * 16777619u;
    }
    return (h & 0xFFFF0000u) | Save_Get(land_info).id;
}

/* settings.c: the title's demo as the town (a test) */
extern int g_nk_net_title_town;

/* The town's clock (after ACGC-PC-Multiplayer's CLOCK_SYNC): the host
 * publishes its game time each frame; a guest whose loaded town is the
 * host's runs its game time from it (src/lb_rtc.c), correcting it only past
 * two seconds of drift */
extern int g_nk_clock_active;
extern long long g_nk_clock_offset;
#define CLOCK_TOLERANCE_TICKS (2LL * OS_TIMER_CLOCK)

static void clock_sync(void)
{
    if (nk_net_mode() == NK_NET_HOST) {
        NkClock c;
        memset(&c, 0, sizeof(c));
        c.game_ticks = (int64_t) (lbRTC_HardTime() + Save_Get(time_delta));
        c.land_id = land_id();
        nk_net_publish_clock(&c);
        return;
    }
    if (nk_net_mode() != NK_NET_GUEST) {
        g_nk_clock_active = 0;
        return;
    }
    static NkClock host;
    static uint32_t host_age_ms;
    static OSTime host_at; /* the hardware clock when host's was read */
    static int have;
    NkClock c;
    uint32_t age_ms;
    if (nk_net_host_clock(&c, &age_ms)) {
        host = c;
        host_age_ms = age_ms;
        host_at = lbRTC_HardTime() - (OSTime) age_ms * (OS_TIMER_CLOCK / 1000);
        have = 1;
    }
    if (!have || land_id() != host.land_id) {
        if (g_nk_clock_active) {
            nk_wii_log("NETGAME: own clock again (not in the host's town)\n");
        }
        g_nk_clock_active = 0;
        return;
    }
    long long offset = (long long) host.game_ticks - (long long) host_at;
    long long drift = offset - (g_nk_clock_active ? g_nk_clock_offset : (long long) Save_Get(time_delta));
    if (!g_nk_clock_active || drift > CLOCK_TOLERANCE_TICKS || drift < -CLOCK_TOLERANCE_TICKS) {
        nk_wii_log("NETGAME: the host's clock: %+lld s from %s (read %u ms ago)\n",
                   drift / OS_TIMER_CLOCK, g_nk_clock_active ? "the last correction" : "this save's",
                   (unsigned) host_age_ms);
        g_nk_clock_offset = offset;
        g_nk_clock_active = 1;
    }
}

static int on_town_field(void)
{
    return Save_Get(scene_no) == SCENE_FG || (g_nk_net_title_town && Save_Get(scene_no) == SCENE_TITLE_DEMO);
}

static void publish_local(GAME_PLAY* play)
{
    static int look_frames;
    static NkAppearance last;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    Private_c* priv = Now_Private;
    NkPlayerState st;
    NkAppearance look;

    if (player == NULL) {
        return;
    }
    memset(&st, 0, sizeof(st));
    st.in_town = (uint8_t) on_town_field();
    st.scene = (uint16_t) Save_Get(scene_no);
    st.land_id = land_id();
    st.x = player->actor_class.world.position.x;
    st.y = player->actor_class.world.position.y;
    st.z = player->actor_class.world.position.z;
    st.angle = player->actor_class.shape_info.rotation.y;
    st.anim = (uint16_t) player->animation0_idx;
    st.speed = player->actor_class.speed;
    nk_net_publish_state(&st);
    s_echo_state = st;
    s_echo_seq++;

    memset(&look, 0, sizeof(look));
    if (priv != NULL) {
        look.sex = (uint8_t) priv->gender;
        look.face = (uint8_t) priv->face;
        look.sunburn = (uint8_t) priv->sunburn.rank;
        look.cloth = priv->cloth.item;
        memcpy(look.name, priv->player_ID.player_name, PLAYER_NAME_LEN);
    } else {
        /* (the title's demo: no player chosen yet) */
        look.cloth = ITM_CLOTH_START;
    }
    if (memcmp(&look, &last, sizeof(look)) != 0 || ++look_frames >= LOOK_RESEND_FRAMES) {
        nk_net_publish_appearance(&look);
        s_echo_look = look;
        last = look;
        look_frames = 0;
    }
}

/* ---- the puppets' lives ------------------------------------------------------ */

static void forget_puppets(void)
{
    for (int i = 0; i < NK_NET_MAX_PLAYERS; i++) {
        s_puppets[i] = NULL;
    }
}

static void update_puppets(GAME_PLAY* play)
{
    int local_in_town = on_town_field();
    uint32_t local_land = land_id();
    profile_init();
    for (int slot = 0; slot < NK_NET_MAX_PLAYERS; slot++) {
        NkPlayerState st;
        NkAppearance look;
        uint32_t age, look_seq;
        int shown = remote_get(slot, &st, &look, &age, &look_seq) && age < STALE_MS && look_seq != 0 &&
                    ((st.in_town && local_in_town && st.land_id == local_land) ||
                     (g_nk_net_echo && nk_net_mode() == NK_NET_OFF));
        NkPuppet* p = s_puppets[slot];
        if (!shown) {
            if (p != NULL) {
                Actor_delete(&p->actor);
                s_puppets[slot] = NULL;
            }
            continue;
        }
        if (s_looks[slot].seq != look_seq) {
            resolve_looks(slot, &look, look_seq);
        }
        if (p == NULL) {
            p = (NkPuppet*) Actor_info_make_actor_from_profile(&play->actor_info, &play->game, &s_profile, &s_dlftbl,
                                                               st.x, st.y, st.z, 0, st.angle, 0,
                                                               PUPPET_NAME(slot), 0);
            if (p == NULL) {
                continue;
            }
            p->slot = slot;
            p->actor.shape_info.rotation.y = st.angle;
            s_puppets[slot] = p;
            nk_wii_log("NETGAME: player %d's puppet is here (scene %d)\n", slot, (int) st.scene);
        }
        p->target.x = st.x;
        p->target.y = st.y;
        p->target.z = st.z;
        p->target_angle = st.angle;
        /* far away (a door, a teleport): straight there */
        if (ABS(p->actor.world.position.x - st.x) + ABS(p->actor.world.position.z - st.z) > 400.0f) {
            p->actor.world.position = p->target;
        }
        if (p->ready && (int) st.anim != p->anim) {
            puppet_set_anim(p, st.anim);
        } else if (!p->ready) {
            p->anim = st.anim;
        }
    }
}

/* ---- the town's ground ------------------------------------------------------- */

static mActor_name_t s_shadow[FG_BLOCK_Z_NUM][FG_BLOCK_X_NUM][UT_Z_NUM][UT_X_NUM];
static uint32_t s_shadow_land;
static int s_shadow_valid;
static int s_scan_acre;

static void shadow_take(uint32_t land)
{
    for (int bz = 0; bz < FG_BLOCK_Z_NUM; bz++) {
        for (int bx = 0; bx < FG_BLOCK_X_NUM; bx++) {
            memcpy(s_shadow[bz][bx], Save_Get(fg[bz][bx]).items, sizeof(s_shadow[bz][bx]));
        }
    }
    s_shadow_land = land;
    s_shadow_valid = TRUE;
}

_Static_assert(sizeof(((mFM_fg_c*) 0)->items) == NK_FIELD_ACRE_TILES * sizeof(uint16_t),
               "an acre's items are NkFieldBlock's");

/* Host: every acre of its town to the guests (one arrived on the streets) */
static void send_whole_field(uint32_t land)
{
    NkFieldBlock b;
    for (int bz = 0; bz < FG_BLOCK_Z_NUM; bz++) {
        for (int bx = 0; bx < FG_BLOCK_X_NUM; bx++) {
            memset(&b, 0, sizeof(b));
            b.block_x = (uint8_t) bx;
            b.block_z = (uint8_t) bz;
            b.land_id = land;
            memcpy(b.items, Save_Get(fg[bz][bx]).items, sizeof(b.items));
            nk_net_send_block(&b);
        }
    }
}

static void field_sync(void)
{
    uint32_t land = land_id();
    NkFieldChange c;
    int applied = FALSE;

    if (!on_town_field()) {
        s_shadow_valid = FALSE;
        /* indoors: the host's own town's grid is still the one loaded, and
         * takes its guests' changes (the shadow is taken again outside); a
         * guest's copy waits for the field */
        while (nk_net_take_field(&c)) {
            if (nk_net_mode() == NK_NET_HOST && c.block_x == NK_FIELD_REQUEST && c.land_id == land) {
                nk_wii_log("NETGAME: a guest asked for the ground: sent\n");
                send_whole_field(land);
                continue;
            }
            if (nk_net_mode() == NK_NET_HOST && c.land_id == land && c.block_x < FG_BLOCK_X_NUM &&
                c.block_z < FG_BLOCK_Z_NUM && c.ut_x < UT_X_NUM && c.ut_z < UT_Z_NUM)
            {
                Save_Get(fg[c.block_z][c.block_x]).items[c.ut_z][c.ut_x] = c.item;
                nk_net_send_field(&c);
            }
        }
        return;
    }
    if (!s_shadow_valid || s_shadow_land != land) {
        shadow_take(land);
        if (nk_net_mode() == NK_NET_GUEST) {
            /* just arrived: the host's ground as it is now (this copy is its
             * last save) */
            memset(&c, 0, sizeof(c));
            c.block_x = NK_FIELD_REQUEST;
            c.land_id = land;
            nk_net_send_field(&c);
        }
    }
    /* guest: the host's whole acres */
    {
        NkFieldBlock b;
        static int acres_got;
        while (nk_net_take_block(&b)) {
            if (++acres_got % (FG_BLOCK_X_NUM * FG_BLOCK_Z_NUM) == 0) {
                nk_wii_log("NETGAME: the host's ground arrived (%d acres)\n", FG_BLOCK_X_NUM * FG_BLOCK_Z_NUM);
            }
            if (b.land_id != land || b.block_x >= FG_BLOCK_X_NUM || b.block_z >= FG_BLOCK_Z_NUM) {
                continue;
            }
            memcpy(Save_Get(fg[b.block_z][b.block_x]).items, b.items, sizeof(b.items));
            memcpy(s_shadow[b.block_z][b.block_x], b.items, sizeof(b.items));
            applied = TRUE;
        }
    }
    /* what the others changed */
    while (nk_net_take_field(&c)) {
        if (c.block_x == NK_FIELD_REQUEST) {
            if (nk_net_mode() == NK_NET_HOST && c.land_id == land) {
                nk_wii_log("NETGAME: a guest asked for the ground: sent\n");
                send_whole_field(land);
            }
            continue;
        }
        if (c.land_id != land || c.block_x >= FG_BLOCK_X_NUM || c.block_z >= FG_BLOCK_Z_NUM || c.ut_x >= UT_X_NUM ||
            c.ut_z >= UT_Z_NUM)
        {
            continue;
        }
        Save_Get(fg[c.block_z][c.block_x]).items[c.ut_z][c.ut_x] = c.item;
        s_shadow[c.block_z][c.block_x][c.ut_z][c.ut_x] = c.item;
        applied = TRUE;
        nk_wii_log("NETGAME: ground change from the others: acre %d,%d tile %d,%d -> %04x\n", c.block_x,
                   c.block_z, c.ut_x, c.ut_z, c.item);
        if (nk_net_mode() == NK_NET_HOST) {
            /* the host takes a guest's change and gives it to everyone */
            nk_net_send_field(&c);
        }
    }
    if (applied) {
        mFI_SetFGUpData();
    }
    /* what changed here: an acre a frame */
    {
        int bz = s_scan_acre / FG_BLOCK_X_NUM, bx = s_scan_acre % FG_BLOCK_X_NUM;
        mActor_name_t* live = &Save_Get(fg[bz][bx]).items[0][0];
        mActor_name_t* shadow = &s_shadow[bz][bx][0][0];
        for (int i = 0; i < UT_Z_NUM * UT_X_NUM; i++) {
            if (live[i] != shadow[i]) {
                memset(&c, 0, sizeof(c));
                c.block_x = (uint8_t) bx;
                c.block_z = (uint8_t) bz;
                c.ut_x = (uint8_t) (i % UT_X_NUM);
                c.ut_z = (uint8_t) (i / UT_X_NUM);
                c.item = live[i];
                c.land_id = land;
                nk_net_send_field(&c);
                nk_wii_log("NETGAME: ground change here: acre %d,%d tile %d,%d -> %04x\n", bx, bz, c.ut_x, c.ut_z,
                           c.item);
                shadow[i] = live[i];
            }
        }
        s_scan_acre = (s_scan_acre + 1) % (FG_BLOCK_X_NUM * FG_BLOCK_Z_NUM);
    }
}

/* ---- each frame -------------------------------------------------------------- */

extern int g_nk_trace_scenes;
extern int g_nk_nes_test;
extern int g_nk_travel_test;

/* travel_test: what the station master does when the player asks to travel
 * (src/actor/ac_station_clip.c_inc, ac_npc_station_master_schedule.c_inc),
 * without the walk to the station and the talk */
static void travel_test(GAME_PLAY* play)
{
    static int state;
    static int field_frames;
    s32 chan = -1;
    int res;
    switch (state) {
        case 0:
            if (Save_Get(scene_no) == SCENE_FG && GET_PLAYER_ACTOR(play) != NULL && ++field_frames > 60) {
                state = 1;
            }
            break;
        case 1:
            res = mCD_CheckStation_bg(&chan);
            if (res == mCD_TRANS_ERR_BUSY) {
                break;
            }
            nk_wii_log("TRAVELTEST: the station's card check: %d (slot %d)\n", res, (int) chan);
            state = res == mCD_TRANS_ERR_NONE_NEXTLAND ? 2 : 9;
            break;
        case 2:
            res = mCD_SaveStation_NextLand_bg(&chan);
            if (res == mCD_TRANS_ERR_BUSY) {
                break;
            }
            nk_wii_log("TRAVELTEST: saved for the journey: %d (slot %d)\n", res, (int) chan);
            if (res != mCD_TRANS_ERR_NONE) {
                state = 9;
                break;
            }
            mEv_SetGateway();
            state = 3;
            break;
        case 3: {
            /* aNPS2_setup_game_start: the train ride, then the other town */
            Door_data_c door;
            bzero(&door, sizeof(door));
            door.next_scene_id = SCENE_START_DEMO3;
            door.exit_orientation = mSc_DIRECT_NORTH;
            door.exit_position.x = 120;
            door.exit_position.z = 340;
            door.door_actor_name = EMPTY_NO;
            door.wipe_type = WIPE_TYPE_CIRCLE_RIGHT;
            Common_Get(transition).wipe_type = WIPE_TYPE_FADE_BLACK;
            if (goto_other_scene(play, &door, TRUE) == TRUE) {
                mBGMPsComp_make_ps_wipe(0x328A);
                nk_wii_log("TRAVELTEST: on the train\n");
                state = 4;
            }
            break;
        }
        default:
            break;
    }
}

extern int g_nk_life_test;
extern int g_nk_life_test_topic;
extern int g_nk_crash_test;

/* The topics life_test_topic = 99 goes through, one gossip line each */
static const int s_life_cycle[] = {
    mNL_TOPIC_SELF_MADE_UP,     mNL_TOPIC_SELF_FELL_OUT, mNL_TOPIC_NEWS_MADE_UP, mNL_TOPIC_NEWS_FELL_OUT,
    mNL_TOPIC_NEWS_NEW_FRIENDS, mNL_TOPIC_PLAYER_CATCH,  mNL_TOPIC_PLAYER_FAN,   mNL_TOPIC_MOVING_SAD,
    mNL_TOPIC_MOVING_GLAD,
};

/* settings life_test = 1, villager friendships (m_npc_life.h): on the town's
 * streets, the drift is checked, two villagers become close friends and two
 * others fall out, the villagers of the acre with the most of them get news to
 * tell (made up, fell out, new friends, the player's catch) and opinions about
 * the villager thinking of moving, every chat is gossip, the player is taken
 * (as through a door) to that acre, and the nearest villager not yet talked to
 * is brought to the player until the input script's A starts a chat. Each
 * message the window loads is logged ("LIFETEST: msg"); "LIFETEST: gossip on
 * screen N" comes 10 frames into the Nth gossip line. life_test_topic = N
 * allows only topic N; 99 goes through the news, fan and moving topics.
 * life_test = 2 only watches that acre: two of its villagers are rivals,
 * always make up when they meet and are brought together every five seconds;
 * every two seconds, the distance between each two villagers, their opinions
 * of each other and how much news they know ("LIFETEST: pair"). */
static void life_test(GAME_PLAY* play)
{
    static int field_frames;
    static int seeded;
    static int warped;
    static int last_msg = -1;
    static int gossip_frames = -1;
    static int gossip_count;
    static int talked_mask;
    static int talking_idx = -1;
    static int acre[3];
    static int acre_num;
    static int warp_i = -1;
    static int cycle;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    mMsg_Window_c* window = mMsg_Get_base_window_p();
    mNpc_NpcList_c* npclist = Common_Get(npclist);
    Animal_c* animals = Save_Get(animals);
    int i;

    if (Save_Get(scene_no) != SCENE_FG || player == NULL) {
        return;
    }
    field_frames++;

    if (!seeded && field_frames > 30) {
        int town[4];
        int n = 0;

        for (i = 0; i < ANIMAL_NUM_MAX && n < 4; i++) {
            if (!mNpc_CheckFreeAnimalPersonalID(&animals[i].id)) {
                town[n++] = i;
            }
        }
        if (n == 4) {
            /* drift: five days since the town was last played */
            lbRTC_ymd_c saved = Save_Get(renew_time);
            lbRTC_time_c five_days_ago;
            lbRTC_TimeCopy(&five_days_ago, Common_GetPointer(time.rtc_time));
            lbRTC_Sub_DD(&five_days_ago, 5);
            mTM_set_renew_time(Save_GetPointer(renew_time), &five_days_ago);
            animals[town[0]].animal_relations[town[1]] = 200;
            animals[town[2]].animal_relations[town[3]] = 100;
            mNL_DriftRelations();
            nk_wii_log("LIFETEST: drift over 5 days: 200 -> %d, 100 -> %d\n",
                       animals[town[0]].animal_relations[town[1]], animals[town[2]].animal_relations[town[3]]);
            Save_Set(renew_time, saved);

            animals[town[0]].animal_relations[town[1]] = 220;
            animals[town[1]].animal_relations[town[0]] = 210;
            animals[town[2]].animal_relations[town[3]] = 60;
            animals[town[3]].animal_relations[town[2]] = 90;
        }

        {
            /* the acre with the most villagers, and its villagers */
            int count[FG_BLOCK_Z_NUM + 2][FG_BLOCK_X_NUM + 2];
            int best = -1, bx, bz, wbx = -1, wbz = -1;

            bzero(count, sizeof(count));
            for (i = 0; i < ANIMAL_NUM_MAX; i++) {
                if (npclist[i].name == EMPTY_NO) {
                    continue;
                }
                mFI_Wpos2BlockNum(&bx, &bz, npclist[i].position);
                if (bz >= 0 && bz < FG_BLOCK_Z_NUM + 2 && bx >= 0 && bx < FG_BLOCK_X_NUM + 2 &&
                    ++count[bz][bx] > best)
                {
                    best = count[bz][bx];
                    warp_i = i;
                    wbx = bx;
                    wbz = bz;
                }
            }
            for (i = 0; i < ANIMAL_NUM_MAX && acre_num < 3; i++) {
                if (npclist[i].name != EMPTY_NO) {
                    mFI_Wpos2BlockNum(&bx, &bz, npclist[i].position);
                    if (bx == wbx && bz == wbz) {
                        acre[acre_num++] = i;
                    }
                }
            }
        }

        if (acre_num == 3) {
            /* news to tell: acre[0] made up with acre[1], who fell out with acre[2]; acre[0] heard about two
             * others becoming friends and about the falling out, acre[2] about the making up; acre[1] and
             * acre[2] saw the player catch a fish */
            int x = -1, y = -1, r = Save_Get(remove_animal_idx);
            for (i = 0; i < ANIMAL_NUM_MAX; i++) {
                if (!mNpc_CheckFreeAnimalPersonalID(&animals[i].id) && i != acre[0] && i != acre[1] &&
                    i != acre[2])
                {
                    if (x < 0) {
                        x = i;
                    } else if (y < 0) {
                        y = i;
                    }
                }
            }
            mNL_RecordPairEvent(mNL_EVENT_MADE_UP, acre[0], acre[1]);
            mNL_RecordPairEvent(mNL_EVENT_FELL_OUT, acre[1], acre[2]);
            if (y >= 0) {
                mNL_RecordPairEvent(mNL_EVENT_NEW_FRIENDS, x, y);
                mNL_TellNews(&animals[x], &animals[acre[0]]);
            }
            mNL_TellNews(&animals[acre[1]], &animals[acre[0]]);
            mNL_TellNews(&animals[acre[0]], &animals[acre[2]]);
            mNL_SetPlayerCatch(ITM_FISH_START + 10);
            mNL_SawPlayerCatch(&animals[acre[1]]);
            mNL_SawPlayerCatch(&animals[acre[2]]);
            /* the villager thinking of moving: missed by acre[0], not by acre[2] */
            if (r < ANIMAL_NUM_MAX && r != acre[0] && r != acre[2]) {
                animals[acre[0]].animal_relations[r] = 200;
                animals[acre[2]].animal_relations[r] = 60;
            }
            /* a fan of the player: acre[0], when they've met */
            if (Now_Private != NULL) {
                int m = mNpc_GetAnimalMemoryIdx(&Now_Private->player_ID, animals[acre[0]].memories,
                                                ANIMAL_MEMORY_NUM);
                if (m != -1) {
                    animals[acre[0]].memories[m].friendship = 90;
                }
            }
            if (g_nk_life_test == 2) {
                /* rivals who always make up */
                animals[acre[1]].animal_relations[acre[2]] = 60;
                animals[acre[2]].animal_relations[acre[1]] = 60;
                mNL_make_up_chance = 100;
            }
            nk_wii_log("LIFETEST: acre villagers %d %d %d know %d %d %d events (moving villager %d)\n", acre[0],
                       acre[1], acre[2], mNL_CountEvents(&animals[acre[0]]), mNL_CountEvents(&animals[acre[1]]),
                       mNL_CountEvents(&animals[acre[2]]), r);
        }

        mNL_gossip_chance = 100;
        mNL_test_topic = g_nk_life_test_topic == 99 ? s_life_cycle[0] : g_nk_life_test_topic;
        /* past the first-job hints, which come before any chat (not saved: the test never saves) */
        if (Now_Private != NULL) {
            Now_Private->hint_count = 0x80;
        }
        seeded = 1;
        nk_wii_log("LIFETEST: seeded %d villagers (%d+%d close, %d+%d feud)\n", n, town[0], town[1], town[2],
                   town[3]);
    }

    if (seeded && !warped && field_frames > 60) {
        if (warp_i >= 0) {
            Door_data_c door;
            bzero(&door, sizeof(door));
            door.next_scene_id = SCENE_FG;
            door.exit_orientation = mSc_DIRECT_SOUTH;
            door.exit_position.x = (s16) npclist[warp_i].position.x;
            door.exit_position.y = (s16) npclist[warp_i].position.y;
            door.exit_position.z = (s16) npclist[warp_i].position.z;
            door.door_actor_name = EMPTY_NO;
            door.wipe_type = WIPE_TYPE_FADE_BLACK;
            if (goto_other_scene(play, &door, TRUE) == TRUE) {
                warped = 1;
                nk_wii_log("LIFETEST: to villager %d's acre (%d,%d)\n", warp_i, (int) door.exit_position.x,
                           (int) door.exit_position.z);
            }
        } else {
            warped = 1;
        }
        return;
    }

    if (warped && g_nk_life_test == 2) {
        Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
        ACTOR* a = list->actor;
        int n;

        if (field_frames % 300 == 0 && acre_num == 3) {
            /* the rivals brought together */
            ACTOR* rival0 = NULL;
            ACTOR* rival1 = NULL;
            for (n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
                int idx = mNL_GetAnimalIdx(((NPC_ACTOR*) a)->npc_info.animal);
                if (idx == acre[1]) {
                    rival0 = a;
                } else if (idx == acre[2]) {
                    rival1 = a;
                }
            }
            if (rival0 != NULL && rival1 != NULL &&
                animals[acre[1]].animal_relations[acre[2]] <= mNL_RELATION_RIVAL)
            {
                rival1->world.position.x = rival0->world.position.x + 50.0f;
                rival1->world.position.z = rival0->world.position.z;
            }
            a = list->actor;
        }
        if (field_frames % 120 == 0) {
            for (n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
                NPC_ACTOR* na = (NPC_ACTOR*) a;
                int ia = mNL_GetAnimalIdx(na->npc_info.animal);
                ACTOR* b;
                int m;
                if (ia < 0) {
                    continue;
                }
                for (b = a->next_actor, m = n - 1; m != 0 && b != NULL; m--, b = b->next_actor) {
                    NPC_ACTOR* nb = (NPC_ACTOR*) b;
                    int ib = mNL_GetAnimalIdx(nb->npc_info.animal);
                    if (ib < 0) {
                        continue;
                    }
                    nk_wii_log("LIFETEST: pair %d-%d dist %.0f rel %d/%d news %d/%d\n", ia, ib,
                               (double) search_position_distanceXZ(&a->world.position, &b->world.position),
                               na->npc_info.animal->animal_relations[ib], nb->npc_info.animal->animal_relations[ia],
                               mNL_CountEvents(na->npc_info.animal), mNL_CountEvents(nb->npc_info.animal));
                }
            }
        }
    } else if (warped) {
        /* the nearest town villager not yet talked to, in front of the player, until a chat starts */
        Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
        ACTOR* actor = list->actor;
        NPC_ACTOR* nearest = NULL;
        f32 best = 1e9f;
        int talking = -1;

        for (i = list->num_actors; i != 0 && actor != NULL; i--, actor = actor->next_actor) {
            NPC_ACTOR* npc = (NPC_ACTOR*) actor;
            int idx;
            f32 dist;

            if (ITEM_NAME_GET_TYPE(actor->npc_id) != NAME_TYPE_NPC) {
                continue;
            }
            idx = mNL_GetAnimalIdx(npc->npc_info.animal);
            if (idx < 0) {
                continue;
            }
            if (npc->condition_info.talk_condition != aNPC_TALK_TYPE_NONE) {
                talking = idx;
            }
            if (talked_mask & (1 << idx)) {
                continue;
            }
            dist = search_position_distanceXZ(&actor->world.position, &player->actor_class.world.position);
            if (dist < best) {
                best = dist;
                nearest = npc;
            }
        }
        if (talking >= 0) {
            talking_idx = talking;
        } else if (talking_idx >= 0) {
            /* that chat is over: the next villager */
            talked_mask |= 1 << talking_idx;
            nk_wii_log("LIFETEST: talked to villager %d\n", talking_idx);
            talking_idx = -1;
        }
        if (nearest == NULL && talked_mask != 0) {
            talked_mask = 0;
        }
        {
            /* the villager's "what can I do for you?" menu (msgs 0x2A6 on): "Let's talk!", the second
             * of the three (aQMgr_talk_quest_select_get_choice) */
            mChoice_c* choice = mChoice_Get_base_window_p();
            if (choice->main_index == mChoice_MAIN_NORMAL && choice->data.choice_num == 3 && last_msg >= 0x2A6 &&
                last_msg <= 0x2B1)
            {
                choice->selected_choice_idx = 1;
            }
        }
        if (nearest != NULL && talking < 0 && window->main_index == mMsg_INDEX_HIDE) {
            /* the villager just beside the player, on its own side, and the player facing it */
            xyz_t* ppos = &player->actor_class.world.position;
            s16 angle = search_position_angleY(ppos, &nearest->actor_class.world.position);
            if (best > 40.0f) {
                nearest->actor_class.world.position.x = ppos->x + sin_s(angle) * 36.0f;
                nearest->actor_class.world.position.z = ppos->z + cos_s(angle) * 36.0f;
                nearest->actor_class.world.position.y = ppos->y;
            }
            player->actor_class.shape_info.rotation.y = angle;
            player->actor_class.world.angle.y = angle;
        }
    }

    {
        static int decide_seen;
        if (mNL_decide_count != decide_seen) {
            decide_seen = mNL_decide_count;
            nk_wii_log("LIFETEST: gossip asked of villager %d (looks %d, topic %d): 0x%04X\n", mNL_decide_last[0],
                       mNL_decide_last[1], mNL_test_topic, mNL_decide_last[2] & 0xFFFF);
        }
    }
    if (window->msg_data != NULL && window->main_index != mMsg_INDEX_HIDE && window->msg_data->msg_no != last_msg) {
        int topic;

        last_msg = window->msg_data->msg_no;
        topic = mNL_GetMsgTopic(last_msg);
        nk_wii_log("LIFETEST: msg 0x%04X%s topic %d\n", last_msg, topic >= 0 ? " (gossip)" : "", topic);
        if (topic >= 0) {
            gossip_frames = 0;
            if (g_nk_life_test_topic == 99 && cycle < (int) (sizeof(s_life_cycle) / sizeof(s_life_cycle[0])) &&
                topic == s_life_cycle[cycle])
            {
                cycle++;
                mNL_test_topic = cycle < (int) (sizeof(s_life_cycle) / sizeof(s_life_cycle[0]))
                                     ? s_life_cycle[cycle]
                                     : -1;
            }
        }
    }
    if (gossip_frames >= 0 && ++gossip_frames == 10) {
        gossip_frames = -1;
        nk_wii_log("LIFETEST: gossip on screen %d\n", ++gossip_count);
    }
}

/* Town life (m_town_life.h) tests. Both take the player (as through a door) to the acre with the most villagers.
 *
 * life_test = 3 watches activities as they happen: every activity started, ended or interrupted
 * ("LIFETEST: act"), and each game minute how many villagers are busy ("LIFETEST: busy"). life_test_topic, when
 * set, is the percent of wander decisions that consider an activity.
 *
 * life_test = 4 is the animation and prop check: the villager nearest the player is brought beside it and
 * plays each candidate animation in turn (mTL_ACT_TEST), facing the camera, from life_test_topic on when set.
 * Three times during each, "LIFETEST: shot N" asks for a screenshot (the test's watcher touches dolphin_run's
 * shot-N.req); the log says whether the animation loops or stops, and whether the prop came out. The villager
 * doesn't seek out or avoid the player meanwhile (the friendship interrupt), which would keep it from starting.
 *
 * life_test = 5 checks that activities end cleanly: the villager nearest the player fans itself (a prop and a
 * sub-animation) and is interrupted in turn by a chat with the player (the input script's A; the line it says
 * is logged), by rain starting and by its schedule sending it home. After each, "LIFETEST: after" says whether
 * the prop, the fan pose and the act are gone.
 *
 * life_test = 6 watches gatherings: the villagers of the acre all become friends, but the first two become
 * rivals; groups may form at any hour (life_test_topic: percent of decisions that try, default 60). Each group
 * formed or ended is logged with its members ("LIFETEST: group"), screenshots are taken of groups, and each
 * game minute says how often friends were within 100 units of each other ("LIFETEST: friends close"). After
 * five minutes, one villager is sent home, and a friend should walk it there ("LIFETEST: escort"). With
 * town_life = 0 the same log gives the baseline.
 *
 * life_test = 7 watches speech bubbles: as in 6, and villagers also do activities; every ten seconds the player
 * is moved next to a group or a busy villager. Each bubble is logged ("LIFETEST: bubble") with a screenshot of
 * the first eight, and a bubble on screen while the speech window is open is an error ("BUBBLE OVER WINDOW";
 * the input script's A starts chats).
 *
 * life_test = 8 watches villagers tending the town, with the limits off: in turn they plant flowers, pick
 * fruit, tend flowers and (in a pretend snowman season) build a snowman. Every ground change against a snapshot
 * of the town is logged ("LIFETEST: ground"), with screenshots of the first few; a gift letter is sent and the
 * mailbox counted; then, limits on, villagers who planted may not plant again. On an online host, a guest's log
 * should show the same changes arriving ("NETGAME: ground change from the others").
 *
 * life_test = 9 checks visitors: today's visitor is put in the player's acre and should appear and wander
 * ("LIFETEST: visitor" every five seconds, with screenshots); then it is brought in front of the player and
 * the input script's A starts a chat (its introduction), and another (its hello for later in the day); then a
 * resident is brought over, whose gossip is about the visitor. The
 * messages shown are logged, and the mask records in use (none must be taken from an event).
 *
 * life_test = 10 checks weather and night: with snow, the acre's villagers (made friends) gather and throw
 * snowballs ("LIFETEST: snowball"); in rain, they splash in puddles and shelter under trees; at night (pretend),
 * they look up at the stars, turning slowly. Screenshots of each. */
typedef struct {
    const char* name;
    u8 anim;
    u8 sub_anim;
    u8 prop;
} town_test_anim_c;

static const town_test_anim_c s_town_anims[] = {
    {"SITDOWN_WAIT1", aNPC_ANIM_SITDOWN_WAIT1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"SITDOWN_DRINK1+cup", aNPC_ANIM_SITDOWN_DRINK1, aNPC_SUB_ANIM_NONE, TOOL_TUMBLER},
    {"SITDOWN_DRINK1", aNPC_ANIM_SITDOWN_DRINK1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"SITDOWN_CLAP1", aNPC_ANIM_SITDOWN_CLAP1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"SITDOWN_WAIT1+fan", aNPC_ANIM_SITDOWN_WAIT1, aNPC_SUB_ANIM_UTIWA, TOOL_UTIWA},
    {"SITDOWN_WAIT_D1", aNPC_ANIM_SITDOWN_WAIT_D1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"KOKKURI_D1", aNPC_ANIM_KOKKURI_D1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"KOKKURI_D2", aNPC_ANIM_KOKKURI_D2, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"DANCE1", aNPC_ANIM_DANCE1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TAISOU1", aNPC_ANIM_TAISOU1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TAISOU2", aNPC_ANIM_TAISOU2, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TAISOU3_A", aNPC_ANIM_TAISOU3_A, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TAISOU4_A", aNPC_ANIM_TAISOU4_A, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TAISOU5_A", aNPC_ANIM_TAISOU5_A, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TAISOU6_A", aNPC_ANIM_TAISOU6_A, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TAISOU7", aNPC_ANIM_TAISOU7, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"WARMUP1", aNPC_ANIM_WARMUP1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"ASIHUMI1", aNPC_ANIM_ASIHUMI1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TIRED1", aNPC_ANIM_TIRED1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TAMANAGE1+ball", aNPC_ANIM_TAMANAGE1, aNPC_SUB_ANIM_NONE, TOOL_TAMA1},
    {"TAMAHIROI1", aNPC_ANIM_TAMAHIROI1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TURI_WAIT1+rod", aNPC_ANIM_TURI_WAIT1, aNPC_SUB_ANIM_NONE, TOOL_NPC_SAO},
    {"BANZAI1", aNPC_ANIM_BANZAI1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"HATAFURI1+flag", aNPC_ANIM_HATAFURI1, aNPC_SUB_ANIM_NONE, TOOL_FLAG},
    {"WAIT1+fan", aNPC_ANIM_WAIT1, aNPC_SUB_ANIM_UTIWA, TOOL_UTIWA},
    {"OMAIRI1", aNPC_ANIM_OMAIRI1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"SAISEN1", aNPC_ANIM_SAISEN1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"KYORO1", aNPC_ANIM_KYORO1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"TKYKYORO1", aNPC_ANIM_TKYKYORO1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"ENSOU_E1", aNPC_ANIM_ENSOU_E1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"KEITAI_TALK1+phone", aNPC_ANIM_KEITAI_TALK1, aNPC_SUB_ANIM_NONE, TOOL_KEITAI},
    {"KEITAI_TALK2+phone", aNPC_ANIM_KEITAI_TALK2, aNPC_SUB_ANIM_NONE, TOOL_KEITAI},
    {"CRACKER_WAIT1+cracker", aNPC_ANIM_CRACKER_WAIT1, aNPC_SUB_ANIM_NONE, TOOL_CRACKER},
    {"KUISINBO1", aNPC_ANIM_KUISINBO1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"CLAP1", aNPC_ANIM_CLAP1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"KIDUKU1", aNPC_ANIM_KIDUKU1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"WAIT_NEMU1", aNPC_ANIM_WAIT_NEMU1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"NEBOKE1", aNPC_ANIM_NEBOKE1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"HAPPY1", aNPC_ANIM_HAPPY1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
    {"SMILE1", aNPC_ANIM_SMILE1, aNPC_SUB_ANIM_NONE, mTL_PROP_NONE},
};

/* The acre with the most villagers: one of its villagers (npclist index), or -1 */
static int busiest_acre_npc(void)
{
    mNpc_NpcList_c* npclist = Common_Get(npclist);
    int count[FG_BLOCK_Z_NUM + 2][FG_BLOCK_X_NUM + 2];
    int best = -1, bx, bz, found = -1, i;

    bzero(count, sizeof(count));
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        if (npclist[i].name == EMPTY_NO) {
            continue;
        }
        mFI_Wpos2BlockNum(&bx, &bz, npclist[i].position);
        if (bz >= 0 && bz < FG_BLOCK_Z_NUM + 2 && bx >= 0 && bx < FG_BLOCK_X_NUM + 2 && ++count[bz][bx] > best) {
            best = count[bz][bx];
            found = i;
        }
    }
    return found;
}

static NPC_ACTOR* town_test_npc(GAME_PLAY* play, int idx)
{
    Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
    ACTOR* a = list->actor;
    int n;

    for (n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
        if (ITEM_NAME_GET_TYPE(a->npc_id) == NAME_TYPE_NPC && mNL_GetAnimalIdx(((NPC_ACTOR*) a)->npc_info.animal) == idx) {
            return (NPC_ACTOR*) a;
        }
    }
    return NULL;
}

/* After an interruption: is the activity gone without a trace? */
static void town_test_check_clean(const char* what, NPC_ACTOR* npc, mTL_villager_c* v)
{
    int clean = v->state != mTL_STATE_DOING && v->prop == NULL && npc->draw.sub_anim_type != aNPC_SUB_ANIM_UTIWA &&
                !(npc->action.idx == aNPC_ACT_SPECIAL && npc->action.step != aNPC_ACTION_END_STEP);
    ACTOR* hand = npc->right_hand.item_actor_p;

    if (hand != NULL && ((TOOLS_ACTOR*) hand)->tool_name == TOOL_UTIWA) {
        clean = FALSE;
    }
    nk_wii_log("LIFETEST: after %s: state %d prop %d hand %d sub %d act %d step %d -> %s\n", what, (int) v->state,
               v->prop != NULL, hand != NULL ? ((TOOLS_ACTOR*) hand)->tool_name : -1, (int) npc->draw.sub_anim_type,
               (int) npc->action.idx, (int) npc->action.step, clean ? "OK" : "FAIL");
}

static void town_test_interrupt(GAME_PLAY* play)
{
    static const char* const why[] = {"wait", "walk", "run", "turn", "turn2", "insect", "fish", "greeting", "chat",
                                      "into house", "leave house", "umbrella", "umbrella", "ensou", "chat",
                                      "react", "clap", "trans", "get", "cloth", "pitfall", "revive", "special"};
    static int subject = -1;
    static int step;
    static int frames;
    static int last_msg = -1;
    static int interrupted_seen;
    static int chats;
    static int line_seen;
    static int line_frames = -1;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    mMsg_Window_c* window = mMsg_Get_base_window_p();
    NPC_ACTOR* npc;
    mTL_villager_c* v;
    xyz_t* ppos = &player->actor_class.world.position;

    if (subject < 0) {
        Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
        ACTOR* a = list->actor;
        f32 best = 1e9f;
        int n;
        for (n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
            int idx;
            f32 d;
            if (ITEM_NAME_GET_TYPE(a->npc_id) != NAME_TYPE_NPC) {
                continue;
            }
            idx = mNL_GetAnimalIdx(((NPC_ACTOR*) a)->npc_info.animal);
            d = search_position_distanceXZ(&a->world.position, ppos);
            if (idx >= 0 && d < best) {
                best = d;
                subject = idx;
            }
        }
        if (subject < 0) {
            return;
        }
        mTL_test_entry.name = "fan";
        mTL_test_entry.anim = aNPC_ANIM_WAIT1;
        mTL_test_entry.sub_anim = aNPC_SUB_ANIM_UTIWA;
        mTL_test_entry.prop = TOOL_UTIWA;
        mTL_test_entry.place = mTL_PLACE_ANYWHERE;
        mTL_test_entry.line = mTL_LINE_FAN;
        mTL_test_activity = mTL_ACT_TEST;
        mTL_test_villager = subject;
        mTL_test_seconds = 600;
        mTL_start_chance = 100;
        mTL_talk_chance = 100;
        if (Now_Private != NULL) {
            Now_Private->hint_count = 0x80; /* (no first-job hints in the way of the chat) */
        }
        nk_wii_log("LIFETEST: interruption test on villager %d\n", subject);
    }

    npc = town_test_npc(play, subject);
    v = mTL_GetVillager(subject);
    if (npc == NULL || v == NULL) {
        return;
    }
    npc->think.interrupt_flags &= ~aNPC_THINK_INTERRUPT_FRIENDSHIP;
    frames++;

    if (window->msg_data != NULL && window->main_index != mMsg_INDEX_HIDE && window->msg_data->msg_no != last_msg) {
        int topic;
        last_msg = window->msg_data->msg_no;
        topic = mNL_GetMsgTopic(last_msg);
        nk_wii_log("LIFETEST: msg 0x%04X topic %d%s\n", last_msg, topic, topic >= mNL_TOPIC_NUM ? " (activity line)" : "");
        if (topic >= mNL_TOPIC_NUM && !line_seen) {
            line_seen = 1;
            line_frames = 0;
        }
    }
    if (line_frames >= 0 && ++line_frames == 40) {
        nk_wii_log("LIFETEST: shot 1 activity-line\n");
    }
    {
        /* the villager's "what can I do for you?" menu (msgs 0x2A6 on): "Let's talk!", the second of three */
        mChoice_c* choice = mChoice_Get_base_window_p();
        if (choice->main_index == mChoice_MAIN_NORMAL && choice->data.choice_num == 3 && last_msg >= 0x2A6 &&
            last_msg <= 0x2B1) {
            choice->selected_choice_idx = 1;
        }
    }
    if (v->state != mTL_STATE_DOING && frames % 600 == 0 && step < 3) {
        nk_wii_log("LIFETEST: waiting in step %d: state %d act %d step %d think %d cooldown %d hand %d talk %d\n", step,
                   (int) v->state, (int) npc->action.idx, (int) npc->action.step, (int) npc->think.idx,
                   (int) v->cooldown, npc->right_hand.item_actor_p != NULL, (int) npc->condition_info.talk_condition);
    }

    if (mTL_stats.interrupted != interrupted_seen) {
        int act = mTL_stats.last_interrupt_act;
        interrupted_seen = mTL_stats.interrupted;
        town_test_check_clean(act >= 0 && act < (int) (sizeof(why) / sizeof(why[0])) ? why[act] : "?", npc, v);
        if (step == 0 && (act == aNPC_ACT_TALK || act == aNPC_ACT_TALK2)) {
            chats++;
        }
        frames = 0;
    }

    switch (step) {
        case 0: /* fanning in front of the player until the player's chat interrupts it */
            if (v->state == mTL_STATE_DOING && v->prop != NULL) {
                s16 angle = search_position_angleY(ppos, &npc->actor_class.world.position);
                npc->actor_class.world.position.x = ppos->x + sin_s(angle) * 36.0f;
                npc->actor_class.world.position.z = ppos->z + cos_s(angle) * 36.0f;
                player->actor_class.shape_info.rotation.y = angle;
                player->actor_class.world.angle.y = angle;
            }
            /* (the first chat with a villager the player hasn't met is an introduction) */
            if ((line_seen || chats >= 4) && npc->condition_info.talk_condition == aNPC_TALK_TYPE_NONE &&
                window->main_index == mMsg_INDEX_HIDE) {
                step = 1;
                frames = 0;
            }
            break;
        case 1: /* fanning again, away from the player; then rain */
            if (frames == 30) {
                npc->actor_class.world.position.x = ppos->x + 120.0f;
                npc->actor_class.world.position.z = ppos->z;
            }
            if (frames > 60 && v->state == mTL_STATE_DOING && v->prop != NULL && npc->action.step != 0) {
                nk_wii_log("LIFETEST: fanning again; rain starts\n");
                Common_Set(weather, mEnv_WEATHER_RAIN);
                Common_Set(weather_intensity, mEnv_WEATHER_INTENSITY_NORMAL);
                step = 2;
                frames = 0;
            }
            break;
        case 2: /* the umbrella out: rain stops; once fanning again, home time */
            if (frames == 300) {
                Common_Set(weather, mEnv_WEATHER_CLEAR);
                Common_Set(weather_intensity, mEnv_WEATHER_INTENSITY_NONE);
            }
            if (frames > 300 && v->state == mTL_STATE_DOING && v->prop != NULL && npc->action.step != 0) {
                nk_wii_log("LIFETEST: fanning again; the schedule sends it home\n");
                if (npc->npc_info.schedule != NULL) {
                    npc->npc_info.schedule->forced_type = mNPS_SCHED_IN_HOUSE;
                    npc->npc_info.schedule->forced_timer = 100000;
                }
                step = 3;
                frames = 0;
            }
            break;
        case 3:
            if (frames == 300) {
                nk_wii_log("LIFETEST: interruption test done (started %d ended %d interrupted %d, %d chats, activity "
                           "line %s)\n",
                           mTL_stats.started, mTL_stats.ended, mTL_stats.interrupted, chats, line_seen ? "seen" : "NOT seen");
            }
            break;
    }
}

static void town_test_groups(GAME_PLAY* play)
{
    static int seeded;
    static int frames;
    static int formed_seen, ended_seen, escorts_seen;
    static int close_n, close_total;
    static int last_min = -1;
    static int shots;
    static int shot_frames = -1;
    static int rival[2] = {-1, -1};
    static int home_idx = -1;
    Animal_c* animals = Save_Get(animals);
    Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
    const lbRTC_time_c* t = Common_GetPointer(time.rtc_time);
    ACTOR* a;
    int n, g, i;

    frames++;
    if (!seeded) {
        int ids[ANIMAL_NUM_MAX], count = 0;
        if (frames < 60 || (list->num_actors < 3 && frames < 900)) {
            return;
        }
        for (a = list->actor, n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
            int idx;
            if (ITEM_NAME_GET_TYPE(a->npc_id) != NAME_TYPE_NPC) {
                continue;
            }
            idx = mNL_GetAnimalIdx(((NPC_ACTOR*) a)->npc_info.animal);
            if (idx >= 0) {
                ids[count++] = idx;
            }
        }
        for (i = 0; i < count; i++) {
            for (n = 0; n < count; n++) {
                if (i != n) {
                    animals[ids[i]].animal_relations[ids[n]] = 200;
                }
            }
        }
        if (count >= 3) {
            rival[0] = ids[0];
            rival[1] = ids[1];
            animals[ids[0]].animal_relations[ids[1]] = 60;
            animals[ids[1]].animal_relations[ids[0]] = 60;
        }
        mTL_test_group_any_hour = TRUE;
        mTL_group_chance = g_nk_life_test_topic > 0 ? g_nk_life_test_topic : 60;
        mTL_start_chance = g_nk_life_test == 7 ? 40 : 0;
        seeded = 1;
        frames = 60;
        nk_wii_log("LIFETEST: %d villagers here made friends, rivals %d and %d; town life %d\n", count, rival[0],
                   rival[1], mTL_IsEnabled());
    }

    if (mTL_stats.groups_formed != formed_seen || mTL_stats.groups_ended != ended_seen) {
        formed_seen = mTL_stats.groups_formed;
        ended_seen = mTL_stats.groups_ended;
        for (g = 0; g < mTL_GROUP_NUM; g++) {
            mTL_group_c* gr = &mTL_groups[g];
            int with_rivals = 0, has0 = 0, has1 = 0;
            if (!gr->active) {
                continue;
            }
            for (i = 0; i < gr->count; i++) {
                has0 |= gr->members[i] == rival[0];
                has1 |= gr->members[i] == rival[1];
            }
            with_rivals = has0 && has1;
            nk_wii_log("LIFETEST: group %d: %d members %d %d %d %d, %d s%s\n", g, gr->count, gr->members[0],
                       gr->members[1], gr->count > 2 ? gr->members[2] : -1, gr->count > 3 ? gr->members[3] : -1,
                       gr->seconds, with_rivals ? " WITH RIVALS" : "");
        }
        nk_wii_log("LIFETEST: groups formed %d ended %d (members %d), last interruption by act %d\n",
                   mTL_stats.groups_formed, mTL_stats.groups_ended, mTL_stats.group_members,
                   mTL_stats.last_interrupt_act);
    }

    if (frames % 60 == 0 && frames < 60 * 150) {
        /* (where each group member is) */
        for (g = 0; g < mTL_GROUP_NUM; g++) {
            mTL_group_c* gr = &mTL_groups[g];
            for (i = 0; gr->active && i < gr->count; i++) {
                mTL_villager_c* v = mTL_GetVillager(gr->members[i]);
                NPC_ACTOR* npc = gr->members[i] >= 0 ? town_test_npc(play, gr->members[i]) : NULL;
                xyz_t spot;
                if (v == NULL || npc == NULL) {
                    continue;
                }
                spot.x = v->spot_x;
                spot.y = 0.0f;
                spot.z = v->spot_z;
                nk_wii_log("LIFETEST: member %d state %d tries %d act %d step %d think %d dist %.0f arrived %d\n",
                           gr->members[i], (int) v->state, (int) v->tries, (int) npc->action.idx,
                           (int) npc->action.step, (int) npc->think.idx,
                           (double) search_position_distanceXZ(&npc->actor_class.world.position, &spot),
                           (gr->arrived >> i) & 1);
            }
        }
    }

    if (g_nk_life_test == 7) {
        static int bubbles_seen, over_window, bubble_shots, bubble_shot_frames = -1;
        PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
        mMsg_Window_c* window = mMsg_Get_base_window_p();

        if (mTL_bubble_stats.shown != bubbles_seen) {
            bubbles_seen = mTL_bubble_stats.shown;
            nk_wii_log("LIFETEST: bubble %d villager %d topic %d screen y %d\n", bubbles_seen,
                       mTL_bubble_stats.last_villager, mTL_bubble_stats.last_topic, mTL_bubble_stats.screen_y);
            if (bubble_shot_frames < 0 && bubble_shots < 8) {
                bubble_shot_frames = 0;
            }
        }
        if (bubble_shot_frames >= 0 && ++bubble_shot_frames == 45) {
            bubble_shot_frames = -1;
            if (mTL_BubblesShowing() > 0) {
                nk_wii_log("LIFETEST: shot %d bubble-topic%d\n", ++bubble_shots, mTL_bubble_stats.last_topic);
            }
        }
        if (window->main_index != mMsg_INDEX_HIDE && mTL_BubblesShowing() > 0 && over_window++ < 5) {
            nk_wii_log("LIFETEST: BUBBLE OVER WINDOW\n");
        }
        if (frames % 600 == 300 && player != NULL && window->main_index == mMsg_INDEX_HIDE) {
            /* the player next to a group, else a busy villager */
            xyz_t* target = NULL;
            xyz_t center;
            for (g = 0; g < mTL_GROUP_NUM && target == NULL; g++) {
                if (mTL_groups[g].active && mTL_groups[g].arrived) {
                    center.x = mTL_groups[g].center_x;
                    center.y = player->actor_class.world.position.y;
                    center.z = mTL_groups[g].center_z;
                    target = &center;
                }
            }
            for (i = 0; i < ANIMAL_NUM_MAX && target == NULL; i++) {
                mTL_villager_c* v = mTL_GetVillager(i);
                NPC_ACTOR* npc = town_test_npc(play, i);
                if (v != NULL && v->state == mTL_STATE_DOING && npc != NULL) {
                    target = &npc->actor_class.world.position;
                }
            }
            if (target != NULL) {
                player->actor_class.world.position.x = target->x + 70.0f;
                player->actor_class.world.position.z = target->z - 30.0f;
            }
        }
        if (frames == 60 * 60 * 6) {
            nk_wii_log("LIFETEST: bubble test done (shown %d, over the speech window %d)\n", mTL_bubble_stats.shown,
                       over_window);
        }
        return;
    }

    /* a screenshot once a group has gathered */
    for (g = 0; g < mTL_GROUP_NUM && shot_frames < 0 && shots < 6; g++) {
        static int shot_seq[mTL_GROUP_NUM];
        mTL_group_c* gr = &mTL_groups[g];
        int arrived = 0;
        for (i = 0; i < gr->count; i++) {
            arrived += (gr->arrived >> i) & 1;
        }
        if (gr->active && arrived >= 2 && gr->turn_seq >= 2 && shot_seq[g] != mTL_stats.groups_formed) {
            PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
            shot_seq[g] = mTL_stats.groups_formed;
            shot_frames = 0;
            if (player != NULL) {
                /* the player beside the group, so the camera sees it */
                player->actor_class.world.position.x = gr->center_x + 80.0f;
                player->actor_class.world.position.z = gr->center_z - 40.0f;
            }
        }
    }
    if (shot_frames >= 0 && ++shot_frames == 90) {
        nk_wii_log("LIFETEST: shot %d group\n", ++shots);
        shot_frames = -1;
    }

    if (frames % 60 == 0) {
        /* friend pairs among the villagers here, and how many are close */
        ACTOR* b;
        int m;
        for (a = list->actor, n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
            int ia = ITEM_NAME_GET_TYPE(a->npc_id) == NAME_TYPE_NPC ? mNL_GetAnimalIdx(((NPC_ACTOR*) a)->npc_info.animal) : -1;
            if (ia < 0) {
                continue;
            }
            for (b = a->next_actor, m = n - 1; m != 0 && b != NULL; m--, b = b->next_actor) {
                int ib = ITEM_NAME_GET_TYPE(b->npc_id) == NAME_TYPE_NPC ? mNL_GetAnimalIdx(((NPC_ACTOR*) b)->npc_info.animal) : -1;
                if (ib < 0 || animals[ia].animal_relations[ib] < mNL_RELATION_FRIEND ||
                    animals[ib].animal_relations[ia] < mNL_RELATION_FRIEND) {
                    continue;
                }
                close_total++;
                close_n += search_position_distanceXZ(&a->world.position, &b->world.position) < 100.0f;
            }
        }
    }
    if (t->min != last_min) {
        if (last_min >= 0) {
            nk_wii_log("LIFETEST: friends close %02d:%02d %d of %d (%d%%), groups formed %d\n", (int) t->hour,
                       (int) t->min, close_n, close_total, close_total ? close_n * 100 / close_total : 0,
                       mTL_stats.groups_formed);
        }
        last_min = t->min;
    }

    /* after five minutes, someone goes home: a friend should walk them */
    if (frames == 60 * 60 * 5) {
        for (a = list->actor, n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
            NPC_ACTOR* npc = (NPC_ACTOR*) a;
            int idx = ITEM_NAME_GET_TYPE(a->npc_id) == NAME_TYPE_NPC ? mNL_GetAnimalIdx(npc->npc_info.animal) : -1;
            if (idx >= 0 && idx != rival[0] && npc->npc_info.schedule != NULL) {
                mTL_test_group_any_hour = TRUE;
                mTL_group_chance = 0;
                npc->npc_info.schedule->forced_type = mNPS_SCHED_IN_HOUSE;
                npc->npc_info.schedule->forced_timer = 100000;
                home_idx = idx;
                nk_wii_log("LIFETEST: villager %d sent home\n", idx);
                break;
            }
        }
    }
    if (mTL_stats.escorts != escorts_seen) {
        escorts_seen = mTL_stats.escorts;
        nk_wii_log("LIFETEST: escort %d starts walking %d home\n", mTL_stats.last_villager, home_idx);
    }
    if (home_idx >= 0 && frames % 120 == 0 && frames < 60 * 60 * 6) {
        for (i = 0; i < ANIMAL_NUM_MAX; i++) {
            mTL_villager_c* v = mTL_GetVillager(i);
            NPC_ACTOR* e = town_test_npc(play, i);
            NPC_ACTOR* h = town_test_npc(play, home_idx);
            if (v != NULL && v->state == mTL_STATE_ESCORT && e != NULL && h != NULL) {
                nk_wii_log("LIFETEST: escort %d beside %d: dist %.0f%s\n", i, home_idx,
                           (double) search_position_distanceXZ(&e->actor_class.world.position,
                                                               &h->actor_class.world.position),
                           h->condition_info.hide_flg ? " (home)" : "");
            }
        }
    }
    if (frames == 60 * 60 * 6) {
        nk_wii_log("LIFETEST: gathering test done (groups formed %d, members %d, escorts %d, friends close %d of %d)\n",
                   mTL_stats.groups_formed, mTL_stats.group_members, mTL_stats.escorts, close_n, close_total);
    }
}

static int mailbox_count(void)
{
    int player_no = Common_Get(player_no);
    mHm_hs_c* home;
    int i, n = 0;
    if (player_no >= PLAYER_NUM) {
        return -1;
    }
    home = Save_GetPointer(homes[mHS_get_arrange_idx(player_no)]);
    for (i = 0; i < HOME_MAILBOX_SIZE; i++) {
        n += !mMl_check_not_used_mail(&home->mailbox[i]);
    }
    return n;
}

static void town_test_tend(GAME_PLAY* play)
{
    static const struct {
        int act;
        int seconds;
    } steps[] = {
        {mTL_ACT_PLANT, 60}, {mTL_ACT_PICK_FRUIT, 60}, {mTL_ACT_TEND_FLOWERS, 30}, {mTL_ACT_SNOWMAN, 60},
    };
    static mActor_name_t snap[FG_BLOCK_Z_NUM][FG_BLOCK_X_NUM][UT_Z_NUM][UT_X_NUM];
    static int frames, step = -1, step_frames, shots, shot_frames = -1;
    static int planted_by[ANIMAL_NUM_MAX];
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    int bx, bz, i;

    frames++;
    if (step < 0) {
        int before;
        if (frames < (nk_net_mode() == NK_NET_HOST ? 60 * 90 : 600)) {
            return; /* (after the acre has settled: buildings mark their tiles as it loads; online, the guest
                     * has time to arrive) */
        }
        memcpy(snap, Save_Get(fg), sizeof(snap));
        /* a gift: the first villager who remembers the player becomes a good friend */
        for (i = 0; i < ANIMAL_NUM_MAX && Now_Private != NULL; i++) {
            Animal_c* animal = &Save_Get(animals[i]);
            int m = mNpc_CheckFreeAnimalPersonalID(&animal->id)
                        ? -1
                        : mNpc_GetAnimalMemoryIdx(&Now_Private->player_ID, animal->memories, ANIMAL_MEMORY_NUM);
            if (m >= 0) {
                animal->memories[m].friendship = 90;
                break;
            }
        }
        mTL_test_no_limits = TRUE;
        before = mailbox_count();
        mTL_DailyGifts();
        nk_wii_log("LIFETEST: gift letters %d (from villager %d), mailbox %d -> %d\n", mTL_stats.gifts,
                   mTL_stats.last_villager, before, mailbox_count());
        mTL_start_chance = 100;
        mTL_test_keep_place = TRUE;
        mTL_test_seconds = 0;
        step = 0;
        step_frames = 0;
        mTL_test_activity = steps[0].act;
        nk_wii_log("LIFETEST: tending: %s\n", mTL_GetActivity(steps[0].act)->name);
    }

    if (step < (int) (sizeof(steps) / sizeof(steps[0])) && ++step_frames >= steps[step].seconds * 60) {
        step++;
        step_frames = 0;
        if (step < (int) (sizeof(steps) / sizeof(steps[0]))) {
            mTL_test_activity = steps[step].act;
            mTL_test_winter = steps[step].act == mTL_ACT_SNOWMAN;
            if (steps[step].act == mTL_ACT_PICK_FRUIT) {
                /* an apple tree in fruit beside each house here (snapshot too, so only the picking shows) */
                for (i = 0; i < ANIMAL_NUM_MAX; i++) {
                    xyz_t home, p;
                    int k;
                    if (town_test_npc(play, i) == NULL || !mTL_GetHomePos(i, &home)) {
                        continue;
                    }
                    for (k = 0; k < 8; k++) {
                        mActor_name_t* fg;
                        p.x = home.x + ((k & 1) ? 3 : -3) * mFI_UT_WORLDSIZE_X_F;
                        p.y = home.y;
                        p.z = home.z + (k / 2 - 1) * mFI_UT_WORLDSIZE_Z_F;
                        fg = mFI_GetUnitFG(p);
                        if (fg != NULL && *fg == EMPTY_NO && mFI_SetFG_common(TREE_APPLE_FRUIT, p, TRUE)) {
                            int ux, uz, bxx, bzz;
                            mFI_Wpos2BlockNum(&bxx, &bzz, p);
                            mFI_Wpos2UtNum_inBlock(&ux, &uz, p);
                            snap[bzz - 1][bxx - 1][uz][ux] = TREE_APPLE_FRUIT;
                            nk_wii_log("LIFETEST: apple tree by villager %d's house\n", i);
                            break;
                        }
                    }
                }
            }
            nk_wii_log("LIFETEST: tending: %s\n", mTL_GetActivity(steps[step].act)->name);
        } else {
            /* the limits back on: those who planted may not plant again today */
            mTL_test_activity = -1;
            mTL_test_no_limits = FALSE;
            mTL_test_winter = FALSE;
            mTL_start_chance = 0;
            for (i = 0; i < ANIMAL_NUM_MAX; i++) {
                if (planted_by[i]) {
                    nk_wii_log("LIFETEST: villager %d planted %d; may plant again: %d; news known %d\n", i,
                               planted_by[i], mTL_ActivityAllowed(mTL_ACT_PLANT, i),
                               mNL_CountEvents(&Save_Get(animals[i])));
                }
            }
            nk_wii_log("LIFETEST: tending test done (planted %d, picked %d, snowmen %d, gifts %d)\n",
                       mTL_stats.planted, mTL_stats.picked, mTL_stats.snowmen, mTL_stats.gifts);
        }
    }

    if (mTL_stats.events && mTL_stats.last_event == 1 && player != NULL && step_frames % 300 == 0) {
        /* the player near whoever is busy, for the screenshots */
        for (i = 0; i < ANIMAL_NUM_MAX; i++) {
            mTL_villager_c* v = mTL_GetVillager(i);
            NPC_ACTOR* npc = town_test_npc(play, i);
            if (v != NULL && v->state != mTL_STATE_IDLE && npc != NULL) {
                player->actor_class.world.position.x = v->spot_x + 70.0f;
                player->actor_class.world.position.z = v->spot_z - 30.0f;
                break;
            }
        }
    }

    if (frames % 60 == 0) {
        /* the ground against the snapshot */
        for (bz = 0; bz < FG_BLOCK_Z_NUM; bz++) {
            for (bx = 0; bx < FG_BLOCK_X_NUM; bx++) {
                mActor_name_t* live = &Save_Get(fg[bz][bx]).items[0][0];
                mActor_name_t* old = &snap[bz][bx][0][0];
                for (i = 0; i < UT_Z_NUM * UT_X_NUM; i++) {
                    if (live[i] != old[i] && (live[i] >= DUMMY_START || old[i] >= DUMMY_START)) {
                        old[i] = live[i]; /* (buildings marking their tiles) */
                    } else if (live[i] != old[i]) {
                        nk_wii_log("LIFETEST: ground acre %d,%d tile %d,%d: %04x -> %04x (last villager %d)\n", bx,
                                   bz, i % UT_X_NUM, i / UT_X_NUM, old[i], live[i], mTL_stats.last_villager);
                        if (IS_ITEM_FLOWER(live[i]) && mTL_stats.last_villager >= 0 &&
                            mTL_stats.last_villager < ANIMAL_NUM_MAX) {
                            planted_by[mTL_stats.last_villager]++;
                        }
                        old[i] = live[i];
                        if (shot_frames < 0 && shots < 4) {
                            shot_frames = 0;
                        }
                    }
                }
            }
        }
    }
    if (shot_frames >= 0 && ++shot_frames == 60) {
        shot_frames = -1;
        nk_wii_log("LIFETEST: shot %d ground\n", ++shots);
    }
}

static void town_test_visitor(GAME_PLAY* play)
{
    static int frames, step, step_frames, shots, last_msg = -1, chatted = -1;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    mMsg_Window_c* window = mMsg_Get_base_window_p();
    ACTOR* visitor = NULL;
    NPC_ACTOR* target = NULL;
    Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
    ACTOR* a;
    int n, masks = 0, i;

    frames++;
    if (player == NULL) {
        return;
    }
    if (frames == 600) {
        /* (after the warp: the acre the player is really in) */
        int bx, bz;
        mFI_Wpos2BlockNum(&bx, &bz, player->actor_class.world.position);
        for (i = 0; i < mNpc_MASK_NPC_NUM; i++) {
            masks += Common_Get(mask_npc[i]).in_use;
            if (Common_Get(mask_npc[i]).in_use) {
                nk_wii_log("LIFETEST: mask record %d in use: %04x as %04x\n", i, Common_Get(mask_npc[i]).mask_id,
                           Common_Get(mask_npc[i]).npc_id);
            }
        }
        mTL_TestVisitorHere(bx, bz);
        if (Now_Private != NULL) {
            Now_Private->hint_count = 0x80;
        }
        nk_wii_log("LIFETEST: a visitor today in acre %d,%d (mask records in use before: %d)\n", bx, bz, masks);
        return;
    }
    if (frames < 600) {
        return;
    }
    for (a = list->actor, n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
        if (mTL_GetVisitor() != EMPTY_NO && a->npc_id == mTL_GetVisitor()) {
            visitor = a;
        }
    }

    if (window->msg_data != NULL && window->main_index != mMsg_INDEX_HIDE && window->msg_data->msg_no != last_msg) {
        last_msg = window->msg_data->msg_no;
        nk_wii_log("LIFETEST: msg 0x%04X topic %d (step %d)\n", last_msg, mNL_GetMsgTopic(last_msg), step);
    }
    {
        mChoice_c* choice = mChoice_Get_base_window_p();
        if (choice->main_index == mChoice_MAIN_NORMAL && choice->data.choice_num == 3 && last_msg >= 0x2A6 &&
            last_msg <= 0x2B1) {
            choice->selected_choice_idx = 1;
        }
    }

    step_frames++;
    switch (step) {
        case 0: /* the visitor appears and wanders */
            if (visitor == NULL && step_frames % 300 == 0) {
                int pbx, pbz;
                mFI_Wpos2BlockNum(&pbx, &pbz, player->actor_class.world.position);
                nk_wii_log("LIFETEST: no visitor yet: %04x, step %d, spawns %d, player acre %d,%d, npcs %d\n",
                           (int) mTL_GetVisitor(), mTL_stats.visitor_step, mTL_stats.visitor_spawns, pbx, pbz,
                           list->num_actors);
            }
            if (visitor != NULL && step_frames % 300 == 0) {
                NPC_ACTOR* v = (NPC_ACTOR*) visitor;
                for (i = 0; i < mNpc_MASK_NPC_NUM; i++) {
                    masks += Common_Get(mask_npc[i]).in_use;
                }
                nk_wii_log("LIFETEST: visitor %04x at %.0f,%.0f act %d think %d sched %d hide %d, %.0f from the player, "
                           "masks %d, spawns %d\n",
                           (int) visitor->npc_id, (double) visitor->world.position.x, (double) visitor->world.position.z,
                           (int) v->action.idx, (int) v->think.idx, (int) v->schedule.type,
                           (int) v->condition_info.hide_flg, (double) visitor->player_distance_xz, masks,
                           mTL_stats.visitor_spawns);
                if (shots < 2) {
                    player->actor_class.world.position.x = visitor->world.position.x - 70.0f;
                    player->actor_class.world.position.z = visitor->world.position.z - 30.0f;
                    nk_wii_log("LIFETEST: shot %d visitor\n", ++shots);
                }
            }
            if (step_frames == 60 * 60) {
                nk_wii_log("LIFETEST: visitor %s after a minute\n", visitor != NULL ? "here" : "NOT HERE");
                step = 1;
                step_frames = 0;
            }
            break;
        case 1: /* a chat with the visitor: they introduce themselves */
        case 2: /* another: a hello */
        case 3: /* a chat with a resident, about the visitor */
            if (step <= 2) {
                target = (NPC_ACTOR*) visitor;
            } else {
                for (a = list->actor, n = list->num_actors; n != 0 && a != NULL && target == NULL; n--,
                    a = a->next_actor) {
                    if (ITEM_NAME_GET_TYPE(a->npc_id) == NAME_TYPE_NPC &&
                        mNL_GetAnimalIdx(((NPC_ACTOR*) a)->npc_info.animal) >= 0 &&
                        ((NPC_ACTOR*) a)->npc_info.animal->mood == mNpc_FEEL_NORMAL) {
                        target = (NPC_ACTOR*) a; /* (an angry villager only tells the player to go away) */
                    }
                }
                mNL_gossip_chance = 100;
                mNL_test_topic = mNL_TOPIC_VISITOR;
                mTL_talk_chance = 0;
            }
            if (target == NULL) {
                break;
            }
            target->think.interrupt_flags &= ~aNPC_THINK_INTERRUPT_FRIENDSHIP;
            if (target->condition_info.talk_condition != aNPC_TALK_TYPE_NONE) {
                chatted = step;
            } else if (chatted == step && window->main_index == mMsg_INDEX_HIDE) {
                nk_wii_log("LIFETEST: chat %d over\n", step);
                chatted = -1;
                step++;
                step_frames = 0;
                if (step == 4) {
                    nk_wii_log("LIFETEST: visitor test done (visitor days %d, spawns %d)\n", mTL_stats.visitor_days,
                               mTL_stats.visitor_spawns);
                }
            } else if (window->main_index == mMsg_INDEX_HIDE) {
                xyz_t* ppos = &player->actor_class.world.position;
                s16 angle = search_position_angleY(ppos, &target->actor_class.world.position);
                target->actor_class.world.position.x = ppos->x + sin_s(angle) * 36.0f;
                target->actor_class.world.position.z = ppos->z + cos_s(angle) * 36.0f;
                target->actor_class.world.position.y = ppos->y;
                player->actor_class.shape_info.rotation.y = angle;
                player->actor_class.world.angle.y = angle;
            }
            if (window->main_index != mMsg_INDEX_HIDE && step_frames % 120 == 60 && shots < 9) {
                nk_wii_log("LIFETEST: shot %d chat%d\n", ++shots, step);
            }
            break;
        default:
            break;
    }
}

static void town_test_weather(GAME_PLAY* play)
{
    static int frames, step = -1, step_frames, shots, snowballs_seen, started_seen;
    static int angle_log[ANIMAL_NUM_MAX];
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
    Animal_c* animals = Save_Get(animals);
    ACTOR* a;
    int n, i, g;

    frames++;
    if (player == NULL) {
        return;
    }
    if (step < 0) {
        int ids[ANIMAL_NUM_MAX], count = 0;
        if (frames < 600) {
            return;
        }
        for (a = list->actor, n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
            int idx = ITEM_NAME_GET_TYPE(a->npc_id) == NAME_TYPE_NPC ? mNL_GetAnimalIdx(((NPC_ACTOR*) a)->npc_info.animal) : -1;
            if (idx >= 0) {
                ids[count++] = idx;
            }
        }
        for (i = 0; i < count; i++) {
            for (n = 0; n < count; n++) {
                if (i != n) {
                    animals[ids[i]].animal_relations[ids[n]] = 200;
                }
            }
        }
        mTL_test_snow = TRUE;
        mTL_test_group_any_hour = TRUE;
        mTL_group_chance = 80;
        mTL_start_chance = 0;
        Common_Set(weather, mEnv_WEATHER_SNOW);
        Common_Set(weather_intensity, mEnv_WEATHER_INTENSITY_LIGHT);
        step = 0;
        step_frames = 0;
        nk_wii_log("LIFETEST: weather: snow (%d villagers here made friends)\n", count);
    }

    step_frames++;
    if (frames % 60 == 0) {
        /* (everyone stays out, whatever the weather: the test is about what they do outside) */
        for (a = list->actor, n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
            NPC_ACTOR* npc = (NPC_ACTOR*) a;
            if (ITEM_NAME_GET_TYPE(a->npc_id) == NAME_TYPE_NPC && mNL_GetAnimalIdx(npc->npc_info.animal) >= 0 &&
                npc->npc_info.schedule != NULL) {
                npc->npc_info.schedule->forced_type = mNPS_SCHED_FIELD;
                npc->npc_info.schedule->forced_timer = 100000;
            }
        }
    }
    if (step == 0 && frames % 300 == 0) {
        for (g = 0; g < mTL_GROUP_NUM; g++) {
            if (mTL_groups[g].active) {
                nk_wii_log("LIFETEST: group %d members %d %d %d arrived %x turns %d\n", g, mTL_groups[g].members[0],
                           mTL_groups[g].members[1], mTL_groups[g].count > 2 ? mTL_groups[g].members[2] : -1,
                           mTL_groups[g].arrived, mTL_groups[g].turn_seq);
            }
        }
    }
    if (mTL_stats.snowballs != snowballs_seen) {
        snowballs_seen = mTL_stats.snowballs;
        nk_wii_log("LIFETEST: snowball %d\n", snowballs_seen);
        if (shots < 3) {
            nk_wii_log("LIFETEST: shot %d snowball\n", ++shots);
        }
    }
    if (mTL_stats.started != started_seen) {
        started_seen = mTL_stats.started;
        if (step >= 1 && mTL_stats.last_event == 1 && shots < 8) {
            /* the player beside the busy villager, a screenshot a few seconds in */
            NPC_ACTOR* npc = town_test_npc(play, mTL_stats.last_villager);
            if (npc != NULL) {
                player->actor_class.world.position.x = npc->actor_class.world.position.x + 70.0f;
                player->actor_class.world.position.z = npc->actor_class.world.position.z - 30.0f;
            }
        }
    }
    if (step >= 1 && step_frames % 400 == 200 && shots < 8) {
        for (i = 0; i < ANIMAL_NUM_MAX; i++) {
            mTL_villager_c* v = mTL_GetVillager(i);
            if (v != NULL && v->state == mTL_STATE_DOING) {
                nk_wii_log("LIFETEST: shot %d %s\n", ++shots, mTL_GetActivity(v->activity)->name);
                break;
            }
        }
    }
    if (step == 0 && step_frames % 300 == 0) {
        /* the player next to a group */
        for (g = 0; g < mTL_GROUP_NUM; g++) {
            if (mTL_groups[g].active && mTL_groups[g].arrived) {
                player->actor_class.world.position.x = mTL_groups[g].center_x + 80.0f;
                player->actor_class.world.position.z = mTL_groups[g].center_z - 40.0f;
                break;
            }
        }
    }
    if (step == 3 && step_frames % 120 == 0) {
        /* stargazers turn slowly */
        for (i = 0; i < ANIMAL_NUM_MAX; i++) {
            mTL_villager_c* v = mTL_GetVillager(i);
            NPC_ACTOR* npc = town_test_npc(play, i);
            if (v != NULL && npc != NULL && v->state == mTL_STATE_DOING &&
                mTL_GetActivity(v->activity)->effect == mTL_EFFECT_STARS &&
                npc->actor_class.shape_info.rotation.y != angle_log[i]) {
                angle_log[i] = npc->actor_class.shape_info.rotation.y;
                nk_wii_log("LIFETEST: stargazer %d facing %d\n", i, angle_log[i] & 0xFFFF);
            }
        }
    }

    if (step == 0 && step_frames == 60 * 240) {
        mTL_test_snow = FALSE;
        mTL_group_chance = 0;
        Common_Set(weather, mEnv_WEATHER_RAIN);
        Common_Set(weather_intensity, mEnv_WEATHER_INTENSITY_NORMAL);
        mTL_start_chance = 100;
        mTL_test_keep_place = TRUE;
        mTL_test_activity = mTL_ACT_SPLASH;
        step = 1;
        step_frames = 0;
        nk_wii_log("LIFETEST: weather: rain, splashing (snowballs %d)\n", mTL_stats.snowballs);
    } else if (step == 1 && step_frames == 60 * 60) {
        mTL_test_activity = mTL_ACT_SHELTER;
        step = 2;
        step_frames = 0;
        nk_wii_log("LIFETEST: rain, sheltering\n");
    } else if (step == 2 && step_frames == 60 * 60) {
        Common_Set(weather, mEnv_WEATHER_CLEAR);
        Common_Set(weather_intensity, mEnv_WEATHER_INTENSITY_NONE);
        mTL_test_activity = mTL_ACT_STARGAZE2;
        step = 3;
        step_frames = 0;
        nk_wii_log("LIFETEST: night (pretend): stargazing\n");
    } else if (step == 3 && step_frames == 60 * 60) {
        mTL_test_activity = -1;
        mTL_start_chance = 0;
        step = 4;
        nk_wii_log("LIFETEST: weather test done (snowballs %d, started %d)\n", mTL_stats.snowballs,
                   mTL_stats.started);
    }
}

/* Frame time: the time between play frames, in the emulated (or real) clock, so a frame whose work runs over its
 * budget shows as a long one; logged every `frames` frames on the streets ("LIFETEST: frame time" in the town
 * tests, every ten seconds; "frame time" in play, every minute) */
u32 nk_wii_frame_work_us; /* src/static/jsyswrap.cpp: the last frame's work, without the wait for the picture */

static void town_test_frame_time(u32 period)
{
    static OSTime last;
    static u32 sum_us, max_us, frames, slow, work_sum, work_max;
    OSTime now = OSGetTime();

    if (last != 0) {
        u32 us = (u32) OSTicksToMicroseconds(now - last);
        if (us > 1000000) {
            /* (a gap: indoors, a menu, a scene change) */
            last = now;
            return;
        }
        sum_us += us;
        max_us = us > max_us ? us : max_us;
        slow += us > 40000; /* (longer than a 30 fps frame and then some) */
        work_sum += nk_wii_frame_work_us;
        work_max = nk_wii_frame_work_us > work_max ? nk_wii_frame_work_us : work_max;
        if (++frames == period) {
            nk_wii_log("%sframe time avg %u.%02u ms, max %u.%02u ms, %u over 40 ms, work avg %u.%02u ms, max %u.%02u "
                       "ms, %d villagers busy, %d bubbles\n",
                       period == 600 ? "LIFETEST: " : "", sum_us / frames / 1000, sum_us / frames / 10 % 100,
                       max_us / 1000, max_us / 10 % 100, slow, work_sum / frames / 1000, work_sum / frames / 10 % 100,
                       work_max / 1000, work_max / 10 % 100, mTL_CountBusy(), mTL_BubblesShowing());
            sum_us = max_us = frames = slow = work_sum = work_max = 0;
        }
    }
    last = now;
}

/* settings walk_test = 1: the player walks a round of the town's acres by itself (the stick held toward each acre's
 * middle in turn, around what stands in the way), with the acre, the actors alive, the free memory and the disc reads
 * logged every five seconds, and a screenshot asked for near each acre border: how acre changes look and what they
 * cost, with borderless acres on or off */
extern int g_nk_walk_test;
extern unsigned nk_wii_dvd_reads, nk_wii_dvd_kb;
extern int nk_wii_gfx_free_min[4]; /* src/graph.c */
/* src/bg_item/bg_item_common.c_inc: this frame's items on the screen from acres the original four wouldn't have had */
int nk_wii_extra_items_seen;
void nk_wii_pad_remote(u16 buttons, int stick_x, int stick_y, int sub_x, int sub_y, u32 reads);

static void walk_test(GAME_PLAY* play)
{
    /* (block x, block z) to walk to, in turn */
    static const s8 route[][2] = { { 3, 2 }, { 4, 2 }, { 4, 3 }, { 3, 3 }, { 2, 3 }, { 2, 2 }, { 3, 2 }, { 3, 1 } };
    static int frames, at, at_frames, stuck, dodge, dodge_dir = 1, shots, border_frames, hold;
    static f32 best = 1.0e9f;
    static u32 fsum, fmax, fn, wsum, wmax;
    static int extra_frames, extra_max;
    static OSTime last;
    const int stops = (int) (sizeof(route) / sizeof(route[0]));
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    OSTime now = OSGetTime();
    f32 x, z, dx, dz, dist, edge_x, edge_z;
    int sx, sy;

    if (Save_Get(scene_no) != SCENE_FG || player == NULL) {
        last = 0;
        return;
    }
    if (last != 0) {
        u32 us = (u32) OSTicksToMicroseconds(now - last);
        fsum += us;
        fmax = us > fmax ? us : fmax;
        wsum += nk_wii_frame_work_us;
        wmax = nk_wii_frame_work_us > wmax ? nk_wii_frame_work_us : wmax;
        extra_frames += nk_wii_extra_items_seen > 0;
        extra_max = nk_wii_extra_items_seen > extra_max ? nk_wii_extra_items_seen : extra_max;
        fn++;
    }
    last = now;
    x = player->actor_class.world.position.x;
    z = player->actor_class.world.position.z;
    if (++frames % 300 == 0 && fn != 0) {
        nk_wii_log("WALKTEST: acre (%d,%d) at %d,%d, stop %d, borderless %d, actors %d (npc %d), free %u KB, "
                   "disc reads %u (%u KB), frame avg %u.%02u ms max %u.%02u ms, work avg %u.%02u ms max %u.%02u ms\n",
                   play->block_table.block_x, play->block_table.block_z, (int) x, (int) z, at,
                   g_mPlib_wade_disabled, play->actor_info.total_num, play->actor_info.list[ACTOR_PART_NPC].num_actors,
                   (unsigned) (zelda_GetTotalFreeSize() / 1024), nk_wii_dvd_reads, nk_wii_dvd_kb, fsum / fn / 1000,
                   fsum / fn / 10 % 100, fmax / 1000, fmax / 10 % 100, wsum / fn / 1000, wsum / fn / 10 % 100,
                   wmax / 1000, wmax / 10 % 100);
        fsum = fmax = wsum = wmax = 0;
        nk_wii_log("WALKTEST: display list room left (least): opa %d KB, xlu %d KB, shadow %d KB, bg %d KB; items "
                   "seen only thanks to the 3x3 acres: in %d of %u frames, at most %d\n",
                   nk_wii_gfx_free_min[0] / 1024, nk_wii_gfx_free_min[1] / 1024, nk_wii_gfx_free_min[2] / 1024,
                   nk_wii_gfx_free_min[3] / 1024, extra_frames, fn, extra_max);
        extra_frames = extra_max = 0;
        fn = 0;
        nk_wii_gfx_free_min[0] = nk_wii_gfx_free_min[1] = nk_wii_gfx_free_min[2] = nk_wii_gfx_free_min[3] =
            0x7FFFFFFF;
    }
    if (frames < 120) {
        return;
    }
    if (at >= stops) {
        if (at++ == stops) {
            nk_wii_log("WALKTEST: done\n");
        }
        return;
    }
    if (hold > 0) {
        /* walk_test = 2: standing still at the acre's middle; halfway through, which acres have their trees and
         * flowers made (the item table's), and a screenshot */
        if (--hold == 30) {
            int i;
            char made[96];
            int n = 0;
            for (i = 0; i < mFM_VISIBLE_BLOCK_NUM && n < (int) sizeof(made) - 8; i++) {
                if (g_fdinfo->bg_draw_info[i].block_x != 0xFF) {
                    n += sprintf(made + n, " (%d,%d)", g_fdinfo->bg_draw_info[i].block_x,
                                 g_fdinfo->bg_draw_info[i].block_z);
                }
            }
            made[n] = '\0';
            nk_wii_log("WALKTEST: standing in (%d,%d), items made for%s\n", play->block_table.block_x,
                       play->block_table.block_z, made);
            nk_wii_log("LIFETEST: shot %d\n", ++shots);
        }
        return;
    }
    /* a screenshot while a border is on the screen (a few, spaced out) */
    edge_x = fmodf(x, mFI_BK_WORLDSIZE_X_F);
    edge_z = fmodf(z, mFI_BK_WORLDSIZE_Z_F);
    border_frames++;
    if (g_nk_walk_test == 1 && shots < 8 && border_frames > 240 &&
        (edge_x < 40.0f || edge_x > mFI_BK_WORLDSIZE_X_F - 40.0f || edge_z < 40.0f ||
         edge_z > mFI_BK_WORLDSIZE_Z_F - 40.0f))
    {
        nk_wii_log("LIFETEST: shot %d\n", ++shots);
        border_frames = 0;
    }
    dx = route[at][0] * mFI_BK_WORLDSIZE_X_F + mFI_BK_WORLDSIZE_HALF_X_F - x;
    dz = route[at][1] * mFI_BK_WORLDSIZE_Z_F + mFI_BK_WORLDSIZE_HALF_Z_F - z;
    dist = sqrtf(dx * dx + dz * dz);
    if (dist < (g_nk_walk_test == 2 ? 30.0f : 120.0f) || ++at_frames > 2700) {
        nk_wii_log("WALKTEST: %s (%d,%d) after %d frames\n", at_frames <= 2700 ? "reached" : "gave up on",
                   route[at][0], route[at][1], at_frames);
        if (g_nk_walk_test == 2 && at_frames <= 2700) {
            hold = 60;
        }
        at++;
        at_frames = stuck = dodge = 0;
        best = 1.0e9f;
        return;
    }
    /* no nearer for a second and a half: around the obstacle, to one side then the other */
    if (dist < best - 4.0f) {
        best = dist;
        stuck = 0;
    } else if (++stuck > 90 && dodge == 0) {
        dodge = 75;
        dodge_dir = -dodge_dir;
        stuck = 0;
        best = 1.0e9f;
    }
    if (dodge > 0) {
        f32 t = dx;
        dodge--;
        dx = -dz * dodge_dir + dx * 0.3f;
        dz = t * dodge_dir + dz * 0.3f;
        dist = sqrtf(dx * dx + dz * dz);
    }
    sx = (int) (dx / dist * 100.0f);
    sy = (int) (-dz / dist * 100.0f);
    nk_wii_pad_remote(0, sx, sy, 0, 0, 4);
}

static void town_test(GAME_PLAY* play)
{
    static int field_frames;
    static int warped;
    static int subject = -1;
    static int k = -1;
    static int phase;
    static int phase_frames;
    static int events_seen;
    static int last_min = -1;
    static int shots;
    static int shot_frames = -1;
    static int shot_act = -1;
    static int shot_looks;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    mNpc_NpcList_c* npclist = Common_Get(npclist);
    int i;

    if (Save_Get(scene_no) != SCENE_FG || player == NULL) {
        return;
    }
    field_frames++;
    town_test_frame_time(600);

    if (!warped) {
        if (g_nk_life_test >= 4 && field_frames == 2) {
            /* fine weather, so no umbrellas in the way */
            Common_Set(weather, mEnv_WEATHER_CLEAR);
            Common_Set(weather_intensity, mEnv_WEATHER_INTENSITY_NONE);
        }
        if (field_frames > 60) {
            int w = busiest_acre_npc();
            if (w >= 0) {
                Door_data_c door;
                bzero(&door, sizeof(door));
                door.next_scene_id = SCENE_FG;
                door.exit_orientation = mSc_DIRECT_SOUTH;
                door.exit_position.x = (s16) npclist[w].position.x;
                door.exit_position.y = (s16) npclist[w].position.y;
                door.exit_position.z = (s16) npclist[w].position.z;
                door.door_actor_name = EMPTY_NO;
                door.wipe_type = WIPE_TYPE_FADE_BLACK;
                if (goto_other_scene(play, &door, TRUE) == TRUE) {
                    warped = 1;
                    nk_wii_log("LIFETEST: to villager %d's acre (%d,%d), town life %d\n", w,
                               (int) door.exit_position.x, (int) door.exit_position.z, mTL_IsEnabled());
                }
            } else {
                warped = 1;
            }
        }
        return;
    }

    if (g_nk_life_test == 3 && shot_frames >= 0 && ++shot_frames == 150 && shots < 12) {
        /* a few seconds into an activity the player can see */
        nk_wii_log("LIFETEST: shot %d %s-looks%d\n", ++shots, mTL_GetActivity(shot_act)->name, shot_looks);
        shot_frames = -1;
    }

    /* every activity started, ended or interrupted */
    if (mTL_stats.events != events_seen) {
        static const char* const what[] = {"?", "started", "ended", "interrupted"};
        mTL_villager_c* v = mTL_GetVillager(mTL_stats.last_villager);
        const mTL_activity_c* a = mTL_GetActivity(mTL_stats.last_activity);
        events_seen = mTL_stats.events;
        if (mTL_stats.last_event == 1 && shot_frames < 0 && mTL_stats.last_activity != shot_act) {
            NPC_ACTOR* npc = town_test_npc(play, mTL_stats.last_villager);
            if (npc != NULL && search_position_distanceXZ(&npc->actor_class.world.position,
                                                         &player->actor_class.world.position) < 160.0f) {
                shot_frames = 0;
                shot_act = mTL_stats.last_activity;
                shot_looks = Save_Get(animals[mTL_stats.last_villager]).id.looks;
            }
        }
        nk_wii_log("LIFETEST: act %s villager %d %s (place %d, %d s, repeats %d%s, prop %d) busy %d\n",
                   what[mTL_stats.last_event & 3], mTL_stats.last_villager, a->name, a->place,
                   v != NULL ? v->seconds : 0, v != NULL ? v->repeats : 0, v != NULL && v->oneshot ? " one-shot" : "",
                   v != NULL && v->prop != NULL, mTL_CountBusy());
    }

    if (g_nk_life_test == 3) {
        const lbRTC_time_c* t = Common_GetPointer(time.rtc_time);
        if (last_min < 0) {
            /* the town's villagers outside each hour of a simulated day (at half past), on the original
             * schedules and on town life's (m_npc_schedule.c), in today's weather */
            char line[2][80];
            int h, mode, total[2] = {0, 0};
            for (mode = 0; mode < 2; mode++) {
                int len = 0;
                for (h = 0; h < 24; h++) {
                    int n = 0;
                    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
                        Animal_c* animal = &Save_Get(animals[i]);
                        if (!mNpc_CheckFreeAnimalPersonalID(&animal->id)) {
                            n += mNPS_test_outside(&animal->id, h * 3600 + 1800, mode);
                        }
                    }
                    total[mode] += n;
                    line[mode][len++] = n < 10 ? '0' + n : 'A' + n - 10;
                }
                line[mode][len] = '\0';
            }
            nk_wii_log("LIFETEST: outside by hour 00-23, original  %s (%d villager-hours)\n", line[0], total[0]);
            nk_wii_log("LIFETEST: outside by hour 00-23, town life %s (%d villager-hours)\n", line[1], total[1]);
        }
        if (field_frames < 200) {
            mTL_start_chance = g_nk_life_test_topic > 0 ? g_nk_life_test_topic : mTL_START_CHANCE;
        }
        if (t->min != last_min) {
            last_min = t->min;
            nk_wii_log("LIFETEST: busy %02d:%02d %d of %d out, started %d ended %d interrupted %d no spot %d\n",
                       (int) t->hour, (int) t->min, mTL_CountBusy(), play->actor_info.list[ACTOR_PART_NPC].num_actors,
                       mTL_stats.started, mTL_stats.ended, mTL_stats.interrupted, mTL_stats.no_spot);
        }
        return;
    }

    if (g_nk_life_test == 5) {
        town_test_interrupt(play);
        return;
    }
    if (g_nk_life_test == 6 || g_nk_life_test == 7) {
        town_test_groups(play);
        return;
    }
    if (g_nk_life_test == 8) {
        town_test_tend(play);
        return;
    }
    if (g_nk_life_test == 9) {
        town_test_visitor(play);
        return;
    }
    if (g_nk_life_test == 10) {
        town_test_weather(play);
        return;
    }

    /* life_test = 4: the animation and prop check */
    if (subject < 0) {
        f32 best = 1e9f;
        Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
        ACTOR* a = list->actor;
        int n;
        if (field_frames < 120) {
            return;
        }
        for (n = list->num_actors; n != 0 && a != NULL; n--, a = a->next_actor) {
            int idx;
            f32 d;
            if (ITEM_NAME_GET_TYPE(a->npc_id) != NAME_TYPE_NPC) {
                continue;
            }
            idx = mNL_GetAnimalIdx(((NPC_ACTOR*) a)->npc_info.animal);
            d = search_position_distanceXZ(&a->world.position, &player->actor_class.world.position);
            if (idx >= 0 && d < best) {
                best = d;
                subject = idx;
            }
        }
        if (subject < 0) {
            return;
        }
        mTL_test_villager = subject;
        mTL_test_seconds = 7;
        mTL_start_chance = 100;
        nk_wii_log("LIFETEST: animation check on villager %d (looks %d), %d animations\n", subject,
                   (int) Save_Get(animals[subject]).id.looks, (int) (sizeof(s_town_anims) / sizeof(s_town_anims[0])));
        k = g_nk_life_test_topic > 0 ? g_nk_life_test_topic - 1 : -1;
        phase = 2;
    }

    {
        NPC_ACTOR* npc = town_test_npc(play, subject);
        mTL_villager_c* v = mTL_GetVillager(subject);
        xyz_t* ppos = &player->actor_class.world.position;

        if (npc == NULL || v == NULL) {
            return;
        }
        npc->think.interrupt_flags &= ~aNPC_THINK_INTERRUPT_FRIENDSHIP;
        phase_frames++;
        switch (phase) {
            case 0: /* waiting for the villager to start it, in front of the player */
                if (v->state == mTL_STATE_DOING && phase_frames > 2) {
                    phase = 1;
                    phase_frames = 0;
                    v->angle = 0;
                } else if (phase_frames % 60 == 1 && v->state == mTL_STATE_IDLE) {
                    npc->actor_class.world.position.x = ppos->x + 90.0f;
                    npc->actor_class.world.position.z = ppos->z + 10.0f;
                    if (npc->action.idx != aNPC_ACT_TALK && npc->action.idx != aNPC_ACT_GREETING) {
                        /* (whatever it was doing ends, so it decides again now) */
                        npc->action.step = aNPC_ACTION_END_STEP;
                    }
                    mTL_start_chance = 100;
                } else if (phase_frames > 1200) {
                    nk_wii_log("LIFETEST: anim %d %s never started (act %d step %d think %d state %d talk %d call %d "
                               "sched %d)\n",
                               k, s_town_anims[k].name, (int) npc->action.idx, (int) npc->action.step,
                               (int) npc->think.idx, (int) v->state, (int) npc->condition_info.talk_condition,
                               (int) npc->think.force_call_flag, (int) npc->schedule.type);
                    phase = 2;
                }
                break;
            case 1: /* doing it */
                v->angle = 0;
                if (phase_frames == 120) {
                    nk_wii_log("LIFETEST: anim %d %s rot %d prop %d sub %d repeats %d%s\n", k, s_town_anims[k].name,
                               (int) npc->actor_class.shape_info.rotation.y, v->prop != NULL,
                               (int) npc->draw.sub_anim_type, v->repeats, v->oneshot ? " one-shot" : "");
                }
                if (phase_frames == 50 || phase_frames == 120 || phase_frames == 190) {
                    nk_wii_log("LIFETEST: shot %d %s@%d\n", ++shots, s_town_anims[k].name, phase_frames);
                }
                if (v->state != mTL_STATE_DOING) {
                    nk_wii_log("LIFETEST: anim %d %s done: repeats %d%s\n", k, s_town_anims[k].name, v->repeats,
                               v->oneshot ? " one-shot" : "");
                    phase = 2;
                }
                break;
            case 3:
                break;
        }
        if (phase == 2) {
            /* the next one, before the villager decides again */
            if (++k >= (int) (sizeof(s_town_anims) / sizeof(s_town_anims[0]))) {
                mTL_test_activity = -1;
                mTL_test_villager = 99;
                nk_wii_log("LIFETEST: animation check done\n");
                phase = 3;
                return;
            }
            mTL_test_entry.name = s_town_anims[k].name;
            mTL_test_entry.anim = s_town_anims[k].anim;
            mTL_test_entry.sub_anim = s_town_anims[k].sub_anim;
            mTL_test_entry.prop = s_town_anims[k].prop;
            mTL_test_entry.place = mTL_PLACE_ANYWHERE;
            mTL_test_activity = mTL_ACT_TEST;
            mTL_start_chance = 0;
            phase = 0;
            phase_frames = 0;
        }
    }
}

void nk_netgame_play_frame(GAME_PLAY* play)
{
    /* town life: only the host (or a console on its own) changes the ground; guests get it by the field sync */
    mTL_ground_changes_allowed = nk_net_mode() != NK_NET_GUEST;
    if (g_nk_trace_scenes) {
        static int last_scene = -1;
        static int frames;
        int scene = Save_Get(scene_no);
        frames++;
        if (scene != last_scene) {
            PLAYER_ACTOR* pl = GET_PLAYER_ACTOR(play);
            const lbRTC_time_c* t = Common_GetPointer(time.rtc_time);
            nk_wii_log("SCENE %d (after %d frames)%s, game time %d/%d %02d:%02d\n", scene, frames,
                       pl != NULL ? " player" : "", (int) t->month, (int) t->day, (int) t->hour, (int) t->min);
            last_scene = scene;
        }
    }
    if (g_nk_travel_test) {
        travel_test(play);
    }
    if (g_nk_crash_test != 0) {
        /* settings crash_test: ten seconds on the streets, then a crash (1: a read from address 0x1d, as a NULL
         * pointer's field), a freeze (2: the game waits forever) or a panic (3), for the reports of stall.c and
         * os.c */
        static int crash_frames;
        if (Save_Get(scene_no) == SCENE_FG && GET_PLAYER_ACTOR(play) != NULL && ++crash_frames == 600) {
            nk_wii_log("CRASHTEST: %s now\n",
                       g_nk_crash_test == 1 ? "a crash" : g_nk_crash_test == 2 ? "a freeze" : "a panic");
            if (g_nk_crash_test == 3) {
                /* 3: the game's own panic, a free of what its heap never gave (os.c's OSPanic) */
                static u8 not_heap[64];
                zelda_free(not_heap + 32);
            }
            if (g_nk_crash_test == 1) {
                volatile uintptr_t bad = 0x1d; /* (volatile: the compiler doesn't see the bad address coming) */
                g_nk_crash_test = *(volatile u8*) bad;
            } else {
                /* stuck waiting for a message that never comes, as a deadlock is (a busy loop starves the
                 * watching thread and leaves no report) */
                static OSMessageQueue queue;
                static OSMessage slot;
                OSMessage msg;
                OSInitMessageQueue(&queue, &slot, 1);
                OSReceiveMessage(&queue, &msg, OS_MESSAGE_BLOCK);
            }
        }
    }
    if (g_nk_life_test == 0 && Save_Get(scene_no) == SCENE_FG && GET_PLAYER_ACTOR(play) != NULL) {
        /* in play, once a minute on the streets: how long frames take (the same measure as the town tests') */
        town_test_frame_time(3600);
    }
    if (g_nk_walk_test) {
        walk_test(play);
    }
    if (g_nk_life_test >= 3) {
        town_test(play);
    } else if (g_nk_life_test) {
        life_test(play);
    }
    if (g_nk_nes_test > 0) {
        /* two seconds on the town's streets, then the game, as the NES
         * furniture starts it */
        static int field_frames;
        static int started;
        if (!started && Save_Get(scene_no) == SCENE_FG && GET_PLAYER_ACTOR(play) != NULL &&
            ++field_frames > 60 && goto_emu_game(&play->game, (s16) g_nk_nes_test))
        {
            started = 1;
            nk_wii_log("NESTEST: starting NES game %d\n", g_nk_nes_test);
        }
    }
    clock_sync();
    if (nk_net_mode() == NK_NET_OFF && !g_nk_net_echo) {
        return;
    }
    /* a new play scene: the old one's actors are gone */
    if (play != s_play || play->game.frame_counter < s_play_frame) {
        forget_puppets();
        s_play = play;
    }
    s_play_frame = play->game.frame_counter;
    {
        static int logged;
        if (!logged) {
            logged = 1;
            nk_wii_log("NETGAME: first play frame: scene %d, player %p, private %p\n", (int) Save_Get(scene_no),
                       (void*) GET_PLAYER_ACTOR(play), (void*) Now_Private);
        }
    }
    publish_local(play);
    update_puppets(play);
    if (nk_net_mode() != NK_NET_OFF) {
        field_sync();
    }
}
