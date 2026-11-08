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
#include "m_choice.h"

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

/* settings life_test = 1, villager friendships (m_npc_life.h): on the
 * town's streets, two villagers become close friends and two others fall out,
 * every chat is gossip, the player is taken (as through a door) to the acre
 * with the most villagers, and the nearest villager not yet talked to is
 * brought in front of the player until the input script's A starts a chat.
 * Each message the window loads is logged ("LIFETEST: msg"); "LIFETEST:
 * gossip on screen N" comes 10 frames into the Nth gossip line.
 * life_test = 2 only watches: the same acre, no chats, and every two seconds
 * the distance between each two villagers there, with their opinions of each
 * other ("LIFETEST: pair"). */
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
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    mMsg_Window_c* window = mMsg_Get_base_window_p();
    mNpc_NpcList_c* npclist = Common_Get(npclist);
    int i;

    if (Save_Get(scene_no) != SCENE_FG || player == NULL) {
        return;
    }
    field_frames++;

    if (!seeded && field_frames > 30) {
        Animal_c* animals = Save_Get(animals);
        int town[4];
        int n = 0;

        for (i = 0; i < ANIMAL_NUM_MAX && n < 4; i++) {
            if (!mNpc_CheckFreeAnimalPersonalID(&animals[i].id)) {
                town[n++] = i;
            }
        }
        if (n == 4) {
            animals[town[0]].animal_relations[town[1]] = 220;
            animals[town[1]].animal_relations[town[0]] = 210;
            animals[town[2]].animal_relations[town[3]] = 60;
            animals[town[3]].animal_relations[town[2]] = 90;
        }
        mNL_gossip_chance = 100;
        /* past the first-job hints, which come before any chat (not saved: the test never saves) */
        if (Now_Private != NULL) {
            Now_Private->hint_count = 0x80;
        }
        seeded = 1;
        nk_wii_log("LIFETEST: seeded %d villagers (%d+%d close, %d+%d feud)\n", n, town[0], town[1], town[2],
                   town[3]);
    }

    if (seeded && !warped && field_frames > 60) {
        /* the acre with the most villagers */
        int count[FG_BLOCK_Z_NUM + 2][FG_BLOCK_X_NUM + 2];
        int best = -1, best_i = -1;

        bzero(count, sizeof(count));
        for (i = 0; i < ANIMAL_NUM_MAX; i++) {
            int bx, bz;
            if (npclist[i].name == EMPTY_NO) {
                continue;
            }
            mFI_Wpos2BlockNum(&bx, &bz, npclist[i].position);
            if (bz >= 0 && bz < FG_BLOCK_Z_NUM + 2 && bx >= 0 && bx < FG_BLOCK_X_NUM + 2 &&
                ++count[bz][bx] > best)
            {
                best = count[bz][bx];
                best_i = i;
            }
        }
        if (best_i >= 0) {
            Door_data_c door;
            bzero(&door, sizeof(door));
            door.next_scene_id = SCENE_FG;
            door.exit_orientation = mSc_DIRECT_SOUTH;
            door.exit_position.x = (s16) npclist[best_i].position.x;
            door.exit_position.y = (s16) npclist[best_i].position.y;
            door.exit_position.z = (s16) npclist[best_i].position.z;
            door.door_actor_name = EMPTY_NO;
            door.wipe_type = WIPE_TYPE_FADE_BLACK;
            if (goto_other_scene(play, &door, TRUE) == TRUE) {
                warped = 1;
                nk_wii_log("LIFETEST: to %d villagers around villager %d (%d,%d)\n", best, best_i,
                           (int) door.exit_position.x, (int) door.exit_position.z);
            }
        } else {
            warped = 1;
        }
        return;
    }

    if (warped && g_nk_life_test == 2) {
        if (field_frames % 120 == 0) {
            Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
            ACTOR* a = list->actor;
            int n;
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
                    nk_wii_log("LIFETEST: pair %d-%d dist %.0f rel %d/%d\n", ia, ib,
                               (double) search_position_distanceXZ(&a->world.position, &b->world.position),
                               na->npc_info.animal->animal_relations[ib], nb->npc_info.animal->animal_relations[ia]);
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

    if (window->msg_data != NULL && window->main_index != mMsg_INDEX_HIDE && window->msg_data->msg_no != last_msg) {
        last_msg = window->msg_data->msg_no;
        nk_wii_log("LIFETEST: msg 0x%04X%s\n", last_msg, mNL_IsExtMsg(last_msg) ? " (gossip)" : "");
        if (mNL_IsExtMsg(last_msg)) {
            gossip_frames = 0;
        }
    }
    if (gossip_frames >= 0 && ++gossip_frames == 10) {
        gossip_frames = -1;
        nk_wii_log("LIFETEST: gossip on screen %d\n", ++gossip_count);
    }
}

void nk_netgame_play_frame(GAME_PLAY* play)
{
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
    if (g_nk_life_test) {
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
