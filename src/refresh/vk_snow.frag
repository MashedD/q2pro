#version 460

// Snow cover (vk_snow.c), built in several variants:
//
// - By default, for snow that is still growing or filling in a crater:
//   v_front is how far the snow at this point has come on its growth
//   schedule, and below zero it hasn't arrived yet. It varies linearly
//   across each triangle, so the edge of the growing snow is a smooth line,
//   made ragged with noise, instead of following the grid.
// - SNOW_FULL, once all of it has grown and no crater is filling in: nothing
//   is left out, so the depth test can run before this shader.
// - SNOW_RT, with ray tracing: dynamic lights cast shadows, with a ray
//   towards each light through the world's acceleration structure.

#ifdef SNOW_RT
#extension GL_EXT_ray_query : require
#endif

#ifdef SNOW_FULL
layout(early_fragment_tests) in;
#endif

layout(location = 0) in vec4 v_color;
layout(location = 1) in float v_front;
layout(location = 2) in vec2 v_xy;
layout(location = 3) in vec3 v_position;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_bloom;

// Same layout as vk_snow_push_t.
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 craters;
    vec4 fog;       // rgb color, a density; the same as for the world
    vec4 dlight_origins[3];     // xyz, w = reach; 0 for none
    vec4 dlight_colors[3];      // rgb, w = brightness
} pc;

#ifdef SNOW_RT
layout(set = 1, binding = 0) uniform accelerationStructureEXT scene;

bool in_shadow(vec3 origin, vec3 target)
{
    vec3 d = target - origin;
    float dist = length(d);
    if (dist < 1.0)
        return false;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xff, origin, 0.1, d / dist, dist - 1.0);
    while (rayQueryProceedEXT(query)) { }
    return rayQueryGetIntersectionTypeEXT(query, true) !=
        gl_RayQueryCommittedIntersectionNoneEXT;
}
#endif

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

// Dynamic lights, like the world's: the snow faces mostly up, so a light
// below it reaches it less.
vec3 dynamic_light(vec3 position)
{
    vec3 light = vec3(0.0);
    for (int i = 0; i < 3; i++) {
        float reach = pc.dlight_origins[i].w;
        if (reach <= 0.0)
            break;
        vec3 d = pc.dlight_origins[i].xyz - position;
        float dist = length(d);
        float falloff = max(1.0 - dist / reach, 0.0);
        if (falloff <= 0.0)
            continue;
#ifdef SNOW_RT
        // Rays are costly over a screen full of snow: only the strongest
        // light (the first) casts shadows, and only where it lights the snow
        // noticeably, as in the world's shaders. The snow isn't in the
        // acceleration structure; the ray starts a little above it, so the
        // floor under thin snow doesn't shadow it.
        if (i == 0 && pc.dlight_colors[i].w * falloff > 0.08 &&
            in_shadow(position + vec3(0.0, 0.0, 1.0), pc.dlight_origins[i].xyz))
            continue;
#endif
        float facing = clamp(0.55 + 0.45 * d.z / max(dist, 1.0), 0.2, 1.0);
        light += pc.dlight_colors[i].rgb * (pc.dlight_colors[i].w * falloff * facing);
    }
    return light;
}

#ifndef SNOW_FULL
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
#endif

void main()
{
#ifdef SNOW_FULL
    out_color = v_color;
#else
    float n = noise(v_xy / 14.0) * 0.65 + noise(v_xy / 5.0) * 0.35;
    float front = v_front + (n - 0.5) * 0.3;
    if (front < 0.0)
        discard;
    // the thin snow just behind the edge lets the floor show through a bit
    out_color = vec4(v_color.rgb * (0.82 + 0.18 * clamp(front * 6.0, 0.0, 1.0)), 1.0);
#endif
    out_color.rgb += 0.7 * dynamic_light(v_position);
    out_color.rgb = apply_fog(out_color.rgb);
    out_bloom = out_color;
}
