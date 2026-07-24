#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 shadedir;
    float backlerp;
    float shellscale;
    float depthscale;
    float _pad;
    vec4 fog;
    float intensity;
    float desaturation;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in float v_mode;
layout(location = 6) in vec3 v_rim_view;
layout(location = 7) in vec3 v_rim_normal;
layout(location = 8) flat in float v_rim_strength;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_bloom;

void main()
{
    vec4 texel = texture(tex_sampler, v_uv);
#ifdef ALIAS_ALPHA_TEST
    if (texel.a <= 0.666)
        discard;
#endif

    if (pc.desaturation > 0.0) {
        float luma = dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722));
        texel.rgb = mix(texel.rgb, vec3(luma), pc.desaturation);
    }

    vec3 base = texel.rgb * max(pc.intensity, 0.0) * v_color.rgb;
    float edge = 0.0;
    float rim = 0.0;
    float glint = 0.0;
    if (v_rim_strength > 0.0) {
        vec3 normal = normalize(v_rim_normal);
        vec3 view_dir = normalize(v_rim_view);
        float facing = abs(dot(normal, view_dir));

        // Keep a restrained cool response across the model, then bias most of
        // the energy toward silhouettes. This stays visible on low-poly alias
        // geometry without needing a second outline draw.
        edge = 1.0 - smoothstep(0.18, 0.92, facing);
        rim = 0.18 + 0.82 * edge;

        if (pc.shadedir.w != 0.0) {
            vec3 half_dir = normalize(view_dir + normalize(pc.shadedir.xyz));
            float h2 = max(dot(normal, half_dir), 0.0);
            h2 *= h2;
            float h4 = h2 * h2;
            float h8 = h4 * h4;
            glint = h8 * h8;
        }
    }

    const vec3 cool = vec3(0.20, 0.38, 0.64);
    const vec3 warm = vec3(1.0, 0.62, 0.28);
    vec3 effect = (cool * rim + warm * glint * 0.48) * v_rim_strength;
    const vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);
    float effect_luma = dot(effect, luma_weights);
    effect *= min(1.0, 0.15 / max(effect_luma, 0.000001));
    vec3 headroom = max(vec3(0.65), vec3(1.0) - clamp(base, 0.0, 1.0));
    vec3 added = effect * headroom;

    out_color = vec4(base + added, texel.a * v_color.a);

    // Keep the persistent fill out of bloom: only the angular edge and warm
    // glint should form a halo around the model.
    vec3 bloom_added = (cool * edge + warm * glint * 0.48) *
                       v_rim_strength * headroom;
    float bloom_added_luma = dot(bloom_added, luma_weights);
    float bloom_weight = smoothstep(0.008, 0.065, bloom_added_luma);
    vec3 bloom = bloom_added * bloom_weight * 0.42;
    float bloom_luma = dot(bloom, luma_weights);
    bloom *= min(1.0, 0.04 / max(bloom_luma, 0.000001));

    if (pc.fog.a < 0.0) {
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
        bloom *= 1.0 + pc.fog.a;
    } else if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        float fog = 1.0 - exp(-(d * d));
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, fog);
        bloom *= 1.0 - fog;
    }

    out_bloom = vec4(bloom, 0.0);
}
