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
    vec2 rt_params;
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
layout(set = 3, binding = 2) uniform sampler2D rt_static_sampler;

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
        if (falloff <= 0.0)
            continue;
        float energy = pc.dlight_colors[i].w * falloff / 255.0;
        if (max(pc.dlight_colors[i].r,
                max(pc.dlight_colors[i].g, pc.dlight_colors[i].b)) *
            energy < 0.01)
            continue;
        float hit_distance = trace_hit_distance(
            v_position + oriented_normal * 0.05,
            delta / max(distance_to_light, 0.001), distance_to_light);
        float visibility = 1.0;
        if (hit_distance >= 0.0) {
            float blocker_ratio = hit_distance / max(distance_to_light, 0.001);
            float shadow_opacity = mix(0.96, 0.72,
                smoothstep(0.15, 0.85, blocker_ratio));
            visibility -= shadow_opacity;
        }
        light += pc.dlight_colors[i].rgb * energy * visibility;
    }
    return light;
}

float surface_light_sample(uint light_index, vec3 normal,
                           out vec3 color, out vec3 direction,
                           out vec3 oriented_normal, out float distance_to_light,
                           float strength_scale)
{
    SurfaceLight source = lights[light_index];
    vec3 delta = source.origin_range.xyz - v_position;
    distance_to_light = length(delta);
    direction = delta / max(distance_to_light, 0.001);
    float falloff = clamp(1.0 - distance_to_light / source.origin_range.w,
                          0.0, 1.0);
    // Smooth the finite edge, then concentrate energy near the emitter so
    // overlapping ranges on light-dense maps do not lift global exposure.
    falloff = falloff * falloff * (3.0 - 2.0 * falloff);
    falloff = falloff * falloff * sqrt(falloff);
    oriented_normal = dot(normal, delta) >= 0.0 ? normal : -normal;
    float source_cosine = max(dot(source.normal.xyz, -direction), 0.0);
    float receiver_cosine = abs(dot(normal, direction));
    float angular = source_cosine * (0.25 + 0.75 * receiver_cosine);
    color = source.color_strength.rgb;
    float strength = min(source.color_strength.w * strength_scale, 192.0);
    return strength * falloff * angular / 255.0;
}

vec3 decode_static_emissive(vec3 encoded)
{
    return encoded * 0.24;
}

vec3 surface_light(vec3 normal, vec3 baked_light)
{
    float emissive = clamp(pc.rt_params.x, 0.0, 2.0);
    if (emissive <= 0.0)
        return vec3(0.0);
    float strength_scale = pow(emissive, 0.75) * 1.5;
    if (surface_info.z != 0u)
        return decode_static_emissive(baked_light) * strength_scale;
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
            color, direction, oriented_normal, distance_to_light,
            strength_scale);
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
            color, direction, oriented_normal, distance_to_light,
            strength_scale);
        float visibility = 1.0;
        float source_radius = lights[best_indices[i]].normal.w;
        float apparent_size = source_radius / max(distance_to_light, 1.0);
        // A single center ray cannot reproduce an area light's penumbra.
        // Fade its shadow away as the emitter grows on screen; this also avoids
        // paying for a visibility query whose result would be imperceptible.
        float shadow_opacity = 0.55 *
            (1.0 - smoothstep(0.025, 0.20, apparent_size));
        // Keep all direct illumination, but reserve traversal for shadows that
        // can make a visible difference. The weaker second source is queried
        // only when it contributes at least half as much as the strongest one.
        bool significant_shadow = contribution >= 0.02 &&
            shadow_opacity >= 0.01 &&
            (i == 0u || contribution >= best_contributions[0] * 0.5);
        if (significant_shadow) {
            float hit_distance = trace_hit_distance(
                v_position + oriented_normal * 0.05,
                direction, distance_to_light);
            if (hit_distance >= 0.0) {
                float blocker_ratio = hit_distance /
                    max(distance_to_light, 0.001);
                float blocker_softness = mix(1.0, 0.55,
                    smoothstep(0.15, 0.85, blocker_ratio));
                visibility -= smoothstep(0.02, 0.05, contribution) *
                    shadow_opacity * blocker_softness;
            }
        }
        light += color * contribution * visibility;
    }
    return light;
}

float ambient_visibility(vec3 normal, float lightmap_weight,
                         float baked_occlusion)
{
    float ao_control = clamp(pc.rt_params.y * 2.0, 0.0, 1.0);
    // Make the default setting clearly visible in contact regions while the
    // lightmap weighting prevents a broad exposure shift.
    float strength = 0.78 * pow(ao_control, 0.60) * lightmap_weight;
    if (pc.lm_scale.x < 0.0 || strength < 0.005)
        return 1.0;

    if (surface_info.z != 0u) {
        return max(1.0 - strength * baked_occlusion, 0.45);
    }

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
    const float radius = 0.88;
    vec3 direction = normalize(tangent * (cos(phi) * radius) +
        bitangent * (sin(phi) * radius) + normal * sqrt(1.0 - radius * radius));
    float hit_distance = trace_hit_distance(v_position + normal * 0.05,
                                            direction, 160.0);
    float contact = hit_distance < 0.0 ? 0.0 :
        1.0 - smoothstep(2.0, 16.0, hit_distance);
    float broad = hit_distance < 0.0 ? 0.0 :
        1.0 - smoothstep(8.0, 160.0, hit_distance);
    float raw_occlusion = 0.70 * contact + 0.30 * broad;
    float occlusion = min(0.90,
        1.25 * smoothstep(0.015, 0.38, raw_occlusion));
    float visibility = max(1.0 - strength * occlusion, 0.45);
    return mix(visibility, 1.0, smoothstep(384.0, 640.0, view_distance));
}

#ifdef RT_QUAD_SHARING
float quad_anchor_distance()
{
    return subgroupQuadBroadcast(distance(v_position, pc.dlight.xyz), 0);
}

vec3 adaptive_dynamic_light(vec3 normal, bool full_resolution)
{
    if (full_resolution)
        return dynamic_light(normal);

    vec3 light = vec3(0.0);
    if ((gl_SubgroupInvocationID & 3u) == 0u)
        light = dynamic_light(normal);
    return subgroupQuadBroadcast(light, 0);
}

vec3 adaptive_surface_light(vec3 normal, bool full_resolution,
                            vec3 baked_light)
{
    if (surface_info.z != 0u)
        return surface_light(normal, baked_light);
    if (full_resolution)
        return surface_light(normal, baked_light);

    vec3 light = vec3(0.0);
    if ((gl_SubgroupInvocationID & 3u) == 0u)
        light = surface_light(normal, baked_light);
    return subgroupQuadBroadcast(light, 0);
}

float adaptive_ambient_visibility(vec3 normal, bool full_resolution,
                                  float lightmap_weight,
                                  float baked_occlusion)
{
    if (surface_info.z != 0u)
        return ambient_visibility(normal, lightmap_weight, baked_occlusion);
    if (full_resolution)
        return ambient_visibility(normal, lightmap_weight, baked_occlusion);

    float visibility = 1.0;
    if ((gl_SubgroupInvocationID & 3u) == 0u)
        visibility = ambient_visibility(normal, lightmap_weight,
                                        baked_occlusion);
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
    vec3 lm = pc.lm_scale.x < 0.0 ? vec3(1.0) :
        texture(lm_sampler, v_lmuv).rgb;
    vec4 static_lighting = surface_info.z != 0u ?
        texture(rt_static_sampler, v_lmuv) : vec4(0.0);
    float static_coverage = step(0.5 / 255.0, static_lighting.a);
    float baked_occlusion = static_coverage *
        clamp((static_lighting.a * 255.0 - 1.0) / 254.0, 0.0, 1.0);
#ifdef RT_GLOWMAP
    vec4 glow = texture(glow_sampler, uv);
#endif
    float lm_luma = dot(lm, vec3(0.2126, 0.7152, 0.0722));
    float ao_weight = mix(0.75, 1.0,
                          smoothstep(0.05, 0.35, lm_luma));
#ifdef RT_GLOWMAP
    ao_weight *= 1.0 - glow.a;
#endif
#ifdef RT_QUAD_SHARING
    // Execute subgroup operations before per-surface branches so all lanes in
    // a fragment quad participate, including lanes crossing a primitive edge.
    float anchor_distance = quad_anchor_distance();
    bool full_resolution = anchor_distance <= 384.0;
    bool full_dynamic_resolution = anchor_distance <= 512.0;
    float quad_ao = adaptive_ambient_visibility(normal, full_resolution,
                                                ao_weight,
                                                baked_occlusion);
    vec3 quad_surface_light = adaptive_surface_light(normal, full_resolution,
                                                     static_lighting.rgb);
    vec3 quad_dynamic_light = adaptive_dynamic_light(
        normal, full_dynamic_resolution);
#endif

    vec3 bloom = vec3(0.0);
    int rt_debug = pc.rt_params.x < 0.0 ?
        int(clamp(floor(-pc.rt_params.x + 0.5), 1.0, 3.0)) : 0;
    if (rt_debug != 0) {
        if (rt_debug == 1)
            out_color = vec4(vec3(static_coverage), 1.0);
        else if (rt_debug == 2)
            out_color = vec4(vec3(baked_occlusion), 1.0);
        else {
            out_color = vec4(clamp(static_lighting.rgb * 2.0, 0.0, 1.0),
                             1.0);
        }
        out_bloom = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
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
        float ao = 1.0;
        vec3 dynamic_lighting;
        vec3 surface_lighting;
#ifdef RT_QUAD_SHARING
        ao = quad_ao;
        dynamic_lighting = quad_dynamic_light;
        surface_lighting = quad_surface_light;
#else
        if (pc.lm_scale.x >= 0.0)
            ao = ambient_visibility(normal, ao_weight, baked_occlusion);
        dynamic_lighting = dynamic_light(normal);
        surface_lighting = surface_light(normal, static_lighting.rgb);
#endif
        surface_lighting *= mix(1.0, ao, 0.75);
#ifdef RT_GLOWMAP
        lm = mix(lm, vec3(1.0), glow.a);
        ao = mix(ao, 1.0, glow.a);
        surface_lighting *= 1.0 - glow.a;
#endif
        lm *= ao;
        vec3 base_lighting = (lm + pc.scroll.www) * pc.color.rgb;

        // Keep the raster/lightmap result authoritative, then add a localized
        // colored pool from ray-shadowed surface emitters. Reject weak tails
        // and cap the added luminance so they cannot become a global exposure
        // correction. Screen-style headroom protects bright surface detail.
        const vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);
        vec3 raster_rgb = texel.rgb *
            max(base_lighting + dynamic_lighting, vec3(0.0));
        float raster_luma = dot(raster_rgb, luma_weights);
        vec3 emissive_pool = max(texel.rgb * surface_lighting, vec3(0.0));
        float pool_luma = dot(emissive_pool, luma_weights);
        vec3 emissive_add = vec3(0.0);
        if (pool_luma > 0.004) {
            float pool_scale = 1.35 *
                smoothstep(0.004, 0.025, pool_luma);
            emissive_pool *= pool_scale;
            pool_luma *= pool_scale;
            float pool_limit = min(max(raster_luma * 0.45, 0.025), 0.12);
            if (pool_luma > pool_limit)
                emissive_pool *= pool_limit / pool_luma;
            emissive_add = emissive_pool *
                max(vec3(1.0) - raster_rgb, vec3(0.0));
        }
        out_color = texel;
        out_color.rgb = max(raster_rgb + emissive_add, vec3(0.0));
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
        float dynamic_luma = dot(dynamic_lighting,
                                 vec3(0.2126, 0.7152, 0.0722));
        float dynamic_bloom = smoothstep(0.22, 0.55, dynamic_luma) * 0.18;
        float emissive_add_luma = dot(emissive_add, luma_weights);
        vec3 static_bloom = vec3(0.0);
        if (emissive_add_luma > 0.008) {
            float surface_bloom = smoothstep(0.008, 0.04,
                                             emissive_add_luma);
            static_bloom = emissive_add * surface_bloom * 0.75;
            float static_bloom_luma = dot(static_bloom, luma_weights);
            if (static_bloom_luma > 0.06)
                static_bloom *= 0.06 / static_bloom_luma;
        }
        bloom += (static_bloom +
                  texel.rgb * dynamic_lighting * dynamic_bloom) *
                 max(pc.intensity, 0.0);
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
    out_bloom = vec4(bloom, out_color.a);
}
