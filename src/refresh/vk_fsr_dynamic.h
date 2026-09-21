/*
 * FSR3 dynamic-resolution control policy.
 *
 * This header intentionally contains only deterministic state transitions.
 * The Vulkan backend owns resource lifetime and may apply a recommendation
 * only after an internal-target rebuild path has been made safe.
 */

#ifndef VK_FSR_DYNAMIC_H
#define VK_FSR_DYNAMIC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VK_FSR_DYNAMIC_TIMING_UNAVAILABLE,
    VK_FSR_DYNAMIC_TIMING_GPU,
    VK_FSR_DYNAMIC_TIMING_CPU,
} vk_fsr_dynamic_timing_t;

typedef struct {
    bool enabled;
    bool cpu_fallback;
    float target_ms;
    float min_scale;
    float max_scale;
    float scale_step;
    float hysteresis_ms;
    float current_scale;
    float recommended_scale;
    float last_ms;
    unsigned cooldown_frames;
    unsigned required_samples;
    unsigned cooldown_remaining;
    unsigned stable_samples;
    int direction;
    uint32_t generation;
    vk_fsr_dynamic_timing_t timing;
} vk_fsr_dynamic_state_t;

static inline float vk_fsr_dynamic_positive(float value, float fallback)
{
    return value > 0.0f ? value : fallback;
}

static inline float vk_fsr_dynamic_clip(float value, float minimum,
                                        float maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static inline float vk_fsr_dynamic_quantize(const vk_fsr_dynamic_state_t *state,
                                            float scale)
{
    float step = vk_fsr_dynamic_clip(
        vk_fsr_dynamic_positive(state->scale_step, 0.125f), 0.001f, 1.0f);
    float minimum = vk_fsr_dynamic_clip(
        vk_fsr_dynamic_positive(state->min_scale, 1.0f), 1.0f, 3.0f);
    float maximum = state->max_scale >= minimum ?
        vk_fsr_dynamic_clip(state->max_scale, minimum, 3.0f) : minimum;
    float steps = (vk_fsr_dynamic_clip(scale, minimum, maximum) - minimum) / step;
    /* Keep the conversion bounded even for malformed or extreme cvars. */
    steps = vk_fsr_dynamic_clip(steps, 0.0f, 1000000.0f);
    float quantized = minimum + (float)((uint32_t)(steps + 0.5f)) * step;
    return vk_fsr_dynamic_clip(quantized, minimum, maximum);
}

static inline void vk_fsr_dynamic_reset(vk_fsr_dynamic_state_t *state,
                                        float current_scale,
                                        uint32_t generation)
{
    float minimum = vk_fsr_dynamic_clip(
        vk_fsr_dynamic_positive(state->min_scale, 1.0f), 1.0f, 3.0f);
    float maximum = state->max_scale >= minimum ?
        vk_fsr_dynamic_clip(state->max_scale, minimum, 3.0f) : minimum;
    state->target_ms = vk_fsr_dynamic_positive(state->target_ms, 16.67f);
    state->min_scale = minimum;
    state->max_scale = maximum;
    state->scale_step = vk_fsr_dynamic_clip(
        vk_fsr_dynamic_positive(state->scale_step, 0.125f), 0.001f, 1.0f);
    state->hysteresis_ms = state->hysteresis_ms >= 0.0f ?
        state->hysteresis_ms : 0.75f;
    /* The active profile may be a non-quantized SDK ratio such as 1.7x.
     * Preserve it; only future recommendations are quantized. */
    state->current_scale = vk_fsr_dynamic_positive(current_scale, minimum);
    state->recommended_scale = state->current_scale;
    state->last_ms = 0.0f;
    state->cooldown_remaining = 0;
    state->stable_samples = 0;
    state->direction = 0;
    state->generation = generation;
    state->timing = VK_FSR_DYNAMIC_TIMING_UNAVAILABLE;
}

static inline const char *vk_fsr_dynamic_timing_name(
    vk_fsr_dynamic_timing_t timing)
{
    switch (timing) {
    case VK_FSR_DYNAMIC_TIMING_GPU: return "GPU";
    case VK_FSR_DYNAMIC_TIMING_CPU: return "CPU";
    case VK_FSR_DYNAMIC_TIMING_UNAVAILABLE:
    default: return "unavailable";
    }
}

/* Return true when the recommendation changed. CPU timing is opt-in and is
 * never mixed with GPU timing within one controller window. */
static inline bool vk_fsr_dynamic_update(vk_fsr_dynamic_state_t *state,
                                         unsigned gpu_usec,
                                         unsigned record_usec,
                                         bool eligible,
                                         uint32_t generation)
{
    if (!state->enabled || !eligible || generation != state->generation)
        return false;

    vk_fsr_dynamic_timing_t timing = gpu_usec ?
        VK_FSR_DYNAMIC_TIMING_GPU :
        (state->cpu_fallback && record_usec ?
         VK_FSR_DYNAMIC_TIMING_CPU : VK_FSR_DYNAMIC_TIMING_UNAVAILABLE);
    if (timing == VK_FSR_DYNAMIC_TIMING_UNAVAILABLE) {
        state->timing = timing;
        state->last_ms = 0.0f;
        state->stable_samples = 0;
        state->direction = 0;
        return false;
    }

    if (state->timing != timing) {
        state->timing = timing;
        state->stable_samples = 0;
        state->direction = 0;
        state->cooldown_remaining = 0;
    }
    state->last_ms = (float)(timing == VK_FSR_DYNAMIC_TIMING_GPU ?
                             gpu_usec : record_usec) / 1000.0f;
    /* A recommendation is pending until the resource owner applies it (or
     * resets the controller). Do not oscillate around an unapplied value. */
    if (state->recommended_scale != state->current_scale)
        return false;
    if (state->cooldown_remaining) {
        state->cooldown_remaining--;
        state->stable_samples = 0;
        state->direction = 0;
        return false;
    }
    float recommendation = state->current_scale;
    float upper = state->target_ms + state->hysteresis_ms;
    float lower = state->target_ms - state->hysteresis_ms;
    int direction = state->last_ms > upper ? 1 :
        state->last_ms < lower ? -1 : 0;
    if (!direction) {
        state->stable_samples = 0;
        state->direction = 0;
        return false;
    }
    if (state->direction != direction) {
        state->direction = direction;
        state->stable_samples = 0;
    }
    state->stable_samples++;
    unsigned samples = state->required_samples ? state->required_samples : 30;
    if (state->stable_samples < samples)
        return false;

    if (direction > 0)
        recommendation += state->scale_step;
    else
        recommendation -= state->scale_step;
    recommendation = vk_fsr_dynamic_quantize(state, recommendation);
    state->stable_samples = 0;
    state->direction = 0;
    if (recommendation == state->current_scale ||
        recommendation == state->recommended_scale)
        return false;

    state->recommended_scale = recommendation;
    state->cooldown_remaining = state->cooldown_frames;
    return true;
}

#endif
