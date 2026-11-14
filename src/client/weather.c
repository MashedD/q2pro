/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

//
// weather.c -- client-side snow
//
// Snow only falls where the sky is above. The map is probed in columns of
// WEATHER_CELL units: from the viewer's height, a trace upwards must reach
// a sky surface; a second trace from there downwards finds the ground.
// Columns are cached per height band and probed lazily, a few per frame.
// Flakes live in their own pool, drift with a slowly changing wind and are
// respawned at the sky when they reach the ground of their column.
//
// Sounds are optional files in sound/weather/: a wind loop and a soft snow
// ambience whose volume follows how much open sky is around the viewer,
// occasional gusts, and footsteps on ground under open sky. The game has no
// such sounds; without the files snow is silent.
//

#include "client.h"

#define WEATHER_CELL        48
#define WEATHER_BAND        256     // viewer height band of a cached column
#define WEATHER_HASH        16384   // column cache size, power of two
#define WEATHER_MAX_FLAKES  4096
#define WEATHER_RADIUS      1024    // flakes are kept within this of the viewer
#define WEATHER_HEIGHT      384     // and at most this far above it
#define WEATHER_PROBES      48      // new columns probed per frame

typedef enum {
    COLUMN_EMPTY,       // cache slot unused
    COLUMN_NO_SKY,      // roofed, or the viewer height is inside solid
    COLUMN_SKY,
} column_state_t;

typedef struct {
    int     x, y, band;
    column_state_t  state;
    float   sky_z;      // just below the sky surface
    float   ground_z;   // first solid or liquid below it
} weather_column_t;

typedef struct {
    vec3_t  org;
    float   ground_z;
    float   fall;       // units per second
    float   phase;      // sway phase
    float   size;       // particle scale
    bool    active;
} snowflake_t;

static cvar_t   *cl_snow;
static cvar_t   *cl_snow_volume;

#define WEATHER_STEPS   3
#define WEATHER_GUSTS   2

static struct {
    qhandle_t   wind;
    qhandle_t   ambience;
    qhandle_t   gusts[WEATHER_GUSTS];
    qhandle_t   steps[WEATHER_STEPS];
    int         num_gusts;
    int         num_steps;
    int         last_step;
} weather_sfx;

static float    weather_exposure;   // 0 indoors .. 1 under open sky
static float    weather_gust_timer;

static void update_exposure(const vec3_t view, float dt);
static void update_sounds(float amount, float dt);
static void stop_sounds(void);

static weather_column_t weather_columns[WEATHER_HASH];
static snowflake_t      snowflakes[WEATHER_MAX_FLAKES];
static int      weather_probes;     // probes left this frame
static float    weather_wind[2];
static float    weather_wind_target[2];
static float    weather_wind_timer;

static unsigned column_hash(int x, int y, int band)
{
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)band * 83492791u;
    return h & (WEATHER_HASH - 1);
}

static void probe_column(weather_column_t *col, float x, float y, float z)
{
    static const vec3_t zero = { 0, 0, 0 };
    static const float starts[] = { 0, 64, -64, 160, -160 };
    trace_t tr;
    vec3_t start, end;

    col->state = COLUMN_NO_SKY;

    for (int i = 0; i < q_countof(starts); i++) {
        VectorSet(start, x, y, z + starts[i]);
        if (CM_PointContents(start, cl.bsp->nodes, cl.csr.extended) & MASK_SOLID)
            continue;

        VectorSet(end, x, y, start[2] + 8192);
        CM_BoxTrace(&tr, start, end, zero, zero, cl.bsp->nodes, MASK_SOLID, cl.csr.extended);
        if (tr.fraction == 1.0f || !tr.surface || !(tr.surface->flags & SURF_SKY))
            return;

        col->sky_z = tr.endpos[2] - 2;
        VectorSet(start, x, y, col->sky_z);
        VectorSet(end, x, y, col->sky_z - 8192);
        CM_BoxTrace(&tr, start, end, zero, zero, cl.bsp->nodes,
                    MASK_SOLID | MASK_WATER, cl.csr.extended);
        col->ground_z = tr.endpos[2];
        if (col->sky_z - col->ground_z > 8)
            col->state = COLUMN_SKY;
        return;
    }
}

// Returns the column above (x, y) for a viewer at height z, or NULL when it
// isn't known yet and no probes are left for this frame.
static const weather_column_t *find_column(float x, float y, float z)
{
    int cx = (int)floorf(x / WEATHER_CELL);
    int cy = (int)floorf(y / WEATHER_CELL);
    int band = (int)floorf(z / WEATHER_BAND);
    unsigned h = column_hash(cx, cy, band);

    for (int i = 0; i < 8; i++) {
        weather_column_t *col = &weather_columns[(h + i) & (WEATHER_HASH - 1)];

        if (col->state == COLUMN_EMPTY) {
            if (weather_probes <= 0)
                return NULL;
            weather_probes--;
            col->x = cx;
            col->y = cy;
            col->band = band;
            probe_column(col, (cx + 0.5f) * WEATHER_CELL, (cy + 0.5f) * WEATHER_CELL, z);
            return col;
        }
        if (col->x == cx && col->y == cy && col->band == band)
            return col;
    }

    // neighbourhood full; reuse the first slot
    weather_column_t *col = &weather_columns[h];
    if (weather_probes <= 0)
        return NULL;
    weather_probes--;
    col->x = cx;
    col->y = cy;
    col->band = band;
    probe_column(col, (cx + 0.5f) * WEATHER_CELL, (cy + 0.5f) * WEATHER_CELL, z);
    return col;
}

static bool spawn_flake(snowflake_t *f, const vec3_t view, bool anywhere)
{
    for (int tries = 0; tries < 8; tries++) {
        float ang = frand() * 2 * M_PIf;
        float r = sqrtf(frand()) * WEATHER_RADIUS;
        float x = view[0] + cosf(ang) * r;
        float y = view[1] + sinf(ang) * r;
        const weather_column_t *col = find_column(x, y, view[2]);

        if (!col || col->state != COLUMN_SKY)
            continue;

        float top = min(col->sky_z, view[2] + WEATHER_HEIGHT);
        if (top <= col->ground_z)
            continue;

        f->org[0] = x;
        f->org[1] = y;
        f->org[2] = anywhere ? col->ground_z + frand() * (top - col->ground_z) : top - frand() * 16;
        f->ground_z = col->ground_z;
        f->fall = 60 + frand() * 40;
        f->phase = frand() * 2 * M_PIf;
        f->size = 0.8f + frand() * 0.7f;
        f->active = true;
        return true;
    }

    f->active = false;
    return false;
}

static void update_wind(float dt)
{
    weather_wind_timer -= dt;
    if (weather_wind_timer <= 0) {
        float ang = frand() * 2 * M_PIf;
        float speed = 10 + frand() * 30;
        weather_wind_target[0] = cosf(ang) * speed;
        weather_wind_target[1] = sinf(ang) * speed;
        weather_wind_timer = 6 + frand() * 8;
    }

    float k = min(dt * 0.3f, 1.0f);
    weather_wind[0] += (weather_wind_target[0] - weather_wind[0]) * k;
    weather_wind[1] += (weather_wind_target[1] - weather_wind[1]) * k;
}

/*
==============
CL_AddWeather

Moves the flakes and adds them to the refresh as particles.
==============
*/
void CL_AddWeather(void)
{
    float amount = Cvar_ClampValue(cl_snow, 0, 4);
    float dt = min(cls.frametime, 0.1f);
    const float *view = cl.refdef.vieworg;
    float t = cl.time * 0.001f;
    int count;
    particle_t p;

    if (!amount || !cl.bsp || cls.state != ca_active) {
        stop_sounds();
        return;
    }

    weather_probes = WEATHER_PROBES;
    update_wind(dt);
    update_exposure(view, dt);
    update_sounds(amount, dt);

    count = min((int)(amount * 1500), WEATHER_MAX_FLAKES);

    memset(&p, 0, sizeof(p));
    p.color = -1;
    p.rgba.u32 = U32_WHITE;
    p.alpha = 0.9f;

    for (int i = 0; i < count; i++) {
        snowflake_t *f = &snowflakes[i];

        if (!f->active) {
            if (!spawn_flake(f, view, true))
                continue;
        } else {
            float sway = sinf(t * 1.3f + f->phase) * 8;

            f->org[0] += (weather_wind[0] + sway) * dt;
            f->org[1] += (weather_wind[1] + cosf(t * 1.1f + f->phase) * 8) * dt;
            f->org[2] -= f->fall * dt;

            float dx = f->org[0] - view[0];
            float dy = f->org[1] - view[1];
            if (f->org[2] <= f->ground_z || dx * dx + dy * dy > WEATHER_RADIUS * WEATHER_RADIUS) {
                if (!spawn_flake(f, view, false))
                    continue;
            }
        }

        VectorCopy(f->org, p.origin);
        p.scale = f->size;
        V_AddParticle(&p);
    }
}

// Fraction of the columns around the viewer that have sky above, from those
// already probed; changes are smoothed over a second or two.
static void update_exposure(const vec3_t view, float dt)
{
    static const float offsets[][2] = {
        { 0, 0 }, { 160, 0 }, { -160, 0 }, { 0, 160 }, { 0, -160 },
        { 320, 320 }, { -320, 320 }, { 320, -320 }, { -320, -320 },
    };
    int known = 0, sky = 0;

    for (int i = 0; i < q_countof(offsets); i++) {
        const weather_column_t *col = find_column(view[0] + offsets[i][0],
                                                  view[1] + offsets[i][1], view[2]);
        if (!col)
            continue;
        known++;
        // the viewer's own column counts three times
        if (col->state == COLUMN_SKY)
            sky += i ? 1 : 3;
    }

    float target = known ? min(sky / (float)(known + 2), 1.0f) : weather_exposure;
    weather_exposure += (target - weather_exposure) * min(dt * 1.5f, 1.0f);
}

static void update_sounds(float amount, float dt)
{
    float volume = Cvar_ClampValue(cl_snow_volume, 0, 1) * min(amount, 1.0f);

    // wind stays faintly audible indoors, like through windows
    S_SetLocalLoop(0, weather_sfx.wind, volume * (0.12f + 0.55f * weather_exposure));
    S_SetLocalLoop(1, weather_sfx.ambience, volume * 0.35f * weather_exposure);

    weather_gust_timer -= dt;
    if (weather_gust_timer <= 0) {
        weather_gust_timer = 8 + frand() * 14;
        if (weather_sfx.num_gusts && weather_exposure > 0.2f)
            S_StartSound(NULL, cl.frame.clientNum + 1, CHAN_AUTO,
                         weather_sfx.gusts[Q_rand_uniform(weather_sfx.num_gusts)],
                         volume * 0.6f * weather_exposure, ATTN_NONE, 0);
    }
}

static void stop_sounds(void)
{
    S_SetLocalLoop(0, 0, 0);
    S_SetLocalLoop(1, 0, 0);
}

/*
==============
CL_SnowFootstep

Plays a snow step instead of the surface's own when an entity walks on
ground under open sky while it snows. Returns false to keep the normal step.
==============
*/
bool CL_SnowFootstep(int entnum, float volume, float attenuation)
{
    vec3_t org;

    if (!weather_sfx.num_steps || !cl_snow || !cl_snow->value || !cl.bsp)
        return false;

    CL_GetEntitySoundOrigin(entnum, org);
    weather_probes = max(weather_probes, 1);
    const weather_column_t *col = find_column(org[0], org[1], org[2]);
    if (!col || col->state != COLUMN_SKY || org[2] - col->ground_z > 48)
        return false;

    int n = Q_rand_uniform(weather_sfx.num_steps);
    if (n == weather_sfx.last_step && weather_sfx.num_steps > 1)
        n = (n + 1) % weather_sfx.num_steps;
    weather_sfx.last_step = n;

    S_StartSound(NULL, entnum, CHAN_FOOTSTEP, weather_sfx.steps[n], volume, attenuation, 0);
    return true;
}

static qhandle_t register_optional(const char *name)
{
    if (!FS_FileExists(va("sound/%s", name)))
        return 0;
    return S_RegisterSound(name);
}

/*
==============
CL_RegisterWeatherSounds

Called while sounds are registered for a map.
==============
*/
void CL_RegisterWeatherSounds(void)
{
    memset(&weather_sfx, 0, sizeof(weather_sfx));
    weather_sfx.wind = register_optional("weather/snow_wind.wav");
    weather_sfx.ambience = register_optional("weather/snow_ambience.wav");
    for (int i = 0; i < WEATHER_GUSTS; i++)
        if ((weather_sfx.gusts[weather_sfx.num_gusts] = register_optional(va("weather/wind_gust%d.wav", i + 1))))
            weather_sfx.num_gusts++;
    for (int i = 0; i < WEATHER_STEPS; i++)
        if ((weather_sfx.steps[weather_sfx.num_steps] = register_optional(va("weather/snow_step%d.wav", i + 1))))
            weather_sfx.num_steps++;
}

/*
==============
CL_ClearWeather

Forgets the probed columns and flakes, for a new map.
==============
*/
void CL_ClearWeather(void)
{
    memset(weather_columns, 0, sizeof(weather_columns));
    memset(snowflakes, 0, sizeof(snowflakes));
    weather_wind_timer = 0;
    weather_exposure = 0;
    weather_gust_timer = 4;
    stop_sounds();
}

static void cl_snow_changed(cvar_t *self)
{
    CL_ClearWeather();
}

void CL_InitWeather(void)
{
    cl_snow = Cvar_Get("cl_snow", "0", CVAR_ARCHIVE);
    cl_snow->changed = cl_snow_changed;
    cl_snow_volume = Cvar_Get("cl_snow_volume", "1", CVAR_ARCHIVE);
}
