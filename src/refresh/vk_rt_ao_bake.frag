#version 460
#extension GL_EXT_ray_query : require

layout(push_constant) uniform Push {
    uvec4 params; // AO samples, emissive samples, atlas width, seed
} pc;

layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
struct SurfaceLight {
    vec4 origin_range;
    vec4 color_strength;
    vec4 normal;
};
layout(std430, set = 0, binding = 1) readonly buffer SurfaceLights {
    uvec4 surface_info;
    SurfaceLight lights[64];
    uint light_indices[];
};

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) flat in uvec2 v_rt_data;
layout(location = 0) out vec4 out_static_lighting;

uint hash_u32(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float random_float(uint value)
{
    return float(hash_u32(value) & 0x00ffffffu) / 16777216.0;
}

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

float bake_ambient_occlusion(vec3 normal, uint seed)
{
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) :
        vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    uint sample_count = clamp(pc.params.x, 1u, 4u);
    float occlusion = 0.0;

    for (uint i = 0u; i < 4u; i++) {
        if (i >= sample_count)
            break;
        float u = (float(i) + random_float(seed + i * 17u)) /
            float(sample_count);
        float v = random_float(seed + i * 29u + 11u);
        float phi = 6.2831853 * u;
        float radius = (i & 1u) == 0u ?
            mix(0.82, 0.96, sqrt(v)) : mix(0.45, 0.86, sqrt(v));
        vec3 direction = normalize(tangent * (cos(phi) * radius) +
            bitangent * (sin(phi) * radius) +
            normal * sqrt(max(1.0 - radius * radius, 0.0)));
        float hit_distance = trace_hit_distance(v_position + normal * 0.08,
                                                direction, 160.0);
        float contact = hit_distance < 0.0 ? 0.0 :
            1.0 - smoothstep(2.0, 16.0, hit_distance);
        float broad = hit_distance < 0.0 ? 0.0 :
            1.0 - smoothstep(8.0, 160.0, hit_distance);
        // Favor creases and nearby blockers over broad scene darkening. This
        // keeps the effect easy to read without flattening the lightmap.
        occlusion += 0.70 * contact + 0.30 * broad;
    }
    float raw_occlusion = occlusion / float(sample_count);
    return min(0.90, 1.25 * smoothstep(0.015, 0.38, raw_occlusion));
}

float light_contribution(uint light_index, vec3 normal, out vec3 color)
{
    SurfaceLight source = lights[light_index];
    vec3 delta = source.origin_range.xyz - v_position;
    float distance_to_light = length(delta);
    vec3 direction = delta / max(distance_to_light, 0.001);
    float range = source.origin_range.w;
    float falloff = clamp(1.0 - distance_to_light / range, 0.0, 1.0);
    falloff = falloff * falloff * (3.0 - 2.0 * falloff);
    // Concentrate the pool around its fixture. Dense custom maps can contain
    // dozens of overlapping SURF_LIGHT ranges; cubic falloff prevents those
    // weak tails from becoming a map-wide exposure lift.
    falloff = falloff * falloff * sqrt(falloff);
    float source_cosine = max(dot(source.normal.xyz, -direction), 0.0);
    float receiver_cosine = abs(dot(normal, direction));
    float direct = source_cosine * (0.25 + 0.75 * receiver_cosine);
    float diffuse_fill = source_cosine * (1.0 - receiver_cosine) * 0.04;
    color = source.color_strength.rgb;
    float strength = min(source.color_strength.w, 192.0);
    return strength * falloff * (direct + diffuse_fill) / 255.0;
}

float area_light_visibility(uint light_index, vec3 normal, uint seed)
{
    SurfaceLight source = lights[light_index];
    vec3 source_normal = normalize(source.normal.xyz);
    vec3 up = abs(source_normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) :
        vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, source_normal));
    vec3 bitangent = cross(source_normal, tangent);
    float radius = sqrt(random_float(seed + 7u)) * source.normal.w;
    float angle = random_float(seed + 19u) * 6.2831853;
    vec3 target = source.origin_range.xyz +
        tangent * (cos(angle) * radius) +
        bitangent * (sin(angle) * radius);
    vec3 delta = target - v_position;
    float distance_to_light = length(delta);
    vec3 oriented_normal = dot(normal, delta) >= 0.0 ? normal : -normal;
    float hit_distance = trace_hit_distance(v_position + oriented_normal * 0.08,
                                            delta / max(distance_to_light, 0.001),
                                            distance_to_light);
    if (hit_distance < 0.0)
        return 1.0;
    float blocker_ratio = hit_distance / max(distance_to_light, 0.001);
    return mix(0.15, 0.40, smoothstep(0.15, 0.85, blocker_ratio));
}

vec3 bake_emissive(vec3 normal, uint seed)
{
    uint best_indices[2] = uint[2](0u, 0u);
    float best_contributions[2] = float[2](0.0, 0.0);
    vec3 best_colors[2] = vec3[2](vec3(0.0), vec3(0.0));
    uint list_offset = v_rt_data.x;
    uint candidate_count = v_rt_data.y & 0xffu;
    for (uint i = 0u; i < candidate_count; i++) {
        uint index_offset = list_offset + i;
        if (index_offset >= surface_info.y)
            break;
        uint light_index = light_indices[index_offset];
        if (light_index >= surface_info.x || light_index >= 64u)
            continue;
        vec3 color;
        float contribution = light_contribution(light_index, normal, color);
        if (contribution > best_contributions[0]) {
            best_contributions[1] = best_contributions[0];
            best_colors[1] = best_colors[0];
            best_indices[1] = best_indices[0];
            best_contributions[0] = contribution;
            best_colors[0] = color;
            best_indices[0] = light_index;
        } else if (contribution > best_contributions[1]) {
            best_contributions[1] = contribution;
            best_colors[1] = color;
            best_indices[1] = light_index;
        }
    }

    if (best_contributions[0] < 0.01 || pc.params.y == 0u)
        return vec3(0.0);

    vec3 irradiance = vec3(0.0);
    if (pc.params.y >= 2u && best_contributions[1] >= 0.01) {
        for (uint i = 0u; i < 2u; i++) {
            float visibility = area_light_visibility(best_indices[i], normal,
                                                     seed + i * 43u);
            irradiance += best_colors[i] * best_contributions[i] * visibility;
        }
    } else {
        uint sample_count = pc.params.y >= 2u ? 2u : 1u;
        float visibility = 0.0;
        for (uint i = 0u; i < 2u; i++) {
            if (i >= sample_count)
                break;
            visibility += area_light_visibility(best_indices[0], normal,
                                                seed + i * 43u);
        }
        irradiance = best_colors[0] * best_contributions[0] *
            (visibility / float(sample_count));
    }
    const float emissive_range = 0.24;
    float peak = max(irradiance.r, max(irradiance.g, irradiance.b));
    if (peak > emissive_range)
        irradiance *= emissive_range / peak;
    return irradiance;
}

vec3 encode_emissive(vec3 irradiance)
{
    return clamp(irradiance / 0.24, 0.0, 1.0);
}

void main()
{
    vec3 normal = normalize(v_normal);
    uvec2 pixel = uvec2(gl_FragCoord.xy);
    uint seed = hash_u32(pixel.x + pixel.y * max(pc.params.z, 1u) +
                         pc.params.w);
    float occlusion = bake_ambient_occlusion(normal, seed);
    vec3 irradiance = bake_emissive(normal, seed + 101u);
    vec3 encoded_emissive = encode_emissive(irradiance);
    // Alpha zero marks atlas space that was not rasterized. Preserve that
    // distinction while retaining 254 AO levels for valid surface texels.
    float encoded_occlusion =
        (1.0 + round(clamp(occlusion, 0.0, 1.0) * 254.0)) / 255.0;
    out_static_lighting = vec4(encoded_emissive, encoded_occlusion);
}
