#version 460
#extension GL_EXT_ray_query : require
#ifdef RT_QUAD_SHARING
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_quad : require
#endif

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 scroll;
    vec4 dlight;
    vec4 dlight_origins[3];
    vec4 dlight_colors[3];
    vec4 fog;
    float intensity;
    float desaturation;
    vec2 lm_scale;
    vec2 lm_offset;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;
layout(set = 1, binding = 0) uniform sampler2D lm_sampler;
#ifdef RT_GLOWMAP
layout(set = 2, binding = 0) uniform sampler2D glow_sampler;
#endif
layout(set = 3, binding = 0) uniform accelerationStructureEXT scene;
struct SurfaceLight {
    vec4 origin_range;
    vec4 color_strength;
    vec4 normal;
};
layout(std430, set = 3, binding = 1) readonly buffer SurfaceLights {
    uvec4 surface_info;
    SurfaceLight lights[64];
    uint light_indices[];
};

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in float v_mode;
layout(location = 3) in vec2 v_lmuv;
layout(location = 4) in vec3 v_position;
layout(location = 5) flat in uvec2 v_rt_data;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_bloom;

float trace_hit_distance(vec3 origin, vec3 direction, float max_distance)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xff, origin, 0.05, direction, max(max_distance - 0.1, 0.05));
    while (rayQueryProceedEXT(query)) { }
    if (rayQueryGetIntersectionTypeEXT(query, true) ==
        gl_RayQueryCommittedIntersectionNoneEXT)
        return -1.0;
    return rayQueryGetIntersectionTEXT(query, true);
}

bool occluded(vec3 origin, vec3 direction, float distance_to_light)
{
    return trace_hit_distance(origin, direction, distance_to_light) >= 0.0;
}

vec3 dynamic_light(vec3 normal)
{
    vec3 light = vec3(0.0);
    for (int i = 0; i < 3; i++) {
        float range = pc.dlight_origins[i].w;
        if (range <= 0.0)
            break;
        vec3 delta = pc.dlight_origins[i].xyz - v_position;
        float distance_to_light = length(delta);
        float falloff = max(1.0 - distance_to_light / range, 0.0);
        vec3 oriented_normal = dot(normal, delta) >= 0.0 ? normal : -normal;
        if (falloff > 0.0 &&
            !occluded(v_position + oriented_normal * 0.05,
                      delta / max(distance_to_light, 0.001),
                      distance_to_light)) {
            light += pc.dlight_colors[i].rgb *
                (pc.dlight_colors[i].w * falloff / 255.0);
        }
    }
    return light;
}

float surface_light_sample(uint light_index, vec3 normal,
                           out vec3 color, out vec3 direction,
                           out vec3 oriented_normal, out float distance_to_light)
{
    SurfaceLight source = lights[light_index];
    vec3 delta = source.origin_range.xyz - v_position;
    distance_to_light = length(delta);
    direction = delta / max(distance_to_light, 0.001);
    float falloff = clamp(1.0 - distance_to_light / source.origin_range.w,
                          0.0, 1.0);
    // Preserve the midpoint and peak of the old linear response while making
    // both ends slope smoothly. This avoids drawing the finite light range as
    // a visible circle on otherwise uniform lightmapped surfaces.
    falloff = falloff * falloff * (3.0 - 2.0 * falloff);
    oriented_normal = dot(normal, delta) >= 0.0 ? normal : -normal;
    float source_cosine = max(dot(source.normal.xyz, -direction), 0.0);
    float receiver_cosine = abs(dot(normal, direction));
    float angular = source_cosine * (0.25 + 0.75 * receiver_cosine);
    color = source.color_strength.rgb;
    return source.color_strength.w * falloff * angular / 255.0;
}

vec3 surface_light(vec3 normal)
{
    uint best_indices[2] = uint[2](0u, 0u);
    float best_contributions[2] = float[2](0.0, 0.0);
    uint list_offset = v_rt_data.x;
    uint candidate_count = v_rt_data.y & 0xffu;
    for (uint i = 0; i < candidate_count; i++) {
        uint index_offset = list_offset + i;
        if (index_offset >= surface_info.y)
            break;
        uint light_index = light_indices[index_offset];
        if (light_index >= surface_info.x || light_index >= 64u)
            continue;
        vec3 color, direction, oriented_normal;
        float distance_to_light;
        float contribution = surface_light_sample(light_index, normal,
            color, direction, oriented_normal, distance_to_light);
        if (contribution > best_contributions[0]) {
            best_contributions[1] = best_contributions[0];
            best_indices[1] = best_indices[0];
            best_contributions[0] = contribution;
            best_indices[0] = light_index;
        } else if (contribution > best_contributions[1]) {
            best_contributions[1] = contribution;
            best_indices[1] = light_index;
        }
    }

    vec3 light = vec3(0.0);
    for (uint i = 0; i < 2u; i++) {
        if (best_contributions[i] <= 0.0)
            continue;
        vec3 color, direction, oriented_normal;
        float distance_to_light;
        float contribution = surface_light_sample(best_indices[i], normal,
            color, direction, oriented_normal, distance_to_light);
        float visibility = 1.0;
        // Weak tails remain continuous without spending a ray query. Fade in
        // occlusion as the light becomes significant instead of introducing a
        // second hard contour at the query threshold.
        if (contribution >= 0.01 &&
            occluded(v_position + oriented_normal * 0.05,
                     direction, distance_to_light)) {
            float source_radius = lights[best_indices[i]].normal.w;
            float apparent_size = source_radius /
                max(distance_to_light, 1.0);
            // A single center ray cannot reproduce an area light's penumbra.
            // Limit its contrast according to the emitter's apparent size so
            // broad fluorescent faces do not project giant solid polygons.
            float shadow_opacity = mix(0.35, 0.12,
                smoothstep(0.025, 0.20, apparent_size));
            visibility -= smoothstep(0.01, 0.03, contribution) *
                shadow_opacity;
        }
        light += color * contribution * visibility;
    }
    return light;
}

float ambient_visibility(vec3 normal)
{
    if (pc.lm_scale.x < 0.0)
        return 1.0;

    float view_distance = distance(v_position, pc.dlight.xyz);
    if (view_distance >= 640.0)
        return 1.0;

    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) :
        vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    // The phase is constant across a BSP face. Neighboring fragments therefore
    // trace coherently instead of producing texture-like stochastic noise.
    float seed = (float((v_rt_data.y >> 8) & 0xffu) + 0.5) / 256.0;
    float phi = seed * 6.2831853;
    const float radius = 0.45;
    vec3 direction = normalize(tangent * (cos(phi) * radius) +
        bitangent * (sin(phi) * radius) + normal * sqrt(1.0 - radius * radius));
    float hit_distance = trace_hit_distance(v_position + normal * 0.05,
                                            direction, 48.0);
    float proximity = hit_distance < 0.0 ? 0.0 :
        1.0 - smoothstep(2.0, 48.0, hit_distance);
    float visibility = 1.0 - 0.18 * proximity;
    return mix(visibility, 1.0, smoothstep(384.0, 640.0, view_distance));
}

#ifdef RT_QUAD_SHARING
float quad_anchor_distance()
{
    return subgroupQuadBroadcast(distance(v_position, pc.dlight.xyz), 0);
}

vec3 adaptive_surface_light(vec3 normal, bool full_resolution)
{
    if (full_resolution)
        return surface_light(normal);

    vec3 light = vec3(0.0);
    if ((gl_SubgroupInvocationID & 3u) == 0u)
        light = surface_light(normal);
    return subgroupQuadBroadcast(light, 0);
}

float adaptive_ambient_visibility(vec3 normal, bool full_resolution)
{
    if (full_resolution)
        return ambient_visibility(normal);

    float visibility = 1.0;
    if ((gl_SubgroupInvocationID & 3u) == 0u)
        visibility = ambient_visibility(normal);
    return subgroupQuadBroadcast(visibility, 0);
}
#endif

void main()
{
    float mode = mod(v_mode, 4.0);
    vec2 uv = v_uv;
    if (v_mode >= 4.0)
        uv += vec2(0.0625) * sin(uv.ts * vec2(4.0) + vec2(pc.dlight.a));

    vec3 normal = normalize(cross(dFdx(v_position), dFdy(v_position)));
#ifdef RT_QUAD_SHARING
    // Execute subgroup operations before per-surface branches so all lanes in
    // a fragment quad participate, including lanes crossing a primitive edge.
    bool full_resolution = quad_anchor_distance() <= 384.0;
    float quad_ao = adaptive_ambient_visibility(normal, full_resolution);
    vec3 quad_surface_light = adaptive_surface_light(normal, full_resolution);
#endif

    vec3 bloom = vec3(0.0);
    if (mode > 1.5) {
        out_color = v_color;
    } else {
        vec4 texel = texture(tex_sampler, uv);
#ifdef RT_ALPHA_TEST
        if (texel.a <= 0.666)
            discard;
#endif
        if (pc.desaturation > 0.0) {
            float luma = dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722));
            texel.rgb = mix(texel.rgb, vec3(luma), pc.desaturation);
        }
        vec3 lm = pc.lm_scale.x < 0.0 ? vec3(1.0) :
            texture(lm_sampler, v_lmuv).rgb;
        float ao = 1.0;
        vec3 lighting;
#ifdef RT_QUAD_SHARING
        ao = quad_ao;
        lighting = dynamic_light(normal) + quad_surface_light;
#else
        if (pc.lm_scale.x >= 0.0)
            ao = ambient_visibility(normal);
        lighting = dynamic_light(normal) + surface_light(normal);
#endif
#ifdef RT_GLOWMAP
        vec4 glow = texture(glow_sampler, uv);
        lm = mix(lm, vec3(1.0), glow.a);
        ao = mix(ao, 1.0, glow.a);
#endif
        lm *= ao;
        out_color = texel;
        out_color.rgb *= (lm + pc.scroll.www) * pc.color.rgb +
            lighting;
        out_color.a *= v_color.a;
        if (pc.intensity < 0.0) {
            out_color.rgb *= (out_color.r + out_color.g + out_color.b) / 3.0;
            out_color.rgb *= v_color.a;
        } else {
            out_color.rgb *= pc.intensity;
        }
#ifdef RT_GLOWMAP
        bloom = texel.rgb * glow.a * pc.intensity;
#endif
    }

    if (pc.fog.a < 0.0) {
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
        bloom *= 1.0 + pc.fog.a;
    } else if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        float fog = 1.0 - exp(-(d * d));
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, fog);
        bloom *= 1.0 - fog;
    }
#ifdef RT_GLOWMAP
    out_bloom = vec4(bloom, out_color.a);
#else
    out_bloom = vec4(0.0);
#endif
}
