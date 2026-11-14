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
// Snow cover (cl_snow_cover) is a heightfield mesh on upward-facing world
// faces under open sky, on a world-aligned grid of COVER_GRID units so that
// neighbouring faces share points. Coverage is blurred so that drifts taper
// at their edges and sink below the floor there; heights follow a smooth
// noise. The mesh grows from nothing to full depth over cl_snow_cover_time
// and is drawn by the renderer (R_SetSnowCover).
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
static cvar_t   *cl_snow_cover;
static cvar_t   *cl_snow_cover_time;

#define COVER_GRID          12
#define COVER_HASH          65536   // grid column hash, power of two
#define COVER_MAX_SAMPLES   200000

typedef struct {
    vec3_t  org;        // on the surface
    int     x, y;       // grid coordinates
    int     next;       // next sample in the same hash chain
    float   cover;      // 0 bare .. 1 fully under open sky, blurred
    float   noise;      // 0..1
    float   height;     // at full depth, may be negative at the edges
    color_t color;
} cover_sample_t;

static struct {
    cover_sample_t  *samples;
    int             num_samples;
    int             *hash;          // first sample per grid column
    uint32_t        *indices;
    int             num_indices;
    vec3_t          *positions;
    color_t         *colors;
    bool            built;
    bool            dirty;          // resend to the renderer
    float           sent_amount;    // growth last sent
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
static void cover_update(void);

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
        if (cover.sent_amount || cover.built) {
            R_SetSnowCover(NULL, NULL, 0, NULL, 0);
            cover_free();
        }
        return;
    }

    if (cl_snow_cover->value > 0) {
        if (!cover.built)
            cover_build();
        cover_update();
    } else if (cover.built) {
        R_SetSnowCover(NULL, NULL, 0, NULL, 0);
        cover_free();
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

static bool cover_exposed(const vec3_t org)
{
    static const vec3_t zero = { 0, 0, 0 };
    trace_t tr;
    vec3_t start, end;

    VectorSet(start, org[0], org[1], org[2] + 1);
    VectorSet(end, org[0], org[1], org[2] + 8192);
    CM_BoxTrace(&tr, start, end, zero, zero, cl.bsp->nodes, MASK_SOLID, cl.csr.extended);
    return !tr.startsolid && tr.fraction < 1.0f && tr.surface && (tr.surface->flags & SURF_SKY);
}

static void cover_add_face(const mface_t *face)
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
            s->cover = cover_exposed(s->org);
            s->noise = cover_noise(x, y);
            unsigned h = cover_hash(gx, gy);
            s->next = cover.hash[h];
            cover.hash[h] = cover.num_samples++;
        }
    }
}

static void cover_free(void)
{
    Z_Freep(&cover.samples);
    Z_Freep(&cover.hash);
    Z_Freep(&cover.indices);
    Z_Freep(&cover.positions);
    Z_Freep(&cover.colors);
    memset(&cover, 0, sizeof(cover));
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
    // face range empty.
    byte *inline_face = Z_Mallocz(cl.bsp->numfaces);
    for (int i = 1; i < cl.bsp->nummodels; i++) {
        const mmodel_t *mod = &cl.bsp->models[i];
        for (int j = 0; j < mod->numfaces; j++)
            inline_face[mod->firstface - cl.bsp->faces + j] = 1;
    }
    for (int i = 0; i < cl.bsp->numfaces; i++) {
        const mface_t *face = &cl.bsp->faces[i];
        if (inline_face[i])
            continue;
        if (face->texinfo->c.flags & (SURF_SKY | SURF_WARP | SURF_TRANS33 | SURF_TRANS66 | SURF_NODRAW))
            continue;
        cover_add_face(face);
    }
    Z_Free(inline_face);

#if USE_DEBUG
    int exposed = 0;
    for (int i = 0; i < cover.num_samples; i++)
        exposed += cover.samples[i].cover > 0;
    Com_DPrintf("Snow cover: %d grid points, %d under open sky\n",
                cover.num_samples, exposed);
#endif
    if (!cover.num_samples)
        return;

    // blur coverage twice over the 3x3 neighbourhood; missing neighbours are
    // bare ground, so drifts taper towards walls and roofs
    float *tmp = Z_Malloc(sizeof(float) * cover.num_samples);
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < cover.num_samples; i++) {
            const cover_sample_t *s = &cover.samples[i];
            float sum = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int j = cover_find(s->x + dx, s->y + dy, s->org[2], 16);
                    if (j >= 0)
                        sum += cover.samples[j].cover;
                }
            tmp[i] = sum / 9;
        }
        for (int i = 0; i < cover.num_samples; i++)
            cover.samples[i].cover = tmp[i];
    }
    Z_Free(tmp);

    for (int i = 0; i < cover.num_samples; i++) {
        cover_sample_t *s = &cover.samples[i];
        // zero height where the blurred coverage is 0.6, so the visible edge
        // of a drift is a smooth contour well inside the jagged outline of
        // the grid, which stays under the floor
        float f = Q_clipf((s->cover - 0.6f) / 0.4f, -1.25f, 1.0f);
        s->height = depth * (0.3f + 0.7f * s->noise) * f;
    }

    // two triangles per grid square that has any snow
    cover.indices = Z_Malloc(sizeof(uint32_t) * 6 * cover.num_samples);
    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        int r = cover_find(s->x + 1, s->y, s->org[2], 16);
        int u = cover_find(s->x, s->y + 1, s->org[2], 16);
        int d = cover_find(s->x + 1, s->y + 1, s->org[2], 16);

        if (r < 0 || u < 0 || d < 0)
            continue;
        if (s->cover <= 0 && cover.samples[r].cover <= 0 &&
            cover.samples[u].cover <= 0 && cover.samples[d].cover <= 0)
            continue;

        uint32_t *idx = &cover.indices[cover.num_indices];
        idx[0] = i; idx[1] = r; idx[2] = d;
        idx[3] = i; idx[4] = d; idx[5] = u;
        cover.num_indices += 6;
    }

    // lighting and shading are fixed; only heights change as snow grows
    cover.positions = Z_Malloc(sizeof(vec3_t) * cover.num_samples);
    cover.colors = Z_Malloc(sizeof(color_t) * cover.num_samples);
    for (int i = 0; i < cover.num_samples; i++) {
        cover_sample_t *s = &cover.samples[i];
        float hx = 0, hy = 0;
        int r = cover_find(s->x + 1, s->y, s->org[2], 16);
        int l = cover_find(s->x - 1, s->y, s->org[2], 16);
        int u = cover_find(s->x, s->y + 1, s->org[2], 16);
        int b = cover_find(s->x, s->y - 1, s->org[2], 16);
        if (r >= 0 && l >= 0) hx = (cover.samples[r].height - cover.samples[l].height) / (2 * COVER_GRID);
        if (u >= 0 && b >= 0) hy = (cover.samples[u].height - cover.samples[b].height) / (2 * COVER_GRID);
        float nz = 1.0f / sqrtf(1 + hx * hx + hy * hy);
        // light from above and from one side shapes the drifts
        float shade = 0.5f + 0.5f * nz + 0.6f * (hx - hy) * nz;
        // thinner snow near its edges catches less light, so drifts look round
        shade *= 0.72f + 0.28f * Q_clipf((s->cover - 0.6f) / 0.4f, 0, 1);

        vec3_t pos, light;
        VectorSet(pos, s->org[0], s->org[1], s->org[2] + 16);
        R_LightPoint(pos, light);
        // snow is white: keep the light's brightness, but only a little of
        // its color, and lift dark areas so it still reads as snow
        float luma = light[0] * 0.2126f + light[1] * 0.7152f + light[2] * 0.0722f;
        float level = 0.35f + 0.9f * min(luma, 1.2f);
        float sparkle = 0.9f + 0.1f * cover_noise(s->org[0] * 7.3f, s->org[1] * 7.3f);
        float k = shade * sparkle * level * 255;
        vec3_t tint;
        for (int c = 0; c < 3; c++)
            tint[c] = 0.75f + 0.25f * (luma > 0.001f ? light[c] / luma : 1.0f);
        s->color.u8[0] = Q_clip(tint[0] * 0.94f * k, 0, 255);
        s->color.u8[1] = Q_clip(tint[1] * 0.96f * k, 0, 255);
        s->color.u8[2] = Q_clip(tint[2] * 1.00f * k, 0, 255);
        s->color.u8[3] = 255;
        cover.colors[i] = s->color;
    }

#if USE_DEBUG
    Com_DPrintf("Snow cover: %d points, %d triangles, %u ms\n",
                cover.num_samples, cover.num_indices / 3, Sys_Milliseconds() - start);
#endif
    cover.dirty = true;
}

// Sends the mesh at the current growth when it changed enough to see.
static void cover_update(void)
{
    float time = Cvar_ClampValue(cl_snow_cover_time, 0, 3600);
    float amount = time ? Q_clipf((cl.time - cover.start_time) / (time * 1000.0f), 0, 1) : 1;

    if (!cover.num_indices)
        return;
    if (!cover.dirty && fabsf(amount - cover.sent_amount) < 0.02f)
        return;

    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        // half a unit below the floor where there is no snow yet
        VectorSet(cover.positions[i], s->org[0], s->org[1],
                  s->org[2] + s->height * amount - 0.5f);
    }

    R_SetSnowCover(cover.positions, cover.colors, cover.num_samples,
                   cover.indices, cover.num_indices);
    cover.sent_amount = amount;
    cover.dirty = false;
}

/*
==============
CL_WeatherRefreshChanged

The renderer forgets the snow cover when it restarts; send it again.
==============
*/
void CL_WeatherRefreshChanged(void)
{
    cover.dirty = true;
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
        R_SetSnowCover(NULL, NULL, 0, NULL, 0);
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
