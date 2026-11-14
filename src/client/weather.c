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

    if (!amount || !cl.bsp || cls.state != ca_active)
        return;

    weather_probes = WEATHER_PROBES;
    update_wind(dt);

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
}

static void cl_snow_changed(cvar_t *self)
{
    CL_ClearWeather();
}

void CL_InitWeather(void)
{
    cl_snow = Cvar_Get("cl_snow", "0", CVAR_ARCHIVE);
    cl_snow->changed = cl_snow_changed;
}
