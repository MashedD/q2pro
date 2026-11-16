// synth.c -- weather sounds made in code
//
// The game has no wind sounds, and sounds from elsewhere can't be shipped
// without a license. Sounds registered as "#synth:weather/...", which no
// file can be named, are synthesized from filtered noise when they are
// loaded instead: the weather code (weather.c) uses them when no file of
// its own is found.
//
// Every sound is made from a fixed seed, so it is the same each time.
// Loops are made a little longer than they play, and the extra end is
// crossfaded into the start, so they repeat without a seam.

#include "sound.h"

#define SYNTH_RATE      22050

typedef struct {
    uint32_t    seed;
} synth_rng_t;

static float synth_white(synth_rng_t *r)
{
    r->seed ^= r->seed << 13;
    r->seed ^= r->seed >> 17;
    r->seed ^= r->seed << 5;
    return (r->seed & 0xffffff) / (float)0x800000 - 1;
}

// one-pole low pass coefficient for a cutoff in Hz
static float synth_lp(float hz)
{
    return 1 - expf(-2 * M_PIf * hz / SYNTH_RATE);
}

// Chamberlin state-variable filter, band pass output
typedef struct {
    float   low, band;
} synth_svf_t;

static float synth_bandpass(synth_svf_t *f, float in, float hz, float damp)
{
    float k = 2 * sinf(M_PIf * min(hz, SYNTH_RATE / 6.0f) / SYNTH_RATE);
    float high = in - f->low - damp * f->band;
    f->band += k * high;
    f->low += k * f->band;
    return f->band;
}

static float synth_loop_phase(int i, int n, float cycles, float offset)
{
    return sinf(2 * M_PIf * cycles * i / n + offset);
}

// Wind: noise through a resonant band pass whose pitch and loudness wander,
// over a low rumble. The wandering repeats exactly every n samples.
static void synth_wind(float *out, int count, int n, synth_rng_t *r, bool gust)
{
    synth_svf_t svf = { 0 };
    float pink = 0, pink2 = 0, rumble = 0, dark = 0;
    float a_pink = synth_lp(900), a_rumble = synth_lp(120), a_dark = synth_lp(1300);

    for (int i = 0; i < count; i++) {
        float hz, amp;
        if (gust) {
            // swells and dies away, rising in pitch as it peaks
            float t = (float)i / count;
            float env = t < 0.3f ? sinf(t / 0.3f * M_PIf / 2) :
                        powf(cosf((t - 0.3f) / 0.7f * M_PIf / 2), 1.5f);
            hz = 280 + 650 * env;
            amp = env;
        } else {
            hz = 330 + 160 * synth_loop_phase(i, n, 1, 0.3f) +
                 90 * synth_loop_phase(i, n, 3, 1.7f) +
                 40 * synth_loop_phase(i, n, 7, 4.1f);
            amp = 0.6f + 0.22f * synth_loop_phase(i, n, 2, 2.2f) +
                  0.12f * synth_loop_phase(i, n, 5, 0.9f);
        }
        float w = synth_white(r);
        pink += a_pink * (w - pink);
        pink2 += a_pink * (pink - pink2);
        rumble += a_rumble * (w - rumble);
        float howl = synth_bandpass(&svf, pink2, hz, 0.35f);
        dark += a_dark * (howl * 1.6f + rumble * 2.5f - dark);
        out[i] = amp * dark;
    }
}

static void synth_normalize(float *buf, int n, float peak)
{
    float m = 0;
    for (int i = 0; i < n; i++)
        m = max(m, fabsf(buf[i]));
    if (m > 0)
        for (int i = 0; i < n; i++)
            buf[i] *= peak / m;
}

// Makes the first n samples of count repeat without a seam, by fading the
// samples past n into the start.
static void synth_close_loop(float *buf, int n, int count)
{
    int fade = count - n;
    for (int i = 0; i < fade; i++) {
        float t = (i + 0.5f) / fade;
        buf[i] = buf[i] * sinf(t * M_PIf / 2) + buf[n + i] * cosf(t * M_PIf / 2);
    }
}

/*
==============
S_SynthesizeSound

Fills s_info with the sound for a name after "synth:", in memory from
Z_Malloc. Returns false for an unknown name.
==============
*/
bool S_SynthesizeSound(const char *name)
{
    static const struct {
        const char  *name;
        float       seconds;
        bool        loop;
        int         kind;
        float       peak;
    } sounds[] = {
        { "weather/wind.wav",          8, true,  0, 0.55f },
        { "weather/gust.wav",          3, false, 1, 0.6f },
    };
    int i;

    for (i = 0; i < q_countof(sounds); i++)
        if (!Q_stricmp(name, sounds[i].name))
            break;
    if (i == q_countof(sounds))
        return false;

    synth_rng_t rng = { .seed = Com_HashString(sounds[i].name, UINT_MAX) | 1 };
    int n = (int)(sounds[i].seconds * SYNTH_RATE);
    int count = sounds[i].loop ? n + SYNTH_RATE / 2 : n;
    float *buf = Z_Mallocz(sizeof(*buf) * count);

    switch (sounds[i].kind) {
    case 0: synth_wind(buf, count, n, &rng, false); break;
    case 1: synth_wind(buf, count, n, &rng, true); break;
    }
    if (sounds[i].loop)
        synth_close_loop(buf, n, count);
    synth_normalize(buf, n, sounds[i].peak);

    int16_t *pcm = Z_Malloc(sizeof(*pcm) * n);
    for (int j = 0; j < n; j++)
        pcm[j] = Q_clip_int16((int)(buf[j] * 32767));
    Z_Free(buf);

    s_info.format = FORMAT_PCM;
    s_info.channels = 1;
    s_info.rate = SYNTH_RATE;
    s_info.width = 2;
    s_info.loopstart = sounds[i].loop ? 0 : -1;
    s_info.samples = n;
    s_info.data = (byte *)pcm;
    return true;
}
