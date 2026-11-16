//
// weather.c -- client-side snow and rain
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
// snow has come (refdef_t.snow_amount); the mesh itself is sent once. Each
// map has a prevailing wind: snow piles up against the walls it blows into
// and its drifts are ridges across it, and the flakes drift with it. No
// snow settles near lava, and flakes melt above it.
//
// Small columns probed from the viewer's height, a few per frame, tell how
// open the sky is around the viewer, for the sound volumes and footsteps.
//
// Rain (cl_rain) falls from the same open points as snow, much faster, as
// streaks that splash where they land and ripple on liquids.
//
// Sounds are optional files in sound/weather/: a wind loop and a soft snow
// ambience whose volume follows how much open sky is around the viewer,
// occasional gusts, and footsteps on ground under open sky. The game has no
// such sounds; without the files, the wind, gusts and rain are synthesized
// (sound/synth.c) and the rest is silent. The rain has a loop of its own.
//

#include "client.h"

#define WEATHER_CELL        48
#define WEATHER_BAND        256     // viewer height band of a cached column
#define WEATHER_HASH        16384   // column cache size, power of two
#define WEATHER_MAX_FLAKES  4096
#define WEATHER_MAX_DROPS   3072
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
    int     sample;     // cover sample it falls onto
    bool    active;
} snowflake_t;

typedef struct {
    vec3_t  org;
    float   ground_z;
    float   fall;       // units per second
    int     sample;     // cover sample it falls onto
    bool    active;
} raindrop_t;

static cvar_t   *cl_snow;
static cvar_t   *cl_snow_volume;
static cvar_t   *cl_snow_cover;
static cvar_t   *cl_snow_cover_time;
static cvar_t   *cl_snow_craters;
static cvar_t   *cl_rain;
static cvar_t   *cl_rain_volume;
static cvar_t   *cl_rain_puddles;

#define COVER_GRID          12
#define COVER_HASH          65536   // grid column hash, power of two
#define COVER_MAX_SAMPLES   200000

#define LAVA_HEAT           48      // no snow settles this close to lava
#define LAVA_HEAT_UP        112     // or this far above it
#define LAVA_MELT           24      // and flakes melt this far above it

#define PUFF_DISTANCE       320     // flakes landing this close puff up
#define SPLASH_DISTANCE     400     // drops landing this close splash
#define PUDDLE_TIME         120   // seconds for puddles to fill
#define PUDDLE_SHARE        0.36f   // of the noise range that is puddle

#define SPAWN_BUCKET        256     // flake spawn index cell
#define SPAWN_HASH          4096    // power of two

typedef struct {
    vec3_t  org;        // on the surface
    int     x, y;       // grid coordinates
    int     next;       // next sample in the same hash chain
    float   sky_z;      // height of the sky above, if open
    bool    open;       // the sky is above
    bool    liquid;
    bool    lava;
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
    r_cover_kind_t  kind;           // snow, or rain puddles
    bool            dirty;          // send to the renderer
    int             start_time;     // cl.time when the map started
} cover;

#define WEATHER_STEPS   3
#define WEATHER_GUSTS   2

static struct {
    qhandle_t   wind;
    qhandle_t   ambience;
    qhandle_t   rain;
    qhandle_t   gusts[WEATHER_GUSTS];
    qhandle_t   steps[WEATHER_STEPS];
    int         num_gusts;
    int         num_steps;
    int         last_step;
} weather_sfx;

static float    weather_exposure;   // 0 indoors .. 1 under open sky
static float    weather_gust_timer;

static void update_exposure(const vec3_t view, float dt);
static void update_sounds(float snow, float rain, float dt);
static void stop_sounds(void);
static void cover_free(void);
static void cover_build(void);
static float cover_amount(void);
static void update_candidates(const vec3_t view);

static weather_column_t weather_columns[WEATHER_HASH];
static snowflake_t      snowflakes[WEATHER_MAX_FLAKES];
static raindrop_t       raindrops[WEATHER_MAX_DROPS];
static int      weather_probes;     // probes left this frame
static float    weather_wind[2];
static float    weather_wind_target[2];
static float    weather_wind_timer;
static vec2_t   weather_prevailing;     // unit direction the wind blows to

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

// Picks where a falling flake or drop starts: above a random open point
// near the viewer, below the sky, at most WEATHER_HEIGHT above the viewer.
// With anywhere, it starts at any height down to the ground, so that a new
// area is already full of them; otherwise at the top. Returns the cover
// sample it falls onto, or -1 if no place was found.
static int spawn_point(const vec3_t view, bool anywhere, vec3_t org)
{
    for (int tries = 0; tries < 8 && cover.num_candidates; tries++) {
        int sample = cover.candidates[Q_rand_uniform(cover.num_candidates)];
        const cover_sample_t *s = &cover.samples[sample];
        float x = s->org[0] + crand() * COVER_GRID * 0.5f;
        float y = s->org[1] + crand() * COVER_GRID * 0.5f;
        float dx = x - view[0], dy = y - view[1];

        if (dx * dx + dy * dy > WEATHER_RADIUS * WEATHER_RADIUS)
            continue;

        float top = min(s->sky_z - 2, view[2] + WEATHER_HEIGHT);
        if (top <= s->org[2])
            continue;

        org[0] = x;
        org[1] = y;
        org[2] = anywhere ? s->org[2] + frand() * (top - s->org[2]) : top - frand() * 16;
        return sample;
    }

    return -1;
}

static bool spawn_flake(snowflake_t *f, const vec3_t view, bool anywhere)
{
    int sample = spawn_point(view, anywhere, f->org);

    f->active = sample >= 0;
    if (!f->active)
        return false;

    const cover_sample_t *s = &cover.samples[sample];
    f->ground_z = s->org[2] + (s->lava ? LAVA_MELT : 0);
    f->sample = sample;
    f->fall = 60 + frand() * 40;
    f->phase = frand() * 2 * M_PIf;
    f->size = 0.8f + frand() * 0.7f;
    return true;
}

static bool spawn_drop(raindrop_t *d, const vec3_t view, bool anywhere)
{
    int sample = spawn_point(view, anywhere, d->org);

    d->active = sample >= 0;
    if (!d->active)
        return false;

    const cover_sample_t *s = &cover.samples[sample];
    d->ground_z = s->org[2] + (s->lava ? LAVA_MELT : 0);
    d->sample = sample;
    d->fall = 560 + frand() * 140;
    return true;
}

// Each map has a prevailing wind, the same for every player, from its name.
// It shapes the drifts of the snow cover, and the flakes drift with it.
static void set_prevailing_wind(void)
{
    float ang = DEG2RAD(Com_HashString(cl.mapname, 360));

    weather_prevailing[0] = cosf(ang);
    weather_prevailing[1] = sinf(ang);
}

// Gusts: the wind turns a little either way from the prevailing direction
// and changes strength every few seconds.
static void update_wind(float dt)
{
    weather_wind_timer -= dt;
    if (weather_wind_timer <= 0) {
        float ang = atan2f(weather_prevailing[1], weather_prevailing[0]) + crand() * 0.45f;
        float speed = 12 + frand() * 36;
        weather_wind_target[0] = cosf(ang) * speed;
        weather_wind_target[1] = sinf(ang) * speed;
        weather_wind_timer = 6 + frand() * 8;
    }

    float k = min(dt * 0.3f, 1.0f);
    weather_wind[0] += (weather_wind_target[0] - weather_wind[0]) * k;
    weather_wind[1] += (weather_wind_target[1] - weather_wind[1]) * k;
}

// How high the snow cover stands at a sample, as far as it has grown, so
// flakes land on it instead of sinking in. Craters are left out.
static float cover_top(int sample, float amount)
{
    if (!cover.num_indices || sample < 0 || sample >= cover.num_vertices)
        return 0;

    const r_snow_vertex_t *v = &cover.vertices[sample];
    if (v->height <= 0)
        return 0;
    float g = Q_clipf((amount - v->start) / max(v->length, 0.001f), 0, 1);
    return v->height * g * g * (3 - 2 * g);
}

// A flake that lands close to the viewer throws up a tiny puff of powder.
static void land_flake(const snowflake_t *f, const vec3_t view)
{
    const cover_sample_t *s = &cover.samples[f->sample];
    vec3_t d;

    if (s->liquid)
        return;
    VectorSubtract(f->org, view, d);
    if (DotProduct(d, d) > PUFF_DISTANCE * PUFF_DISTANCE || DotProduct(d, cl.v_forward) < 0)
        return;

    for (int i = 0; i < 2; i++) {
        cparticle_t *p = CL_AllocParticle();
        if (!p)
            return;
        p->time = cl.time;
        p->color = -1;
        p->rgba.u32 = U32_WHITE;
        VectorCopy(f->org, p->org);
        p->vel[0] = crand() * 12 + weather_wind[0] * 0.3f;
        p->vel[1] = crand() * 12 + weather_wind[1] * 0.3f;
        p->vel[2] = 6 + frand() * 10;
        VectorSet(p->accel, 0, 0, -20);
        p->scale = 0.5f;
        p->alpha = 0.45f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
    }
}

// A drop that lands close to the viewer splashes: a few droplets that
// jump up from the ground, or a ring spreading on a liquid or a puddle.
static void land_drop(const raindrop_t *d, const vec3_t view, const vec3_t vel,
                      float cover_grown)
{
    const cover_sample_t *s = &cover.samples[d->sample];
    vec3_t delta;
    bool liquid = s->liquid || (cover.kind == R_COVER_PUDDLES &&
                                cover_top(d->sample, cover_grown) > 0.5f);

    if (s->lava)
        return;     // it hisses away above the lava
    VectorSubtract(d->org, view, delta);
    if (DotProduct(delta, delta) > SPLASH_DISTANCE * SPLASH_DISTANCE ||
        DotProduct(delta, cl.v_forward) < 0 || frand() > 0.6f)
        return;

    int count = liquid ? 6 : 2 + (Q_rand() & 1);
    for (int i = 0; i < count; i++) {
        cparticle_t *p = CL_AllocParticle();
        if (!p)
            return;
        p->time = cl.time;
        p->color = -1;
        p->rgba.u8[0] = 200;
        p->rgba.u8[1] = 210;
        p->rgba.u8[2] = 225;
        p->rgba.u8[3] = 255;
        VectorCopy(d->org, p->org);
        p->org[2] += 0.5f;
        p->scale = 0.4f;
        if (liquid) {
            float ang = (i + frand() * 0.5f) * (2 * M_PIf / count);
            VectorSet(p->vel, cosf(ang) * 30, sinf(ang) * 30, 0);
            VectorClear(p->accel);
            p->alpha = 0.3f;
            p->alphavel = -1.0f / 0.35f;
        } else {
            VectorSet(p->vel, crand() * 25 + vel[0] * 0.05f,
                      crand() * 25 + vel[1] * 0.05f, 30 + frand() * 35);
            VectorSet(p->accel, 0, 0, -400);
            p->alpha = 0.4f;
            p->alphavel = -1.0f / (0.2f + frand() * 0.1f);
        }
    }
}

static void add_flakes(float amount, const vec3_t view, float dt, float cover_grown)
{
    float t = cl.time * 0.001f;
    particle_t p;

    // light snow is sparse, so it hides little and costs little
    int count = min((int)(400 * amount * sqrtf(amount)), WEATHER_MAX_FLAKES);

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
            } else if (f->org[2] <= f->ground_z + cover_top(f->sample, cover_grown)) {
                land_flake(f, view);
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

static void add_drops(float amount, const vec3_t view, float dt, float cover_grown)
{
    // the wind slants the rain a little
    vec3_t vel = { weather_wind[0] * 3, weather_wind[1] * 3, 0 };
    r_streak_t streak;

    int count = min((int)(500 * amount * powf(amount, 0.25f)), WEATHER_MAX_DROPS);

    for (int i = 0; i < count; i++) {
        raindrop_t *r = &raindrops[i];

        vel[2] = -r->fall;
        if (!r->active) {
            if (!spawn_drop(r, view, true))
                continue;
        } else {
            VectorMA(r->org, dt, vel, r->org);

            float dx = r->org[0] - view[0];
            float dy = r->org[1] - view[1];
            if (dx * dx + dy * dy > WEATHER_RADIUS * WEATHER_RADIUS) {
                if (!spawn_drop(r, view, true))
                    continue;
            } else if (r->org[2] <= r->ground_z + cover_top(r->sample, cover_grown)) {
                r->org[2] = r->ground_z + cover_top(r->sample, cover_grown);
                land_drop(r, view, vel, cover_grown);
                if (!spawn_drop(r, view, false))
                    continue;
            }
        }

        vec3_t d;
        VectorSubtract(r->org, view, d);
        if (DotProduct(d, cl.v_forward) < -32)
            continue;

        // like snow, distant rain fades
        float dist2 = d[0] * d[0] + d[1] * d[1];
        float alpha = 0.3f;
        if (dist2 > WEATHER_FADE * WEATHER_FADE) {
            float fade = (sqrtf(dist2) - WEATHER_FADE) / (WEATHER_RADIUS - WEATHER_FADE);
            alpha *= 1 - 0.8f * min(fade, 1.0f);
        }

        // a streak as long as the drop falls in about a thirtieth of a
        // second, ending at the drop
        vel[2] = -r->fall;
        VectorCopy(r->org, streak.end);
        VectorMA(r->org, -0.035f, vel, streak.start);
        streak.width = 0.35f;
        streak.rgba.u8[0] = 190;
        streak.rgba.u8[1] = 200;
        streak.rgba.u8[2] = 215;
        streak.rgba.u8[3] = alpha * 255;
        V_AddStreak(&streak);
    }
}

/*
==============
CL_AddWeather

Moves the flakes and drops and adds them to the refresh.
==============
*/
void CL_AddWeather(void)
{
    float snow = Cvar_ClampValue(cl_snow, 0, 4);
    float rain = Cvar_ClampValue(cl_rain, 0, 4);
    float dt = min(cls.frametime, 0.1f);
    const float *view = cl.refdef.vieworg;

    cl.refdef.snow_amount = 0;
    cl.refdef.rain_amount = 0;

    if ((!snow && !rain) || !cl.bsp || cls.state != ca_active) {
        stop_sounds();
        if (cover.built) {
            R_SetSnowCover(R_COVER_SNOW, NULL, 0, NULL, 0);
            cover_free();
        }
        return;
    }

    if (!cover.built)
        cover_build();
    if (cover.start_time < 0)
        cover.start_time = cl.time;
    if (cover.dirty) {
        R_SetSnowCover(cover.kind, cover.vertices, cover.num_vertices, cover.indices, cover.num_indices);
        cover.dirty = false;
    }
    if (cover.num_indices)
        cl.refdef.snow_amount = cover_amount();
    float cover_grown = cl.refdef.snow_amount;
    // heavier rain ripples puddles more
    cl.refdef.rain_amount = min(rain, 2.0f) * 0.7f;
    update_candidates(view);

    weather_probes = WEATHER_PROBES;
    update_wind(dt);
    update_exposure(view, dt);
    update_sounds(snow, rain, dt);

    if (snow)
        add_flakes(snow, view, dt, cover_grown);
    if (rain)
        add_drops(rain, view, dt, cover_grown);
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

static void update_sounds(float snow, float rain, float dt)
{
    float snow_volume = Cvar_ClampValue(cl_snow_volume, 0, 1) * min(snow, 1.0f);
    float rain_volume = Cvar_ClampValue(cl_rain_volume, 0, 1) * min(rain, 1.0f);
    // the wind blows in the rain too, less loudly than the rain itself
    float volume = max(snow_volume, rain_volume * 0.5f);

    // wind stays faintly audible indoors, like through windows, and rain
    // drums on the roof
    S_SetLocalLoop(0, weather_sfx.wind, volume * (0.12f + 0.55f * weather_exposure));
    S_SetLocalLoop(1, weather_sfx.ambience, snow_volume * 0.35f * weather_exposure);
    S_SetLocalLoop(2, weather_sfx.rain, rain_volume * (0.15f + 0.7f * weather_exposure));

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
    S_SetLocalLoop(2, 0, 0);
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

    // without files of their own, wind and gusts are made in code (synth.c)
    if (!weather_sfx.wind)
        weather_sfx.wind = S_RegisterSound("#synth:weather/wind.wav");
    if (!weather_sfx.num_gusts && (weather_sfx.gusts[0] = S_RegisterSound("#synth:weather/gust.wav")))
        weather_sfx.num_gusts = 1;
    weather_sfx.rain = register_optional("weather/rain.wav");
    if (!weather_sfx.rain)
        weather_sfx.rain = S_RegisterSound("#synth:weather/rain.wav");
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

// Whether the sky is above a point on a floor, and how high. The floor of a
// pool isn't: snow settles on the liquid's surface instead.
static bool cover_exposed(const vec3_t org, float *sky_z)
{
    static const vec3_t zero = { 0, 0, 0 };
    static const float nudges[][2] = { { 0, 0 }, { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 } };
    trace_t tr;
    vec3_t start, end;

    VectorSet(start, org[0], org[1], org[2] + 2);
    if (CM_PointContents(start, cl.bsp->nodes, cl.csr.extended) & MASK_WATER)
        return false;

    for (int i = 0; i < q_countof(nudges); i++) {
        VectorSet(start, org[0] + nudges[i][0], org[1] + nudges[i][1], org[2] + 1);
        VectorSet(end, start[0], start[1], org[2] + 8192);
        CM_BoxTrace(&tr, start, end, zero, zero, cl.bsp->nodes, MASK_SOLID, cl.csr.extended);
        *sky_z = tr.endpos[2];
        if (!tr.startsolid && tr.fraction < 1.0f && tr.surface && (tr.surface->flags & SURF_SKY))
            return true;
        // A point right at the foot of a wall can graze it on the way up;
        // try again a little away from it. A ceiling above is a real roof.
        if (!tr.startsolid && tr.fraction < 1.0f && tr.plane.normal[2] < -0.7f)
            return false;
    }
    return false;
}

static bool cover_add_sample(float x, float y, float z, int gx, int gy,
                             bool liquid, bool lava)
{
    if (cover_find(gx, gy, z, 2) >= 0)
        return true;    // shared with a neighbouring face
    if (cover.num_samples >= COVER_MAX_SAMPLES)
        return false;

    cover_sample_t *s = &cover.samples[cover.num_samples];
    VectorSet(s->org, x, y, z);
    s->x = gx;
    s->y = gy;
    s->open = cover_exposed(s->org, &s->sky_z);
    s->liquid = liquid;
    s->lava = lava;
    s->cover = s->open && !liquid;
    s->noise = cover_noise(x, y);
    unsigned h = cover_hash(gx, gy);
    s->next = cover.hash[h];
    cover.hash[h] = cover.num_samples++;
    return true;
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
    int added = 0;

    // what lies under a liquid's surface, in the middle of the face
    bool lava = false;
    if (liquid) {
        vec3_t mid = { 0, 0, 0 };
        for (int i = 0; i < n; i++)
            VectorAdd(mid, pts[i], mid);
        VectorScale(mid, 1.0f / n, mid);
        VectorMA(mid, -2, normal, mid);
        lava = CM_PointContents(mid, cl.bsp->nodes, cl.csr.extended) & CONTENTS_LAVA;
    }

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
            if (!cover_add_sample(x, y, z, gx, gy, liquid, lava))
                return;
            added++;
        }
    }
    if (added)
        return;

    // A face narrower than the grid, such as a ledge or the top of a thin
    // wall, can fall between its points all along. Put points along its
    // middle instead, at grid steps along its length; each takes the grid
    // column it lies in, so the mesh joins them into a strip.
    int l = maxs[0] - mins[0] >= maxs[1] - mins[1] ? 0 : 1;
    int w = !l;
    for (int g = (int)ceilf(mins[l] / COVER_GRID); g * COVER_GRID <= maxs[l]; g++) {
        float c = g * COVER_GRID, lo = 1e9f, hi = -1e9f;

        for (int i = 0; i < n; i++) {
            const float *a = pts[i], *b = pts[(i + 1) % n];
            if ((a[l] - c) * (b[l] - c) > 0 || a[l] == b[l])
                continue;
            float v = a[w] + (c - a[l]) / (b[l] - a[l]) * (b[w] - a[w]);
            lo = min(lo, v);
            hi = max(hi, v);
        }
        if (hi - lo < 2)
            continue;   // too thin to hold snow

        float p[2];
        p[l] = c;
        p[w] = (lo + hi) * 0.5f;
        int gp[2];
        gp[l] = g;
        gp[w] = (int)floorf(p[w] / COVER_GRID + 0.5f);
        float z = (dist - normal[0] * p[0] - normal[1] * p[1]) / normal[2];
        if (!cover_add_sample(p[0], p[1], z, gp[0], gp[1], liquid, lava))
            return;
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
            liquid |= o->liquid;
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
        f->liquid = liquid;
        f->lava = false;
        f->cover = f->open && !liquid;
        f->noise = cover_noise(p[0], p[1]);
        unsigned h = cover_hash(x, y);
        f->next = cover.hash[h];
        cover.hash[h] = cover.num_samples++;
    }
}

// Lava is hot: no snow settles near it.
static int cover_melt_near_lava(void)
{
    int reach = LAVA_HEAT / COVER_GRID;
    int melted = 0;

    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        if (!s->lava)
            continue;
        for (int dy = -reach; dy <= reach; dy++) {
            for (int dx = -reach; dx <= reach; dx++) {
                if (dx * dx + dy * dy > reach * reach)
                    continue;
                // heat rises: it reaches higher than it spreads
                for (int j = cover.hash[cover_hash(s->x + dx, s->y + dy)]; j >= 0;
                     j = cover.samples[j].next) {
                    cover_sample_t *o = &cover.samples[j];
                    float dz = o->org[2] - s->org[2];
                    if (o->x == s->x + dx && o->y == s->y + dy &&
                        dz > -8 && dz < LAVA_HEAT_UP && o->cover) {
                        o->cover = 0;
                        melted++;
                    }
                }
            }
        }
    }
    return melted;
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
    bool *ledge = Z_Mallocz(sizeof(bool) * cover.num_samples);
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
            if (type == EDGE_WALL) {
                // the wind piles snow up against the walls it blows into,
                // and leaves some in the shelter of those it comes over
                float dot = edge_dirs[d][0] * weather_prevailing[0] +
                            edge_dirs[d][1] * weather_prevailing[1];
                bank[i] = max(bank[i], 0.5f + 0.9f * max(dot, 0.0f) + 0.4f * max(-dot, 0.0f));
            }
            else if (type == EDGE_DROP)
                ledge[i] = true;
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

    // drifts against a wall slope away from it, along the wind: upwind from
    // a wall the wind blows into, downwind in the shelter of one
    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        float tail = height[i];
        for (int k = 1; k <= 3; k++) {
            int dx = (int)roundf(weather_prevailing[0] * k);
            int dy = (int)roundf(weather_prevailing[1] * k);
            int j = cover_find(s->x + dx, s->y + dy, s->org[2], 16 * k);
            if (j >= 0)
                tail = max(tail, height[j] * (1 - 0.25f * k));
            j = cover_find(s->x - dx, s->y - dy, s->org[2], 16 * k);
            if (j >= 0 && k < 3)
                tail = max(tail, height[j] * (0.8f - 0.3f * k));
        }
        bank[i] = tail;
    }

    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        // zero height where the blurred coverage is 0.6, so the visible edge
        // of a drift is a smooth contour well inside the jagged outline of
        // the grid, which stays under the floor
        float f = Q_clipf((s->cover - 0.6f) / 0.4f, -1.25f, 1.0f);
        // broad drifts, mounds and small bumps: from a fifth of the depth
        // to almost twice it; the drifts are ridges across the wind
        float u = s->org[0] * weather_prevailing[0] + s->org[1] * weather_prevailing[1];
        float w = s->org[1] * weather_prevailing[0] - s->org[0] * weather_prevailing[1];
        float drift = cover_noise(u * 0.6f - 311, w * 0.25f + 173);
        float bump = cover_noise(s->org[0] * 3 + 91, s->org[1] * 3 - 47);
        float n = 0.45f * drift + 0.35f * s->noise + 0.2f * bump;
        height[i] = depth * (0.2f + 1.6f * powf(n, 1.3f)) * f;
        if (f > 0)
            height[i] += depth * 0.6f * bank[i] * f;
        // snow on a narrow ledge or at the edge of a drop is thin otherwise
        // where the noise is low, and hard to see from below
        if (f > 0 && (ledge[i] || strip_axes[i]))
            height[i] = max(height[i], depth * 0.7f * f);
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
        float shade = 0.5f + 0.5f * nz + 1.5f * (hx - hy) * nz;
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
    Z_Free(ledge);
    Z_Free(bank);
    Z_Free(height);
}

// Rain puddles: a flat layer a little above level floors under open sky.
// Where puddles are comes from noise: its lowest spots fill first and the
// water spreads from them as refdef_t.snow_amount goes from 0 to 1, along
// the same ragged growth front as snow, so the edges are smooth curves and
// not the grid's. Their color is the light there.
static void build_puddles(void)
{
    float *start = Z_Malloc(sizeof(float) * cover.num_samples);
    bool *level = Z_Malloc(sizeof(bool) * cover.num_samples);

    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];

        // water runs off steep slopes and steps
        level[i] = s->cover > 0;
        for (int d = 0; d < 4 && level[i]; d++) {
            int j = cover_find(s->x + edge_dirs[d][0], s->y + edge_dirs[d][1], s->org[2], 16);
            if (j >= 0 && fabsf(cover.samples[j].org[2] - s->org[2]) > 2.5f)
                level[i] = false;
        }

        // the noise's lowest spots fill first; the water reaches the
        // PUDDLE_SHARE contour when the puddles are full
        float n = 0.55f * cover_noise(s->org[0] * 0.7f + 71, s->org[1] * 0.7f - 29) +
                  0.45f * s->noise;
        start[i] = (n - 0.2f) / (PUDDLE_SHARE - 0.2f);
        // never filled where the water runs off, but the edge still cuts
        // smoothly across the squares around it
        if (!level[i])
            start[i] = 2;
    }

    cover.indices = Z_Malloc(sizeof(uint32_t) * 6 * cover.num_samples);
    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        int r = cover_find(s->x + 1, s->y, s->org[2], 16);
        int u = cover_find(s->x, s->y + 1, s->org[2], 16);
        int d = cover_find(s->x + 1, s->y + 1, s->org[2], 16);

        if (r < 0 || u < 0 || d < 0)
            continue;
        // squares the water never reaches, with room for the ragged edge
        if (start[i] > 1.1f && start[r] > 1.1f && start[u] > 1.1f && start[d] > 1.1f)
            continue;
        add_triangle(i, r, d);
        add_triangle(i, d, u);
    }

    cover.vertices = Z_Malloc(sizeof(*cover.vertices) * cover.num_samples);
    for (int i = 0; i < cover.num_samples; i++) {
        const cover_sample_t *s = &cover.samples[i];
        r_snow_vertex_t *v = &cover.vertices[i];
        vec3_t pos, light;

        VectorSet(pos, s->org[0], s->org[1], s->org[2] + 16);
        R_LightPoint(pos, light);
        float luma = light[0] * 0.2126f + light[1] * 0.7152f + light[2] * 0.0722f;
        float bright = 0.25f + 0.9f * min(luma, 1.2f);
        for (int c = 0; c < 3; c++) {
            float tint = 0.6f + 0.4f * (luma > 0.001f ? light[c] / luma : 1.0f);
            v->color.u8[c] = Q_clip(tint * bright * 200, 0, 255);
        }
        v->color.u8[3] = 255;

        VectorCopy(s->org, v->origin);
        v->height = 0.75f;
        v->start = start[i];
        v->length = 0.25f;
    }
    cover.num_vertices = cover.num_samples;

    Z_Free(level);
    Z_Free(start);
}

static void cover_build(void)
{
#if USE_DEBUG
    unsigned start = Sys_Milliseconds();
#endif
    // without snow, the points are for rain to fall on and its puddles
    float depth = cl_snow->value ? Cvar_ClampValue(cl_snow_cover, 0, 32) : 0;
    bool puddles = !cl_snow->value && cl_rain->value && cl_rain_puddles->integer;

    cover_free();
    cover.built = true;
    cover.start_time = -1;      // set on the first frame of the map
    set_prevailing_wind();

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
    int melted = cover_melt_near_lava();

    build_spawn_index();
    cover.kind = R_COVER_SNOW;
    if (depth > 0 && cover.num_samples) {
        build_mesh(depth);
    } else if (puddles && cover.num_samples) {
        cover.kind = R_COVER_PUDDLES;
        build_puddles();
    }

#if USE_DEBUG
    Com_DPrintf("Snow: %d grid points, %d open, %d melted by lava, %d cover triangles, %u ms\n",
                cover.num_samples, cover.num_spawn_points, melted, cover.num_indices / 3,
                Sys_Milliseconds() - start);
#else
    (void)melted;
#endif
    cover.dirty = cover.num_indices > 0;
}

// How far the snow cover has grown, or the puddles have filled, since the
// map started, 0..1.
static float cover_amount(void)
{
    float time = cover.kind == R_COVER_PUDDLES ? PUDDLE_TIME :
                 Cvar_ClampValue(cl_snow_cover_time, 0, 3600);

    if (!time)
        return 1;
    return Q_clipf((cl.time - cover.start_time) / (time * 1000.0f), 0.001f, 1);
}

/*
==============
CL_WeatherRefreshChanged

Called when a map's refresh data is loaded, and when the renderer
restarts, which forgets the snow cover; send it again.
==============
*/
void CL_WeatherRefreshChanged(void)
{
    // Build the snow cover or puddles while the map loads: it takes up to
    // a few hundred milliseconds, a hitch on the map's first frame.
    if (!cover.built && cl.bsp && cls.state >= ca_loading &&
        (Cvar_ClampValue(cl_snow, 0, 4) || Cvar_ClampValue(cl_rain, 0, 4)))
        cover_build();
    cover.dirty = cover.num_indices > 0;
    if (cover.dirty && cls.ref_initialized) {
        R_SetSnowCover(cover.kind, cover.vertices, cover.num_vertices,
                       cover.indices, cover.num_indices);
        cover.dirty = false;
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
    memset(raindrops, 0, sizeof(raindrops));
    if (cover.built && cls.ref_initialized)
        R_SetSnowCover(R_COVER_SNOW, NULL, 0, NULL, 0);
    cover_free();
    weather_wind_timer = 0;
    weather_exposure = 0;
    weather_gust_timer = 4;
    stop_sounds();
}

static void cl_weather_changed(cvar_t *self)
{
    CL_ClearWeather();
}

void CL_InitWeather(void)
{
    cl_snow = Cvar_Get("cl_snow", "0", CVAR_ARCHIVE);
    cl_snow->changed = cl_weather_changed;
    cl_snow_volume = Cvar_Get("cl_snow_volume", "1", CVAR_ARCHIVE);
    cl_snow_cover = Cvar_Get("cl_snow_cover", "6", CVAR_ARCHIVE);
    cl_snow_cover->changed = cl_weather_changed;
    cl_snow_cover_time = Cvar_Get("cl_snow_cover_time", "300", CVAR_ARCHIVE);
    cl_snow_craters = Cvar_Get("cl_snow_craters", "1", CVAR_ARCHIVE);
    cl_rain = Cvar_Get("cl_rain", "0", CVAR_ARCHIVE);
    cl_rain->changed = cl_weather_changed;
    cl_rain_volume = Cvar_Get("cl_rain_volume", "1", CVAR_ARCHIVE);
    cl_rain_puddles = Cvar_Get("cl_rain_puddles", "1", CVAR_ARCHIVE);
    cl_rain_puddles->changed = cl_weather_changed;
}

#define MAX_CRATER_VERTICES 4096

/*
==============
CL_SnowExplosion

Blows the snow cover away around an explosion: every vertex within radius
that the explosion can see, most strongly at the centre.
==============
*/
void CL_SnowExplosion(const vec3_t origin, float radius)
{
    static const vec3_t zero = { 0, 0, 0 };
    static int verts[MAX_CRATER_VERTICES];
    static float strength[MAX_CRATER_VERTICES];
    int count = 0;

    if (!cover.num_indices || cover.kind != R_COVER_SNOW || !cl_snow->value ||
        !cl_snow_craters->integer || !cl.bsp)
        return;

    for (int i = 0; i < cover.num_vertices && count < MAX_CRATER_VERTICES; i++) {
        const r_snow_vertex_t *v = &cover.vertices[i];
        vec3_t d;
        VectorSubtract(v->origin, origin, d);
        float dist2 = DotProduct(d, d);
        if (dist2 >= radius * radius)
            continue;

        // not through walls and floors; skirt bottoms share their top's
        // position, so the trace starts above the floor for both
        trace_t tr;
        vec3_t point;
        VectorCopy(v->origin, point);
        point[2] += 4;
        CM_BoxTrace(&tr, origin, point, zero, zero, cl.bsp->nodes, MASK_SOLID,
                    cl.csr.extended);
        if (tr.fraction < 1.0f && !tr.startsolid)
            continue;

        // noise keeps the crater from being a circle
        float wobble = 0.6f + 0.8f * cover_noise(v->origin[0] * 3 + origin[0],
                                                 v->origin[1] * 3 + origin[1]);
        verts[count] = i;
        strength[count] = min((1 - dist2 / (radius * radius)) * wobble, 1.0f);
        count++;
    }

    if (count)
        R_AddSnowCrater(verts, strength, count, cl.time * 0.001f);
}
