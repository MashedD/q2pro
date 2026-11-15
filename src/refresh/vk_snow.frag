#version 450

// Snow cover (vk_snow.c). v_front is how far the snow at this point has come
// on its growth schedule: below zero it hasn't arrived yet. It varies
// linearly across each triangle, so the edge of the growing snow is a smooth
// line, made ragged with noise, instead of following the grid.

layout(location = 0) in vec4 v_color;
layout(location = 1) in float v_front;
layout(location = 2) in vec2 v_xy;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_bloom;

// Same layout as vk_snow_push_t.
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 craters;
    vec4 fog;       // rgb color, a density; the same as for the world
} pc;

vec3 apply_fog(vec3 color)
{
    if (pc.fog.a < 0.0)
        return mix(color, pc.fog.rgb, -pc.fog.a);
    if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        return mix(color, pc.fog.rgb, 1.0 - exp(-(d * d)));
    }
    return color;
}

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

void main()
{
    float n = noise(v_xy / 14.0) * 0.65 + noise(v_xy / 5.0) * 0.35;
    float front = v_front + (n - 0.5) * 0.3;
    if (front < 0.0)
        discard;
    // the thin snow just behind the edge lets the floor show through a bit
    out_color = vec4(v_color.rgb * (0.82 + 0.18 * clamp(front * 6.0, 0.0, 1.0)), 1.0);
    out_color.rgb = apply_fog(out_color.rgb);
    out_bloom = out_color;
}
