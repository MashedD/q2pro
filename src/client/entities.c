/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
// cl_ents.c -- entity parsing and management

#include "client.h"

extern qhandle_t cl_mod_powerscreen;
extern qhandle_t cl_mod_laser;
extern qhandle_t cl_mod_dmspot;
extern qhandle_t cl_img_flare;

typedef struct {
    uint64_t    shell;
    vec3_t      color;
} item_highlight_t;

/*
=========================================================================

FRAME PARSING

=========================================================================
*/

// returns true if origin/angles update has been optimized out
static inline bool entity_is_optimized(const centity_state_t *state)
{
    return cls.serverProtocol == PROTOCOL_VERSION_Q2PRO
        && state->number == cl.frame.clientNum + 1
        && cl.frame.ps.pmove.pm_type < PM_DEAD;
}

static inline void entity_reset_water_wake(centity_t *ent,
                                           const vec_t *origin)
{
    VectorCopy(origin, ent->rt_water_wake_sample_origin);
    VectorCopy(origin, ent->rt_water_wake_origin);
    VectorClear(ent->rt_water_wake_normal);
    VectorClear(ent->rt_water_wake_direction);
    ent->rt_water_wake_sample_time = cl.time;
    ent->rt_water_wake_seen_time = 0;
    ent->rt_water_wake_scale = 0.0f;
    ent->rt_water_wake_valid = false;
}

static inline void
entity_update_new(centity_t *ent, const centity_state_t *state, const vec_t *origin)
{
    ent->trailcount = 1024;     // for diminishing rocket / grenade trails
    ent->flashlightfrac = 1.0f;
    entity_reset_water_wake(ent, origin);

    // duplicate the current state so lerping doesn't hurt anything
    ent->prev = *state;
#if USE_FPS
    ent->prev_frame = state->frame;
    ent->event_frame = cl.frame.number;
#endif

    if (state->event == EV_PLAYER_TELEPORT ||
        state->event == EV_OTHER_TELEPORT ||
        (state->renderfx & RF_BEAM)) {
        // no lerping if teleported
        VectorCopy(origin, ent->lerp_origin);
        return;
    }

    // old_origin is valid for new entities,
    // so use it as starting point for interpolating between
    VectorCopy(state->old_origin, ent->prev.origin);
    VectorCopy(state->old_origin, ent->lerp_origin);
}

static inline void
entity_update_old(centity_t *ent, const centity_state_t *state, const vec_t *origin)
{
    int event = state->event;

#if USE_FPS
    // check for new event
    if (state->event != ent->current.event)
        ent->event_frame = cl.frame.number; // new
    else if (cl.frame.number - ent->event_frame >= cl.frametime.div)
        ent->event_frame = cl.frame.number; // refreshed
    else
        event = 0; // duplicated
#endif

    if (state->modelindex != ent->current.modelindex
        || state->modelindex2 != ent->current.modelindex2
        || state->modelindex3 != ent->current.modelindex3
        || state->modelindex4 != ent->current.modelindex4
        || event == EV_PLAYER_TELEPORT
        || event == EV_OTHER_TELEPORT
        || fabsf(origin[0] - ent->current.origin[0]) > 512
        || fabsf(origin[1] - ent->current.origin[1]) > 512
        || fabsf(origin[2] - ent->current.origin[2]) > 512
        || cl_nolerp->integer == 1) {
        // some data changes will force no lerping
        ent->trailcount = 1024;     // for diminishing rocket / grenade trails
        ent->flashlightfrac = 1.0f;
        entity_reset_water_wake(ent, origin);

        // duplicate the current state so lerping doesn't hurt anything
        ent->prev = *state;
#if USE_FPS
        ent->prev_frame = state->frame;
#endif
        // no lerping if teleported or morphed
        VectorCopy(origin, ent->lerp_origin);
        return;
    }

#if USE_FPS
    // start alias model animation
    if (state->frame != ent->current.frame) {
        ent->prev_frame = ent->current.frame;
        ent->anim_start = cl.servertime - cl.frametime.time;
    }
#endif

    // shuffle the last state to previous
    ent->prev = ent->current;
}

static inline bool entity_is_new(const centity_t *ent)
{
    if (!cl.oldframe.valid)
        return true;    // last received frame was invalid

    if (ent->serverframe != cl.oldframe.number)
        return true;    // wasn't in last received frame

    if (cl_nolerp->integer == 2)
        return true;    // developer option, always new

    if (cl_nolerp->integer == 3)
        return false;   // developer option, lerp from last received frame

    if (cl.oldframe.number != cl.frame.number - 1)
        return true;    // previous server frame was dropped

    return false;
}

static void parse_entity_update(const centity_state_t *state)
{
    centity_t *ent = &cl_entities[state->number];
    const vec_t *origin;
    vec3_t origin_v;

    // if entity is solid, decode mins/maxs and add to the list
    if (state->solid && state->number != cl.frame.clientNum + 1
        && cl.numSolidEntities < MAX_PACKET_ENTITIES)
        cl.solidEntities[cl.numSolidEntities++] = ent;

    if (state->solid && state->solid != PACKED_BSP) {
        // encoded bbox
        if (cl.csr.extended)
            MSG_UnpackSolid32_Ver2(state->solid, ent->mins, ent->maxs);
        else if (cl.esFlags & MSG_ES_LONGSOLID)
            MSG_UnpackSolid32_Ver1(state->solid, ent->mins, ent->maxs);
        else
            MSG_UnpackSolid16(state->solid, ent->mins, ent->maxs);
        ent->radius = Distance(ent->maxs, ent->mins) * 0.5f;
    } else {
        VectorClear(ent->mins);
        VectorClear(ent->maxs);
        ent->radius = 0;
    }

    // work around Q2PRO server bandwidth optimization
    if (entity_is_optimized(state)) {
        VectorScale(cl.frame.ps.pmove.origin, 0.125f, origin_v);
        origin = origin_v;
    } else {
        origin = state->origin;
    }

    if (entity_is_new(ent)) {
        // wasn't in last update, so initialize some things
        entity_update_new(ent, state, origin);
    } else {
        entity_update_old(ent, state, origin);
    }

    ent->serverframe = cl.frame.number;
    ent->current = *state;

    // work around Q2PRO server bandwidth optimization
    if (entity_is_optimized(state)) {
        Com_PlayerToEntityState(&cl.frame.ps, &ent->current.s);
    }
}

// an entity has just been parsed that has an event value
static void parse_entity_event(int number)
{
    const centity_t *cent = &cl_entities[number];

    if (CL_FRAMESYNC) {
        // EF_TELEPORTER acts like an event, but is not cleared each frame
        if (cent->current.effects & EF_TELEPORTER)
            CL_TeleporterParticles(cent->current.origin);

        if (cent->current.morefx & EFX_TELEPORTER2)
            CL_TeleporterParticles2(cent->current.origin);

        if (cent->current.morefx & EFX_BARREL_EXPLODING)
            CL_BarrelExplodingParticles(cent->current.origin);
    }

#if USE_FPS
    if (cent->event_frame != cl.frame.number)
        return;
#endif

    switch (cent->current.event) {
    case EV_ITEM_RESPAWN:
        S_StartSound(NULL, number, CHAN_WEAPON, S_RegisterSound("items/respawn1.wav"), 1, ATTN_IDLE, 0);
        CL_ItemRespawnParticles(cent->current.origin);
        CL_RTItemRespawn(cent->current.origin);
        break;
    case EV_PLAYER_TELEPORT:
        S_StartSound(NULL, number, CHAN_WEAPON, S_RegisterSound("misc/tele1.wav"), 1, ATTN_IDLE, 0);
        CL_TeleportParticles(cent->current.origin);
        CL_RTTeleportVortex(cent->current.origin, RT_TELEPORT_STANDARD);
        break;
    case EV_FOOTSTEP:
        if (cl_footsteps->integer)
            CL_PlayFootstepSfx(-1, number, 1.0f, ATTN_NORM);
        break;
    case EV_OTHER_FOOTSTEP:
        if (cl.csr.extended && cl_footsteps->integer)
            CL_PlayFootstepSfx(-1, number, 0.5f, ATTN_IDLE);
        break;
    case EV_LADDER_STEP:
        if (cl.csr.extended && cl_footsteps->integer)
            CL_PlayFootstepSfx(FOOTSTEP_ID_LADDER, number, 0.5f, ATTN_IDLE);
        break;
    case EV_FALLSHORT:
        S_StartSound(NULL, number, CHAN_AUTO, S_RegisterSound("player/land1.wav"), 1, ATTN_NORM, 0);
        CL_RTLandingDust(number, RT_LANDING_SHORT);
        break;
    case EV_FALL:
        S_StartSound(NULL, number, CHAN_AUTO, S_RegisterSound("*fall2.wav"), 1, ATTN_NORM, 0);
        CL_RTLandingDust(number, RT_LANDING_NORMAL);
        break;
    case EV_FALLFAR:
        S_StartSound(NULL, number, CHAN_AUTO, S_RegisterSound("*fall1.wav"), 1, ATTN_NORM, 0);
        CL_RTLandingDust(number, RT_LANDING_FAR);
        break;
    }
}

static void set_active_state(void)
{
    cls.state = ca_active;

    cl.serverdelta = Q_align_down(cl.frame.number, CL_FRAMEDIV);
    cl.time = cl.servertime = 0; // set time, needed for demos
#if USE_FPS
    cl.keytime = cl.keyservertime = 0;
    cl.keyframe = cl.frame; // initialize keyframe to make sure it's valid
#endif

    // initialize oldframe so lerping doesn't hurt anything
    cl.oldframe.valid = false;
    cl.oldframe.ps = cl.frame.ps;
#if USE_FPS
    cl.oldkeyframe.valid = false;
    cl.oldkeyframe.ps = cl.keyframe.ps;
#endif

    cl.frameflags = 0;
    cl.initialSeq = cls.netchan.outgoing_sequence;

    if (cls.demo.playback) {
        // init some demo things
        CL_FirstDemoFrame();
    } else {
        // set initial cl.predicted_origin and cl.predicted_angles
        VectorScale(cl.frame.ps.pmove.origin, 0.125f, cl.predicted_origin);
        VectorScale(cl.frame.ps.pmove.velocity, 0.125f, cl.predicted_velocity);
        if (cl.frame.ps.pmove.pm_type < PM_DEAD &&
            cls.serverProtocol > PROTOCOL_VERSION_DEFAULT) {
            // enhanced servers don't send viewangles
            CL_PredictAngles();
        } else {
            // just use what server provided
            VectorCopy(cl.frame.ps.viewangles, cl.predicted_angles);
        }
    }

    SCR_EndLoadingPlaque();     // get rid of loading plaque
    SCR_LagClear();
    Con_Close(false);           // get rid of connection screen

    CL_CheckForPause();

    CL_UpdateFrameTimes();

    IN_Activate();

    if (!cls.demo.playback) {
        EXEC_TRIGGER(cl_beginmapcmd);
        Cmd_ExecTrigger("#cl_enterlevel");
    }
}

static void
check_player_lerp(server_frame_t *oldframe, server_frame_t *frame, int framediv)
{
    player_state_t *ps, *ops;
    const centity_t *ent;
    int oldnum;

    // find states to interpolate between
    ps = &frame->ps;
    ops = &oldframe->ps;

    // no lerping if previous frame was dropped or invalid
    if (!oldframe->valid)
        goto dup;

    oldnum = frame->number - framediv;
    if (oldframe->number != oldnum)
        goto dup;

    // no lerping if player entity was teleported (origin check)
    if (abs(ops->pmove.origin[0] - ps->pmove.origin[0]) > 256 * 8 ||
        abs(ops->pmove.origin[1] - ps->pmove.origin[1]) > 256 * 8 ||
        abs(ops->pmove.origin[2] - ps->pmove.origin[2]) > 256 * 8) {
        goto dup;
    }

    // no lerping if player entity was teleported (event check)
    ent = &cl_entities[frame->clientNum + 1];
    if (ent->serverframe > oldnum &&
        ent->serverframe <= frame->number &&
#if USE_FPS
        ent->event_frame > oldnum &&
        ent->event_frame <= frame->number &&
#endif
        (ent->current.event == EV_PLAYER_TELEPORT
         || ent->current.event == EV_OTHER_TELEPORT)) {
        goto dup;
    }

    // no lerping if teleport bit was flipped
    if (!cl.csr.extended && (ops->pmove.pm_flags ^ ps->pmove.pm_flags) & PMF_TELEPORT_BIT)
        goto dup;

    if (cl.csr.extended && (ops->rdflags ^ ps->rdflags) & RDF_TELEPORT_BIT)
        goto dup;

    // no lerping if POV number changed
    if (oldframe->clientNum != frame->clientNum)
        goto dup;

    // developer option
    if (cl_nolerp->integer == 1)
        goto dup;

    return;

dup:
    // duplicate the current state so lerping doesn't hurt anything
    *ops = *ps;
}

/*
==================
CL_DeltaFrame

A valid frame has been parsed.
==================
*/
void CL_DeltaFrame(void)
{
    centity_t           *ent;
    int                 i, j;
    int                 framenum;
    int                 prevstate = cls.state;

    // getting a valid frame message ends the connection process
    if (cls.state == ca_precached)
        set_active_state();

    // set server time
    framenum = cl.frame.number - cl.serverdelta;

    if (framenum < 0)
        Com_Error(ERR_DROP, "%s: server time went backwards", __func__);

    if (framenum > INT_MAX / CL_FRAMETIME)
        Com_Error(ERR_DROP, "%s: server time overflowed", __func__);

    cl.servertime = framenum * CL_FRAMETIME;
#if USE_FPS
    cl.keyservertime = (framenum / cl.frametime.div) * BASE_FRAMETIME;
#endif

    // rebuild the list of solid entities for this frame
    cl.numSolidEntities = 0;

    // initialize position of the player's own entity from playerstate.
    // this is needed in situations when player entity is invisible, but
    // server sends an effect referencing it's origin (such as MZ_LOGIN, etc)
    ent = &cl_entities[cl.frame.clientNum + 1];
    Com_PlayerToEntityState(&cl.frame.ps, &ent->current.s);

    // set current and prev, unpack solid, etc
    for (i = 0; i < cl.frame.numEntities; i++) {
        j = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        parse_entity_update(&cl.entityStates[j]);
    }

    // fire events. due to footstep tracing this must be after updating entities.
    for (i = 0; i < cl.frame.numEntities; i++) {
        j = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        parse_entity_event(cl.entityStates[j].number);
    }

    if (cls.demo.recording && !cls.demo.paused && !cls.demo.seeking && CL_FRAMESYNC) {
        CL_EmitDemoFrame();
    }

    if (prevstate == ca_precached)
        CL_GTV_Resume();
    else
        CL_GTV_EmitFrame();

    if (cls.demo.playback) {
        // this delta has nothing to do with local viewangles,
        // clear it to avoid interfering with demo freelook hack
        VectorClear(cl.frame.ps.pmove.delta_angles);
    }

    if (cl.oldframe.ps.pmove.pm_type != cl.frame.ps.pmove.pm_type) {
        IN_Activate();
    }

    check_player_lerp(&cl.oldframe, &cl.frame, 1);

#if USE_FPS
    if (CL_FRAMESYNC)
        check_player_lerp(&cl.oldkeyframe, &cl.keyframe, cl.frametime.div);
#endif

    CL_CheckPredictionError();

    SCR_SetCrosshairColor();
}

#if USE_DEBUG
// for debugging problems when out-of-date entity origin is referenced
void CL_CheckEntityPresent(int entnum, const char *what)
{
    const centity_t *e;

    if (entnum == cl.frame.clientNum + 1) {
        return; // player entity = current
    }

    e = &cl_entities[entnum];
    if (e->serverframe == cl.frame.number) {
        return; // current
    }

    if (e->serverframe) {
        Com_LPrintf(PRINT_DEVELOPER,
                    "SERVER BUG: %s on entity %d last seen %d frames ago\n",
                    what, entnum, cl.frame.number - e->serverframe);
    } else {
        Com_LPrintf(PRINT_DEVELOPER,
                    "SERVER BUG: %s on entity %d never seen before\n",
                    what, entnum);
    }
}
#endif


/*
==========================================================================

INTERPOLATE BETWEEN FRAMES TO GET RENDERING PARAMS

==========================================================================
*/

static float lerp_entity_alpha(const centity_t *ent)
{
    float prev = ent->prev.alpha;
    float curr = ent->current.alpha;

    // no lerping from/to default alpha
    if (prev && curr)
        return prev + cl.lerpfrac * (curr - prev);

    return curr ? curr : 1.0f;
}

/*
==========================================================================

SKIN TINTS

The default colors reproduce the q2dl-skins-tinted-items-ammo-weapons skin
pack: each skin there is a gradient map of the original, from a dark color
for black texels to dark + gain for white ones along luminance^gamma. The
values were fitted per skin against the pack, so tinting the original skins
looks like the pack without having to install it. cl_tint_<group> replaces
a group's colors with one color of the player's choice.

==========================================================================
*/

typedef enum {
    TINT_BLASTER,
    TINT_SHOTGUN,
    TINT_MACHINEGUN,
    TINT_GRENADES,
    TINT_ROCKETS,
    TINT_CELLS,
    TINT_RAILGUN,
    TINT_WEAPONS,
    TINT_BODY_ARMOR,
    TINT_COMBAT_ARMOR,
    TINT_JACKET_ARMOR,
    TINT_ARMOR_SHARD,
    TINT_POWER_SCREEN,
    TINT_POWER_SHIELD,
    TINT_HEALTH,
    TINT_ADRENALINE,
    TINT_QUAD,
    TINT_INVULNERABILITY,
    TINT_ITEMS,

    TINT_NUM_GROUPS
} tint_group_t;

typedef enum {
    TINT_KIND_ITEM,     // cl_itemtint
    TINT_KIND_AMMO,     // cl_ammotint or cl_itemtint
    TINT_KIND_WEAPON,   // cl_weapontint
} tint_kind_t;

typedef struct {
    vec3_t  dark;       // color of black texels
    vec3_t  gain;       // added at full luminance
    float   gamma;      // luminance exponent; 0 tints the lit color instead
} skin_tint_t;

typedef struct {
    const char      *prefix;
    tint_group_t    group;
    tint_kind_t     kind;
    skin_tint_t     tint;
} skin_tint_def_t;

static const char *const tint_group_names[TINT_NUM_GROUPS] = {
    "blaster", "shotgun", "machinegun", "grenades", "rockets", "cells",
    "railgun", "weapons", "body_armor", "combat_armor", "jacket_armor",
    "armor_shard", "power_screen", "power_shield", "health", "adrenaline",
    "quad", "invulnerability", "items"
};

// View weapons aren't in the pack. They use the colors of their pickup
// models, with the milder curve of the items so that they keep their shading.
static const skin_tint_def_t skin_tint_defs[] = {
    { "models/items/ammo/shells/", TINT_SHOTGUN, TINT_KIND_AMMO, { { 0.325f, 0.244f, 0.007f }, { 0.921f, 0.714f, 0.141f }, 0.700f } },
    { "models/items/ammo/bullets/", TINT_MACHINEGUN, TINT_KIND_AMMO, { { -0.008f, 0.257f, 0.429f }, { 0.327f, 0.543f, 0.785f }, 0.750f } },
    { "models/items/ammo/grenades/", TINT_GRENADES, TINT_KIND_AMMO, { { -0.002f, 0.358f, 0.089f }, { 0.022f, 0.735f, 0.265f }, 0.600f } },
    { "models/items/ammo/mines/", TINT_GRENADES, TINT_KIND_AMMO, { { 0.342f, 0.138f, -0.018f }, { 0.727f, 0.616f, 0.383f }, 0.700f } },
    { "models/items/ammo/rockets/", TINT_ROCKETS, TINT_KIND_AMMO, { { 0.485f, -0.014f, -0.014f }, { 0.715f, 0.170f, 0.146f }, 0.600f } },
    { "models/items/ammo/cells/", TINT_CELLS, TINT_KIND_AMMO, { { 0.093f, 0.327f, -0.009f }, { 0.438f, 0.683f, 0.259f }, 0.600f } },
    { "models/items/ammo/nuke/", TINT_CELLS, TINT_KIND_AMMO, { { 0.144f, 0.328f, 0.041f }, { 0.527f, 0.762f, 0.332f }, 0.650f } },
    { "models/items/ammo/slugs/", TINT_RAILGUN, TINT_KIND_AMMO, { { 0.242f, -0.004f, 0.333f }, { 0.894f, 0.269f, 0.710f }, 0.700f } },
    { "models/weapons/g_shotg/", TINT_SHOTGUN, TINT_KIND_WEAPON, { { 0.138f, 0.104f, 0.000f }, { 0.844f, 0.630f, 0.000f }, 0.300f } },
    { "models/weapons/v_shotg/", TINT_SHOTGUN, TINT_KIND_WEAPON, { { 0.138f, 0.104f, 0.000f }, { 0.844f, 0.630f, 0.000f }, 0.700f } },
    { "models/weapons/g_shotg2/", TINT_SHOTGUN, TINT_KIND_WEAPON, { { 0.136f, 0.103f, 0.000f }, { 0.769f, 0.574f, 0.000f }, 0.250f } },
    { "models/weapons/v_shotg2/", TINT_SHOTGUN, TINT_KIND_WEAPON, { { 0.136f, 0.103f, 0.000f }, { 0.769f, 0.574f, 0.000f }, 0.700f } },
    { "models/weapons/g_machn/", TINT_MACHINEGUN, TINT_KIND_WEAPON, { { 0.000f, 0.032f, 0.131f }, { 0.000f, 0.173f, 0.700f }, 0.275f } },
    { "models/weapons/v_machn/", TINT_MACHINEGUN, TINT_KIND_WEAPON, { { 0.000f, 0.032f, 0.131f }, { 0.000f, 0.173f, 0.700f }, 0.700f } },
    { "models/weapons/g_chain/", TINT_MACHINEGUN, TINT_KIND_WEAPON, { { 0.000f, 0.032f, 0.132f }, { 0.000f, 0.185f, 0.745f }, 0.300f } },
    { "models/weapons/v_chain/", TINT_MACHINEGUN, TINT_KIND_WEAPON, { { 0.000f, 0.032f, 0.132f }, { 0.000f, 0.185f, 0.745f }, 0.700f } },
    { "models/weapons/g_launch/", TINT_GRENADES, TINT_KIND_WEAPON, { { 0.125f, 0.069f, 0.012f }, { 0.890f, 0.424f, 0.027f }, 0.350f } },
    { "models/weapons/v_launch/", TINT_GRENADES, TINT_KIND_WEAPON, { { 0.125f, 0.069f, 0.012f }, { 0.890f, 0.424f, 0.027f }, 0.700f } },
    { "models/weapons/v_handgr/", TINT_GRENADES, TINT_KIND_WEAPON, { { -0.002f, 0.358f, 0.089f }, { 0.022f, 0.735f, 0.265f }, 0.700f } },
    { "models/weapons/g_rocket/", TINT_ROCKETS, TINT_KIND_WEAPON, { { 0.137f, 0.013f, 0.013f }, { 0.826f, 0.077f, 0.077f }, 0.350f } },
    { "models/weapons/v_rocket/", TINT_ROCKETS, TINT_KIND_WEAPON, { { 0.137f, 0.013f, 0.013f }, { 0.826f, 0.077f, 0.077f }, 0.700f } },
    { "models/weapons/g_hyperb/", TINT_CELLS, TINT_KIND_WEAPON, { { 0.041f, 0.135f, 0.000f }, { 0.412f, 0.821f, 0.331f }, 0.300f } },
    { "models/weapons/v_hyperb/", TINT_CELLS, TINT_KIND_WEAPON, { { 0.041f, 0.135f, 0.000f }, { 0.412f, 0.821f, 0.331f }, 0.700f } },
    { "models/weapons/g_bfg/", TINT_CELLS, TINT_KIND_WEAPON, { { 0.042f, 0.137f, 0.002f }, { 0.407f, 0.813f, 0.326f }, 0.300f } },
    { "models/weapons/v_bfg/", TINT_CELLS, TINT_KIND_WEAPON, { { 0.042f, 0.137f, 0.002f }, { 0.407f, 0.813f, 0.326f }, 0.700f } },
    { "models/weapons/g_rail/", TINT_RAILGUN, TINT_KIND_WEAPON, { { 0.101f, 0.000f, 0.133f }, { 0.550f, 0.000f, 0.737f }, 0.300f } },
    { "models/weapons/v_rail/", TINT_RAILGUN, TINT_KIND_WEAPON, { { 0.101f, 0.000f, 0.133f }, { 0.550f, 0.000f, 0.737f }, 0.700f } },
    { "models/weapons/g_blast/", TINT_BLASTER, TINT_KIND_WEAPON, { { 0.000f, 0.130f, 0.153f }, { 0.001f, 0.269f, 0.307f }, 0.200f } },
    { "models/weapons/v_blast/", TINT_BLASTER, TINT_KIND_WEAPON, { { 0.000f, 0.130f, 0.153f }, { 0.001f, 0.269f, 0.307f }, 0.700f } },
    { "models/weapons/g_disint/", TINT_BLASTER, TINT_KIND_WEAPON, { { -0.003f, 0.111f, 0.129f }, { 0.523f, 0.854f, 0.892f }, 0.800f } },
    { "models/weapons/v_disint/", TINT_BLASTER, TINT_KIND_WEAPON, { { -0.003f, 0.111f, 0.129f }, { 0.523f, 0.854f, 0.892f }, 0.800f } },
    { "models/weapons/g_flareg/", TINT_WEAPONS, TINT_KIND_WEAPON, { { 0.134f, 0.070f, 0.006f }, { 0.888f, 0.819f, 0.702f }, 0.850f } },
    { "models/weapons/v_flareg/", TINT_WEAPONS, TINT_KIND_WEAPON, { { 0.134f, 0.070f, 0.006f }, { 0.888f, 0.819f, 0.702f }, 0.850f } },
    { "models/objects/grenade/", TINT_GRENADES, TINT_KIND_ITEM, { { 0.295f, 0.307f, 0.298f }, { -0.170f, 0.592f, 0.043f }, 0.275f } },
    { "models/objects/grenade2/", TINT_GRENADES, TINT_KIND_ITEM, { { 0.296f, 0.305f, 0.298f }, { -0.170f, 0.577f, 0.037f }, 0.275f } },
    { "models/objects/rocket/", TINT_ROCKETS, TINT_KIND_ITEM, { { 0.302f, 0.298f, 0.298f }, { 0.706f, -0.106f, -0.113f }, 0.275f } },
    { "models/items/armor/body/", TINT_BODY_ARMOR, TINT_KIND_ITEM, { { 0.330f, 0.028f, 0.027f }, { 0.847f, 0.310f, 0.259f }, 0.650f } },
    { "models/items/armor/combat/", TINT_COMBAT_ARMOR, TINT_KIND_ITEM, { { 0.327f, 0.241f, -0.000f }, { 0.832f, 0.653f, 0.162f }, 0.650f } },
    { "models/items/armor/jacket/", TINT_JACKET_ARMOR, TINT_KIND_ITEM, { { 0.145f, 0.211f, 0.024f }, { 0.590f, 0.670f, 0.314f }, 0.700f } },
    { "models/items/armor/shard/", TINT_ARMOR_SHARD, TINT_KIND_ITEM, { { 0.016f, 0.282f, 0.325f }, { 0.454f, 0.872f, 0.859f }, 0.800f } },
    { "models/items/armor/screen/", TINT_POWER_SCREEN, TINT_KIND_ITEM, { { -0.000f, 0.097f, 0.330f }, { 0.348f, 0.491f, 0.747f }, 0.700f } },
    { "models/items/armor/shield/", TINT_POWER_SHIELD, TINT_KIND_ITEM, { { 0.241f, -0.007f, 0.330f }, { 0.733f, 0.291f, 0.671f }, 0.650f } },
    { "models/items/armor/effect/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.282f, 0.325f, 0.325f }, { 1.045f, 1.176f, 0.710f }, 1.350f } },
    { "models/items/healing/large/", TINT_HEALTH, TINT_KIND_ITEM, { { 0.350f, 0.052f, 0.208f }, { 0.786f, 0.474f, 0.721f }, 0.750f } },
    { "models/items/healing/medium/", TINT_HEALTH, TINT_KIND_ITEM, { { 0.367f, 0.046f, 0.212f }, { 0.714f, 0.504f, 0.661f }, 0.750f } },
    { "models/items/healing/stimpack/", TINT_HEALTH, TINT_KIND_ITEM, { { 0.336f, 0.055f, 0.207f }, { 0.792f, 0.428f, 0.681f }, 0.700f } },
    { "models/items/mega_h/", TINT_HEALTH, TINT_KIND_ITEM, { { 0.328f, 0.057f, 0.211f }, { 0.850f, 0.402f, 0.666f }, 0.700f } },
    { "models/items/adrenal/", TINT_ADRENALINE, TINT_KIND_ITEM, { { 0.070f, 0.196f, 0.346f }, { 0.413f, 0.493f, 0.612f }, 0.550f } },
    { "models/items/quaddama/", TINT_QUAD, TINT_KIND_ITEM, { { 0.200f, 0.238f, 0.328f }, { 0.633f, 0.659f, 0.677f }, 0.650f } },
    { "models/items/invulner/", TINT_INVULNERABILITY, TINT_KIND_ITEM, { { 0.345f, 0.173f, 0.067f }, { 0.850f, 0.584f, 0.220f }, 0.650f } },
    { "models/items/band/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.331f, 0.211f, 0.065f }, { 0.814f, 0.676f, 0.336f }, 0.700f } },
    { "models/items/breather/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.331f, 0.211f, 0.063f }, { 0.794f, 0.681f, 0.405f }, 0.700f } },
    { "models/items/c_head/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.332f, 0.212f, 0.064f }, { 0.891f, 0.703f, 0.376f }, 0.750f } },
    { "models/items/enviro/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.329f, 0.211f, 0.065f }, { 0.856f, 0.667f, 0.348f }, 0.700f } },
    { "models/items/pack/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.194f, 0.024f, 0.388f }, { 0.630f, 0.288f, 0.784f }, 0.700f } },
    { "models/items/silencer/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.327f, 0.212f, 0.065f }, { 0.864f, 0.663f, 0.392f }, 0.700f } },
    { "models/items/keys/data_cd/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.330f, 0.212f, 0.064f }, { 0.735f, 0.751f, 0.506f }, 0.800f } },
    { "models/items/keys/key/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.341f, 0.210f, 0.054f }, { 0.695f, 0.695f, 0.533f }, 0.700f } },
    { "models/items/keys/pass/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.327f, 0.209f, 0.067f }, { 0.816f, 0.619f, 0.280f }, 0.650f } },
    { "models/items/keys/power/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.332f, 0.211f, 0.061f }, { 0.851f, 0.662f, 0.394f }, 0.700f } },
    { "models/items/keys/pyramid/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.331f, 0.211f, 0.063f }, { 0.781f, 0.678f, 0.428f }, 0.700f } },
    { "models/items/keys/red_key/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.327f, 0.207f, 0.059f }, { 0.868f, 0.525f, 0.252f }, 0.600f } },
    { "models/items/keys/spinner/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.338f, 0.208f, 0.059f }, { 0.759f, 0.696f, 0.438f }, 0.700f } },
    { "models/items/keys/target/", TINT_ITEMS, TINT_KIND_ITEM, { { 0.330f, 0.211f, 0.064f }, { 0.793f, 0.673f, 0.442f }, 0.700f } },
};

// Models the pack doesn't cover: other weapons, items and ammo, such as the
// mission packs' ones, get the pack's plain orange item look.
static const skin_tint_def_t skin_tint_weapon_default = {
    "models/weapons/", TINT_WEAPONS, TINT_KIND_WEAPON,
    { { 0.325f, 0.212f, 0.067f }, { 0.863f, 0.663f, 0.392f }, 0.700f }
};

static const skin_tint_def_t skin_tint_item_default = {
    "models/items/", TINT_ITEMS, TINT_KIND_ITEM,
    { { 0.325f, 0.212f, 0.067f }, { 0.863f, 0.663f, 0.392f }, 0.700f }
};

static const skin_tint_def_t skin_tint_ammo_default = {
    "models/items/ammo/", TINT_ITEMS, TINT_KIND_AMMO,
    { { 0.325f, 0.212f, 0.067f }, { 0.863f, 0.663f, 0.392f }, 0.700f }
};

static cvar_t   *cl_tint_cvars[TINT_NUM_GROUPS];
static vec3_t   cl_tint_custom[TINT_NUM_GROUPS];
static bool     cl_tint_has_custom[TINT_NUM_GROUPS];

static void cl_tint_changed(cvar_t *self)
{
    for (int i = 0; i < TINT_NUM_GROUPS; i++) {
        color_t color;

        if (cl_tint_cvars[i] != self)
            continue;

        cl_tint_has_custom[i] = false;
        if (!*self->string)
            return;
        if (!SCR_ParseColor(self->string, &color)) {
            Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
            return;
        }
        VectorSet(cl_tint_custom[i], color.u8[0] / 255.0f,
                  color.u8[1] / 255.0f, color.u8[2] / 255.0f);
        cl_tint_has_custom[i] = true;
        return;
    }
}

void CL_InitSkinTints(void)
{
    for (int i = 0; i < TINT_NUM_GROUPS; i++) {
        char name[MAX_QPATH];

        Q_concat(name, sizeof(name), "cl_tint_", tint_group_names[i]);
        cl_tint_cvars[i] = Cvar_Get(name, "", CVAR_ARCHIVE);
        cl_tint_cvars[i]->changed = cl_tint_changed;
        cl_tint_changed(cl_tint_cvars[i]);
    }
}

static bool model_starts_with(const char *model, const char *prefix)
{
    return !Q_strncasecmp(model, prefix, strlen(prefix));
}

static const skin_tint_def_t *CL_FindSkinTint(const char *model)
{
    for (int i = 0; i < q_countof(skin_tint_defs); i++)
        if (model_starts_with(model, skin_tint_defs[i].prefix))
            return &skin_tint_defs[i];

    if (model_starts_with(model, "models/items/ammo/"))
        return &skin_tint_ammo_default;
    if (model_starts_with(model, "models/items/"))
        return &skin_tint_item_default;
    if (model_starts_with(model, "models/weapons/g_") ||
        model_starts_with(model, "models/weapons/v_"))
        return &skin_tint_weapon_default;

    return NULL;
}

// Weapon models carried by players, in the order of the standard vwep list.
static const skin_tint_def_t *CL_FindWeaponIndexTint(int weapon)
{
    static const char *const models[] = {
        NULL,
        "models/weapons/g_blast/",
        "models/weapons/g_shotg/",
        "models/weapons/g_shotg2/",
        "models/weapons/g_machn/",
        "models/weapons/g_chain/",
        "models/weapons/v_handgr/",
        "models/weapons/g_launch/",
        "models/weapons/g_rocket/",
        "models/weapons/g_hyperb/",
        "models/weapons/g_rail/",
        "models/weapons/g_bfg/",
    };

    if (weapon > 0 && weapon < q_countof(models))
        return CL_FindSkinTint(models[weapon]);

    return &skin_tint_weapon_default;
}

static bool CL_SkinTintEnabled(const skin_tint_def_t *def)
{
    switch (def->kind) {
    case TINT_KIND_AMMO:
        return cl_ammotint->integer || cl_itemtint->integer;
    case TINT_KIND_WEAPON:
        return cl_weapontint->integer;
    default:
        return cl_itemtint->integer;
    }
}

// A custom color keeps the pack's typical contrast: about a third of the
// color for black texels, rising to the full color on bright ones.
static void CL_ResolveSkinTint(const skin_tint_def_t *def, skin_tint_t *tint)
{
    if (cl_tint_has_custom[def->group]) {
        VectorScale(cl_tint_custom[def->group], 0.33f, tint->dark);
        VectorScale(cl_tint_custom[def->group], 0.85f, tint->gain);
        tint->gamma = 0.7f;
    } else {
        *tint = def->tint;
    }
}

// Highlight shells use the color of a typical texel of the tinted skin,
// brightened so that its strongest channel is 1.
static void CL_SkinTintHighlightColor(const skin_tint_def_t *def, vec3_t color)
{
    skin_tint_t tint;
    float m;

    CL_ResolveSkinTint(def, &tint);
    VectorMA(tint.dark, powf(0.3f, tint.gamma), tint.gain, color);
    for (int i = 0; i < 3; i++)
        color[i] = max(color[i], 0.0f);
    m = max(color[0], max(color[1], color[2]));
    if (m > 0.0f)
        VectorScale(color, 1.0f / m, color);
    else
        VectorSet(color, 1.0f, 1.0f, 1.0f);
}

static bool CL_GetWeaponModelTint(const char *model, skin_tint_t *tint)
{
    const skin_tint_def_t *def = CL_FindSkinTint(model);

    if (!def || def->kind != TINT_KIND_WEAPON || !CL_SkinTintEnabled(def))
        return false;

    CL_ResolveSkinTint(def, tint);
    return true;
}

static bool CL_GetWeaponIndexTint(int weapon, skin_tint_t *tint)
{
    const skin_tint_def_t *def;

    if (!cl_weapontint->integer)
        return false;

    def = CL_FindWeaponIndexTint(weapon);
    CL_ResolveSkinTint(def, tint);
    return true;
}

static bool CL_GetModelSkinTint(const centity_state_t *state, skin_tint_t *tint)
{
    const skin_tint_def_t *def;

    if (state->modelindex == MODELINDEX_PLAYER && cl_playertint->integer) {
        int clientnum = state->skinnum & 0xff;
        const clientinfo_t *ci;

        if (clientnum >= MAX_CLIENTS)
            return false;
        ci = &cl.clientinfo[clientnum];

        // Players aren't in the skin pack: they keep a tint of the lit
        // color. Player tints get an extra 20% boost over the old item tints.
        VectorClear(tint->dark);
        tint->gamma = 0.0f;
        if (!Q_stricmp(ci->model_name, "male"))
            VectorSet(tint->gain, 2.0f, 2.0f, 2.0f);
        else if (!Q_stricmp(ci->model_name, "female"))
            VectorSet(tint->gain, 0.08f, 1.2f, 0.05f);
        else
            VectorSet(tint->gain, 1.2f, 0.78f, 0.24f);
        return true;
    }

    if (!state->modelindex)
        return false;

    def = CL_FindSkinTint(cl.configstrings[cl.csr.models + state->modelindex]);
    if (!def || !CL_SkinTintEnabled(def))
        return false;

    CL_ResolveSkinTint(def, tint);
    return true;
}

static void CL_SetEntitySkinTint(entity_t *ent, const skin_tint_t *tint)
{
    VectorCopy(tint->gain, ent->skin_tint);
    VectorCopy(tint->dark, ent->skin_tint_dark);
    ent->skin_tint_gamma = tint->gamma;
}

static void CL_ClearEntitySkinTint(entity_t *ent)
{
    VectorSet(ent->skin_tint, 1.0f, 1.0f, 1.0f);
    VectorClear(ent->skin_tint_dark);
    ent->skin_tint_gamma = 0.0f;
}

static float CL_ItemHighlightBloom(void)
{
    return Cvar_ClampValue(cl_itemhighlight_glow, 0, 5);
}

static float CL_ItemHighlightBloomAlpha(float bloom)
{
    return min(bloom * 0.15f, 1.0f);
}

static bool CL_GetPlayerHighlight(const centity_state_t *state, item_highlight_t *highlight)
{
    int clientnum;
    const clientinfo_t *ci;

    if (!cl_playerhighlight->integer || state->modelindex != MODELINDEX_PLAYER)
        return false;

    clientnum = state->skinnum & 0xff;
    if (clientnum < 0 || clientnum >= MAX_CLIENTS)
        return false;

    ci = &cl.clientinfo[clientnum];
    if (!Q_stricmp(ci->model_name, "male")) {
        highlight->shell = RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE;
        VectorSet(highlight->color, 1.0f, 1.0f, 1.0f);
    } else if (!Q_stricmp(ci->model_name, "female")) {
        highlight->shell = RF_SHELL_GREEN;
        VectorSet(highlight->color, 0.0f, 1.0f, 0.0f);
    } else {
        highlight->shell = RF_SHELL_RED | RF_SHELL_DOUBLE;
        VectorSet(highlight->color, 1.0f, 0.65f, 0.2f);
    }

    return true;
}

static uint64_t CL_GetPlayerPowerupHighlightShell(unsigned effects)
{
    bool quad = effects & EF_QUAD;
    bool pent = effects & EF_PENT;

    if (quad && pent)
        return RF_SHELL_RED | RF_SHELL_BLUE;
    if (quad)
        return RF_SHELL_BLUE;
    if (pent)
        return RF_SHELL_RED;

    return 0;
}

static uint64_t CL_GetPlayerHighlightShell(uint64_t player_shell, uint64_t powerup_shell)
{
    if (!powerup_shell)
        return player_shell;

    return (cl.time / 500) & 1 ? powerup_shell : player_shell;
}

static bool CL_GetItemHighlightShell(const centity_state_t *state, item_highlight_t *highlight)
{
    const char *model;
    bool item_highlight = cl_itemhighlight->integer;
    bool weapon_highlight = cl_weaponhighlight->integer;
    bool ammo_highlight = cl_ammohighlight->integer;

    if ((!item_highlight && !weapon_highlight && !ammo_highlight) ||
        !state->modelindex)
        return false;

    model = cl.configstrings[cl.csr.models + state->modelindex];

    if (item_highlight &&
        (model_starts_with(model, "models/items/healing/") ||
         model_starts_with(model, "models/items/mega_h/"))) {
        highlight->shell = RF_SHELL_RED | RF_SHELL_BLUE;
        VectorSet(highlight->color, 1.0f, 0.18f, 0.65f);
        return true;
    }

    if (item_highlight && model_starts_with(model, "models/items/adrenal/")) {
        highlight->shell = RF_SHELL_RED | RF_SHELL_DOUBLE;
        VectorSet(highlight->color, 1.0f, 0.68f, 0.3f);
        return true;
    }

    if (item_highlight && model_starts_with(model, "models/items/armor/")) {
        if (model_starts_with(model, "models/items/armor/body/")) {
            highlight->shell = RF_SHELL_RED;
            VectorSet(highlight->color, 1.0f, 0.1f, 0.1f);
        } else if (model_starts_with(model, "models/items/armor/combat/")) {
            highlight->shell = RF_SHELL_DOUBLE;
            VectorSet(highlight->color, 1.0f, 0.75f, 0.0f);
        } else if (model_starts_with(model, "models/items/armor/jacket/")) {
            highlight->shell = RF_SHELL_GREEN | RF_SHELL_DOUBLE;
            VectorSet(highlight->color, 0.45f, 0.65f, 0.08f);
        } else if (model_starts_with(model, "models/items/armor/screen/")) {
            highlight->shell = RF_SHELL_BLUE;
            VectorSet(highlight->color, 0.0f, 0.3f, 1.0f);
        } else if (model_starts_with(model, "models/items/armor/shield/")) {
            highlight->shell = RF_SHELL_RED | RF_SHELL_BLUE;
            VectorSet(highlight->color, 0.75f, 0.0f, 1.0f);
        } else {
            highlight->shell = RF_SHELL_GREEN | RF_SHELL_BLUE;
            VectorSet(highlight->color, 0.0f, 0.85f, 1.0f);
        }
        return true;
    }

    if (item_highlight &&
        (!Q_strcasecmp(model, "models/objects/rocket/tris.md2") ||
         !Q_strcasecmp(model, "models/objects/grenade/tris.md2") ||
         !Q_strcasecmp(model, "models/objects/grenade2/tris.md2"))) {
        highlight->shell = RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE;
        VectorSet(highlight->color, 1.0f, 1.0f, 1.0f);
        return true;
    }

    if ((ammo_highlight &&
         !Q_strcasecmp(model, "models/items/ammo/shells/medium/tris.md2")) ||
        (weapon_highlight &&
         (!Q_strcasecmp(model, "models/weapons/g_shotg/tris.md2") ||
          !Q_strcasecmp(model, "models/weapons/g_shotg2/tris.md2")))) {
        highlight->shell = RF_SHELL_DOUBLE;
        VectorSet(highlight->color, 1.0f, 0.75f, 0.0f);
        return true;
    }

    if ((ammo_highlight &&
         !Q_strcasecmp(model, "models/items/ammo/rockets/medium/tris.md2")) ||
        (weapon_highlight && !Q_strcasecmp(model, "models/weapons/g_rocket/tris.md2"))) {
        highlight->shell = RF_SHELL_RED;
        VectorSet(highlight->color, 1.0f, 0.0f, 0.0f);
        return true;
    }

    if ((ammo_highlight &&
         !Q_strcasecmp(model, "models/items/ammo/bullets/medium/tris.md2")) ||
        (weapon_highlight &&
         (!Q_strcasecmp(model, "models/weapons/g_machn/tris.md2") ||
          !Q_strcasecmp(model, "models/weapons/g_chain/tris.md2")))) {
        highlight->shell = RF_SHELL_BLUE;
        VectorSet(highlight->color, 0.0f, 0.25f, 1.0f);
        return true;
    }

    if ((ammo_highlight &&
         !Q_strcasecmp(model, "models/items/ammo/slugs/medium/tris.md2")) ||
        (weapon_highlight && !Q_strcasecmp(model, "models/weapons/g_rail/tris.md2"))) {
        highlight->shell = RF_SHELL_RED | RF_SHELL_BLUE;
        VectorSet(highlight->color, 0.75f, 0.0f, 1.0f);
        return true;
    }

    if ((ammo_highlight &&
         !Q_strcasecmp(model, "models/items/ammo/grenades/medium/tris.md2")) ||
        (weapon_highlight && !Q_strcasecmp(model, "models/weapons/g_launch/tris.md2"))) {
        highlight->shell = RF_SHELL_RED | RF_SHELL_DOUBLE;
        VectorSet(highlight->color, 1.0f, 0.45f, 0.0f);
        return true;
    }

    if ((ammo_highlight &&
         !Q_strcasecmp(model, "models/items/ammo/cells/medium/tris.md2")) ||
        (weapon_highlight &&
         (!Q_strcasecmp(model, "models/weapons/g_hyperb/tris.md2") ||
          !Q_strcasecmp(model, "models/weapons/g_bfg/tris.md2")))) {
        highlight->shell = RF_SHELL_GREEN;
        VectorSet(highlight->color, 0.3f, 1.0f, 0.0f);
        return true;
    }

    if (ammo_highlight && model_starts_with(model, "models/items/ammo/nuke/")) {
        highlight->shell = RF_SHELL_GREEN;
        VectorSet(highlight->color, 0.45f, 1.0f, 0.15f);
        return true;
    }

    if (ammo_highlight && model_starts_with(model, "models/items/ammo/")) {
        highlight->shell = RF_SHELL_RED | RF_SHELL_DOUBLE;
        VectorSet(highlight->color, 1.0f, 0.75f, 0.0f);
        return true;
    }

    if (weapon_highlight &&
        (model_starts_with(model, "models/weapons/g_blast/") ||
         model_starts_with(model, "models/weapons/g_disint/"))) {
        highlight->shell = RF_SHELL_GREEN | RF_SHELL_BLUE;
        VectorSet(highlight->color, 0.0f, 0.85f, 1.0f);
        return true;
    }

    if (weapon_highlight && model_starts_with(model, "models/weapons/g_")) {
        highlight->shell = RF_SHELL_RED | RF_SHELL_DOUBLE;
        VectorSet(highlight->color, 1.0f, 0.65f, 0.2f);
        return true;
    }

    if (item_highlight && model_starts_with(model, "models/items/quaddama/")) {
        highlight->shell = RF_SHELL_BLUE;
        VectorSet(highlight->color, 0.61f, 0.73f, 1.0f);
        return true;
    }

    if (item_highlight && model_starts_with(model, "models/items/")) {
        highlight->shell = RF_SHELL_RED | RF_SHELL_DOUBLE;
        VectorSet(highlight->color, 1.0f, 0.65f, 0.2f);
        return true;
    }

    return false;
}

static bool CL_GetWeaponHighlightShell(int weapon, item_highlight_t *highlight)
{
    switch (weapon) {
    case 2: // shotgun
    case 3: // super shotgun
        highlight->shell = RF_SHELL_DOUBLE;
        VectorSet(highlight->color, 1.0f, 0.75f, 0.0f);
        return true;
    case 4: // machinegun
    case 5: // chaingun
        highlight->shell = RF_SHELL_BLUE;
        VectorSet(highlight->color, 0.0f, 0.25f, 1.0f);
        return true;
    case 8: // rocket launcher
        highlight->shell = RF_SHELL_RED;
        VectorSet(highlight->color, 1.0f, 0.0f, 0.0f);
        return true;
    case 10: // railgun
        highlight->shell = RF_SHELL_RED | RF_SHELL_BLUE;
        VectorSet(highlight->color, 0.75f, 0.0f, 1.0f);
        return true;
    case 6: // grenade launcher
    case 7: // grenade launcher
        highlight->shell = RF_SHELL_RED | RF_SHELL_DOUBLE;
        VectorSet(highlight->color, 1.0f, 0.45f, 0.0f);
        return true;
    case 9: // hyperblaster
    case 11: // BFG
        highlight->shell = RF_SHELL_GREEN;
        VectorSet(highlight->color, 0.3f, 1.0f, 0.0f);
        return true;
    default:
        highlight->shell = RF_SHELL_RED | RF_SHELL_DOUBLE;
        VectorSet(highlight->color, 1.0f, 0.65f, 0.2f);
        return true;
    }
}

// Highlights pick their shell above and take their color from the skin tint
// of the same model, so that tints and highlights agree.
static bool CL_GetItemHighlight(const centity_state_t *state, item_highlight_t *highlight)
{
    const skin_tint_def_t *def;

    if (!CL_GetItemHighlightShell(state, highlight))
        return false;

    def = CL_FindSkinTint(cl.configstrings[cl.csr.models + state->modelindex]);
    if (def)
        CL_SkinTintHighlightColor(def, highlight->color);
    return true;
}

static bool CL_GetWeaponHighlight(int weapon, item_highlight_t *highlight)
{
    if (!CL_GetWeaponHighlightShell(weapon, highlight))
        return false;

    CL_SkinTintHighlightColor(CL_FindWeaponIndexTint(weapon), highlight->color);
    return true;
}

#define RT_WATER_WAKE_SAMPLE_MSEC 100
#define RT_WATER_WAKE_HOLD_MSEC   150

// The renderer sets QVF_RAYTRACING while Vulkan ray tracing is active; the
// strength cvar is the renderer's, so it only exists once Vulkan has started.
static bool CL_RTWaterWakesEnabled(void)
{
    static cvar_t *water_wakes;

    if (!(r_config.flags & QVF_RAYTRACING))
        return false;
    if (!water_wakes)
        water_wakes = Cvar_FindVar("vk_rt_water_wakes");

    return water_wakes && water_wakes->value > 0.0f;
}

static void CL_AddRTWaterWake(centity_t *cent, const vec3_t origin,
                              int entity_number)
{
    if (!cl.bsp || !CL_RTWaterWakesEnabled()) {
        entity_reset_water_wake(cent, origin);
        return;
    }

    int elapsed = cl.time - cent->rt_water_wake_sample_time;
    if (elapsed < 0) {
        entity_reset_water_wake(cent, origin);
        return;
    }

    if (elapsed >= RT_WATER_WAKE_SAMPLE_MSEC) {
        vec3_t movement;
        VectorSubtract(origin, cent->rt_water_wake_sample_origin, movement);
        movement[2] = 0.0f;
        float distance = VectorLength(movement);

        VectorCopy(origin, cent->rt_water_wake_sample_origin);
        cent->rt_water_wake_sample_time = cl.time;

        if (distance > 96.0f) {
            cent->rt_water_wake_valid = false;
        } else if (distance >= 4.0f) {
            vec3_t start, end;
            trace_t trace;
            VectorCopy(origin, start);
            VectorCopy(origin, end);
            start[2] += max(cent->maxs[2], 32.0f) + 12.0f;
            end[2] += min(cent->mins[2], -24.0f) - 12.0f;

            CL_Trace(&trace, start, end, vec3_origin, vec3_origin,
                     CONTENTS_WATER);
            if (!trace.startsolid && !trace.allsolid &&
                trace.fraction > 0.0f && trace.fraction < 1.0f &&
                trace.plane.normal[2] >= 0.7f) {
                vec3_t above, below;
                VectorMA(trace.endpos, 3.0f, trace.plane.normal, above);
                VectorMA(trace.endpos, -3.0f, trace.plane.normal, below);
                int above_contents = CM_PointContents(above, cl.bsp->nodes,
                                                       cl.csr.extended);
                int below_contents = CM_PointContents(below, cl.bsp->nodes,
                                                       cl.csr.extended);

                if (!(above_contents & MASK_WATER) &&
                    (below_contents & CONTENTS_WATER)) {
                    VectorMA(trace.endpos, 0.75f, trace.plane.normal,
                             cent->rt_water_wake_origin);
                    VectorCopy(trace.plane.normal,
                               cent->rt_water_wake_normal);
                    VectorNormalize(movement);
                    VectorCopy(movement, cent->rt_water_wake_direction);
                    cent->rt_water_wake_scale =
                        Q_clipf(distance / 12.0f, 0.8f, 1.25f);
                    cent->rt_water_wake_seen_time = cl.time;
                    cent->rt_water_wake_valid = true;
                } else {
                    cent->rt_water_wake_valid = false;
                }
            } else {
                cent->rt_water_wake_valid = false;
            }
        }
    }

    if (!cent->rt_water_wake_valid ||
        cl.time - cent->rt_water_wake_seen_time > RT_WATER_WAKE_HOLD_MSEC)
        return;

    entity_t wake = { 0 };
    VectorCopy(cent->rt_water_wake_origin, wake.origin);
    VectorCopy(cent->rt_water_wake_direction, wake.oldorigin);
    VectorCopy(cent->rt_water_wake_normal, wake.angles);
    wake.frame = entity_number;
    wake.rgba.u32 = MakeColor(112, 196, 238, 255);
    wake.flags = RF_EFFECT_ONLY | RF_RT_WATER_WAKE | RF_TRANSLUCENT;
    wake.alpha = 1.0f;
    wake.scale = cent->rt_water_wake_scale;
    V_AddEntity(&wake);
}

/*
===============
CL_AddPacketEntities

===============
*/
static void CL_AddPacketEntities(void)
{
    entity_t                ent;
    const centity_state_t   *s1;
    float                   autorotate, autobob;
    int                     i;
    int                     pnum;
    centity_t               *cent;
    int                     autoanim;
    const clientinfo_t      *ci;
    unsigned int            effects, renderfx;
    bool                    has_alpha, has_trail;
    float                   custom_alpha;
    uint64_t                custom_flags;
    item_highlight_t        item_highlight;
    item_highlight_t        player_highlight;
    skin_tint_t             skin_tint;
    bool                    has_item_highlight;
    bool                    has_player_highlight;
    bool                    has_skin_tint;
    uint64_t                player_powerup_highlight_shell;

    // bonus items rotate at a fixed rate
    autorotate = anglemod(cl.time * 0.1f);

    // brush models can auto animate their frames
    autoanim = cl.time / 500;

    autobob = 5 * sinf(cl.time / 400.0f);

    memset(&ent, 0, sizeof(ent));

    for (pnum = 0; pnum < cl.frame.numEntities; pnum++) {
        i = (cl.frame.firstEntity + pnum) & PARSE_ENTITIES_MASK;
        s1 = &cl.entityStates[i];

        cent = &cl_entities[s1->number];

        has_trail = false;

        effects = s1->effects;
        renderfx = s1->renderfx;
        has_item_highlight = CL_GetItemHighlight(s1, &item_highlight);
        has_player_highlight = CL_GetPlayerHighlight(s1, &player_highlight);
        has_skin_tint = CL_GetModelSkinTint(s1, &skin_tint);
        CL_ClearEntitySkinTint(&ent);
        player_powerup_highlight_shell = CL_GetPlayerPowerupHighlightShell(s1->effects);

        // set frame
        if (effects & EF_ANIM01)
            ent.frame = autoanim & 1;
        else if (effects & EF_ANIM23)
            ent.frame = 2 + (autoanim & 1);
        else if (effects & EF_ANIM_ALL)
            ent.frame = autoanim;
        else if (effects & EF_ANIM_ALLFAST)
            ent.frame = cl.time / 100;
        else
            ent.frame = s1->frame;

        // quad and pent can do different things on client
        if (effects & EF_PENT) {
            effects &= ~EF_PENT;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_RED;
        }

        if (effects & EF_QUAD) {
            effects &= ~EF_QUAD;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_BLUE;
        }

        if (effects & EF_DOUBLE) {
            effects &= ~EF_DOUBLE;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_DOUBLE;
        }

        if (effects & EF_HALF_DAMAGE) {
            effects &= ~EF_HALF_DAMAGE;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_HALF_DAM;
        }

        if (s1->morefx & EFX_DUALFIRE) {
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_LITE_GREEN;
        }

        if (has_item_highlight) {
            effects |= EF_COLOR_SHELL;
            renderfx |= item_highlight.shell;
        }

        if (has_player_highlight) {
            effects |= EF_COLOR_SHELL;
            if (player_powerup_highlight_shell)
                renderfx &= ~RF_SHELL_MASK;
            renderfx |= CL_GetPlayerHighlightShell(player_highlight.shell,
                                                   player_powerup_highlight_shell);
        }

        // optionally remove the glowing effect
        if (cl_noglow->integer && !(renderfx & RF_BEAM))
            renderfx &= ~RF_GLOW;

        ent.oldframe = cent->prev.frame;
        ent.backlerp = 1.0f - cl.lerpfrac;

        if (renderfx & RF_BEAM) {
            // interpolate start and end points for beams
            LerpVector(cent->prev.origin, cent->current.origin,
                       cl.lerpfrac, ent.origin);
            LerpVector(cent->prev.old_origin, cent->current.old_origin,
                       cl.lerpfrac, ent.oldorigin);
        } else {
            if (s1->number == cl.frame.clientNum + 1) {
                // use predicted origin
                VectorCopy(cl.playerEntityOrigin, ent.origin);
                VectorCopy(cl.playerEntityOrigin, ent.oldorigin);
            } else {
                // interpolate origin
                LerpVector(cent->prev.origin, cent->current.origin,
                           cl.lerpfrac, ent.origin);
                VectorCopy(ent.origin, ent.oldorigin);
            }
#if USE_FPS
            // run alias model animation
            if (cent->prev_frame != s1->frame) {
                int delta = cl.time - cent->anim_start;
                float frac;

                if (delta > BASE_FRAMETIME) {
                    cent->prev_frame = s1->frame;
                    frac = 1;
                } else if (delta > 0) {
                    frac = delta * BASE_1_FRAMETIME;
                } else {
                    frac = 0;
                }

                ent.oldframe = cent->prev_frame;
                ent.backlerp = 1.0f - frac;
            }
#endif
        }

        if (s1->modelindex == MODELINDEX_PLAYER && !(renderfx & RF_BEAM))
            CL_AddRTWaterWake(cent, ent.origin, s1->number);

        if (effects & EF_BOB && !cl_nobob->integer) {
            ent.origin[2] += autobob;
            ent.oldorigin[2] += autobob;
        }

        if (!cl_gibs->integer) {
            if (effects & EF_GIB && !(cl.csr.extended && effects & EF_ROCKET))
                goto skip;
            if (effects & EF_GREENGIB)
                goto skip;
        }

        // create a new entity

        if (cl.csr.extended) {
            if (renderfx & RF_FLARE) {
                if (!cl_flares->integer)
                    goto skip;
                float fade_start = s1->modelindex2;
                float fade_end = s1->modelindex3;
                float d = Distance(cl.refdef.vieworg, ent.origin);
                if (d < fade_start)
                    goto skip;
                if (d > fade_end)
                    ent.alpha = 1;
                else
                    ent.alpha = (d - fade_start) / (fade_end - fade_start);
                ent.skin = 0;
                if (renderfx & RF_CUSTOMSKIN && (unsigned)s1->frame < cl.csr.max_images)
                    ent.skin = cl.image_precache[s1->frame];
                if (!ent.skin)
                    ent.skin = cl_img_flare;
                ent.scale = s1->scale ? s1->scale : 1;
                ent.flags = renderfx | RF_TRANSLUCENT;
                if (!s1->skinnum)
                    ent.rgba.u32 = U32_WHITE;
                else
                    ent.rgba.u32 = BigLong(s1->skinnum);
                ent.skinnum = s1->number;
                V_AddEntity(&ent);
                goto skip;
            }

            if (renderfx & RF_CUSTOM_LIGHT) {
                color_t color;
                if (!s1->skinnum)
                    color.u32 = U32_WHITE;
                else
                    color.u32 = BigLong(s1->skinnum);
                V_AddLight(ent.origin, DLIGHT_CUTOFF + s1->frame,
                           color.u8[0] / 255.0f,
                           color.u8[1] / 255.0f,
                           color.u8[2] / 255.0f);
                goto skip;
            }

            if (renderfx & RF_BEAM && s1->modelindex > 1) {
                CL_DrawBeam(ent.oldorigin, ent.origin, cl.model_draw[s1->modelindex]);
                goto skip;
            }
        }

        // tweak the color of beams
        if (renderfx & RF_BEAM) {
            // the four beam colors are encoded in 32 bits of skinnum (hack)
            ent.alpha = 0.30f;
            ent.skinnum = (s1->skinnum >> ((Com_SlowRand() % 4) * 8)) & 0xff;
            ent.model = 0;
        } else {
            // set skin
            if (s1->modelindex == MODELINDEX_PLAYER) {
                // use custom player skin
                ent.skinnum = 0;
                ci = &cl.clientinfo[s1->skinnum & 0xff];
                ent.skin = ci->skin;
                ent.model = ci->model;
                if (!ent.skin || !ent.model) {
                    ent.skin = cl.baseclientinfo.skin;
                    ent.model = cl.baseclientinfo.model;
                    ci = &cl.baseclientinfo;
                }
                if (renderfx & RF_USE_DISGUISE) {
                    char buffer[MAX_QPATH];

                    Q_concat(buffer, sizeof(buffer), "players/", ci->model_name, "/disguise.pcx");
                    ent.skin = R_RegisterSkin(buffer);
                }
            } else {
                ent.skinnum = s1->skinnum;
                ent.skin = 0;
                ent.model = cl.model_draw[s1->modelindex];
                if (ent.model == cl_mod_laser || ent.model == cl_mod_dmspot)
                    renderfx |= RF_NOSHADOW;
            }
        }

        // allow skin override for remaster
        if (cl.csr.extended && renderfx & RF_CUSTOMSKIN && (unsigned)s1->skinnum < cl.csr.max_images) {
            ent.skin = cl.image_precache[s1->skinnum];
            ent.skinnum = 0;
        }

        // only used for black hole model right now, FIXME: do better
        if ((renderfx & RF_TRANSLUCENT) && !(renderfx & RF_BEAM))
            ent.alpha = 0.70f;

        // render effects (fullbright, translucent, etc)
        if (effects & EF_COLOR_SHELL)
            ent.flags = 0;  // renderfx go on color shell entity
        else
            ent.flags = renderfx;

        if (has_item_highlight || has_player_highlight)
            ent.flags |= renderfx & RF_GLOW;

        // calculate angles
        if (effects & EF_ROTATE) {  // some bonus items auto-rotate
            ent.angles[0] = 0;
            ent.angles[1] = autorotate;
            ent.angles[2] = 0;
        } else if (effects & EF_SPINNINGLIGHTS) {
            vec3_t forward;
            vec3_t start;

            ent.angles[0] = 0;
            ent.angles[1] = anglemod(cl.time / 2) + s1->angles[1];
            ent.angles[2] = 180;

            AngleVectors(ent.angles, forward, NULL, NULL);
            VectorMA(ent.origin, 64, forward, start);
            V_AddLight(start, 100, 1, 0, 0);
        } else if (s1->number == cl.frame.clientNum + 1) {
            VectorCopy(cl.playerEntityAngles, ent.angles);      // use predicted angles
        } else { // interpolate angles
            LerpAngles(cent->prev.angles, cent->current.angles,
                       cl.lerpfrac, ent.angles);
            // mimic original ref_gl "leaning" bug (uuugly!)
            if (s1->modelindex == MODELINDEX_PLAYER && cl_rollhack->integer)
                ent.angles[ROLL] = -ent.angles[ROLL];
        }

        if (s1->morefx & EFX_FLASHLIGHT) {
            vec3_t forward, start, end;
            trace_t trace;
            const int mask = CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER;

            if (s1->number == cl.frame.clientNum + 1) {
                VectorMA(cl.refdef.vieworg, 256, cl.v_forward, end);
                VectorCopy(cl.refdef.vieworg, start);
            } else {
                AngleVectors(ent.angles, forward, NULL, NULL);
                VectorMA(ent.origin, 256, forward, end);
                VectorCopy(ent.origin, start);
            }

            CL_Trace(&trace, start, end, vec3_origin, vec3_origin, mask);
            LerpVector(start, end, cent->flashlightfrac, end);
            V_AddLight(end, 256, 1, 1, 1);

            // smooth out distance "jumps"
            CL_AdvanceValue(&cent->flashlightfrac, trace.fraction, 1);
        }

        if (s1->morefx & EFX_GRENADE_LIGHT)
            V_AddLight(ent.origin, 100, 1, 1, 0);

        if (s1->number == cl.frame.clientNum + 1 && !cl.thirdPersonView) {
            if (effects & EF_FLAG1)
                V_AddLight(ent.origin, 225, 1.0f, 0.1f, 0.1f);
            else if (effects & EF_FLAG2)
                V_AddLight(ent.origin, 225, 0.1f, 0.1f, 1.0f);
            else if (effects & EF_TAGTRAIL)
                V_AddLight(ent.origin, 225, 1.0f, 1.0f, 0.0f);
            else if (effects & EF_TRACKERTRAIL)
                V_AddLight(ent.origin, 225, -1.0f, -1.0f, -1.0f);
            goto skip;
        }

        // if set to invisible, skip
        if (!s1->modelindex)
            goto skip;

        if (effects & EF_BFG) {
            ent.flags |= RF_TRANSLUCENT;
            ent.alpha = 0.30f;
        }

        if (effects & EF_PLASMA) {
            ent.flags |= RF_TRANSLUCENT;
            ent.alpha = 0.6f;
        }

        if (effects & EF_SPHERETRANS) {
            ent.flags |= RF_TRANSLUCENT;
            if (effects & EF_TRACKERTRAIL)
                ent.alpha = 0.6f;
            else
                ent.alpha = 0.3f;
        }

        // custom alpha overrides any derived value
        custom_alpha = 1.0f;
        custom_flags = 0;
        has_alpha = false;

        if (s1->alpha) {
            custom_alpha = lerp_entity_alpha(cent);
            has_alpha = true;
        }

        if (s1->number == cl.frame.clientNum + 1 && cl.thirdPersonView && cl.thirdPersonAlpha != 1.0f) {
            custom_alpha *= cl.thirdPersonAlpha;
            has_alpha = true;
        }

        if (has_alpha) {
            ent.alpha = custom_alpha;
            if (custom_alpha == 1.0f)
                ent.flags &= ~RF_TRANSLUCENT;
            else
                ent.flags |= RF_TRANSLUCENT;
            custom_flags = ent.flags & RF_TRANSLUCENT;
        }

        // tracker effect is duplicated for linked models
        if (IS_TRACKER(effects)) {
            ent.flags    |= RF_TRACKER;
            custom_flags |= RF_TRACKER;
        }

        if (has_skin_tint) {
            CL_SetEntitySkinTint(&ent, &skin_tint);
            ent.flags |= RF_SKINTINT;
            custom_flags |= RF_SKINTINT;
        }

        ent.scale = s1->scale;

        // add to refresh list
        V_AddEntity(&ent);

        // color shells generate a separate entity for the main model
        if (effects & EF_COLOR_SHELL) {
            // PMM - at this point, all of the shells have been handled
            // if we're in the rogue pack, set up the custom mixing, otherwise just
            // keep going
            if (!strcmp(fs_game->string, "rogue")) {
                // all of the solo colors are fine.  we need to catch any of the combinations that look bad
                // (double & half) and turn them into the appropriate color, and make double/quad something special
                if (renderfx & RF_SHELL_HALF_DAM) {
                    // ditch the half damage shell if any of red, blue, or double are on
                    if (renderfx & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE))
                        renderfx &= ~RF_SHELL_HALF_DAM;
                }

                if (renderfx & RF_SHELL_DOUBLE) {
                    // lose the yellow shell if we have a red, blue, or green shell
                    if (renderfx & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_GREEN))
                        renderfx &= ~RF_SHELL_DOUBLE;
                    // if we have a red shell, turn it to purple by adding blue
                    if (renderfx & RF_SHELL_RED)
                        renderfx |= RF_SHELL_BLUE;
                    // if we have a blue shell (and not a red shell), turn it to cyan by adding green
                    else if (renderfx & RF_SHELL_BLUE) {
                        // go to green if it's on already, otherwise do cyan (flash green)
                        if (renderfx & RF_SHELL_GREEN)
                            renderfx &= ~RF_SHELL_BLUE;
                        else
                            renderfx |= RF_SHELL_GREEN;
                    }
                }
            }
            ent.flags = renderfx | RF_TRANSLUCENT;
            if (has_item_highlight && !has_player_highlight) {
                ent.flags |= RF_SKINTINT;
                CL_ClearEntitySkinTint(&ent);
                VectorCopy(item_highlight.color, ent.skin_tint);
            }
            if (has_item_highlight || has_player_highlight) {
                float bloom = CL_ItemHighlightBloom();
                if (bloom)
                    ent.flags |= RF_BLOOM_ONLY;
                else
                    ent.flags |= RF_NOBLOOM | RF_NOSHELLSCALE;
                if (bloom <= 1)
                    ent.flags |= RF_NOSHELLSCALE;
                ent.alpha = custom_alpha * (bloom ?
                    CL_ItemHighlightBloomAlpha(bloom) : 0.30f);
            } else {
                ent.alpha = custom_alpha * 0.30f;
            }
            V_AddEntity(&ent);
        }

        ent.skin = 0;       // never use a custom skin on others
        ent.skinnum = 0;
        ent.flags = custom_flags & ~RF_SKINTINT;
        CL_ClearEntitySkinTint(&ent);
        ent.alpha = custom_alpha;

        // duplicate for linked models
        if (s1->modelindex2) {
            item_highlight_t weapon_highlight;
            skin_tint_t weapon_tint;
            bool has_weapon_highlight = false;
            bool has_weapon_tint = false;

            if (s1->modelindex2 == MODELINDEX_PLAYER) {
                // custom weapon
                ci = &cl.clientinfo[s1->skinnum & 0xff];
                i = (s1->skinnum >> 8); // 0 is default weapon model
                if (cl.csr.extended)
                    i &= 0xff;
                has_weapon_highlight = cl_weaponhighlight->integer ?
                    CL_GetWeaponHighlight(i, &weapon_highlight) : false;
                if (i < 0 || i > cl.numWeaponModels - 1)
                    i = 0;
                has_weapon_tint = CL_GetWeaponIndexTint(i, &weapon_tint);
                ent.model = ci->weaponmodel[i];
                if (!ent.model) {
                    if (i != 0)
                        ent.model = ci->weaponmodel[0];
                    if (!ent.model)
                        ent.model = cl.baseclientinfo.weaponmodel[0];
                }
            } else {
                ent.model = cl.model_draw[s1->modelindex2];
                has_weapon_tint = CL_GetWeaponModelTint(
                    cl.configstrings[cl.csr.models + s1->modelindex2],
                    &weapon_tint);
            }

            if (has_weapon_tint) {
                CL_SetEntitySkinTint(&ent, &weapon_tint);
                ent.flags |= RF_SKINTINT;
            }

            // PMM - check for the defender sphere shell .. make it translucent
            if (!Q_strcasecmp(cl.configstrings[cl.csr.models + s1->modelindex2], "models/items/shell/tris.md2")) {
                ent.alpha = custom_alpha * 0.32f;
                ent.flags = RF_TRANSLUCENT;
            }

            V_AddEntity(&ent);

            if (has_weapon_highlight) {
                float bloom = CL_ItemHighlightBloom();
                ent.flags = weapon_highlight.shell | RF_TRANSLUCENT | RF_SKINTINT;
                CL_ClearEntitySkinTint(&ent);
                VectorCopy(weapon_highlight.color, ent.skin_tint);
                if (bloom)
                    ent.flags |= RF_BLOOM_ONLY;
                else
                    ent.flags |= RF_NOBLOOM | RF_NOSHELLSCALE;
                if (bloom <= 1)
                    ent.flags |= RF_NOSHELLSCALE;
                ent.alpha = custom_alpha * (bloom ?
                    CL_ItemHighlightBloomAlpha(bloom) : 0.30f);
                V_AddEntity(&ent);
            }

            //PGM - make sure these get reset.
            ent.flags = custom_flags;
            ent.alpha = custom_alpha;
        }

        if (s1->modelindex3) {
            ent.model = cl.model_draw[s1->modelindex3];
            V_AddEntity(&ent);
        }

        if (s1->modelindex4) {
            ent.model = cl.model_draw[s1->modelindex4];
            V_AddEntity(&ent);
        }

        if (effects & EF_POWERSCREEN) {
            ent.model = cl_mod_powerscreen;
            ent.oldframe = 0;
            ent.frame = 0;
            ent.flags = RF_TRANSLUCENT;
            ent.alpha = custom_alpha * 0.30f;

            // remaster powerscreen is tiny and needs scaling
            if (cl.need_powerscreen_scale) {
                vec3_t forward, mid, tmp;
                VectorCopy(ent.origin, tmp);
                VectorAvg(cent->mins, cent->maxs, mid);
                VectorAdd(ent.origin, mid, ent.origin);
                AngleVectors(ent.angles, forward, NULL, NULL);
                VectorMA(ent.origin, cent->maxs[0], forward, ent.origin);
                ent.scale = cent->radius * 0.8f;
                ent.flags |= RF_FULLBRIGHT;
                V_AddEntity(&ent);
                VectorCopy(tmp, ent.origin);
            } else {
                ent.flags |= RF_SHELL_GREEN;
                V_AddEntity(&ent);
            }
        }

        if (s1->morefx & EFX_HOLOGRAM)
            CL_HologramParticles(ent.origin);

        // add automatic particle trails
        if (!(effects & EF_TRAIL_MASK))
            goto skip;

        if (effects & EF_ROCKET) {
            if (cl.csr.extended && effects & EF_GIB) {
                CL_DiminishingTrail(cent, ent.origin, DT_FIREBALL);
                has_trail = true;
            } else if (!(cl_disable_particles->integer & NOPART_ROCKET_TRAIL)) {
                CL_DiminishingTrail(cent, ent.origin, DT_ROCKET);
                has_trail = true;
            }
            if (has_item_highlight || (cl_dlight_hacks->integer & DLHACK_ROCKET_COLOR))
                V_AddLight(ent.origin, 200, 1, 0.23f, 0);
            else
                V_AddLight(ent.origin, 200, 1, 1, 0);
        } else if (effects & EF_BLASTER) {
            if (effects & EF_TRACKER) {
                CL_BlasterTrail2(cent, ent.origin);
                V_AddLight(ent.origin, 200, 0, 1, 0);
                has_trail = true;
            } else {
                if (!(cl_disable_particles->integer & NOPART_BLASTER_TRAIL)) {
                    CL_BlasterTrail(cent, ent.origin);
                    has_trail = true;
                }
                V_AddLight(ent.origin, 200, 1, 1, 0);
            }
        } else if (effects & EF_HYPERBLASTER) {
            if (effects & EF_TRACKER)
                V_AddLight(ent.origin, 200, 0, 1, 0);
            else
                V_AddLight(ent.origin, 200, 1, 1, 0);
        } else if (effects & EF_GIB) {
            CL_DiminishingTrail(cent, ent.origin, DT_GIB);
            has_trail = true;
        } else if (effects & EF_GRENADE) {
            if (!(cl_disable_particles->integer & NOPART_GRENADE_TRAIL)) {
                CL_DiminishingTrail(cent, ent.origin, DT_GRENADE);
                has_trail = true;
            }
        } else if (effects & EF_FLIES) {
            CL_FlyEffect(cent, ent.origin);
        } else if (effects & EF_BFG) {
            static const uint16_t bfg_lightramp[6] = {300, 400, 600, 300, 150, 75};
            if (effects & EF_ANIM_ALLFAST) {
                CL_BfgParticles(&ent);
                i = 200;
            } else if (cl_smooth_explosions->integer) {
                i = bfg_lightramp[Q_clip(ent.oldframe, 0, 5)] * ent.backlerp +
                    bfg_lightramp[Q_clip(ent.frame,    0, 5)] * (1.0f - ent.backlerp);
            } else {
                i = bfg_lightramp[Q_clip(s1->frame, 0, 5)];
            }
            V_AddLight(ent.origin, i, 0, 1, 0);
        } else if (effects & EF_TRAP) {
            ent.origin[2] += 32;
            CL_TrapParticles(cent, ent.origin);
            i = (Com_SlowRand() % 100) + 100;
            V_AddLight(ent.origin, i, 1, 0.8f, 0.1f);
        } else if (effects & EF_FLAG1) {
            CL_FlagTrail(cent, ent.origin, 242);
            V_AddLight(ent.origin, 225, 1, 0.1f, 0.1f);
            has_trail = true;
        } else if (effects & EF_FLAG2) {
            CL_FlagTrail(cent, ent.origin, 115);
            V_AddLight(ent.origin, 225, 0.1f, 0.1f, 1);
            has_trail = true;
        } else if (effects & EF_TAGTRAIL) {
            CL_TagTrail(cent, ent.origin, 220);
            V_AddLight(ent.origin, 225, 1.0f, 1.0f, 0.0f);
            has_trail = true;
        } else if (effects & EF_TRACKERTRAIL) {
            if (effects & EF_TRACKER) {
                float intensity = 50 + (500 * (sinf(cl.time / 500.0f) + 1.0f));
                V_AddLight(ent.origin, intensity, -1.0f, -1.0f, -1.0f);
            } else {
                CL_Tracker_Shell(cent, ent.origin);
                V_AddLight(ent.origin, 155, -1.0f, -1.0f, -1.0f);
            }
        } else if (effects & EF_TRACKER) {
            CL_TrackerTrail(cent, ent.origin);
            V_AddLight(ent.origin, 200, -1, -1, -1);
            has_trail = true;
        } else if (effects & EF_GREENGIB) {
            CL_DiminishingTrail(cent, ent.origin, DT_GREENGIB);
            has_trail = true;
        } else if (effects & EF_IONRIPPER) {
            CL_IonripperTrail(cent, ent.origin);
            V_AddLight(ent.origin, 100, 1, 0.5f, 0.5f);
            has_trail = true;
        } else if (effects & EF_BLUEHYPERBLASTER) {
            V_AddLight(ent.origin, 200, 0, 0, 1);
        } else if (effects & EF_PLASMA) {
            if (effects & EF_ANIM_ALLFAST) {
                CL_BlasterTrail(cent, ent.origin);
                has_trail = true;
            }
            V_AddLight(ent.origin, 130, 1, 0.5f, 0.5f);
        }

skip:
        if (!has_trail)
            VectorCopy(ent.origin, cent->lerp_origin);
    }
}

static const centity_t *get_player_entity(void)
{
    const centity_t *ent = &cl_entities[cl.frame.clientNum + 1];

    if (ent->serverframe != cl.frame.number)
        return NULL;
    if (!ent->current.modelindex)
        return NULL;

    return ent;
}

static int shell_effect_hack(const centity_t *ent)
{
    int flags = 0;

    if (ent->current.effects & EF_PENT)
        flags |= RF_SHELL_RED;
    if (ent->current.effects & EF_QUAD)
        flags |= RF_SHELL_BLUE;
    if (ent->current.effects & EF_DOUBLE)
        flags |= RF_SHELL_DOUBLE;
    if (ent->current.effects & EF_HALF_DAMAGE)
        flags |= RF_SHELL_HALF_DAM;

    if (cl.csr.extended) {
        if (ent->current.morefx & EFX_DUALFIRE)
            flags |= RF_SHELL_LITE_GREEN;
        if (ent->current.effects & EF_COLOR_SHELL)
            flags |= ent->current.renderfx & RF_SHELL_MASK;
    }

    return flags;
}

/*
==============
CL_AddViewWeapon
==============
*/
static void CL_AddViewWeapon(void)
{
    const centity_t *ent;
    const player_state_t *ps, *ops;
    entity_t    gun;        // view model
    skin_tint_t weapon_tint;
    bool        has_weapon_tint = false;
    int         i, flags;

    // allow the gun to be completely removed
    if (cl_gun->integer < 1) {
        return;
    }

    if (cl_gun->integer == 1) {
        // don't draw gun if in wide angle view
        if (cls.demo.playback && cls.demo.compat && cl.frame.ps.fov > 90) {
            return;
        }
        // don't draw gun if center handed
        if (info_hand->integer == 2) {
            return;
        }
    }

    // find states to interpolate between
    ps = CL_KEYPS;
    ops = CL_OLDKEYPS;

    memset(&gun, 0, sizeof(gun));

    if (gun_model) {
        gun.model = gun_model;  // development tool
    } else {
        gun.model = cl.model_draw[ps->gunindex & GUNINDEX_MASK];
        gun.skinnum = ps->gunindex >> GUNINDEX_BITS;
    }
    if (!gun.model) {
        return;
    }

    if (!gun_model) {
        int modelindex = ps->gunindex & GUNINDEX_MASK;
        has_weapon_tint = CL_GetWeaponModelTint(
            cl.configstrings[cl.csr.models + modelindex], &weapon_tint);
    }

    // set up gun position
    for (i = 0; i < 3; i++) {
        gun.origin[i] = cl.refdef.vieworg[i] + ops->gunoffset[i] +
                        CL_KEYLERPFRAC * (ps->gunoffset[i] - ops->gunoffset[i]);
        gun.angles[i] = cl.refdef.viewangles[i] + LerpAngle(ops->gunangles[i],
                        ps->gunangles[i], CL_KEYLERPFRAC);
    }

    VectorMA(gun.origin, cl_gun_y->value, cl.v_forward, gun.origin);
    VectorMA(gun.origin, cl_gun_x->value, cl.v_right, gun.origin);
    VectorMA(gun.origin, cl_gun_z->value, cl.v_up, gun.origin);

    VectorCopy(gun.origin, gun.oldorigin);      // don't lerp at all

    if (gun_frame) {
        gun.frame = gun_frame;  // development tool
        gun.oldframe = gun_frame;   // development tool
    } else {
        gun.frame = ps->gunframe;
        if (gun.frame == 0) {
            gun.oldframe = 0;   // just changed weapons, don't lerp from old
        } else {
            gun.oldframe = ops->gunframe;
            gun.backlerp = 1.0f - CL_KEYLERPFRAC;
        }
    }

    gun.flags = RF_MINLIGHT | RF_DEPTHHACK | RF_WEAPONMODEL;
    if (has_weapon_tint) {
        CL_SetEntitySkinTint(&gun, &weapon_tint);
        gun.flags |= RF_SKINTINT;
    }
    gun.alpha = Cvar_ClampValue(cl_gunalpha, 0.1f, 1.0f);

    ent = get_player_entity();

    // add alpha from cvar or player entity
    if (ent && gun.alpha == 1.0f)
        gun.alpha = lerp_entity_alpha(ent);

    if (gun.alpha != 1.0f)
        gun.flags |= RF_TRANSLUCENT;

    V_AddEntity(&gun);

    // add shell effect from player entity
    if (ent && (flags = shell_effect_hack(ent))) {
        gun.alpha *= 0.30f;
        gun.flags |= flags | RF_TRANSLUCENT;
        V_AddEntity(&gun);
    }

    // add muzzle flash
    if (!cl.weapon.muzzle.model)
        return;

    if (cl.time - cl.weapon.muzzle.time > 50) {
        cl.weapon.muzzle.model = 0;
        return;
    }

    gun.flags = RF_FULLBRIGHT | RF_DEPTHHACK | RF_WEAPONMODEL | RF_TRANSLUCENT;
    gun.alpha = 1.0f;
    gun.model = cl.weapon.muzzle.model;
    gun.skinnum = 0;
    gun.scale = cl.weapon.muzzle.scale;
    gun.backlerp = 0.0f;
    gun.frame = gun.oldframe = 0;

    vec3_t forward, right, up;
    AngleVectors(gun.angles, forward, right, up);

    VectorMA(gun.origin, cl.weapon.muzzle.offset[0], forward, gun.origin);
    VectorMA(gun.origin, cl.weapon.muzzle.offset[1], right, gun.origin);
    VectorMA(gun.origin, cl.weapon.muzzle.offset[2], up, gun.origin);

    VectorCopy(cl.refdef.viewangles, gun.angles);
    gun.angles[2] += cl.weapon.muzzle.roll;

    V_AddEntity(&gun);
}

static void CL_SetupFirstPersonView(void)
{
    // add kick angles
    if (cl_kickangles->integer) {
        vec3_t kickangles;
        LerpAngles(CL_OLDKEYPS->kick_angles, CL_KEYPS->kick_angles, CL_KEYLERPFRAC, kickangles);
        VectorAdd(cl.refdef.viewangles, kickangles, cl.refdef.viewangles);
    }

    // add the weapon
    CL_AddViewWeapon();

    cl.thirdPersonView = false;
}

// need to interpolate bmodel positions, or third person view would be very jerky
static void CL_LerpedTrace(trace_t *tr, const vec3_t start, const vec3_t end,
                           const vec3_t mins, const vec3_t maxs, int contentmask)
{
    trace_t trace;
    const centity_t *ent;
    const mmodel_t *cmodel;
    vec3_t org, ang;

    // check against world
    CM_BoxTrace(tr, start, end, mins, maxs, cl.bsp->nodes, contentmask, cl.csr.extended);
    tr->ent = (struct edict_s *)cl_entities;
    if (tr->fraction == 0)
        return;     // blocked by the world

    // check all other solid models
    for (int i = 0; i < cl.numSolidEntities; i++) {
        ent = cl.solidEntities[i];

        // special value for bmodel
        if (ent->current.solid != PACKED_BSP)
            continue;

        cmodel = cl.model_clip[ent->current.modelindex];
        if (!cmodel)
            continue;

        LerpVector(ent->prev.origin, ent->current.origin, cl.lerpfrac, org);
        LerpAngles(ent->prev.angles, ent->current.angles, cl.lerpfrac, ang);

        CM_TransformedBoxTrace(&trace, start, end, mins, maxs, cmodel->headnode,
                               contentmask, org, ang, cl.csr.extended);

        CM_ClipEntity(tr, &trace, (struct edict_s *)ent);
    }
}

/*
===============
CL_SetupThirdPersionView
===============
*/
static void CL_SetupThirdPersionView(void)
{
    static const vec3_t mins = { -4, -4, -4 };
    static const vec3_t maxs = {  4,  4,  4 };
    vec3_t focus;
    float fscale, rscale;
    float dist, angle, range;
    trace_t trace;

    // if dead, set a nice view angle
    if (cl.frame.ps.stats[STAT_HEALTH] <= 0) {
        cl.refdef.viewangles[ROLL] = 0;
        cl.refdef.viewangles[PITCH] = 10;
    }

    VectorMA(cl.refdef.vieworg, 512, cl.v_forward, focus);
    cl.refdef.vieworg[2] += 8;

    cl.refdef.viewangles[PITCH] *= 0.5f;
    AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

    angle = DEG2RAD(cl_thirdperson_angle->value);
    range = cl_thirdperson_range->value;
    fscale = cosf(angle);
    rscale = sinf(angle);
    VectorMA(cl.refdef.vieworg, -range * fscale, cl.v_forward, cl.refdef.vieworg);
    VectorMA(cl.refdef.vieworg, -range * rscale, cl.v_right, cl.refdef.vieworg);

    CL_LerpedTrace(&trace, cl.playerEntityOrigin, cl.refdef.vieworg, mins, maxs, CONTENTS_SOLID);
    VectorCopy(trace.endpos, cl.refdef.vieworg);
    cl.thirdPersonAlpha = trace.fraction;

    VectorSubtract(focus, cl.refdef.vieworg, focus);
    dist = sqrtf(focus[0] * focus[0] + focus[1] * focus[1]);

    cl.refdef.viewangles[PITCH] = -RAD2DEG(atan2f(focus[2], dist));
    cl.refdef.viewangles[YAW] -= cl_thirdperson_angle->value;

    cl.thirdPersonView = true;
}

static void CL_FinishViewValues(void)
{
    if (cl_thirdperson->integer && get_player_entity())
        CL_SetupThirdPersionView();
    else
        CL_SetupFirstPersonView();
}

static inline float lerp_client_fov(float ofov, float nfov, float lerp)
{
    if (cls.demo.playback && !cls.demo.compat) {
        int fov = info_fov->integer;

        if (fov < 1)
            fov = 90;
        else if (fov > 160)
            fov = 160;

        if (info_uf->integer & UF_LOCALFOV)
            return fov;

        if (!(info_uf->integer & UF_PLAYERFOV)) {
            if (ofov >= 90)
                ofov = fov;
            if (nfov >= 90)
                nfov = fov;
        }
    }

    return ofov + lerp * (nfov - ofov);
}

static inline void lerp_values(const void *from, const void *to, float lerp, void *out, int count)
{
    float backlerp = 1.0f - lerp;

    for (int i = 0; i < count; i++)
        ((float *)out)[i] = ((const float *)from)[i] * backlerp + ((const float *)to)[i] * lerp;
}

/*
===============
CL_CalcViewValues

Sets cl.refdef view values and sound spatialization params.
Usually called from CL_AddEntities, but may be directly called from the main
loop if rendering is disabled but sound is running.
===============
*/
void CL_CalcViewValues(void)
{
    const player_state_t *ps, *ops;
    vec3_t viewoffset;
    float lerp;

    if (!cl.frame.valid) {
        return;
    }

    // find states to interpolate between
    ps = &cl.frame.ps;
    ops = &cl.oldframe.ps;

    lerp = cl.lerpfrac;

    // calculate the origin
    if (!cls.demo.playback && cl_predict->integer && !(ps->pmove.pm_flags & PMF_NO_PREDICTION)) {
        // use predicted values
        unsigned delta = cls.realtime - cl.predicted_step_time;
        float backlerp = lerp - 1.0f;

        VectorMA(cl.predicted_origin, backlerp, cl.prediction_error, cl.refdef.vieworg);

        // smooth out stair climbing
        if (delta < 100) {
            cl.refdef.vieworg[2] -= cl.predicted_step * (100 - delta) * 0.01f;
        }
    } else {
        // just use interpolated values
        for (int i = 0; i < 3; i++) {
            cl.refdef.vieworg[i] = SHORT2COORD(ops->pmove.origin[i] +
                lerp * (ps->pmove.origin[i] - ops->pmove.origin[i]));
        }
    }

    // if not running a demo or on a locked frame, add the local angle movement
    if (cls.demo.playback) {
        if (cls.key_dest == KEY_GAME && Key_IsDown(K_SHIFT)) {
            VectorCopy(cl.viewangles, cl.refdef.viewangles);
        } else {
            LerpAngles(ops->viewangles, ps->viewangles, lerp,
                       cl.refdef.viewangles);
        }
    } else if (ps->pmove.pm_type < PM_DEAD) {
        // use predicted values
        VectorCopy(cl.predicted_angles, cl.refdef.viewangles);
    } else if (ops->pmove.pm_type < PM_DEAD && cls.serverProtocol > PROTOCOL_VERSION_DEFAULT) {
        // lerp from predicted angles, since enhanced servers
        // do not send viewangles each frame
        LerpAngles(cl.predicted_angles, ps->viewangles, lerp, cl.refdef.viewangles);
    } else {
        // just use interpolated values
        LerpAngles(ops->viewangles, ps->viewangles, lerp, cl.refdef.viewangles);
    }

    // interpolate blend
    if (cl.csr.extended && ops->blend[3])
        lerp_values(ops->blend, ps->blend, lerp, cl.refdef.screen_blend, 4);
    else
        Vector4Copy(ps->blend, cl.refdef.screen_blend);

    if (cl.csr.extended && ops->damage_blend[3])
        lerp_values(ops->damage_blend, ps->damage_blend, lerp, cl.refdef.damage_blend, 4);
    else
        Vector4Copy(ps->damage_blend, cl.refdef.damage_blend);

    // interpolate fog
    if (cl.psFlags & MSG_PS_MOREBITS) {
        lerp_values(&ops->fog, &ps->fog, lerp,
                    &cl.refdef.fog, sizeof(cl.refdef.fog) / sizeof(float));
        // no lerping if moved too far
        if (fabsf(ps->heightfog.start.dist - ops->heightfog.start.dist) > 512 ||
            fabsf(ps->heightfog.end  .dist - ops->heightfog.end  .dist) > 512)
            cl.refdef.heightfog = ps->heightfog;
        else
            lerp_values(&ops->heightfog, &ps->heightfog, lerp,
                        &cl.refdef.heightfog, sizeof(cl.refdef.heightfog) / sizeof(float));
    }

#if USE_FPS
    ps = &cl.keyframe.ps;
    ops = &cl.oldkeyframe.ps;

    lerp = cl.keylerpfrac;
#endif

    // interpolate field of view
    cl.fov_x = lerp_client_fov(ops->fov, ps->fov, lerp);
    cl.fov_y = V_CalcFov(cl.fov_x, 4, 3);

    LerpVector(ops->viewoffset, ps->viewoffset, lerp, viewoffset);

    AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

    VectorCopy(cl.refdef.vieworg, cl.playerEntityOrigin);
    VectorCopy(cl.refdef.viewangles, cl.playerEntityAngles);

    if (cl.playerEntityAngles[PITCH] > 180) {
        cl.playerEntityAngles[PITCH] -= 360;
    }

    cl.playerEntityAngles[PITCH] = cl.playerEntityAngles[PITCH] / 3;

    VectorAdd(cl.refdef.vieworg, viewoffset, cl.refdef.vieworg);

    VectorCopy(cl.refdef.vieworg, listener_origin);
    VectorCopy(cl.v_forward, listener_forward);
    VectorCopy(cl.v_right, listener_right);
    VectorCopy(cl.v_up, listener_up);
}

/*
===============
CL_AddEntities

Emits all entities, particles, and lights to the refresh
===============
*/
void CL_AddEntities(void)
{
    CL_CalcViewValues();
    CL_FinishViewValues();
    CL_AddPacketEntities();
    CL_AddTEnts();
    CL_AddParticles();
    CL_AddDLights();
    CL_AddLightStyles();
    LOC_AddLocationsToScene();
}

/*
===============
CL_GetEntitySoundOrigin

Called to get the sound spatialization origin
===============
*/
void CL_GetEntitySoundOrigin(unsigned entnum, vec3_t org)
{
    const centity_t *ent;
    const mmodel_t  *mod;
    vec3_t          mid;

    if (entnum >= cl.csr.max_edicts)
        Com_Error(ERR_DROP, "%s: bad entity", __func__);

    if (!entnum || entnum == listener_entnum) {
        // should this ever happen?
        VectorCopy(listener_origin, org);
        return;
    }

    // interpolate origin
    // FIXME: what should be the sound origin point for RF_BEAM entities?
    ent = &cl_entities[entnum];
    LerpVector(ent->prev.origin, ent->current.origin, cl.lerpfrac, org);

    // offset the origin for BSP models
    if (ent->current.solid == PACKED_BSP) {
        mod = cl.model_clip[ent->current.modelindex];
        if (mod) {
            VectorAvg(mod->mins, mod->maxs, mid);
            VectorAdd(org, mid, org);
        }
    }
}
