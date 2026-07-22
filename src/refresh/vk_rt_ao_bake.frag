#version 460
#extension GL_EXT_ray_query : require

layout(push_constant) uniform Push {
    uvec4 params;
} pc;

layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 0) out float out_occlusion;

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

float trace_hit_distance(vec3 origin, vec3 direction)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xff, origin, 0.05, direction, 47.9);
    while (rayQueryProceedEXT(query)) { }
    if (rayQueryGetIntersectionTypeEXT(query, true) ==
        gl_RayQueryCommittedIntersectionNoneEXT)
        return -1.0;
    return rayQueryGetIntersectionTEXT(query, true);
}

void main()
{
    vec3 normal = normalize(v_normal);
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) :
        vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    uvec2 pixel = uvec2(gl_FragCoord.xy);
    uint seed = hash_u32(pixel.x + pixel.y * max(pc.params.y, 1u) +
                         pc.params.w);
    uint sample_count = clamp(pc.params.x, 1u, 4u);
    float occlusion = 0.0;

    for (uint i = 0u; i < 4u; i++) {
        if (i >= sample_count)
            break;
        float u = (float(i) + random_float(seed + i * 17u)) /
            float(sample_count);
        float v = random_float(seed + i * 29u + 11u);
        float phi = 6.2831853 * u;
        float radius = sqrt(v) * 0.72;
        vec3 direction = normalize(tangent * (cos(phi) * radius) +
            bitangent * (sin(phi) * radius) +
            normal * sqrt(max(1.0 - radius * radius, 0.0)));
        float hit_distance = trace_hit_distance(v_position + normal * 0.08,
                                                direction);
        float contact = hit_distance < 0.0 ? 0.0 :
            1.0 - smoothstep(2.0, 12.0, hit_distance);
        float broad = hit_distance < 0.0 ? 0.0 :
            1.0 - smoothstep(2.0, 48.0, hit_distance);
        occlusion += 0.7 * contact + 0.3 * broad;
    }

    out_occlusion = occlusion / float(sample_count);
}
