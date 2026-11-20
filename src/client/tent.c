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
// cl_tent.c -- client side temporary entities

#include "client.h"
#include "common/mdfour.h"

qhandle_t   cl_sfx_ric1;
qhandle_t   cl_sfx_ric2;
qhandle_t   cl_sfx_ric3;
qhandle_t   cl_sfx_lashit;
qhandle_t   cl_sfx_spark5;
qhandle_t   cl_sfx_spark6;
qhandle_t   cl_sfx_spark7;
qhandle_t   cl_sfx_railg;
qhandle_t   cl_sfx_rockexp;
qhandle_t   cl_sfx_grenexp;
qhandle_t   cl_sfx_watrexp;

qhandle_t   cl_sfx_lightning;
qhandle_t   cl_sfx_disrexp;

qhandle_t   cl_sfx_hit_marker;

qhandle_t   cl_mod_explode;
qhandle_t   cl_mod_smoke;
qhandle_t   cl_mod_flash;
qhandle_t   cl_mod_parasite_segment;
qhandle_t   cl_mod_grapple_cable;
qhandle_t   cl_mod_explo4;
qhandle_t   cl_mod_bfg_explo;
qhandle_t   cl_mod_powerscreen;
qhandle_t   cl_mod_laser;
qhandle_t   cl_mod_dmspot;

qhandle_t   cl_mod_lightning;
qhandle_t   cl_mod_heatbeam;

qhandle_t   cl_mod_muzzles[MFLASH_TOTAL];

qhandle_t   cl_img_flare;

static cvar_t   *cl_muzzleflashes;
static cvar_t   *cl_hit_markers;

#define MAX_FOOTSTEP_SFX    15

typedef struct {
    int         num_sfx;
    qhandle_t   sfx[MAX_FOOTSTEP_SFX];
} cl_footstep_sfx_t;

static cl_footstep_sfx_t    *cl_footstep_sfx;
static int                  cl_num_footsteps;
static qhandle_t            cl_last_footstep;

extern mtexinfo_t nulltexinfo;

/*
=================
CL_FindFootstepSurface
=================
*/
static int CL_FindFootstepSurface(int entnum)
{
    int footstep_id = FOOTSTEP_ID_DEFAULT;
    centity_t *cent = &cl_entities[entnum];

    // skip if no materials loaded
    if (cl_num_footsteps <= FOOTSTEP_RESERVED_COUNT)
        return footstep_id;

    // allow custom footsteps to be disabled
    if (cl_footsteps->integer >= 2)
        return footstep_id;

    // use an X/Y only mins/maxs copy of the entity,
    // since we don't want it to get caught inside of any geometry above or below
    const vec3_t trace_mins = { cent->mins[0], cent->mins[1], 0 };
    const vec3_t trace_maxs = { cent->maxs[0], cent->maxs[1], 0 };

    // trace start position is the entity's current origin + { 0 0 1 },
    // so that entities with their mins at 0 won't get caught in the floor
    vec3_t trace_start;
    VectorCopy(cent->current.origin, trace_start);
    trace_start[2] += 1;

    // the end of the trace starts down by half of STEPSIZE
    vec3_t trace_end;
    VectorCopy(trace_start, trace_end);
    trace_end[2] -= 9;
    if (cent->current.solid && cent->current.solid != PACKED_BSP) {
        // if the entity is a bbox'd entity, the mins.z is added to the end point as well
        trace_end[2] += cent->mins[2];
    } else {
        // otherwise use a value that should cover every monster in the game
        trace_end[2] -= 66; // should you wonder: monster_guardian is the biggest boi
    }

    // first, a trace done solely against MASK_SOLID
    trace_t tr;
    CL_Trace(&tr, trace_start, trace_end, trace_mins, trace_maxs, MASK_SOLID);

    if (tr.fraction == 1.0f) {
        // if we didn't hit anything, use default step ID
        return footstep_id;
    }

    if (tr.surface != &(nulltexinfo.c)) {
        // copy over the surfaces' step ID
        footstep_id = ((mtexinfo_t *)tr.surface)->step_id;

        // do another trace that ends instead at endpos + { 0 0 1 }, and is against MASK_SOLID | MASK_WATER
        vec3_t new_end;
        VectorCopy(tr.endpos, new_end);
        new_end[2] += 1;

        CL_Trace(&tr, trace_start, new_end, trace_mins, trace_maxs, MASK_SOLID | MASK_WATER);
        // if we hit something else, use that new footstep id instead of the first traces' value
        if (tr.surface != &(nulltexinfo.c))
            footstep_id = ((mtexinfo_t *)tr.surface)->step_id;
    }

    return footstep_id;
}

/*
=================
CL_PlayFootstepSfx
=================
*/
void CL_PlayFootstepSfx(int step_id, int entnum, float volume, float attenuation)
{
    const cl_footstep_sfx_t *sfx;
    qhandle_t footstep_sfx;
    int sfx_num;

    if (!cl_num_footsteps)
        return; // should not really happen

    if (step_id == -1 && CL_WeatherFootstep(entnum, volume, attenuation))
        return;

    if (step_id == -1)
        step_id = CL_FindFootstepSurface(entnum);

    Q_assert((unsigned)step_id < cl_num_footsteps);

    sfx = &cl_footstep_sfx[step_id];
    if (!sfx->num_sfx)
        sfx = &cl_footstep_sfx[0];
    if (!sfx->num_sfx)
        return; // no footsteps, not even fallbacks

    // pick a random footstep sound, but avoid playing the same one twice in a row
    sfx_num = Q_rand_uniform(sfx->num_sfx);
    footstep_sfx = sfx->sfx[sfx_num];
    if (footstep_sfx == cl_last_footstep)
        footstep_sfx = sfx->sfx[(sfx_num + 1) % sfx->num_sfx];

    S_StartSound(NULL, entnum, CHAN_FOOTSTEP, footstep_sfx, volume, attenuation, 0);
    cl_last_footstep = footstep_sfx;
}

/*
=================
CL_RegisterFootstep
=================
*/
static void CL_RegisterFootstep(cl_footstep_sfx_t *sfx, const char *material)
{
    char name[MAX_QPATH];
    size_t len;
    int i;

    Q_assert(!material || *material);

    for (i = 0; i < MAX_FOOTSTEP_SFX; i++) {
        if (material)
            len = Q_snprintf(name, sizeof(name), "#sound/player/steps/%s%i.wav", material, i + 1);
        else
            len = Q_snprintf(name, sizeof(name), "#sound/player/step%i.wav", i + 1);
        Q_assert(len < sizeof(name));
        if (FS_LoadFile(name + 1, NULL) < 0)
            break;
        sfx->sfx[i] = S_RegisterSound(name);
    }

    sfx->num_sfx = i;
}

/*
=================
CL_RegisterFootsteps
=================
*/
static void CL_RegisterFootsteps(void)
{
    mtexinfo_t *tex;
    int i;

    cl_last_footstep = 0;

    Z_Freep(&cl_footstep_sfx);
    if (!cl.bsp) {
        cl_num_footsteps = 0;
        return;
    }

    cl_num_footsteps = BSP_LoadMaterials(cl.bsp);
    Q_assert(cl_num_footsteps >= FOOTSTEP_RESERVED_COUNT);
    cl_footstep_sfx = Z_Malloc(sizeof(cl_footstep_sfx[0]) * cl_num_footsteps);

    for (i = 0; i < cl_num_footsteps; i++)
        cl_footstep_sfx[i].num_sfx = -1;

    // load reserved footsteps
    CL_RegisterFootstep(&cl_footstep_sfx[FOOTSTEP_ID_DEFAULT], NULL);
    CL_RegisterFootstep(&cl_footstep_sfx[FOOTSTEP_ID_LADDER], "ladder");

    // load the rest
    for (i = 0, tex = cl.bsp->texinfo; i < cl.bsp->numtexinfo; i++, tex++) {
        cl_footstep_sfx_t *sfx = &cl_footstep_sfx[tex->step_id];
        if (sfx->num_sfx == -1)
            CL_RegisterFootstep(sfx, tex->material);
    }
}

/*
=================
CL_RegisterTEntSounds
=================
*/
void CL_RegisterTEntSounds(void)
{
    cl_sfx_ric1 = S_RegisterSound("world/ric1.wav");
    cl_sfx_ric2 = S_RegisterSound("world/ric2.wav");
    cl_sfx_ric3 = S_RegisterSound("world/ric3.wav");
    cl_sfx_lashit = S_RegisterSound("weapons/lashit.wav");
    cl_sfx_spark5 = S_RegisterSound("world/spark5.wav");
    cl_sfx_spark6 = S_RegisterSound("world/spark6.wav");
    cl_sfx_spark7 = S_RegisterSound("world/spark7.wav");
    cl_sfx_railg = S_RegisterSound("weapons/railgf1a.wav");
    cl_sfx_rockexp = S_RegisterSound("weapons/rocklx1a.wav");
    cl_sfx_grenexp = S_RegisterSound("weapons/grenlx1a.wav");
    cl_sfx_watrexp = S_RegisterSound("weapons/xpld_wat.wav");

    S_RegisterSound("player/land1.wav");
    S_RegisterSound("player/fall2.wav");
    S_RegisterSound("player/fall1.wav");

    CL_RegisterFootsteps();

    cl_sfx_lightning = S_RegisterSound("weapons/tesla.wav");
    cl_sfx_disrexp = S_RegisterSound("weapons/disrupthit.wav");

    cl_sfx_hit_marker = S_RegisterSound("weapons/marker.wav");
}

static const char *const muzzlenames[MFLASH_TOTAL] = {
    [MFLASH_MACHN]     = "v_machn",
    [MFLASH_SHOTG2]    = "v_shotg2",
    [MFLASH_SHOTG]     = "v_shotg",
    [MFLASH_ROCKET]    = "v_rocket",
    [MFLASH_RAIL]      = "v_rail",
    [MFLASH_LAUNCH]    = "v_launch",
    [MFLASH_ETF_RIFLE] = "v_etf_rifle",
    [MFLASH_DIST]      = "v_dist",
    [MFLASH_BOOMER]    = "v_boomer",
    [MFLASH_BLAST]     = "v_blast",
    [MFLASH_BFG]       = "v_bfg",
    [MFLASH_BEAMER]    = "v_beamer",
};

/*
=================
CL_RegisterTEntModels
=================
*/
void CL_RegisterTEntModels(void)
{
    void *data;
    int len;

    cl_mod_explode = R_RegisterModel("models/objects/explode/tris.md2");
    cl_mod_smoke = R_RegisterModel("models/objects/smoke/tris.md2");
    cl_mod_flash = R_RegisterModel("models/objects/flash/tris.md2");
    cl_mod_parasite_segment = R_RegisterModel("models/monsters/parasite/segment/tris.md2");
    cl_mod_grapple_cable = R_RegisterModel("models/ctf/segment/tris.md2");
    cl_mod_explo4 = R_RegisterModel("models/objects/r_explode/tris.md2");
    cl_mod_bfg_explo = R_RegisterModel("sprites/s_bfg2.sp2");
    cl_mod_powerscreen = R_RegisterModel("models/items/armor/effect/tris.md2");
    cl_mod_laser = R_RegisterModel("models/objects/laser/tris.md2");
    cl_mod_dmspot = R_RegisterModel("models/objects/dmspot/tris.md2");

    cl_mod_lightning = R_RegisterModel("models/proj/lightning/tris.md2");
    cl_mod_heatbeam = R_RegisterModel("models/proj/beam/tris.md2");

    for (int i = 0; i < MFLASH_TOTAL; i++)
        cl_mod_muzzles[i] = R_RegisterModel(va("models/weapons/%s/flash/tris.md2", muzzlenames[i]));

    cl_img_flare = R_RegisterImage("misc/flare.tga", IT_SPRITE, IF_DEFAULT_FLARE);

    // check for remaster powerscreen model (ugly!)
    len = FS_LoadFile("models/items/armor/effect/tris.md2", &data);
    cl.need_powerscreen_scale = len == 2300 && Com_BlockChecksum(data, len) == 0x19fca65b;
    FS_FreeFile(data);
}

/*
==============================================================

EXPLOSION MANAGEMENT

==============================================================
*/

#define MAX_EXPLOSIONS  256

typedef struct {
    enum {
        ex_free,
        ex_misc,
        ex_flash,
        ex_mflash,
        ex_poly,
        ex_light,
        ex_rt_effect
    } type;

    entity_t    ent;
    int         frames;
    float       light;
    vec3_t      lightcolor;
    float       start;
    int         baseframe;
} explosion_t;

static explosion_t  cl_explosions[MAX_EXPLOSIONS];

static void CL_ClearExplosions(void)
{
    memset(cl_explosions, 0, sizeof(cl_explosions));
}

static explosion_t *CL_AllocExplosion(void)
{
    explosion_t *e, *oldest;
    int     i;
    int     time;

    for (i = 0, e = cl_explosions; i < MAX_EXPLOSIONS; i++, e++) {
        if (e->type == ex_free) {
            memset(e, 0, sizeof(*e));
            return e;
        }
    }
// find the oldest explosion
    time = cl.time;
    oldest = cl_explosions;

    for (i = 0, e = cl_explosions; i < MAX_EXPLOSIONS; i++, e++) {
        if (e->start < time) {
            time = e->start;
            oldest = e;
        }
    }
    memset(oldest, 0, sizeof(*oldest));
    return oldest;
}

static explosion_t *CL_PlainExplosion(void)
{
    explosion_t *ex;

    ex = CL_AllocExplosion();
    VectorCopy(te.pos1, ex->ent.origin);
    ex->type = ex_poly;
    ex->ent.flags = RF_FULLBRIGHT;
    ex->start = cl.servertime - CL_FRAMETIME;
    ex->light = 350;
    VectorSet(ex->lightcolor, 1.0f, 0.5f, 0.5f);
    ex->ent.angles[1] = Q_rand() % 360;
    ex->ent.model = cl_mod_explo4;
    ex->baseframe = 15 * (Q_rand() & 1);
    ex->frames = 15;

    return ex;
}

static void CL_BFGExplosion(const vec3_t pos)
{
    explosion_t *ex;

    ex = CL_AllocExplosion();
    VectorCopy(pos, ex->ent.origin);
    ex->type = ex_poly;
    ex->ent.flags = RF_FULLBRIGHT;
    ex->start = cl.servertime - CL_FRAMETIME;
    ex->light = 350;
    VectorSet(ex->lightcolor, 0.0f, 1.0f, 0.0f);
    ex->ent.model = cl_mod_bfg_explo;
    ex->ent.flags |= RF_TRANSLUCENT;
    ex->ent.alpha = 0.30f;
    ex->frames = 4;
}

void CL_AddWeaponMuzzleFX(cl_muzzlefx_t fx, const vec3_t offset, float scale)
{
    if (!cl_muzzleflashes->integer)
        return;
    if (mz.entity != cl.frame.clientNum + 1)
        return;

    Q_assert(fx < q_countof(cl_mod_muzzles));

    if (!cl_mod_muzzles[fx])
        return;

    cl.weapon.muzzle.model = cl_mod_muzzles[fx];
    cl.weapon.muzzle.scale = scale;
    if (fx == MFLASH_MACHN || fx == MFLASH_BEAMER)
        cl.weapon.muzzle.roll = Q_rand() % 360;
    else
        cl.weapon.muzzle.roll = 0;
    VectorCopy(offset, cl.weapon.muzzle.offset);
    cl.weapon.muzzle.time = cl.servertime - CL_FRAMETIME;
}

void CL_AddMuzzleFX(const vec3_t origin, const vec3_t angles, cl_muzzlefx_t fx, int skin, float scale)
{
    explosion_t *ex;

    if (!cl_muzzleflashes->integer)
        return;

    Q_assert(fx < q_countof(cl_mod_muzzles));

    if (!cl_mod_muzzles[fx])
        return;

    ex = CL_AllocExplosion();
    VectorCopy(origin, ex->ent.origin);
    VectorCopy(angles, ex->ent.angles);
    ex->type = ex_mflash;
    ex->ent.flags = RF_TRANSLUCENT | RF_NOSHADOW | RF_FULLBRIGHT;
    ex->ent.alpha = 1.0f;
    ex->start = cl.servertime - CL_FRAMETIME;
    ex->ent.model = cl_mod_muzzles[fx];
    ex->ent.skinnum = skin;
    ex->ent.scale = scale;
    if (fx != MFLASH_BOOMER)
        ex->ent.angles[2] = Q_rand() % 360;
}

/*
=================
CL_SmokeAndFlash
=================
*/
void CL_SmokeAndFlash(const vec3_t origin)
{
    explosion_t *ex;

    ex = CL_AllocExplosion();
    VectorCopy(origin, ex->ent.origin);
    ex->type = ex_misc;
    ex->frames = 4;
    ex->ent.flags = RF_TRANSLUCENT | RF_NOSHADOW;
    ex->start = cl.servertime - CL_FRAMETIME;
    ex->ent.model = cl_mod_smoke;

    ex = CL_AllocExplosion();
    VectorCopy(origin, ex->ent.origin);
    ex->type = ex_flash;
    ex->ent.flags = RF_FULLBRIGHT;
    ex->frames = 2;
    ex->start = cl.servertime - CL_FRAMETIME;
    ex->ent.model = cl_mod_flash;
}

static void CL_AddExplosions(void)
{
    entity_t    *ent;
    int         i;
    explosion_t *ex;
    float       frac;
    int         f;

    for (i = 0, ex = cl_explosions; i < MAX_EXPLOSIONS; i++, ex++) {
        if (ex->type == ex_free)
            continue;

        if (ex->type == ex_mflash) {
            if (cl.time - ex->start > 50)
                ex->type = ex_free;
            else
                V_AddEntity(&ex->ent);
            continue;
        }

        frac = (cl.time - ex->start) * BASE_1_FRAMETIME;
        f = floorf(frac);

        ent = &ex->ent;

        switch (ex->type) {
        case ex_misc:
        case ex_light:
        case ex_rt_effect:
            if (f >= ex->frames - 1) {
                ex->type = ex_free;
                break;
            }
            ent->alpha = 1.0f - frac / (ex->frames - 1);
            break;
        case ex_flash:
            if (f >= 1) {
                ex->type = ex_free;
                break;
            }
            ent->alpha = 1.0f;
            break;
        case ex_poly:
            ent->flags |= RF_EXPLOSION;
            if (f >= ex->frames - 1) {
                ex->type = ex_free;
                break;
            }

            if (cl_smooth_explosions->integer) {
                ent->alpha = 1.0f - frac / (ex->frames - 1);
                ent->flags |= RF_TRANSLUCENT;
            } else {
                ent->alpha = (16.0f - (float)f) / 16.0f;
                ent->alpha = max(ent->alpha, 0.0f);
            }

            if (f < 10) {
                ent->skinnum = (f >> 1);
                if (ent->skinnum < 0)
                    ent->skinnum = 0;
            } else {
                ent->flags |= RF_TRANSLUCENT;
                if (f < 13)
                    ent->skinnum = 5;
                else
                    ent->skinnum = 6;
            }
            break;
        default:
            Q_assert(!"bad type");
        }

        if (ex->type == ex_free)
            continue;

        if (ex->light)
            V_AddLight(ent->origin, ex->light * ent->alpha,
                       ex->lightcolor[0], ex->lightcolor[1], ex->lightcolor[2]);

        if (ex->type != ex_light) {
            if (ex->type != ex_rt_effect)
                VectorCopy(ent->origin, ent->oldorigin);

            if (f < 0)
                f = 0;
            ent->frame = ex->baseframe + f + 1;
            ent->oldframe = ex->baseframe + f;
            ent->backlerp = 1.0f - (frac - f);

            V_AddEntity(ent);
        }
    }
}

/*
==============================================================

LASER MANAGEMENT

==============================================================
*/

#define MAX_LASERS  256

typedef struct {
    vec3_t      start;
    vec3_t      end;
    int         color;
    color_t     rgba;
    int         width;
    int         lifetime, starttime;
    bool        additive;
} laser_t;

static laser_t  cl_lasers[MAX_LASERS];

static void CL_ClearLasers(void)
{
    memset(cl_lasers, 0, sizeof(cl_lasers));
}

static laser_t *CL_AllocLaser(void)
{
    laser_t *l;
    int i;

    for (i = 0, l = cl_lasers; i < MAX_LASERS; i++, l++) {
        if (cl.time - l->starttime >= l->lifetime) {
            memset(l, 0, sizeof(*l));
            l->starttime = cl.time;
            return l;
        }
    }

    return NULL;
}

static void CL_AddLasers(void)
{
    laser_t     *l;
    entity_t    ent;
    int         i;
    int         time;

    memset(&ent, 0, sizeof(ent));

    for (i = 0, l = cl_lasers; i < MAX_LASERS; i++, l++) {
        time = l->lifetime - (cl.time - l->starttime);
        if (time <= 0) {
            continue;
        }

        if (l->color == -1) {
            ent.rgba = l->rgba;
            ent.alpha = (float)time / (float)l->lifetime;
        } else {
            ent.alpha = 0.30f;
        }

        ent.skinnum = l->color;
        ent.flags = RF_TRANSLUCENT | RF_BEAM;
        if (l->additive)
            ent.flags |= RF_BEAM_ADDITIVE;
        VectorCopy(l->start, ent.origin);
        VectorCopy(l->end, ent.oldorigin);
        ent.frame = l->width;

        V_AddEntity(&ent);
    }
}

static void CL_ParseLaser(unsigned colors)
{
    laser_t *l;

    l = CL_AllocLaser();
    if (!l)
        return;

    VectorCopy(te.pos1, l->start);
    VectorCopy(te.pos2, l->end);
    l->lifetime = 100;
    l->color = (colors >> ((Q_rand() % 4) * 8)) & 0xff;
    l->width = 4;
}

/*
==============================================================

BEAM MANAGEMENT

==============================================================
*/

#define MAX_BEAMS   128

typedef struct {
    int         entity;
    int         dest_entity;
    qhandle_t   model;
    int         endtime;
    vec3_t      offset;
    vec3_t      start, end;
} beam_t;

static beam_t   cl_beams[MAX_BEAMS];
static beam_t   cl_playerbeams[MAX_BEAMS];

static void CL_ClearBeams(void)
{
    memset(cl_beams, 0, sizeof(cl_beams));
    memset(cl_playerbeams, 0, sizeof(cl_playerbeams));
}

static void CL_ParseBeam(qhandle_t model)
{
    beam_t  *b;
    int     i;

// override any beam with the same source AND destination entities
    for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++)
        if (b->entity == te.entity1 && b->dest_entity == te.entity2)
            goto override;

// find a free beam
    for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++) {
        if (!b->model || b->endtime < cl.time) {
override:
            b->entity = te.entity1;
            b->dest_entity = te.entity2;
            b->model = model;
            b->endtime = cl.time + 200;
            VectorCopy(te.pos1, b->start);
            VectorCopy(te.pos2, b->end);
            VectorCopy(te.offset, b->offset);
            return;
        }
    }
}

static void CL_ParsePlayerBeam(qhandle_t model)
{
    beam_t  *b;
    int     i;

// override any beam with the same entity
    for (i = 0, b = cl_playerbeams; i < MAX_BEAMS; i++, b++) {
        if (b->entity == te.entity1) {
            b->model = model;
            b->endtime = cl.time + 200;
            VectorCopy(te.pos1, b->start);
            VectorCopy(te.pos2, b->end);
            VectorCopy(te.offset, b->offset);
            return;
        }
    }

// find a free beam
    for (i = 0, b = cl_playerbeams; i < MAX_BEAMS; i++, b++) {
        if (!b->model || b->endtime < cl.time) {
            b->entity = te.entity1;
            b->model = model;
            b->endtime = cl.time + 100;     // PMM - this needs to be 100 to prevent multiple heatbeams
            VectorCopy(te.pos1, b->start);
            VectorCopy(te.pos2, b->end);
            VectorCopy(te.offset, b->offset);
            return;
        }
    }
}

void CL_DrawBeam(const vec3_t start, const vec3_t end, qhandle_t model)
{
    int         i, steps;
    vec3_t      dist, angles;
    entity_t    ent;
    float       d, len, model_length;

    // calculate pitch and yaw
    VectorSubtract(end, start, dist);
    vectoangles2(dist, angles);

    // add new entities for the beams
    d = VectorNormalize(dist);
    if (model == cl_mod_lightning) {
        model_length = 35.0f;
        d -= 20.0f; // correction so it doesn't end in middle of tesla
    } else {
        model_length = 30.0f;
    }
    steps = ceilf(d / model_length);

    memset(&ent, 0, sizeof(ent));
    ent.model = model;

    // PMM - special case for lightning model .. if the real length is shorter than the model,
    // flip it around & draw it from the end to the start.  This prevents the model from going
    // through the tesla mine (instead it goes through the target)
    if ((model == cl_mod_lightning) && (steps <= 1)) {
        VectorCopy(end, ent.origin);
        ent.flags = RF_FULLBRIGHT;
        ent.angles[0] = angles[0];
        ent.angles[1] = angles[1];
        ent.angles[2] = Com_SlowRand() % 360;
        V_AddEntity(&ent);
        return;
    }

    if (steps > 1) {
        len = (d - model_length) / (steps - 1);
        VectorScale(dist, len, dist);
    }

    if (model == cl_mod_lightning) {
        ent.flags = RF_FULLBRIGHT;
        ent.angles[0] = -angles[0];
        ent.angles[1] = angles[1] + 180.0f;
    } else {
        ent.flags = RF_NOSHADOW;
        ent.angles[0] = angles[0];
        ent.angles[1] = angles[1];
    }

    VectorCopy(start, ent.origin);
    for (i = 0; i < steps; i++) {
        ent.angles[2] = Com_SlowRand() % 360;
        V_AddEntity(&ent);
        VectorAdd(ent.origin, dist, ent.origin);
    }
}

/*
=================
CL_AddBeams
=================
*/
static void CL_AddBeams(void)
{
    int         i;
    beam_t      *b;
    vec3_t      org;

// update beams
    for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++) {
        if (!b->model || b->endtime < cl.time)
            continue;

        // if coming from the player, update the start position
        if (b->entity == cl.frame.clientNum + 1)
            VectorAdd(cl.playerEntityOrigin, b->offset, org);
        else
            VectorAdd(b->start, b->offset, org);

        CL_DrawBeam(org, b->end, b->model);
    }
}

/*
=================
CL_AddPlayerBeams

Draw player locked beams.
=================
*/
static void CL_AddPlayerBeams(void)
{
    int         i, j, steps;
    beam_t      *b;
    vec3_t      dist, org;
    float       x, y, z, d;
    entity_t    ent;
    vec3_t      angles;
    float       len;
    int         framenum;
    float       model_length;
    float       hand_multiplier;
    player_state_t  *ps, *ops;

    if (cl_gun->integer == 3)
        hand_multiplier = -1;
    else if (cl_gun->integer == 2)
        hand_multiplier = 1;
    else if (info_hand->integer == 2)
        hand_multiplier = 0;
    else if (info_hand->integer == 1)
        hand_multiplier = -1;
    else
        hand_multiplier = 1;

// update beams
    for (i = 0, b = cl_playerbeams; i < MAX_BEAMS; i++, b++) {
        if (!b->model || b->endtime < cl.time)
            continue;

        // if coming from the player, update the start position
        if (b->entity == cl.frame.clientNum + 1) {
            // set up gun position
            ps = CL_KEYPS;
            ops = CL_OLDKEYPS;

            for (j = 0; j < 3; j++)
                b->start[j] = cl.refdef.vieworg[j] + ops->gunoffset[j] +
                    CL_KEYLERPFRAC * (ps->gunoffset[j] - ops->gunoffset[j]);

            x = b->offset[0];
            y = b->offset[1];
            z = b->offset[2];

            // adjust offset for gun fov
            if (cl_gunfov->value > 0) {
                float fov_x = Cvar_ClampValue(cl_gunfov, 30, 160);
                float fov_y = V_CalcFov(fov_x, 4, 3);

                x *= tanf(cl.fov_x * (M_PIf / 360)) / tanf(fov_x * (M_PIf / 360));
                z *= tanf(cl.fov_y * (M_PIf / 360)) / tanf(fov_y * (M_PIf / 360));
            }

            VectorMA(b->start, hand_multiplier * x, cl.v_right, org);
            VectorMA(org, y, cl.v_forward, org);
            VectorMA(org, z, cl.v_up, org);
            if (hand_multiplier == 0)
                VectorMA(org, -1, cl.v_up, org);

            // calculate pitch and yaw
            VectorSubtract(b->end, org, dist);

            if (b->model != cl_mod_grapple_cable) {
                d = VectorLength(dist);
                VectorScale(cl.v_forward, d, dist);
            }

            // FIXME: use cl.refdef.viewangles?
            vectoangles2(dist, angles);

            // if it's the heatbeam, draw the particle effect
            if (b->model == cl_mod_heatbeam && !sv_paused->integer)
                CL_Heatbeam(org, dist);

            framenum = 1;
        } else {
            VectorCopy(b->start, org);

            // if it's a non-origin offset, it's a player, so use the hardcoded player offset
            if (!VectorEmpty(b->offset)) {
                vec3_t  tmp, f, r, u;

                // calculate pitch and yaw
                VectorSubtract(b->end, org, dist);
                vectoangles2(dist, angles);

                tmp[0] = -angles[0];
                tmp[1] = angles[1] + 180.0f;
                tmp[2] = 0;
                AngleVectors(tmp, f, r, u);

                VectorMA(org, -b->offset[0] + 1, r, org);
                VectorMA(org, -b->offset[1], f, org);
                VectorMA(org, -b->offset[2] - 10, u, org);
            } else if (b->model == cl_mod_heatbeam) {
                // if it's a monster, do the particle effect
                CL_MonsterPlasma_Shell(b->start);
            }

            // calculate pitch and yaw
            VectorSubtract(b->end, org, dist);
            vectoangles2(dist, angles);

            framenum = 2;
        }

        // add new entities for the beams
        d = VectorNormalize(dist);
        if (b->model == cl_mod_heatbeam) {
            model_length = 32.0f;
        } else if (b->model == cl_mod_lightning) {
            model_length = 35.0f;
            d -= 20.0f; // correction so it doesn't end in middle of tesla
        } else {
            model_length = 30.0f;
        }

        // correction for grapple cable model, which has origin in the middle
        if (b->entity == cl.frame.clientNum + 1 && b->model == cl_mod_grapple_cable && hand_multiplier) {
            VectorMA(org, model_length * 0.5f, dist, org);
            d -= model_length * 0.5f;
        }

        steps = ceilf(d / model_length);

        memset(&ent, 0, sizeof(ent));
        ent.model = b->model;

        // PMM - special case for lightning model .. if the real length is shorter than the model,
        // flip it around & draw it from the end to the start.  This prevents the model from going
        // through the tesla mine (instead it goes through the target)
        if ((b->model == cl_mod_lightning) && (steps <= 1)) {
            VectorCopy(b->end, ent.origin);
            ent.flags = RF_FULLBRIGHT;
            ent.angles[0] = angles[0];
            ent.angles[1] = angles[1];
            ent.angles[2] = Com_SlowRand() % 360;
            V_AddEntity(&ent);
            continue;
        }

        if (steps > 1) {
            len = (d - model_length) / (steps - 1);
            VectorScale(dist, len, dist);
        }

        if (b->model == cl_mod_heatbeam) {
            ent.frame = framenum;
            ent.flags = RF_FULLBRIGHT;
            ent.angles[0] = -angles[0];
            ent.angles[1] = angles[1] + 180.0f;
            ent.angles[2] = cl.time % 360;
        } else if (b->model == cl_mod_lightning) {
            ent.flags = RF_FULLBRIGHT;
            ent.angles[0] = -angles[0];
            ent.angles[1] = angles[1] + 180.0f;
        } else {
            ent.flags = RF_NOSHADOW;
            ent.angles[0] = angles[0];
            ent.angles[1] = angles[1];
        }

        VectorCopy(org, ent.origin);
        for (j = 0; j < steps; j++) {
            if (b->model != cl_mod_heatbeam)
                ent.angles[2] = Com_SlowRand() % 360;
            V_AddEntity(&ent);
            VectorAdd(ent.origin, dist, ent.origin);
        }
    }
}


/*
==============================================================

SUSTAIN MANAGEMENT

==============================================================
*/

#define MAX_SUSTAINS    32

static cl_sustain_t     cl_sustains[MAX_SUSTAINS];

static void CL_ClearSustains(void)
{
    memset(cl_sustains, 0, sizeof(cl_sustains));
}

static cl_sustain_t *CL_AllocSustain(void)
{
    cl_sustain_t    *s;
    int             i;

    for (i = 0, s = cl_sustains; i < MAX_SUSTAINS; i++, s++) {
        if (s->id == 0)
            return s;
    }

    return NULL;
}

static void CL_ProcessSustain(void)
{
    cl_sustain_t    *s;
    int             i;

    for (i = 0, s = cl_sustains; i < MAX_SUSTAINS; i++, s++) {
        if (s->id) {
            if ((s->endtime >= cl.time) && (cl.time >= s->nextthink))
                s->think(s);
            else if (s->endtime < cl.time)
                s->id = 0;
        }
    }
}

static void CL_ParseSteam(void)
{
    cl_sustain_t    *s;

    if (te.entity1 == -1) {
        CL_ParticleSteamEffect(te.pos1, te.dir, te.color & 0xff, te.count, te.entity2);
        return;
    }

    s = CL_AllocSustain();
    if (!s)
        return;

    s->id = te.entity1;
    s->count = te.count;
    VectorCopy(te.pos1, s->org);
    VectorCopy(te.dir, s->dir);
    s->color = te.color & 0xff;
    s->magnitude = te.entity2;
    s->endtime = cl.time + te.time;
    s->think = CL_ParticleSteamEffect2;
    s->nextthink = cl.time;
}

static void CL_ParseWidow(void)
{
    cl_sustain_t    *s;

    s = CL_AllocSustain();
    if (!s)
        return;

    s->id = te.entity1;
    VectorCopy(te.pos1, s->org);
    s->endtime = cl.time + 2100;
    s->think = CL_Widowbeamout;
    s->nextthink = cl.time;
}

static void CL_ParseNuke(void)
{
    cl_sustain_t    *s;

    s = CL_AllocSustain();
    if (!s)
        return;

    s->id = 21000;
    VectorCopy(te.pos1, s->org);
    s->endtime = cl.time + 1000;
    s->think = CL_Nukeblast;
    s->nextthink = cl.time;
}

//==============================================================

static color_t  railcore_color;
static color_t  railspiral_color;

static cvar_t *cl_railtrail_type;
static cvar_t *cl_railtrail_time;
static cvar_t *cl_railcore_color;
static cvar_t *cl_railcore_width;
static cvar_t *cl_railcore_glow;
static cvar_t *cl_railspiral_color;
static cvar_t *cl_railspiral_radius;

static void cl_railcore_color_changed(cvar_t *self)
{
    if (!SCR_ParseColor(self->string, &railcore_color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
        Cvar_Reset(self);
        railcore_color.u32 = U32_RED;
    }
}

static void cl_railspiral_color_changed(cvar_t *self)
{
    if (!SCR_ParseColor(self->string, &railspiral_color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
        Cvar_Reset(self);
        railspiral_color.u32 = U32_BLUE;
    }
}

static void CL_RailCore(void)
{
    float glow = Cvar_ClampValue(cl_railcore_glow, 0.0f, 1.0f);
    int width = cl_railcore_width->integer;
    color_t inner = railcore_color;
    int peak = max(inner.u8[0], max(inner.u8[1], inner.u8[2]));
    if (peak > 0 && glow > 0.0f) {
        float white_mix = 0.55f * glow;
        for (int i = 0; i < 3; i++)
            inner.u8[i] = Q_rint(inner.u8[i] * (1.0f - white_mix) +
                                255.0f * white_mix);
    }

    laser_t *l = CL_AllocLaser();
    if (!l)
        return;

    VectorCopy(te.pos1, l->start);
    VectorCopy(te.pos2, l->end);
    l->color = -1;
    l->lifetime = cl_railtrail_time->integer;
    l->width = width;
    l->rgba = inner;

    if (glow <= 0.0f)
        return;

    l = CL_AllocLaser();
    if (!l)
        return;

    VectorCopy(te.pos1, l->start);
    VectorCopy(te.pos2, l->end);
    l->color = -1;
    l->lifetime = cl_railtrail_time->integer;
    // A wider, faint additive layer around the core.
    l->width = width + max(1, (int)ceilf(3.0f * glow));
    l->rgba = railcore_color;
    l->rgba.u8[3] = Q_rint(l->rgba.u8[3] * 0.38f * glow);
    l->additive = true;
}

static void CL_RailSpiral(void)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    vec3_t      right, up;
    int         i;
    float       d, c, s;
    vec3_t      dir;

    VectorCopy(te.pos1, move);
    VectorSubtract(te.pos2, te.pos1, vec);
    len = VectorNormalize(vec);

    MakeNormalVectors(vec, right, up);

    for (i = 0; i < len; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        VectorClear(p->accel);

        d = i * 0.1f;
        c = cosf(d);
        s = sinf(d);

        VectorScale(right, c, dir);
        VectorMA(dir, s, up, dir);

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (cl_railtrail_time->value + frand() * 0.2f);
        p->color = -1;
        p->rgba = railspiral_color;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + dir[j] * cl_railspiral_radius->value;
            p->vel[j] = dir[j] * 6;
        }

        VectorAdd(move, vec, move);
    }
}

/*
Rail trail type 3, an energy bolt. Around the core runs a coil of solid,
glowing rings, slightly tilted, that shoots out from the muzzle behind a
bright flare; each ring flashes white as it appears, then widens and fades.
Lightning jumps from ring to ring and lashes outward, sparks spray off the
coil and the impact, and shockwave rings burst at the muzzle and where the
shot hits. Dynamic lights travel with the coil, so the walls and floor pick
up its color (with ray traced shadows when ray tracing is on).

Everything but the core is redrawn every frame as additive streaks, so it
looks the same in both renderers. Shapes come from a hash of the bolt, the
ring and the time, so the crackle changes at a steady rate whatever the
frame rate, and nothing but the bolt itself has to be stored.
*/

#define MAX_RAIL_BOLTS          16
#define RAIL_BOLT_SPEED         8.0f    // units per millisecond the coil grows out
#define RAIL_RING_SPACING       20.0f   // units between rings
#define RAIL_RING_MAX           256     // rings in one bolt; longer bolts space them out
#define RAIL_RING_RAMP          72.0f   // rings grow to full size over this distance from the muzzle
#define RAIL_RING_SEGS          24      // segments in a nearby ring
#define RAIL_FLICKER_MS         35      // lightning and ring jitter change shape this often
#define RAIL_STREAK_BUDGET      5000    // streaks for all bolts in one frame
#define RAIL_ARC_RESERVE        1200    // of those, kept for lightning and sparks
#define RAIL_MAX_LIGHTS         12      // dynamic lights for all bolts in one frame

typedef struct {
    vec3_t      start, end, dir, right, up;
    vec3_t      normal;         // of the surface hit, for the impact shockwave
    float       len, radius;
    int         starttime, lifetime;
    uint32_t    seed;
    color_t     color;
} rail_bolt_t;

typedef struct {
    vec3_t      center;
    float       radius, tilt, tilt_at, phase;
    float       alpha, flash;
    bool        twin;
    uint32_t    seed;
} rail_ring_t;

static rail_bolt_t  cl_railbolts[MAX_RAIL_BOLTS];
static cvar_t       *cl_railring_radius;
static cvar_t       *cl_railbolt_light;

static uint32_t rail_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

// next value in 0..1 from a hashed sequence
static float rail_rand(uint32_t *state)
{
    *state = rail_hash(*state + 0x9e3779b9);
    return (*state >> 8) * 0x1p-24f;
}

// random offset in the bolt's frame, each axis -scale..scale
static void rail_rand_offset(const rail_bolt_t *b, uint32_t *state, float scale,
                             vec3_t out)
{
    VectorScale(b->right, (rail_rand(state) * 2 - 1) * scale, out);
    VectorMA(out, (rail_rand(state) * 2 - 1) * scale, b->up, out);
    VectorMA(out, (rail_rand(state) * 2 - 1) * scale, b->dir, out);
}

static color_t rail_whiten(color_t c, float f)
{
    f = Q_clipf(f, 0, 1);
    for (int i = 0; i < 3; i++)
        c.u8[i] = Q_rint(c.u8[i] + (255 - c.u8[i]) * f);
    return c;
}

static void rail_streak(const vec3_t a, const vec3_t b, float width,
                        color_t color, float alpha, int *budget)
{
    r_streak_t s;

    if (*budget <= 0 || alpha <= 0.004f)
        return;
    (*budget)--;

    VectorCopy(a, s.start);
    VectorCopy(b, s.end);
    s.width = width;
    s.rgba = color;
    s.rgba.u8[3] = Q_rint(color.u8[3] * min(alpha, 1.0f));
    s.additive = true;
    V_AddStreak(&s);
}

// A glowing segment of solid color: with 3 layers a wide soft halo, a thick
// body and a white-hot center line; fewer layers drop the halo, then the
// center, for distant segments.
static void rail_glow(const vec3_t a, const vec3_t b, color_t color, float alpha,
                      float scale, int layers, int *budget)
{
    if (layers >= 3)
        rail_streak(a, b, 5.0f * scale, color, alpha * 0.22f, budget);
    rail_streak(a, b, 1.7f * scale, color, alpha * 0.95f, budget);
    if (layers >= 2)
        rail_streak(a, b, 0.55f * scale, rail_whiten(color, 0.8f), alpha, budget);
}

// A circle of 'segs' segments around 'center' in the plane of u and v,
// sheared along w so that it tilts towards angle 'tilt_at'. With a jitter
// seed, the radius wavers a little at each point.
static void rail_circle(const vec3_t center, const vec3_t u, const vec3_t v,
                        const vec3_t w, float radius, float tilt, float tilt_at,
                        float phase, int segs, uint32_t jitter, color_t color,
                        float alpha, float scale, int layers, int *budget)
{
    vec3_t pts[RAIL_RING_SEGS];

    segs = Q_clip(segs, 3, RAIL_RING_SEGS);
    if (*budget < segs * layers)
        return;

    for (int k = 0; k < segs; k++) {
        float a = phase + k * (2 * M_PIf / segs);
        float r = radius;
        if (jitter)
            r *= 1 + (rail_rand(&jitter) - 0.5f) * 0.07f;
        VectorMA(center, r * cosf(a), u, pts[k]);
        VectorMA(pts[k], r * sinf(a), v, pts[k]);
        VectorMA(pts[k], tilt * r * cosf(a - tilt_at), w, pts[k]);
    }
    for (int k = 0; k < segs; k++)
        rail_glow(pts[k], pts[(k + 1) % segs], color, alpha, scale, layers, budget);
}

// Jagged lightning from a to b, its middle points thrown sideways by up to
// 'disp', sometimes with a branch.
static void rail_lightning(const rail_bolt_t *b, const vec3_t from,
                           const vec3_t to, int segs, float disp, bool branch,
                           uint32_t *state, float alpha, int *budget)
{
    color_t glow = b->color;
    color_t hot = rail_whiten(b->color, 0.85f);
    vec3_t prev, cur, offset, tip;

    VectorCopy(from, prev);
    for (int k = 1; k <= segs; k++) {
        float t = (float)k / segs;
        LerpVector(from, to, t, cur);
        if (k < segs) {
            rail_rand_offset(b, state, disp * sinf(t * M_PIf), offset);
            VectorAdd(cur, offset, cur);
        }
        rail_streak(prev, cur, 1.5f, glow, alpha * 0.4f, budget);
        rail_streak(prev, cur, 0.45f, hot, alpha, budget);

        if (branch && k < segs && rail_rand(state) < 0.3f) {
            rail_rand_offset(b, state, Distance(from, to) * 0.45f, offset);
            VectorAdd(cur, offset, tip);
            rail_lightning(b, cur, tip, 3, disp * 0.5f, false, state,
                           alpha * 0.7f, budget);
        }
        VectorCopy(cur, prev);
    }
}

// Fades out what is right in front of the viewer, such as the muzzle of
// their own shot, so it doesn't cover the screen.
static float rail_near_fade(const vec3_t org)
{
    return Q_clipf((Distance(org, cl.refdef.vieworg) - 24) / 96, 0, 1);
}

static void CL_ClearRailBolts(void)
{
    memset(cl_railbolts, 0, sizeof(cl_railbolts));
}

// Ring i of the bolt at age 'age': 1 if it is visible, 0 if it has faded,
// -1 if the coil hasn't reached it yet.
static int rail_ring_get(const rail_bolt_t *b, int i, float spacing, int age,
                         rail_ring_t *r)
{
    float d = i * spacing;
    float ring_age = age - d / RAIL_BOLT_SPEED;
    if (ring_age < 0)
        return -1;
    float frac = ring_age / b->lifetime;
    if (frac >= 1)
        return 0;

    uint32_t rs = b->seed ^ rail_hash(i + 1);
    r->tilt = 0.12f + rail_rand(&rs) * 0.3f;
    r->tilt_at = rail_rand(&rs) * 2 * M_PIf;
    float spin = (rail_rand(&rs) - 0.5f) * 8;
    r->phase = rail_rand(&rs) * 2 * M_PIf + spin * ring_age * 0.001f;
    r->twin = rail_rand(&rs) < 0.35f;
    r->seed = rs;

    // pops out in the first few frames, then slowly widens as it fades
    float pop = min(ring_age / 50.0f, 1.0f);
    float ramp = 0.15f + 0.85f * min(d / RAIL_RING_RAMP, 1.0f);
    float fade = 1 - frac;
    r->radius = b->radius * ramp * (0.45f + 0.55f * pop + 0.45f * frac);
    r->flash = max(0.0f, 1 - ring_age / 100.0f);
    r->alpha = fade * sqrtf(fade);
    VectorMA(b->start, d, b->dir, r->center);
    return 1;
}

static void rail_ring_point(const rail_bolt_t *b, const rail_ring_t *r,
                            float a, float scale, vec3_t out)
{
    float radius = r->radius * scale;

    VectorMA(r->center, radius * cosf(a), b->right, out);
    VectorMA(out, radius * sinf(a), b->up, out);
    VectorMA(out, r->tilt * radius * cosf(a - r->tilt_at), b->dir, out);
}

static void CL_RailBolt(void)
{
    rail_bolt_t *b, *oldest = NULL;
    int i;

    for (i = 0, b = cl_railbolts; i < MAX_RAIL_BOLTS; i++, b++) {
        if (cl.time - b->starttime >= b->lifetime)
            break;
        if (!oldest || b->starttime < oldest->starttime)
            oldest = b;
    }
    if (i == MAX_RAIL_BOLTS)
        b = oldest;

    VectorCopy(te.pos1, b->start);
    VectorCopy(te.pos2, b->end);
    VectorSubtract(te.pos2, te.pos1, b->dir);
    b->len = VectorNormalize(b->dir);
    if (b->len < 1) {
        b->lifetime = 0;
        return;
    }
    MakeNormalVectors(b->dir, b->right, b->up);
    b->radius = Cvar_ClampValue(cl_railring_radius, 1, 64);
    b->starttime = cl.time;
    b->lifetime = max(cl_railtrail_time->integer, 100);
    b->seed = Q_rand();
    b->color = railspiral_color;

    // the surface hit, so the impact shockwave lies on it
    VectorNegate(b->dir, b->normal);
    if (cl.bsp) {
        trace_t trace;
        vec3_t from, to;
        VectorMA(b->end, -8, b->dir, from);
        VectorMA(b->end, 2, b->dir, to);
        CL_Trace(&trace, from, to, vec3_origin, vec3_origin, MASK_SOLID);
        if (!trace.allsolid && !trace.startsolid && trace.fraction < 1 &&
            VectorLength(trace.plane.normal) > 0.1f)
            VectorCopy(trace.plane.normal, b->normal);
    }

    // a wide, faint glow around the core
    laser_t *l = CL_AllocLaser();
    if (l) {
        VectorCopy(b->start, l->start);
        VectorCopy(b->end, l->end);
        l->color = -1;
        l->lifetime = b->lifetime;
        l->width = max(cl_railcore_width->integer, 1) + 9;
        l->rgba = railcore_color;
        l->rgba.u8[3] = Q_rint(l->rgba.u8[3] * 0.16f);
        l->additive = true;
    }
}

static void rail_add_light(const vec3_t org, float radius, color_t c, int *lights)
{
    float scale = Cvar_ClampValue(cl_railbolt_light, 0, 1);

    if (*lights <= 0 || radius * scale < 16)
        return;
    (*lights)--;
    V_AddLight(org, radius * scale, c.u8[0] / 255.0f, c.u8[1] / 255.0f,
               c.u8[2] / 255.0f);
}

static void CL_AddRailBolt(const rail_bolt_t *b, int *budget, int *lights)
{
    int age = cl.time - b->starttime;
    int slot = cl.time / RAIL_FLICKER_MS;
    float life = b->lifetime;
    float reach = min(age * RAIL_BOLT_SPEED, b->len);
    float travel = b->len / RAIL_BOLT_SPEED;   // ms for the coil to reach the end
    color_t hot = rail_whiten(b->color, 0.7f);
    rail_ring_t r;

    int rings = min((int)(b->len / RAIL_RING_SPACING), RAIL_RING_MAX);
    float spacing = rings > 0 ? b->len / rings : b->len;

    // the rings; keep some of the budget for lightning and sparks
    int ring_budget = *budget - RAIL_ARC_RESERVE;
    int ring_start = ring_budget;
    int born = 0;
    for (int i = 1; i <= rings; i++) {
        int state = rail_ring_get(b, i, spacing, age, &r);
        if (state < 0)
            break;
        born = i;
        if (!state)
            continue;

        float dist = Distance(r.center, cl.refdef.vieworg);
        int segs = dist < 400 ? RAIL_RING_SEGS : dist < 1100 ? 14 : 8;
        int layers = dist < 400 ? 3 : dist < 1100 ? 2 : 1;
        color_t c = rail_whiten(b->color, 0.6f * r.flash);
        float alpha = r.alpha * (1 + r.flash);
        uint32_t jitter = r.seed ^ rail_hash(slot);

        rail_circle(r.center, b->right, b->up, b->dir, r.radius, r.tilt,
                    r.tilt_at, r.phase, segs, jitter | 1, c, alpha, 1.0f,
                    layers, &ring_budget);
        if (r.twin) {
            vec3_t center;
            VectorMA(r.center, spacing * 0.3f, b->dir, center);
            rail_circle(center, b->right, b->up, b->dir, r.radius * 0.8f,
                        r.tilt, r.tilt_at + 0.5f, -r.phase, max(segs * 3 / 4, 8),
                        jitter ^ 0x55, c, alpha * 0.6f, 0.7f,
                        max(layers - 1, 1), &ring_budget);
        }
    }
    *budget -= ring_start - ring_budget;

    // the flare racing out at the front of the coil
    if (age < travel) {
        vec3_t head, tail;
        VectorMA(b->start, reach, b->dir, head);
        VectorMA(head, -40, b->dir, tail);
        float size = 0.2f + 0.8f * min(reach / RAIL_RING_RAMP, 1.0f);
        float near = rail_near_fade(head);
        rail_streak(tail, head, 9.0f * size, b->color, 0.5f * near, budget);
        rail_streak(tail, head, 3.0f * size, hot, 0.9f * near, budget);
        rail_add_light(head, 220, b->color, lights);
    }

    // shockwaves: one at the muzzle, one on the surface hit
    float muzzle = age / 180.0f;
    if (muzzle < 1) {
        vec3_t center;
        VectorMA(b->start, 6, b->dir, center);
        rail_circle(center, b->right, b->up, b->dir,
                    b->radius * (0.4f + 2.6f * sqrtf(muzzle)), 0, 0, 0, 16, 0,
                    hot, (1 - muzzle) * 0.9f * rail_near_fade(center), 1.2f, 2,
                    budget);
    }
    float impact = (age - travel) / 260.0f;
    if (impact >= 0 && impact < 1) {
        vec3_t u, v, center;
        MakeNormalVectors(b->normal, u, v);
        VectorMA(b->end, 1, b->normal, center);
        rail_circle(center, u, v, b->normal,
                    b->radius * (0.5f + 4.0f * sqrtf(impact)), 0, 0, 0, 20, 0,
                    b->color, (1 - impact), 1.4f, 3, budget);
        VectorMA(center, 4, b->normal, center);
        rail_add_light(center, 260 * (1 - impact * impact), b->color, lights);
    }

    // light along the coil, fading with it
    float fade = 1 - age / life;
    if (reach > 96 && fade > 0) {
        for (int k = 1; k <= 2; k++) {
            vec3_t org;
            VectorMA(b->start, reach * k / 3, b->dir, org);
            rail_add_light(org, 200 * fade * fade, b->color, lights);
        }
    }

    // Lightning, reshaped every flicker and dying out before the rings do:
    // jumps from one ring to the next, and tendrils lashing outward.
    float arc_fade = 1 - age / (life * 0.75f);
    if (arc_fade > 0 && born > 1) {
        uint32_t as = b->seed ^ rail_hash(slot * 31 + 17);
        int arcs = Q_clip(born * 2 / 3, 4, 56) * arc_fade + 1;
        for (int i = 0; i < arcs && *budget > 0; i++) {
            int j = 1 + (int)(rail_rand(&as) * (born - 1));
            if (rail_ring_get(b, j, spacing, age, &r) <= 0)
                continue;

            float a = rail_rand(&as) * 2 * M_PIf;
            float alpha = arc_fade * (0.7f + rail_rand(&as) * 0.5f);
            vec3_t from, to;
            rail_ring_point(b, &r, a, 1, from);

            if (rail_rand(&as) < 0.55f) {
                rail_ring_t next;
                if (rail_ring_get(b, j + 1, spacing, age, &next) <= 0)
                    continue;
                rail_ring_point(b, &next, a + (rail_rand(&as) - 0.5f) * 1.6f, 1, to);
                rail_lightning(b, from, to, 5, r.radius * 0.35f, true, &as,
                               alpha, budget);
            } else {
                rail_ring_point(b, &r, a + (rail_rand(&as) - 0.5f) * 0.8f,
                                1.6f + rail_rand(&as) * 1.2f, to);
                VectorMA(to, (rail_rand(&as) - 0.5f) * 24, b->dir, to);
                rail_lightning(b, from, to, 4, r.radius * 0.3f, true, &as,
                               alpha, budget);
            }
        }
    }

    // Sparks: fast streaks thrown off the coil as it passes, and a spray from
    // the impact, falling as they fade. Each follows a path fixed at the
    // shot, so they need no storage.
    int sparks = Q_clip(b->len / 24, 12, 64);
    for (int i = 0; i < sparks + 28 && *budget > 1; i++) {
        uint32_t ss = b->seed ^ rail_hash(0x5a17 + i);
        bool at_impact = i >= sparks;
        float d = at_impact ? b->len : rail_rand(&ss) * b->len;
        float t = (age - d / RAIL_BOLT_SPEED) * 0.001f;
        float spark_life = 0.25f + rail_rand(&ss) * 0.45f;
        if (t < 0 || t >= spark_life)
            continue;

        vec3_t org, vel, pos, tail;
        if (at_impact) {
            VectorMA(b->end, 1, b->normal, org);
            rail_rand_offset(b, &ss, 220, vel);
            VectorMA(vel, 80 + rail_rand(&ss) * 200, b->normal, vel);
        } else {
            float a = rail_rand(&ss) * 2 * M_PIf;
            float ramp = 0.15f + 0.85f * min(d / RAIL_RING_RAMP, 1.0f);
            vec3_t radial;
            VectorScale(b->right, cosf(a), radial);
            VectorMA(radial, sinf(a), b->up, radial);
            VectorMA(b->start, d, b->dir, org);
            VectorMA(org, b->radius * ramp, radial, org);
            VectorScale(radial, 120 + rail_rand(&ss) * 220, vel);
            VectorMA(vel, (rail_rand(&ss) - 0.5f) * 160, b->dir, vel);
            vel[2] += 40;
        }

        float gravity = PARTICLE_GRAVITY * 10;
        float t0 = max(t - 0.03f, 0.0f);
        VectorMA(org, t, vel, pos);
        pos[2] -= 0.5f * gravity * t * t;
        VectorMA(org, t0, vel, tail);
        tail[2] -= 0.5f * gravity * t0 * t0;

        uint32_t fs = ss ^ rail_hash(slot);
        float alpha = (1 - t / spark_life) * (0.6f + 0.4f * rail_rand(&fs));
        rail_streak(tail, pos, 1.3f, b->color, alpha * 0.4f, budget);
        rail_streak(tail, pos, 0.4f, hot, alpha, budget);
    }
}

static void CL_AddRailBolts(void)
{
    int budget = RAIL_STREAK_BUDGET;
    int lights = RAIL_MAX_LIGHTS;
    rail_bolt_t *b;
    int i;

    for (i = 0, b = cl_railbolts; i < MAX_RAIL_BOLTS; i++, b++) {
        if (cl.time - b->starttime >= b->lifetime || cl.time < b->starttime)
            continue;
        CL_AddRailBolt(b, &budget, &lights);
    }
}

static void CL_RailTrail(void)
{
    if (!cl_railtrail_type->integer && te.type != TE_RAILTRAIL2) {
        CL_OldRailTrail();
    } else {
        if (cl_railcore_width->integer > 0) {
            CL_RailCore();
        }
        if (cl_railtrail_type->integer == 2) {
            CL_RailSpiral();
        } else if (cl_railtrail_type->integer >= 3) {
            CL_RailBolt();
        }
    }
}

static void dirtoangles(const vec3_t dir, vec3_t angles)
{
    angles[0] = RAD2DEG(acosf(Q_clipf(dir[2], -1.0f, 1.0f)));
    if (dir[0])
        angles[1] = RAD2DEG(atan2f(dir[1], dir[0]));
    else if (dir[1] > 0)
        angles[1] = 90;
    else if (dir[1] < 0)
        angles[1] = 270;
    else
        angles[1] = 0;
}

// Effects that only the Vulkan renderer draws, and only with ray tracing.
// Without it they would just take explosion slots from real explosions.
static bool CL_RTEffectsActive(void)
{
    return r_config.flags & QVF_RAYTRACING;
}

static explosion_t *CL_RTEffect(const vec3_t origin, const vec3_t normal,
                                uint64_t effect_flag, uint32_t rgba,
                                float scale, int frames)
{
    explosion_t *ex;
    vec3_t direction;

    if (!CL_RTEffectsActive())
        return NULL;

    ex = CL_AllocExplosion();
    VectorCopy(origin, ex->ent.origin);
    VectorCopy(normal, direction);
    if (VectorNormalize(direction) <= 0.0f)
        VectorSet(direction, 0.0f, 0.0f, 1.0f);
    VectorCopy(direction, ex->ent.oldorigin);
    ex->type = ex_rt_effect;
    ex->ent.flags = RF_EFFECT_ONLY | effect_flag | RF_TRANSLUCENT;
    ex->ent.rgba.u32 = rgba;
    ex->ent.skinnum = -1;
    ex->ent.scale = scale;
    ex->ent.alpha = 1.0f;
    ex->start = cl.servertime - CL_FRAMETIME;
    ex->frames = frames;
    return ex;
}

static void CL_RTRailIonization(void)
{
    vec3_t direction, origin, normal, trace_start, trace_end;

    if (!CL_RTEffectsActive())
        return;
    VectorSubtract(te.pos2, te.pos1, direction);
    if (VectorNormalize(direction) <= 1.0f)
        return;

    VectorCopy(te.pos2, origin);
    VectorNegate(direction, normal);
    if (cl.bsp) {
        trace_t trace;
        VectorMA(te.pos2, -8.0f, direction, trace_start);
        VectorMA(te.pos2, 2.0f, direction, trace_end);
        CL_Trace(&trace, trace_start, trace_end, vec3_origin, vec3_origin,
                 MASK_SOLID);
        if (!trace.allsolid && !trace.startsolid && trace.fraction < 1.0f &&
            VectorLength(trace.plane.normal) > 0.1f) {
            VectorMA(trace.endpos, 0.75f, trace.plane.normal, origin);
            VectorCopy(trace.plane.normal, normal);
        }
    }

    explosion_t *ex = CL_RTEffect(origin, normal,
                                  RF_RT_RAIL_IONIZATION,
                                  railcore_color.u32,
                                  railspiral_color.u8[3] / 255.0f, 4);
    if (!ex)
        return;
    ex->ent.angles[0] = railspiral_color.u8[0] / 255.0f;
    ex->ent.angles[1] = railspiral_color.u8[1] / 255.0f;
    ex->ent.angles[2] = railspiral_color.u8[2] / 255.0f;
}

static explosion_t *CL_RTImpact(const vec3_t origin, const vec3_t normal,
                                uint32_t rgba, float scale)
{
    return CL_RTEffect(origin, normal, RF_RT_IMPACT, rgba, scale, 5);
}

static void CL_RTElectricFilaments(const vec3_t origin, const vec3_t normal,
                                   uint32_t rgba, float scale)
{
    explosion_t *ex = CL_RTEffect(origin, normal, RF_RT_ELECTRIC, rgba,
                                  scale, 5);
    if (!ex)
        return;
    ex->ent.angles[0] = 4.0f;
    ex->ent.angles[1] = Q_rand() & 255;
}

static void CL_RTSplashRipple(const vec3_t origin, const vec3_t normal,
                              int splash, int count)
{
    uint32_t rgba;

    switch (splash) {
    case SPLASH_BLUE_WATER:
        rgba = MakeColor(112, 196, 238, 255);
        break;
    case SPLASH_BROWN_WATER:
        rgba = MakeColor(194, 156, 106, 255);
        break;
    case SPLASH_SLIME:
        rgba = MakeColor(104, 226, 76, 255);
        break;
    case SPLASH_LAVA:
        rgba = MakeColor(255, 112, 28, 255);
        break;
    default:
        return;
    }

    float scale = 0.80f + Q_clipf(count / 16.0f, 0.0f, 1.0f) * 0.40f;
    explosion_t *ex = CL_RTEffect(origin, normal, RF_RT_SPLASH_RIPPLE,
                                  rgba, scale, 10);
    if (!ex)
        return;
    ex->ent.skinnum = splash;
}

void CL_RTTeleportVortex(const vec3_t origin, rt_teleport_type_t type)
{
    static const vec3_t up = { 0.0f, 0.0f, 1.0f };
    uint32_t rgba;
    float scale;
    int frames;

    switch (type) {
    case RT_TELEPORT_STANDARD:
        rgba = MakeColor(78, 188, 255, 255);
        scale = 1.0f;
        frames = 12;
        break;
    case RT_TELEPORT_DBALL:
        rgba = MakeColor(190, 78, 255, 255);
        scale = 1.15f;
        frames = 13;
        break;
    case RT_TELEPORT_BOSS:
        rgba = MakeColor(255, 112, 92, 255);
        scale = 1.8f;
        frames = 16;
        break;
    default:
        return;
    }

    explosion_t *ex = CL_RTEffect(origin, up, RF_RT_TELEPORT, rgba,
                                  scale, frames);
    if (!ex)
        return;
    ex->ent.skinnum = type;
    ex->ent.angles[0] = frames - 1;
}

void CL_RTItemRespawn(const vec3_t origin)
{
    static const vec3_t up = { 0.0f, 0.0f, 1.0f };
    explosion_t *ex = CL_RTEffect(origin, up, RF_RT_ITEM_RESPAWN,
                                  MakeColor(88, 255, 132, 255), 1.0f, 11);
    if (!ex)
        return;
    ex->ent.angles[0] = 10.0f;
}

void CL_RTLandingDust(int entnum, rt_landing_type_t type)
{
    if (!CL_RTEffectsActive() || (unsigned)entnum >= MAX_EDICTS || !cl.bsp)
        return;

    const centity_t *cent = &cl_entities[entnum];
    const vec3_t trace_mins = { cent->mins[0], cent->mins[1], 0.0f };
    const vec3_t trace_maxs = { cent->maxs[0], cent->maxs[1], 0.0f };
    vec3_t start, end;

    VectorCopy(cent->current.origin, start);
    start[2] += 1.0f;
    VectorCopy(start, end);
    end[2] -= 9.0f;
    if (cent->current.solid && cent->current.solid != PACKED_BSP)
        end[2] += cent->mins[2];
    else
        end[2] -= 66.0f;

    trace_t trace;
    CL_Trace(&trace, start, end, trace_mins, trace_maxs, MASK_SOLID);
    if (trace.allsolid || trace.startsolid || trace.fraction == 1.0f ||
        trace.plane.normal[2] < 0.45f)
        return;

    vec3_t origin;
    VectorMA(trace.endpos, 1.5f, trace.plane.normal, origin);
    float scale = type == RT_LANDING_FAR ? 1.3f :
                  type == RT_LANDING_NORMAL ? 1.0f : 0.7f;
    explosion_t *ex = CL_RTEffect(origin, trace.plane.normal,
                                  RF_RT_LANDING_DUST,
                                  MakeColor(154, 144, 126, 255), scale, 7);
    if (!ex)
        return;
    ex->ent.skinnum = type & RT_LANDING_SEVERITY_MASK;
    if (entnum == cl.frame.clientNum + 1)
        ex->ent.skinnum |= RT_LANDING_LOCAL;
    ex->ent.angles[0] = 6.0f;
    ex->ent.angles[1] = Q_rand() & 255;
}

void CL_RTEnergyCollapse(const vec3_t origin,
                         rt_energy_collapse_type_t type)
{
    static const vec3_t up = { 0.0f, 0.0f, 1.0f };
    uint32_t rgba;
    float scale;
    int frames;

    switch (type) {
    case RT_ENERGY_BFG_CORE:
        rgba = MakeColor(76, 255, 104, 255);
        scale = 1.5f;
        frames = 9;
        break;
    case RT_ENERGY_BFG_SECONDARY:
        rgba = MakeColor(104, 230, 116, 255);
        scale = 0.75f;
        frames = 7;
        break;
    case RT_ENERGY_TRACKER:
        rgba = MakeColor(192, 82, 255, 255);
        scale = 1.0f;
        frames = 7;
        break;
    default:
        return;
    }

    explosion_t *ex = CL_RTEffect(origin, up, RF_RT_ENERGY_COLLAPSE,
                                  rgba, scale, frames);
    if (!ex)
        return;
    ex->ent.skinnum = type;
    ex->ent.angles[0] = frames - 1;
    ex->ent.angles[1] = Q_rand() & 255;
}

/*
=================
CL_ParseTEnt
=================
*/
static const byte splash_color[] = {0x00, 0xe0, 0xb0, 0x50, 0xd0, 0xe0, 0xe8};

static bool CL_IsSelfBloodImpact(const vec3_t origin)
{
    const centity_t *ent;
    vec3_t player_origin;
    int entnum, i;

    if (!VALIDATE_CLIENTNUM(&cl.csr, cl.frame.clientNum))
        return false;

    entnum = cl.frame.clientNum + 1;
    ent = &cl_entities[entnum];
    if (!ent->current.solid || ent->current.solid == PACKED_BSP)
        return false;

    VectorScale(cl.frame.ps.pmove.origin, 0.125f, player_origin);
    for (i = 0; i < 3; i++) {
        // Network positions are quantized to 1/8th of a unit.
        if (origin[i] < player_origin[i] + ent->mins[i] - 0.125f ||
            origin[i] > player_origin[i] + ent->maxs[i] + 0.125f)
            return false;
    }

    return true;
}

void CL_ParseTEnt(void)
{
    explosion_t *ex;
    int r;

    switch (te.type) {
    case TE_BLOOD:          // bullet hitting flesh
        if (!(cl_disable_particles->integer & NOPART_BLOOD) &&
            !(cl_disable_self_blood->integer && CL_IsSelfBloodImpact(te.pos1)))
            CL_ParticleEffect(te.pos1, te.dir, 0xe8, 60);
        break;

    case TE_GUNSHOT:            // bullet hitting wall
    case TE_SPARKS:
    case TE_BULLET_SPARKS:
        if (te.type == TE_GUNSHOT)
            CL_ParticleEffect(te.pos1, te.dir, 0, 40);
        else
            CL_ParticleEffect(te.pos1, te.dir, 0xe0, 6);

        CL_RTImpact(te.pos1, te.dir, MakeColor(255, 154, 48, 255),
                    te.type == TE_SPARKS ? 0.75f : 0.9f);

        if (te.type != TE_SPARKS) {
            CL_SmokeAndFlash(te.pos1);

            // impact sound
            r = Q_rand() & 15;
            if (r == 1)
                S_StartSound(te.pos1, 0, 0, cl_sfx_ric1, 1, ATTN_NORM, 0);
            else if (r == 2)
                S_StartSound(te.pos1, 0, 0, cl_sfx_ric2, 1, ATTN_NORM, 0);
            else if (r == 3)
                S_StartSound(te.pos1, 0, 0, cl_sfx_ric3, 1, ATTN_NORM, 0);
        }
        break;

    case TE_SCREEN_SPARKS:
    case TE_SHIELD_SPARKS:
        if (te.type == TE_SCREEN_SPARKS)
            CL_ParticleEffect(te.pos1, te.dir, 0xd0, 40);
        else
            CL_ParticleEffect(te.pos1, te.dir, 0xb0, 40);
        CL_RTImpact(te.pos1, te.dir,
                    te.type == TE_SCREEN_SPARKS ?
                        MakeColor(255, 90, 62, 255) :
                        MakeColor(90, 130, 255, 255),
                    1.0f);
        CL_RTElectricFilaments(te.pos1, te.dir,
                    te.type == TE_SCREEN_SPARKS ?
                        MakeColor(255, 104, 64, 255) :
                        MakeColor(104, 132, 255, 255),
                    1.0f);
        //FIXME : replace or remove this sound
        S_StartSound(te.pos1, 0, 257, cl_sfx_lashit, 1, ATTN_NORM, 0);
        break;

    case TE_SHOTGUN:            // bullet hitting wall
        CL_ParticleEffect(te.pos1, te.dir, 0, 20);
        CL_SmokeAndFlash(te.pos1);
        CL_RTImpact(te.pos1, te.dir, MakeColor(255, 148, 42, 255), 0.85f);
        break;

    case TE_SPLASH:         // bullet hitting water
        if (cl.csr.extended && te.color == SPLASH_ELECTRIC_N64) {
            CL_ParticleEffect(te.pos1, te.dir, 0x6c, te.count / 2);
            CL_ParticleEffect(te.pos1, te.dir, 0xb0, (te.count + 1) / 2);
            te.color = SPLASH_SPARKS;
        } else {
            if (te.color >= q_countof(splash_color))
                r = 0x00;
            else
                r = splash_color[te.color];
            CL_ParticleEffect(te.pos1, te.dir, r, te.count);
        }

        CL_RTSplashRipple(te.pos1, te.dir, te.color, te.count);

        if (te.color == SPLASH_SPARKS) {
            r = Q_rand() & 3;
            if (r == 0)
                S_StartSound(te.pos1, 0, 0, cl_sfx_spark5, 1, ATTN_STATIC, 0);
            else if (r == 1)
                S_StartSound(te.pos1, 0, 0, cl_sfx_spark6, 1, ATTN_STATIC, 0);
            else
                S_StartSound(te.pos1, 0, 0, cl_sfx_spark7, 1, ATTN_STATIC, 0);
        }
        break;

    case TE_LASER_SPARKS:
        CL_ParticleEffect2(te.pos1, te.dir, te.color, te.count);
        ex = CL_RTImpact(te.pos1, te.dir, U32_WHITE, 0.9f);
        if (ex)
            ex->ent.skinnum = te.color & 0xff;
        break;

    case TE_BLUEHYPERBLASTER:   // broken version
        CL_BlasterParticles(te.pos1, te.pos2);
        CL_RTImpact(te.pos1, te.pos2, MakeColor(72, 132, 255, 255), 1.0f);
        break;

    case TE_BLUEHYPERBLASTER_2: // fixed version
        CL_BlasterParticles(te.pos1, te.dir);
        CL_RTImpact(te.pos1, te.dir, MakeColor(72, 132, 255, 255), 1.0f);
        break;

    case TE_BLASTER:            // blaster hitting wall
    case TE_BLASTER2:           // green blaster hitting wall
    case TE_FLECHETTE:          // flechette
        ex = CL_AllocExplosion();
        VectorCopy(te.pos1, ex->ent.origin);
        dirtoangles(te.dir, ex->ent.angles);
        ex->type = ex_misc;
        ex->ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
        switch (te.type) {
        case TE_BLASTER:
            CL_BlasterParticles(te.pos1, te.dir);
            ex->lightcolor[0] = 1;
            ex->lightcolor[1] = 1;
            CL_RTImpact(te.pos1, te.dir, MakeColor(255, 210, 64, 255), 1.1f);
            break;
        case TE_BLASTER2:
            CL_BlasterParticles2(te.pos1, te.dir, 0xd0);
            ex->ent.skinnum = 1;
            ex->lightcolor[1] = 1;
            CL_RTImpact(te.pos1, te.dir, MakeColor(76, 255, 92, 255), 1.1f);
            break;
        case TE_FLECHETTE:
            CL_BlasterParticles2(te.pos1, te.dir, 0x6f);  // 75
            ex->ent.skinnum = 2;
            VectorSet(ex->lightcolor, 0.19f, 0.41f, 0.75f);
            CL_RTImpact(te.pos1, te.dir, MakeColor(72, 142, 255, 255), 1.1f);
            break;
        }
        ex->start = cl.servertime - CL_FRAMETIME;
        ex->light = 150;
        ex->ent.model = cl_mod_explode;
        ex->frames = 4;
        S_StartSound(te.pos1,  0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
        break;

    case TE_RAILTRAIL:          // railgun effect
    case TE_RAILTRAIL2:
        CL_RailTrail();
        CL_RTRailIonization();
        S_StartSound(te.pos2, 0, 0, cl_sfx_railg, 1, ATTN_NORM, 0);
        break;

    case TE_GRENADE_EXPLOSION:
    case TE_GRENADE_EXPLOSION_WATER:
        ex = CL_PlainExplosion();
        ex->frames = 19;
        ex->baseframe = 30;
        if (cl_disable_explosions->integer & NOEXP_GRENADE)
            ex->type = ex_light;

        if (!(cl_disable_particles->integer & NOPART_GRENADE_EXPLOSION))
            CL_ExplosionParticles(te.pos1);

        if (cl_dlight_hacks->integer & DLHACK_SMALLER_EXPLOSION)
            ex->light = 200;

        if (te.type == TE_GRENADE_EXPLOSION_WATER) {
            S_StartSound(te.pos1, 0, 0, cl_sfx_watrexp, 1, ATTN_NORM, 0);
        } else {
            S_StartSound(te.pos1, 0, 0, cl_sfx_grenexp, 1, ATTN_NORM, 0);
            CL_SnowExplosion(te.pos1, 64);
        }
        break;

    case TE_EXPLOSION2:
    case TE_EXPLOSION2_NL:
        ex = CL_PlainExplosion();
        ex->frames = 19;
        ex->baseframe = 30;
        if (te.type == TE_EXPLOSION2_NL)
            ex->light = 0;
        CL_ExplosionParticles(te.pos1);
        S_StartSound(te.pos1, 0, 0, cl_sfx_grenexp, 1, ATTN_NORM, 0);
        CL_SnowExplosion(te.pos1, 64);
        break;

    case TE_ROCKET_EXPLOSION:
    case TE_ROCKET_EXPLOSION_WATER:
        ex = CL_PlainExplosion();
        if (cl_disable_explosions->integer & NOEXP_ROCKET)
            ex->type = ex_light;

        if (!(cl_disable_particles->integer & NOPART_ROCKET_EXPLOSION))
            CL_ExplosionParticles(te.pos1);

        if (cl_dlight_hacks->integer & DLHACK_SMALLER_EXPLOSION)
            ex->light = 200;

        if (te.type == TE_ROCKET_EXPLOSION_WATER) {
            S_StartSound(te.pos1, 0, 0, cl_sfx_watrexp, 1, ATTN_NORM, 0);
        } else {
            S_StartSound(te.pos1, 0, 0, cl_sfx_rockexp, 1, ATTN_NORM, 0);
            CL_SnowExplosion(te.pos1, 64);
        }
        break;

    case TE_EXPLOSION1:
    case TE_EXPLOSION1_NL:
    case TE_PLASMA_EXPLOSION:
        ex = CL_PlainExplosion();
        if (te.type == TE_EXPLOSION1_NL)
            ex->light = 0;
        CL_ExplosionParticles(te.pos1);
        S_StartSound(te.pos1, 0, 0, cl_sfx_rockexp, 1, ATTN_NORM, 0);
        CL_SnowExplosion(te.pos1, 64);
        break;

    case TE_EXPLOSION1_NP:
        CL_PlainExplosion();
        S_StartSound(te.pos1, 0, 0, cl_sfx_rockexp, 1, ATTN_NORM, 0);
        break;

    case TE_EXPLOSION1_BIG:
        ex = CL_PlainExplosion();
        ex->ent.model = cl_mod_explo4;
        ex->ent.scale = 2.0f;
        S_StartSound(te.pos1, 0, 0, cl_sfx_rockexp, 1, ATTN_NORM, 0);
        CL_SnowExplosion(te.pos1, 112);
        break;

    case TE_BFG_EXPLOSION:
        CL_BFGExplosion(te.pos1);
        CL_RTEnergyCollapse(te.pos1, RT_ENERGY_BFG_SECONDARY);
        break;

    case TE_BFG_BIGEXPLOSION:
        CL_BFGExplosionParticles(te.pos1);
        CL_SnowExplosion(te.pos1, 112);
        CL_RTEnergyCollapse(te.pos1, RT_ENERGY_BFG_CORE);
        break;

    case TE_BFG_LASER:
        CL_ParseLaser(0xd0d1d2d3);
        break;

    case TE_BFG_ZAP:
        CL_ParseLaser(0xd0d1d2d3);
        CL_BFGExplosion(te.pos2);
        CL_RTEnergyCollapse(te.pos2, RT_ENERGY_BFG_SECONDARY);
        break;

    case TE_BUBBLETRAIL:
        CL_BubbleTrail(te.pos1, te.pos2);
        break;

    case TE_PARASITE_ATTACK:
    case TE_MEDIC_CABLE_ATTACK:
        VectorClear(te.offset);
        te.entity2 = 0;
        CL_ParseBeam(cl_mod_parasite_segment);
        break;

    case TE_BOSSTPORT:          // boss teleporting to station
        CL_BigTeleportParticles(te.pos1);
        CL_RTTeleportVortex(te.pos1, RT_TELEPORT_BOSS);
        S_StartSound(te.pos1, 0, 0, S_RegisterSound("misc/bigtele.wav"), 1, ATTN_NONE, 0);
        break;

    case TE_GRAPPLE_CABLE:
        te.entity2 = 0;
        CL_ParseBeam(cl_mod_grapple_cable);
        break;

    case TE_WELDING_SPARKS:
        CL_ParticleEffect2(te.pos1, te.dir, te.color, te.count);

        ex = CL_AllocExplosion();
        VectorCopy(te.pos1, ex->ent.origin);
        ex->type = ex_flash;
        // note to self
        // we need a better no draw flag
        ex->ent.flags = RF_BEAM;
        ex->start = cl.servertime - CL_FRAMETIME;
        ex->light = 100 + (Q_rand() % 75);
        VectorSet(ex->lightcolor, 1.0f, 1.0f, 0.3f);
        ex->ent.model = cl_mod_flash;
        ex->frames = 2;
        break;

    case TE_GREENBLOOD:
        CL_ParticleEffect2(te.pos1, te.dir, 0xdf, 30);
        break;

    case TE_TUNNEL_SPARKS:
        CL_ParticleEffect3(te.pos1, te.dir, te.color, te.count);
        break;

    case TE_LIGHTNING:
        S_StartSound(NULL, te.entity1, CHAN_WEAPON, cl_sfx_lightning, 1, ATTN_NORM, 0);
        VectorClear(te.offset);
        CL_ParseBeam(cl_mod_lightning);
        break;

    case TE_DEBUGTRAIL:
        CL_DebugTrail(te.pos1, te.pos2);
        break;

    case TE_PLAIN_EXPLOSION:
        CL_PlainExplosion();
        break;

    case TE_FLASHLIGHT:
        CL_Flashlight(te.entity1, te.pos1);
        break;

    case TE_FORCEWALL:
        CL_ForceWall(te.pos1, te.pos2, te.color);
        break;

    case TE_HEATBEAM:
        VectorSet(te.offset, 2, 7, -3);
        CL_ParsePlayerBeam(cl_mod_heatbeam);
        break;

    case TE_MONSTER_HEATBEAM:
        VectorClear(te.offset);
        CL_ParsePlayerBeam(cl_mod_heatbeam);
        break;

    case TE_HEATBEAM_SPARKS:
        CL_ParticleSteamEffect(te.pos1, te.dir, 0x8, 50, 60);
        CL_RTElectricFilaments(te.pos1, te.dir,
                              MakeColor(255, 172, 84, 255), 1.15f);
        S_StartSound(te.pos1,  0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
        break;

    case TE_HEATBEAM_STEAM:
        CL_ParticleSteamEffect(te.pos1, te.dir, 0xE0, 20, 60);
        S_StartSound(te.pos1,  0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
        break;

    case TE_STEAM:
        CL_ParseSteam();
        break;

    case TE_BUBBLETRAIL2:
        CL_BubbleTrail2(te.pos1, te.pos2, 8);
        S_StartSound(te.pos1,  0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
        break;

    case TE_MOREBLOOD:
        CL_ParticleEffect(te.pos1, te.dir, 0xe8, 250);
        break;

    case TE_CHAINFIST_SMOKE:
        VectorSet(te.dir, 0, 0, 1);
        CL_ParticleSmokeEffect(te.pos1, te.dir, 0, 20, 20);
        break;

    case TE_ELECTRIC_SPARKS:
        CL_ParticleEffect(te.pos1, te.dir, 0x75, 40);
        CL_RTElectricFilaments(te.pos1, te.dir,
                              MakeColor(104, 176, 255, 255), 1.05f);
        //FIXME : replace or remove this sound
        S_StartSound(te.pos1, 0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
        break;

    case TE_TRACKER_EXPLOSION:
        CL_ColorFlash(te.pos1, 0, 150, -1, -1, -1);
        CL_ColorExplosionParticles(te.pos1, 0, 1);
        CL_RTEnergyCollapse(te.pos1, RT_ENERGY_TRACKER);
        S_StartSound(te.pos1, 0, 0, cl_sfx_disrexp, 1, ATTN_NORM, 0);
        break;

    case TE_TELEPORT_EFFECT:
        CL_TeleportParticles(te.pos1);
        CL_RTTeleportVortex(te.pos1, RT_TELEPORT_STANDARD);
        break;

    case TE_DBALL_GOAL:
        CL_TeleportParticles(te.pos1);
        CL_RTTeleportVortex(te.pos1, RT_TELEPORT_DBALL);
        break;

    case TE_WIDOWBEAMOUT:
        CL_ParseWidow();
        break;

    case TE_NUKEBLAST:
        CL_ParseNuke();
        break;

    case TE_WIDOWSPLASH:
        CL_WidowSplash();
        break;

    case TE_BERSERK_SLAM:
        CL_BerserkSlamParticles(te.pos1, te.dir);

        ex = CL_AllocExplosion();
        VectorCopy(te.pos1, ex->ent.origin);
        dirtoangles(te.dir, ex->ent.angles);
        ex->type = ex_misc;
        ex->ent.model = cl_mod_explode;
        ex->ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
        ex->ent.scale = 3;
        ex->ent.skinnum = 2;
        ex->start = cl.servertime - CL_FRAMETIME;
        ex->light = 550;
        VectorSet(ex->lightcolor, 0.19f, 0.41f, 0.75f);
        ex->frames = 4;
        break;

    case TE_GRAPPLE_CABLE_2:
        VectorSet(te.offset, 9, 12, -3);
        CL_ParsePlayerBeam(cl_mod_grapple_cable);
        break;

    case TE_LIGHTNING_BEAM:
        VectorSet(te.offset, 0, 12, -12);
        CL_ParsePlayerBeam(cl_mod_lightning);
        break;

    case TE_POWER_SPLASH:
        CL_PowerSplash();
        break;

    case TE_DAMAGE_DEALT:
        if (te.count > 0 && cl_hit_markers->integer > 0) {
            cl.hit_marker_time = cls.realtime;
            cl.hit_marker_count = te.count;
            if (cl_hit_markers->integer > 1)
                S_StartSound(NULL, listener_entnum, 257, cl_sfx_hit_marker, 1, ATTN_NONE, 0);
        }
        break;

    default:
        Com_Error(ERR_DROP, "%s: bad type", __func__);
    }
}

/*
=================
CL_AddTEnts
=================
*/
void CL_AddTEnts(void)
{
    CL_AddBeams();
    CL_AddPlayerBeams();
    CL_AddExplosions();
    CL_ProcessSustain();
    CL_AddLasers();
    CL_AddRailBolts();
}

/*
=================
CL_ClearTEnts
=================
*/
void CL_ClearTEnts(void)
{
    CL_ClearBeams();
    CL_ClearExplosions();
    CL_ClearLasers();
    CL_ClearRailBolts();
    CL_ClearSustains();
}

void CL_InitTEnts(void)
{
    cl_muzzleflashes = Cvar_Get("cl_muzzleflashes", "1", 0);
    cl_hit_markers = Cvar_Get("cl_hit_markers", "2", 0);
    cl_railtrail_type = Cvar_Get("cl_railtrail_type", "0", 0);
    cl_railtrail_time = Cvar_Get("cl_railtrail_time", "1.0", 0);
    cl_railtrail_time->changed = cl_timeout_changed;
    cl_railtrail_time->changed(cl_railtrail_time);
    cl_railcore_color = Cvar_Get("cl_railcore_color", "red", 0);
    cl_railcore_color->changed = cl_railcore_color_changed;
    cl_railcore_color->generator = Com_Color_g;
    cl_railcore_color_changed(cl_railcore_color);
    cl_railcore_width = Cvar_Get("cl_railcore_width", "2", 0);
    cl_railcore_glow = Cvar_Get("cl_railcore_glow", "0", 0);
    cl_railspiral_color = Cvar_Get("cl_railspiral_color", "blue", 0);
    cl_railspiral_color->changed = cl_railspiral_color_changed;
    cl_railspiral_color->generator = Com_Color_g;
    cl_railspiral_color_changed(cl_railspiral_color);
    cl_railspiral_radius = Cvar_Get("cl_railspiral_radius", "3", 0);
    cl_railring_radius = Cvar_Get("cl_railring_radius", "8", 0);
    cl_railbolt_light = Cvar_Get("cl_railbolt_light", "1", 0);
}
