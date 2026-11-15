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
// When a map is loaded, upward-facing world faces are sampled on a
// world-aligned grid of COVER_GRID units, and each point traces upwards to
// find whether the sky is above it and how high. Snow only falls there:
// flakes start below the sky above a random open point near the viewer and
// vanish at its floor, so every open floor gets snow whatever level the
// viewer is on, without tracing while playing.
//
// Snow cover (cl_snow_cover) is a heightfield mesh over the same points.
// Coverage is blurred so that drifts taper at their edges and sink below the
// floor there; heights follow a smooth noise. Each point grows on its own
// schedule, the most open spots first, as the renderer is told how far the
// snow has come (refdef_t.snow_amount); the mesh itself is sent once.
//
// Small columns probed from the viewer's height, a few per frame, tell how
// open the sky is around the viewer, for the sound volumes and footsteps.
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
#define WEATHER_RADIUS      768     // flakes are kept within this of the viewer
#define WEATHER_FADE        320     // and fade out from this distance
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
static cvar_t   *cl_snow_cover;
static cvar_t   *cl_snow_cover_time;

#define COVER_GRID          12
#define COVER_HASH          65536   // grid column hash, power of two
#define COVER_MAX_SAMPLES   200000

#define SPAWN_BUCKET        256     // flake spawn index cell
#define SPAWN_HASH          4096    // power of two

typedef struct {
    vec3_t  org;        // on the surface
    int     x, y;       // grid coordinates
    int     next;       // next sample in the same hash chain
    float   sky_z;      // height of the sky above, if open
    bool    open;       // the sky is above
    float   cover;      // 0 bare .. 1 fully under open sky, blurred
    float   noise;      // 0..1
} cover_sample_t;

typedef struct {
    int     bx, by;     // bucket coordinates
    int     first, count;   // in cover.spawn_points
} spawn_bucket_t;

static struct {
    cover_sample_t  *samples;
    int             num_samples;
    int             *hash;          // first sample per grid column
    bool            built;

    // open points sorted by spawn bucket, and the buckets near the viewer
    int             *spawn_points;
    int             num_spawn_points;
    spawn_bucket_t  spawn_buckets[SPAWN_HASH];
    int             *candidates;
    int             num_candidates;
    int             max_candidates;
    int             viewer_bx, viewer_by;
    bool            have_candidates;

    r_snow_vertex_t *vertices;      // snow cover mesh
    int             num_vertices;
    uint32_t        *indices;
    int             num_indices;
    bool            dirty;          // send to the renderer
    int             start_time;     // cl.time when the map started
} cover;

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
static void cover_free(void);
static void cover_build(void);
static float cover_amount(void);
static void update_candidates(const vec3_t view);

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
    for (int tries = 0; tries < 8 && cover.num_candidates; tries++) {
        const cover_sample_t *s = &cover.samples[cover.candidates[Q_rand_uniform(cover.num_candidates)]];
        float x = s->org[0] + crand() * COVER_GRID * 0.5f;
        float y = s->org[1] + crand() * COVER_GRID * 0.5f;
        float dx = x - view[0], dy = y - view[1];

        if (dx * dx + dy * dy > WEATHER_RADIUS * WEATHER_RADIUS)
            continue;

        float top = min(s->sky_z - 2, view[2] + WEATHER_HEIGHT);
        if (top <= s->org[2])
            continue;

        f->org[0] = x;
        f->org[1] = y;
        f->org[2] = anywhere ? s->org[2] + frand() * (top - s->org[2]) : top - frand() * 16;
        f->ground_z = s->org[2];
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

    cl.refdef.snow_amount = 0;

    if (!amount || !cl.bsp || cls.state != ca_active) {
        stop_sounds();
        if (cover.built) {
            R_SetSnowCover(NULL, 0, NULL, 0);
            cover_free();
        }
        return;
    }

    if (!cover.built)
        cover_build();
    if (cover.dirty) {
        R_SetSnowCover(cover.vertices, cover.num_vertices, cover.indices, cover.num_indices);
        cover.dirty = false;
    }
    if (cover.num_indices)
        cl.refdef.snow_amount = cover_amount();
    update_candidates(view);

    weather_probes = WEATHER_PROBES;
    update_wind(dt);
    update_exposure(view, dt);
    update_sounds(amount, dt);

    // light snow is sparse, so it hides little and costs little
    count = min((int)(400 * amount * sqrtf(amount)), WEATHER_MAX_FLAKES);

    memset(&p, 0, sizeof(p));
    p.color = -1;
    p.rgba.u32 = U32_WHITE;

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

            // a flake that lands starts again at the top, but one left
            // behind by the viewer goes anywhere around the new position,
            // so snow is already falling everywhere there
            float dx = f->org[0] - view[0];
            float dy = f->org[1] - view[1];
            if (dx * dx + dy * dy > WEATHER_RADIUS * WEATHER_RADIUS) {
                if (!spawn_flake(f, view, true))
                    continue;
            } else if (f->org[2] <= f->ground_z) {
                if (!spawn_flake(f, view, false))
                    continue;
            }
        }

        // flakes behind the viewer can't be seen
        vec3_t d;
        VectorSubtract(f->org, view, d);
        if (DotProduct(d, cl.v_forward) < -16)
            continue;

        // distant flakes fade, so snow doesn't hide the far side of an area
        float dist2 = d[0] * d[0] + d[1] * d[1];
        p.alpha = 0.9f;
        if (dist2 > WEATHER_FADE * WEATHER_FADE) {
            float fade = (sqrtf(dist2) - WEATHER_FADE) / (WEATHER_RADIUS - WEATHER_FADE);
            p.alpha *= 1 - 0.8f * min(fade, 1.0f);
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

static unsigned cover_hash(int x, int y)
{
    return ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u) & (COVER_HASH - 1);
}

// The sample at grid point (x, y) closest to height z, within max_dz.
static int cover_find(int x, int y, float z, float max_dz)
{
    int best = -1;
    float best_dz = max_dz;

    for (int i = cover.hash[cover_hash(x, y)]; i >= 0; i = cover.samples[i].next) {
        const cover_sample_t *s = &cover.samples[i];
        float dz = fabsf(s->org[2] - z);
        if (s->x == x && s->y == y && dz < best_dz) {
            best = i;
            best_dz = dz;
        }
    }
    return best;
}

// Smooth value noise in 0..1 over world x/y, period about 96 units.
static float cover_noise(float x, float y)
{
    float fx = x / 96.0f, fy = y / 96.0f;
    int ix = (int)floorf(fx), iy = (int)floorf(fy);
    float tx = fx - ix, ty = fy - iy;
    float v[4];

    for (int i = 0; i < 4; i++) {
        unsigned h = (unsigned)(ix + (i & 1)) * 374761393u + (unsigned)(iy + (i >> 1)) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        v[i] = ((h ^ (h >> 16)) & 0xffff) / 65535.0f;
    }
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    return (v[0] * (1 - tx) + v[1] * tx) * (1 - ty) + (v[2] * (1 - tx) + v[3] * tx) * ty;
}

// Whether the sky is above a point on a floor, and how high.
static bool cover_exposed(const vec3_t org, float *sky_z)
{
    static const vec3_t zero = { 0, 0, 0 };
    trace_t tr;
    vec3_t start, end;

    VectorSet(start, org[0], org[1], org[2] + 1);
    VectorSet(end, org[0], org[1], org[2] + 8192);
    CM_BoxTrace(&tr, start, end, zero, zero, cl.bsp->nodes, MASK_SOLID, cl.csr.extended);
    *sky_z = tr.endpos[2];
    return !tr.startsolid && tr.fraction < 1.0f && tr.surface && (tr.surface->flags & SURF_SKY);
}

static void cover_add_face(const mface_t *face, bool liquid)
{
    vec3_t pts[64], normal;
    int n = min(face->numsurfedges, (int)q_countof(pts));
    float mins[2] = { 1e9f, 1e9f }, maxs[2] = { -1e9f, -1e9f };
    float dist;

    VectorCopy(face->plane->normal, normal);
    dist = face->plane->dist;
    if (face->drawflags & DSURF_PLANEBACK) {
        VectorNegate(normal, normal);
        dist = -dist;
    }
    if (normal[2] < 0.7f || n < 3)
        return;

    for (int i = 0; i < n; i++) {
        const msurfedge_t *se = &face->firstsurfedge[i];
        const medge_t *e = &cl.bsp->edges[se->edge];
        VectorCopy(cl.bsp->vertices[e->v[se->vert]].point, pts[i]);
        mins[0] = min(mins[0], pts[i][0]); maxs[0] = max(maxs[0], pts[i][0]);
        mins[1] = min(mins[1], pts[i][1]); maxs[1] = max(maxs[1], pts[i][1]);
    }

    // the projection of an upward face onto the XY plane keeps its winding
    float area = 0;
    for (int i = 0; i < n; i++) {
        const float *a = pts[i], *b = pts[(i + 1) % n];
        area += a[0] * b[1] - b[0] * a[1];
    }
    float sign = area >= 0 ? 1 : -1;

    for (int gx = (int)ceilf(mins[0] / COVER_GRID); gx * COVER_GRID <= maxs[0]; gx++) {
        for (int gy = (int)ceilf(mins[1] / COVER_GRID); gy * COVER_GRID <= maxs[1]; gy++) {
            float x = gx * COVER_GRID, y = gy * COVER_GRID;
            bool inside = true;

            for (int i = 0; i < n && inside; i++) {
                const float *a = pts[i], *b = pts[(i + 1) % n];
                float cross = (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
                if (cross * sign < -0.01f)
                    inside = false;
            }
            if (!inside)
                continue;

            float z = (dist - normal[0] * x - normal[1] * y) / normal[2];
            if (cover_find(gx, gy, z, 2) >= 0)
                continue;   // shared with a neighbouring face
            if (cover.num_samples >= COVER_MAX_SAMPLES)
                return;

            cover_sample_t *s = &cover.samples[cover.num_samples];
            VectorSet(s->org, x, y, z);
            s->x = gx;
            s->y = gy;
            s->open = cover_exposed(s->org, &s->sky_z);
            s->cover = s->open && !liquid;
            s->noise = cover_noise(x, y);
            unsigned h = cover_hash(gx, gy);
            s->next = cover.hash[h];
            cover.hash[h] = cover.num_samples++;
        }
    }
}

// Fills single missing points that have floor on all four sides, which
// sampling misses where the edges of faces meet, so the cover has no holes.
static void cover_fill_holes(void)
{
    int num = cover.num_samples;

    for (int i = 0; i < num; i++) {
        const cover_sample_t *s = &cover.samples[i];
        int x = s->x + 1, y = s->y;
        int n[4];
        float z = 0;
        bool open = true, liquid = false;

        if (cover_find(x, y, s->org[2], 16) >= 0)
            continue;
        n[0] = i;
        n[1] = cover_find(x + 1, y, s->org[2], 16);
        n[2] = cover_find(x, y + 1, s->org[2], 16);
        n[3] = cover_find(x, y - 1, s->org[2], 16);
        if (n[1] < 0 || n[2] < 0 || n[3] < 0)
            continue;
        for (int j = 0; j < 4; j++) {
            const cover_sample_t *o = &cover.samples[n[j]];
            z += o->org[2] * 0.25f;
            open &= o->open;
            liquid |= o->open && !o->cover;
        }

        vec3_t p = { x * COVER_GRID, y * COVER_GRID, z + 4 };
        if (CM_PointContents(p, cl.bsp->nodes, cl.csr.extended) & MASK_SOLID)
            continue;   // a thin pillar
        if (cover.num_samples >= COVER_MAX_SAMPLES)
            return;

        cover_sample_t *f = &cover.samples[cover.num_samples];
        VectorSet(f->org, p[0], p[1], z);
        f->x = x;
        f->y = y;
        f->open = open && cover_exposed(f->org, &f->sky_z);
        f->cover = f->open && !liquid;
        f->noise = cover_noise(p[0], p[1]);
        unsigned h = cover_hash(x, y);
        f->next = cover.hash[h];
        cover.hash[h] = cover.num_samples++;
    }
}

static void cover_free(void)
{
    Z_Freep(&cover.samples);
    Z_Freep(&cover.hash);
    Z_Freep(&cover.spawn_points);
    Z_Freep(&cover.candidates);
    Z_Freep(&cover.vertices);
    Z_Freep(&cover.indices);
    memset(&cover, 0, sizeof(cover));
}

static unsigned spawn_hash(int bx, int by)
{
    return ((unsigned)bx * 73856093u ^ (unsigned)by * 19349663u) & (SPAWN_HASH - 1);
}

static spawn_bucket_t *spawn_bucket(int bx, int by, bool create)
{
    unsigned h = spawn_hash(bx, by);

    for (int i = 0; i < SPAWN_HASH; i++) {
        spawn_bucket_t *b = &cover.spawn_buckets[(h + i) & (SPAWN_HASH - 1)];
        if (!b->count && !create)
            return NULL;
        if (!b->count) {
            b->bx = bx;
            b->by = by;
            return b;
        }
        if (b->bx == bx && b->by == by)
            return b;
    }
    return NULL;
}

static int spawn_bucket_coord(float v)
{
    return (int)floorf(v / SPAWN_BUCKET);
}

// Sorts the open points by spawn bucket, so the points near the viewer can
// be gathered quickly.
static void build_spawn_index(void)
{
    int *filled = Z_Mallocz(sizeof(int) * SPAWN_HASH);
    int num = 0;

    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        if (!s->open)
            continue;
        spawn_bucket_t *b = spawn_bucket(spawn_bucket_coord(s->org[0]),
                                         spawn_bucket_coord(s->org[1]), true);
        if (!b)
            continue;   // index full
        b->count++;
        num++;
    }

    cover.spawn_points = Z_Malloc(sizeof(int) * max(num, 1));
    int first = 0;
    for (int i = 0; i < SPAWN_HASH; i++) {
        spawn_bucket_t *b = &cover.spawn_buckets[i];
        b->first = first;
        first += b->count;
    }
    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        if (!s->open)
            continue;
        spawn_bucket_t *b = spawn_bucket(spawn_bucket_coord(s->org[0]),
                                         spawn_bucket_coord(s->org[1]), false);
        if (!b)
            continue;
        int slot = b - cover.spawn_buckets;
        cover.spawn_points[b->first + filled[slot]++] = i;
    }
    cover.num_spawn_points = num;
    Z_Free(filled);
}

// Gathers the open points within reach whenever the viewer enters another
// spawn bucket.
static void update_candidates(const vec3_t view)
{
    int vbx = spawn_bucket_coord(view[0]);
    int vby = spawn_bucket_coord(view[1]);
    int reach = WEATHER_RADIUS / SPAWN_BUCKET + 1;

    if (cover.have_candidates && vbx == cover.viewer_bx && vby == cover.viewer_by)
        return;

    cover.num_candidates = 0;
    for (int by = vby - reach; by <= vby + reach; by++) {
        for (int bx = vbx - reach; bx <= vbx + reach; bx++) {
            const spawn_bucket_t *b = spawn_bucket(bx, by, false);
            if (!b)
                continue;
            if (cover.num_candidates + b->count > cover.max_candidates) {
                cover.max_candidates = cover.num_candidates + b->count + 1024;
                cover.candidates = Z_Realloc(cover.candidates,
                                             sizeof(int) * cover.max_candidates);
            }
            memcpy(cover.candidates + cover.num_candidates,
                   cover.spawn_points + b->first, sizeof(int) * b->count);
            cover.num_candidates += b->count;
        }
    }

    cover.viewer_bx = vbx;
    cover.viewer_by = vby;
    cover.have_candidates = true;
}

typedef enum {
    EDGE_NONE,      // the floor goes on, at another height or unsampled
    EDGE_WALL,
    EDGE_DROP,
} edge_type_t;

static const int edge_dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

// Looks from a point without a grid neighbour towards where it would be, and
// returns what ends the floor there and how far away.
static edge_type_t probe_edge(const cover_sample_t *s, int dir, float *dist)
{
    static const vec3_t zero = { 0, 0, 0 };
    trace_t tr;
    vec3_t p, end;

    *dist = 0;
    for (int step = 2; step <= COVER_GRID; step += 2) {
        VectorSet(p, s->org[0] + edge_dirs[dir][0] * step,
                  s->org[1] + edge_dirs[dir][1] * step, s->org[2] + 4);
        if (CM_PointContents(p, cl.bsp->nodes, cl.csr.extended) & MASK_SOLID)
            return EDGE_WALL;
        VectorSet(end, p[0], p[1], s->org[2] - 4);
        CM_BoxTrace(&tr, p, end, zero, zero, cl.bsp->nodes,
                    MASK_SOLID | MASK_WATER, cl.csr.extended);
        if (tr.fraction == 1.0f)
            return EDGE_DROP;
        *dist = step;
    }
    *dist = 0;
    return EDGE_NONE;
}

static int compare_edges(const void *p1, const void *p2)
{
    uint64_t a = *(const uint64_t *)p1, b = *(const uint64_t *)p2;
    return a < b ? -1 : a > b;
}

static void add_triangle(int a, int b, int c)
{
    uint32_t *idx = &cover.indices[cover.num_indices];
    idx[0] = a; idx[1] = b; idx[2] = c;
    cover.num_indices += 3;
}

// Walls between the snow and the floor along the open edges of the mesh, so
// snow ending at a ledge or a step shows its depth.
static void add_skirts(void)
{
    int num_tris = cover.num_indices / 3;
    uint64_t *edges = Z_Malloc(sizeof(*edges) * 3 * max(num_tris, 1));
    int num_edges = 0;

    for (int i = 0; i < num_tris; i++) {
        const uint32_t *t = &cover.indices[i * 3];
        for (int j = 0; j < 3; j++) {
            uint32_t a = t[j], b = t[(j + 1) % 3];
            edges[num_edges++] = (uint64_t)min(a, b) << 32 | max(a, b);
        }
    }
    qsort(edges, num_edges, sizeof(*edges), compare_edges);

    int num_open = 0;
    for (int i = 0; i < num_edges; i++)
        if ((i == 0 || edges[i] != edges[i - 1]) &&
            (i + 1 == num_edges || edges[i] != edges[i + 1]))
            num_open++;
    cover.indices = Z_Realloc(cover.indices, sizeof(uint32_t) *
                              (cover.num_indices + 6 * num_open + 1));

    int num_top = cover.num_vertices;
    cover.vertices = Z_Realloc(cover.vertices, sizeof(*cover.vertices) * num_top * 2);
    int *bottom = Z_Malloc(sizeof(int) * num_top);
    memset(bottom, 0xff, sizeof(int) * num_top);

    for (int i = 0; i < num_edges; i++) {
        if ((i > 0 && edges[i] == edges[i - 1]) ||
            (i + 1 < num_edges && edges[i] == edges[i + 1]))
            continue;   // shared by two triangles

        int v[2] = { edges[i] >> 32, edges[i] & 0xffffffff };
        if (cover.vertices[v[0]].height <= 0 && cover.vertices[v[1]].height <= 0)
            continue;

        for (int j = 0; j < 2; j++) {
            if (bottom[v[j]] >= 0)
                continue;
            r_snow_vertex_t *b = &cover.vertices[cover.num_vertices];
            *b = cover.vertices[v[j]];
            b->height = -2;     // stays under the floor
            for (int c = 0; c < 3; c++)
                b->color.u8[c] = b->color.u8[c] * 3 / 4;
            bottom[v[j]] = cover.num_vertices++;
        }
        add_triangle(v[0], v[1], bottom[v[1]]);
        add_triangle(v[0], bottom[v[1]], bottom[v[0]]);
    }

    Z_Free(bottom);
    Z_Free(edges);
}

static void build_mesh(float depth)
{
    float *height = Z_Malloc(sizeof(float) * cover.num_samples);
    float *bank = Z_Mallocz(sizeof(float) * cover.num_samples);
    vec2_t *shift = Z_Mallocz(sizeof(vec2_t) * cover.num_samples);
    float (*strip)[2][2] = Z_Mallocz(sizeof(*strip) * cover.num_samples);
    byte *strip_axes = Z_Mallocz(cover.num_samples);

    // Where the floor ends, find what ends it: snow is pushed up to it, and
    // banks up against walls.
    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        float extent[4] = { 0 };
        bool missing[4];

        for (int d = 0; d < 4; d++) {
            missing[d] = cover_find(s->x + edge_dirs[d][0], s->y + edge_dirs[d][1],
                                    s->org[2], 16) < 0;
            if (!missing[d])
                continue;
            edge_type_t type = probe_edge(s, d, &extent[d]);
            if (type == EDGE_WALL)
                bank[i] = 1;
            // keep the side of the snow a unit away from the wall or the
            // face below the drop, even for a point right on the edge, so
            // the two never fight for depth
            if (type != EDGE_NONE)
                extent[d] -= 1;
        }
        // a strip one point wide, such as a narrow ledge or the top of a
        // wall, gets snow of its own width on both sides of the point
        for (int a = 0; a < 2; a++) {
            bool hi = missing[a * 2], lo = missing[a * 2 + 1];
            if (hi != lo) {
                shift[i][a] = hi ? extent[a * 2] : -extent[a * 2 + 1];
            } else if (hi && extent[a * 2] + extent[a * 2 + 1] > 1) {
                strip[i][a][0] = -extent[a * 2 + 1];
                strip[i][a][1] = extent[a * 2];
                strip_axes[i] |= BIT(a);
            }
        }
    }

    // blur coverage twice over the 3x3 neighbourhood, so drifts taper towards
    // roofs; walls and drops don't count, so ledges are covered to the edge
    float *tmp = Z_Malloc(sizeof(float) * cover.num_samples);
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < cover.num_samples; i++) {
            const cover_sample_t *s = &cover.samples[i];
            float sum = 0, wall = 0;
            int n = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int j = cover_find(s->x + dx, s->y + dy, s->org[2], 16);
                    if (j >= 0) {
                        sum += cover.samples[j].cover;
                        wall = max(wall, bank[j]);
                        n++;
                    }
                }
            tmp[i] = sum / n;
            // spread the banks one point away from the wall, a little lower
            if (pass == 0)
                height[i] = max(bank[i], wall * 0.5f);
        }
        for (int i = 0; i < cover.num_samples; i++)
            cover.samples[i].cover = tmp[i];
    }
    Z_Free(tmp);
    memcpy(bank, height, sizeof(float) * cover.num_samples);

    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        // zero height where the blurred coverage is 0.6, so the visible edge
        // of a drift is a smooth contour well inside the jagged outline of
        // the grid, which stays under the floor
        float f = Q_clipf((s->cover - 0.6f) / 0.4f, -1.25f, 1.0f);
        height[i] = depth * (0.3f + 0.7f * s->noise) * f;
        if (f > 0)
            height[i] += depth * 0.6f * bank[i] * f;
    }

    // two triangles per grid square that has any snow, or one where a wall
    // cuts off a corner
    cover.indices = Z_Malloc(sizeof(uint32_t) * 21 * cover.num_samples);
    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        int r = cover_find(s->x + 1, s->y, s->org[2], 16);
        int u = cover_find(s->x, s->y + 1, s->org[2], 16);
        int d = cover_find(s->x + 1, s->y + 1, s->org[2], 16);
        int c[4] = { i, r, d, u };  // counterclockwise
        int n = (r >= 0) + (u >= 0) + (d >= 0);

        if (n < 2)
            continue;
        if (n == 3) {
            if (height[i] <= 0 && height[r] <= 0 && height[u] <= 0 && height[d] <= 0)
                continue;
            add_triangle(i, r, d);
            add_triangle(i, d, u);
            continue;
        }

        // three corners: one triangle of them
        int t[3], k = 0;
        bool any = false;
        for (int j = 0; j < 4; j++) {
            if (c[j] < 0)
                continue;
            t[k++] = c[j];
            any |= height[c[j]] > 0;
        }
        if (any)
            add_triangle(t[0], t[1], t[2]);
    }
    // the corner missing from a square starting at a missing point
    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        if (cover_find(s->x - 1, s->y - 1, s->org[2], 16) >= 0)
            continue;
        int l = cover_find(s->x - 1, s->y, s->org[2], 16);
        int b = cover_find(s->x, s->y - 1, s->org[2], 16);
        if (l >= 0 && b >= 0 && (height[i] > 0 || height[l] > 0 || height[b] > 0))
            add_triangle(b, i, l);
    }

    cover.vertices = Z_Malloc(sizeof(*cover.vertices) * cover.num_samples * 5);
    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        r_snow_vertex_t *v = &cover.vertices[i];
        float hx = 0, hy = 0;
        int r = cover_find(s->x + 1, s->y, s->org[2], 16);
        int l = cover_find(s->x - 1, s->y, s->org[2], 16);
        int u = cover_find(s->x, s->y + 1, s->org[2], 16);
        int b = cover_find(s->x, s->y - 1, s->org[2], 16);
        if (r >= 0 && l >= 0) hx = (height[r] - height[l]) / (2 * COVER_GRID);
        if (u >= 0 && b >= 0) hy = (height[u] - height[b]) / (2 * COVER_GRID);
        float nz = 1.0f / sqrtf(1 + hx * hx + hy * hy);
        // light from above and from one side shapes the drifts
        float shade = 0.5f + 0.5f * nz + 0.6f * (hx - hy) * nz;
        // thinner snow near its edges catches less light, so drifts look round
        float inner = Q_clipf((s->cover - 0.6f) / 0.4f, 0, 1);
        shade *= 0.72f + 0.28f * inner;

        vec3_t pos, light;
        VectorSet(pos, s->org[0], s->org[1], s->org[2] + 16);
        R_LightPoint(pos, light);
        // snow is white: keep the light's brightness, but only a little of
        // its color, and lift dark areas so it still reads as snow
        float luma = light[0] * 0.2126f + light[1] * 0.7152f + light[2] * 0.0722f;
        float level = 0.35f + 0.9f * min(luma, 1.2f);
        float sparkle = 0.9f + 0.1f * cover_noise(s->org[0] * 7.3f, s->org[1] * 7.3f);
        float k = shade * sparkle * level * 255;
        for (int c = 0; c < 3; c++) {
            static const float albedo[3] = { 0.94f, 0.96f, 1.0f };
            float tint = 0.75f + 0.25f * (luma > 0.001f ? light[c] / luma : 1.0f);
            v->color.u8[c] = Q_clip(tint * albedo[c] * k, 0, 255);
        }
        v->color.u8[3] = 255;

        // snow starts as a dusting everywhere at about the same time, a
        // little earlier in the most open spots, and the renderer breaks its
        // edges up finely; it then takes a while to reach its full depth
        float patch = cover_noise(s->org[0] * 1.5f + 517, s->org[1] * 1.5f - 233);
        v->start = Q_clipf(0.12f * (1 - inner) + 0.12f * patch, 0, 0.5f);
        v->length = max(min(0.45f + 0.3f * s->noise, 1 - v->start), 0.05f);
        v->height = height[i];
        VectorCopy(s->org, v->origin);
        v->origin[0] += shift[i][0];
        v->origin[1] += shift[i][1];
    }
    cover.num_vertices = cover.num_samples;

    // strips: a quad between each point and the next one along the strip,
    // from copies of the points moved to both sides
    int (*side)[2][2] = Z_Malloc(sizeof(*side) * cover.num_samples);
    memset(side, 0xff, sizeof(*side) * cover.num_samples);
    for (int i = 0; i < cover.num_samples; i++) {
        for (int a = 0; a < 2; a++) {
            if (!(strip_axes[i] & BIT(a)))
                continue;
            for (int k = 0; k < 2; k++) {
                r_snow_vertex_t *v = &cover.vertices[cover.num_vertices];
                *v = cover.vertices[i];
                v->origin[a] += strip[i][a][k];
                side[i][a][k] = cover.num_vertices++;
            }
        }
    }
    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        for (int a = 0; a < 2; a++) {
            if (!(strip_axes[i] & BIT(a)))
                continue;
            // the next point runs along the other axis
            int j = cover_find(s->x + a, s->y + !a, s->org[2], 16);
            if (j < 0 || !(strip_axes[j] & BIT(a)))
                continue;
            if (height[i] <= 0 && height[j] <= 0)
                continue;
            add_triangle(side[i][a][0], side[i][a][1], side[j][a][1]);
            add_triangle(side[i][a][0], side[j][a][1], side[j][a][0]);
        }
    }
    Z_Free(side);

    add_skirts();

    Z_Free(strip_axes);
    Z_Free(strip);
    Z_Free(shift);
    Z_Free(bank);
    Z_Free(height);
}

static void cover_build(void)
{
#if USE_DEBUG
    unsigned start = Sys_Milliseconds();
#endif
    float depth = Cvar_ClampValue(cl_snow_cover, 0, 32);

    cover_free();
    cover.built = true;
    cover.start_time = cl.time;

    cover.samples = Z_Malloc(sizeof(*cover.samples) * COVER_MAX_SAMPLES);
    cover.hash = Z_Malloc(sizeof(*cover.hash) * COVER_HASH);
    memset(cover.hash, 0xff, sizeof(*cover.hash) * COVER_HASH);

    // The world's faces are those of no inline model (doors, platforms and
    // the like move, so they get no snow); the loader leaves model 0's own
    // face range empty. Flakes also fall onto liquids, but snow doesn't
    // settle there.
    byte *inline_face = Z_Mallocz(cl.bsp->numfaces);
    for (int i = 1; i < cl.bsp->nummodels; i++) {
        const mmodel_t *mod = &cl.bsp->models[i];
        for (int j = 0; j < mod->numfaces; j++)
            inline_face[mod->firstface - cl.bsp->faces + j] = 1;
    }
    for (int i = 0; i < cl.bsp->numfaces; i++) {
        const mface_t *face = &cl.bsp->faces[i];
        int flags = face->texinfo->c.flags;
        if (inline_face[i] || (flags & (SURF_SKY | SURF_NODRAW)))
            continue;
        if ((flags & (SURF_TRANS33 | SURF_TRANS66)) && !(flags & SURF_WARP))
            continue;   // glass
        cover_add_face(face, flags & SURF_WARP);
    }
    Z_Free(inline_face);
    cover_fill_holes();

    build_spawn_index();
    if (depth > 0 && cover.num_samples)
        build_mesh(depth);

#if USE_DEBUG
    Com_DPrintf("Snow: %d grid points, %d open, %d cover triangles, %u ms\n",
                cover.num_samples, cover.num_spawn_points, cover.num_indices / 3,
                Sys_Milliseconds() - start);
#endif
    cover.dirty = cover.num_indices > 0;
}

// How far the snow cover has grown since the map started, 0..1.
static float cover_amount(void)
{
    float time = Cvar_ClampValue(cl_snow_cover_time, 0, 3600);

    if (!time)
        return 1;
    return Q_clipf((cl.time - cover.start_time) / (time * 1000.0f), 0.001f, 1);
}

/*
==============
CL_WeatherRefreshChanged

The renderer forgets the snow cover when it restarts; send it again.
==============
*/
void CL_WeatherRefreshChanged(void)
{
    cover.dirty = cover.num_indices > 0;
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
    if (cover.built && cls.ref_initialized)
        R_SetSnowCover(NULL, 0, NULL, 0);
    cover_free();
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
    cl_snow_cover = Cvar_Get("cl_snow_cover", "6", CVAR_ARCHIVE);
    cl_snow_cover->changed = cl_snow_changed;
    cl_snow_cover_time = Cvar_Get("cl_snow_cover_time", "300", CVAR_ARCHIVE);
}
