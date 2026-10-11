#version 460
#extension GL_EXT_ray_query : require
#extension GL_GOOGLE_include_directive : require
#include "vk_rt_reflection_common.glsl"

layout(set = 1, binding = 0) uniform accelerationStructureEXT scene;
layout(std430, set = 1, binding = 1) readonly buffer SceneLighting {
    uvec4 surface_info;
};
layout(set = 1, binding = 2) uniform sampler2D static_sampler;
layout(set = 1, binding = 4) uniform sampler2D atlas_sampler;
layout(set = 2, binding = 0) uniform sampler2D lightmap_sampler;
struct RTTriangle {
    vec4 uv_layer[3];
    vec4 lighting[3];
    vec4 normal;
};
layout(std430, set = 1, binding = 3) readonly buffer WorldHits { RTTriangle triangles[]; };
layout(std430, set = 1, binding = 5) readonly buffer ModelHits { RTTriangle model_triangles[]; };

void main()
{
    out_color = vec4(0.0);
    if (any(lessThan(v_uv, pc.viewport.xy)) ||
        any(greaterThanEqual(v_uv, pc.viewport.xy + pc.viewport.zw)))
        return;
    vec2 uv = pixel_uv(v_uv);
    float depth = depth_at(uv);
    vec2 material = material_at(uv);
    if (depth >= 0.9999 || depth <= 0.00001 || material.x <= 0.001)
        return;
    vec3 p = view_position(uv, depth);
    vec3 n = normal_at(uv, p);
    if (receiver_weight(n, material.y) <= 0.001)
        return;
    vec3 position = pc.camera.xyz + world_vector(p);
    vec3 normal = normalize(world_vector(n));
    vec3 direction = normalize(world_vector(reflect(normalize(p), n)));
    float magnitude = max(abs(position.x), max(abs(position.y), abs(position.z)));
    vec3 origin = position + normal * clamp(0.25 + magnitude * 0.00005, 0.25, 0.75);
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene,
        gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
        0x03, origin, 0.15, direction, 1536.0);
    while (rayQueryProceedEXT(query)) { }
    if (rayQueryGetIntersectionTypeEXT(query, true) ==
        gl_RayQueryCommittedIntersectionNoneEXT)
        return;
    uint primitive = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
    bool model = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true) == 1u;
    if (primitive >= (model ? uint(model_triangles.length()) : uint(triangles.length())))
        return;
    RTTriangle triangle;
    if (model) triangle = model_triangles[primitive];
    else triangle = triangles[primitive];
    vec2 bary = rayQueryGetIntersectionBarycentricsEXT(query, true);
    vec3 b = vec3(1.0 - bary.x - bary.y, bary);
    vec2 hit_uv = triangle.uv_layer[0].xy * b.x +
                  triangle.uv_layer[1].xy * b.y + triangle.uv_layer[2].xy * b.z;
    vec3 lighting = triangle.lighting[0].rgb * b.x +
                    triangle.lighting[1].rgb * b.y + triangle.lighting[2].rgb * b.z;
    if (!model && triangle.uv_layer[0].w >= 0.0) {
        vec2 lmuv = vec2(triangle.uv_layer[0].w, triangle.lighting[0].w) * b.x +
                    vec2(triangle.uv_layer[1].w, triangle.lighting[1].w) * b.y +
                    vec2(triangle.uv_layer[2].w, triangle.lighting[2].w) * b.z;
        lighting = textureLod(lightmap_sampler, lmuv, 0.0).rgb;
        if (surface_info.z != 0u)
            lighting += textureLod(static_sampler, lmuv, 0.0).rgb;
    }
    float distance = rayQueryGetIntersectionTEXT(query, true);
    int slot = int(floor(triangle.uv_layer[0].z + 0.5));
    vec3 radiance = vec3(0.0);
    if (slot >= 0) {
        vec2 tile = vec2(slot % 32, slot / 32) * 64.0;
        vec2 atlas_uv = (tile + 1.5 + fract(hit_uv) * 61.0) /
                        vec2(textureSize(atlas_sampler, 0));
        vec3 albedo = textureLod(atlas_sampler, atlas_uv, 0.0).rgb;
        radiance = albedo * max(lighting, vec3(0.20));
    }
    // Store unweighted radiance for filtering, plus normalized hit distance.
    // 1/4 encoding preserves values up to 4 in the existing UNORM target.
    out_color = vec4(radiance * 0.25, max(distance / 1536.0, 1.0 / 255.0));
}
