#version 450

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 color;
    vec2 screen;
    vec4 uv;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

float ghost_edge_fade(vec2 uv)
{
    vec2 low = smoothstep(vec2(0.0), vec2(0.045), uv);
    vec2 high = smoothstep(vec2(0.0), vec2(0.045), vec2(1.0) - uv);
    return low.x * low.y * high.x * high.y;
}

vec3 ghost_sample(vec2 uv)
{
    float edge = ghost_edge_fade(uv);
    if (edge <= 0.0)
        return vec3(0.0);

    vec3 sample_color = texture(tex_sampler, clamp(uv, 0.0, 1.0)).rgb;
    float luma = dot(sample_color, vec3(0.2126, 0.7152, 0.0722));
    // Bloom MRT contributions are intentionally low-energy before blur;
    // retain that useful range while still rejecting quantization noise.
    float bright = smoothstep(0.0015, 0.045, luma);
    sample_color *= min(1.0, 0.30 / max(luma, 0.000001));
    return sample_color * bright * edge;
}

void main()
{
    vec2 step_uv = pc.color.xy;
    // Keep a center tap so narrow highlights and one-pixel glow sources are
    // not lost between the four diagonal samples at quarter resolution.
    vec3 color = texture(tex_sampler, v_uv).rgb * 0.50;
    color += texture(tex_sampler,
                     v_uv + vec2(-step_uv.x, -step_uv.y)).rgb * 0.125;
    color += texture(tex_sampler,
                     v_uv + vec2(-step_uv.x,  step_uv.y)).rgb * 0.125;
    color += texture(tex_sampler,
                     v_uv + vec2( step_uv.x, -step_uv.y)).rgb * 0.125;
    color += texture(tex_sampler,
                     v_uv + vec2( step_uv.x,  step_uv.y)).rgb * 0.125;

    float ghost_strength = clamp(pc.color.z, 0.0, 1.0);
    if (ghost_strength > 0.0001) {
        vec2 axis = v_uv - vec2(0.5);
        vec2 first_uv = vec2(0.5) - axis * 0.55;
        vec2 radial = normalize(axis + vec2(0.000001));
        vec2 chroma_step = radial /
            max(vec2(textureSize(tex_sampler, 0)), vec2(1.0)) * 1.5;
        vec3 first_center = ghost_sample(first_uv);
        vec3 first_chroma = vec3(ghost_sample(first_uv + chroma_step).r,
                                 first_center.g,
                                 ghost_sample(first_uv - chroma_step).b);
        vec3 first = mix(first_center, first_chroma, 0.35);
        vec3 second = ghost_sample(vec2(0.5) - axis * 1.05);
        vec3 third = ghost_sample(vec2(0.5) + axis * 1.55);
        vec3 ghosts = first * 0.48 + second * 0.28 + third * 0.16;
        color += ghosts * ghost_strength;
    }

    float shaft_strength = clamp(pc.color.w, 0.0, 1.0);
    if (shaft_strength > 0.0001) {
        vec2 toward_center = vec2(0.5) - v_uv;
        vec3 shafts = ghost_sample(v_uv + toward_center * 0.08) * 0.34;
        shafts += ghost_sample(v_uv + toward_center * 0.18) * 0.28;
        shafts += ghost_sample(v_uv + toward_center * 0.32) * 0.22;
        shafts += ghost_sample(v_uv + toward_center * 0.50) * 0.16;

        float center_fade = smoothstep(0.06, 0.22,
                                       length(toward_center));
        shafts *= center_fade;
        float shaft_luma = dot(shafts, vec3(0.2126, 0.7152, 0.0722));
        shafts *= min(1.0, 0.08 / max(shaft_luma, 0.000001));
        color += shafts * shaft_strength;
    }

    out_color = vec4(color, 1.0);
}
