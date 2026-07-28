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
    vec4 rt_params;
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

float dynamic_ray_bias(vec3 position)
{
    float magnitude = max(abs(position.x),
        max(abs(position.y), abs(position.z)));
    return clamp(0.25 + magnitude * 0.00005, 0.25, 0.75);
}

float trace_dynamic_hit_distance(vec3 origin, vec3 direction,
                                 float max_distance)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xff, origin, 0.10, direction, max(max_distance - 0.20, 0.101));
    while (rayQueryProceedEXT(query)) { }
    if (rayQueryGetIntersectionTypeEXT(query, true) ==
        gl_RayQueryCommittedIntersectionNoneEXT)
        return -1.0;
    return rayQueryGetIntersectionTEXT(query, true);
}

float dynamic_shadow_visibility(float hit_distance, float ray_distance)
{
    if (hit_distance < 0.0)
        return 1.0;
    float blocker_ratio = hit_distance / max(ray_distance, 0.001);
    float shadow_opacity = mix(0.96, 0.72,
        smoothstep(0.15, 0.85, blocker_ratio));
    return 1.0 - shadow_opacity;
}

float dynamic_light_visibility(vec3 origin, vec3 delta, float range,
                               float contribution, int light_index,
                               out float penumbra)
{
    penumbra = 0.0;
    float center_distance = length(delta);
    vec3 center_direction = delta / max(center_distance, 0.001);
    if (light_index != 0 || contribution < 0.06 || center_distance > 320.0) {
        float hit = trace_dynamic_hit_distance(origin, center_direction,
                                               center_distance);
        return dynamic_shadow_visibility(hit, center_distance);
    }

    vec3 reference = abs(center_direction.z) < 0.90 ?
        vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(reference, center_direction));
    float source_radius = clamp(range * 0.018, 1.5, 6.0);
    vec3 first_delta = delta + tangent * source_radius;
    vec3 second_delta = delta - tangent * source_radius;
    float first_distance = length(first_delta);
    float second_distance = length(second_delta);
    float first_hit = trace_dynamic_hit_distance(origin,
        first_delta / max(first_distance, 0.001), first_distance);
    float second_hit = trace_dynamic_hit_distance(origin,
        second_delta / max(second_distance, 0.001), second_distance);
    float first_visibility = dynamic_shadow_visibility(first_hit,
                                                        first_distance);
    float second_visibility = dynamic_shadow_visibility(second_hit,
                                                         second_distance);
    penumbra = smoothstep(0.12, 0.55,
                          abs(first_visibility - second_visibility));
    return 0.5 * (first_visibility + second_visibility);
}

vec2 material_params(float packed)
{
    float bits = mod(floor(max(packed, 0.0) + 0.5), 256.0);
    return vec2(floor(bits / 16.0), mod(bits, 16.0)) / 15.0;
}

float material_skylight(float packed)
{
    return mod(floor(max(packed, 0.0) / 256.0), 16.0) / 15.0;
}

float material_environment(float packed)
{
    return mod(floor(max(packed, 0.0) / 4096.0), 16.0) / 15.0;
}

float material_sunlight(float packed)
{
    return mod(floor(max(packed, 0.0) / 65536.0), 16.0) / 15.0;
}

float material_output(float packed)
{
    return mod(floor(max(packed, 0.0) + 0.5), 256.0) / 255.0;
}

vec4 rt_controls(float packed)
{
    uint bits = uint(clamp(floor(packed + 0.5), 0.0, 16777215.0));
    return vec4(float(bits & 127u) / 127.0,
                float((bits >> 7) & 127u) / 127.0,
                float((bits >> 14) & 31u) / 31.0,
                float((bits >> 19) & 31u) / 31.0);
}

vec3 underwater_caustics(vec3 position, vec3 normal, vec4 controls)
{
    float strength = controls.z;
    if (strength <= 0.001)
        return vec3(0.0);

    vec3 axis = abs(normal);
    vec2 coord = axis.z >= axis.x && axis.z >= axis.y ? position.xy :
        (axis.x >= axis.y ? position.yz : position.xz);
    vec2 p = coord * 0.045;
    float time = pc.dlight.w;
    float wave0 = sin(dot(p, vec2(1.72, 1.13)) + time);
    float wave1 = sin(dot(p, vec2(-1.31, 1.91)) - time * 1.27);
    float wave2 = sin(dot(p, vec2(0.73, -2.08)) + time * 0.71);
    float ridge = 1.0 - abs((wave0 + wave1 + wave2) / 3.0);
    float pattern = smoothstep(0.52, 0.88, ridge);
    float orientation = 0.40 + 0.60 * axis.z;
    float distance_fade = 1.0 - smoothstep(512.0, 1152.0,
        distance(position, pc.dlight.xyz));
    vec3 tint = vec3(0.18, 0.55, 0.85);
    return tint * pattern * orientation * distance_fade * 0.18 * strength;
}

float material_specular_lobe(float ndoth, float ndotv, float gloss,
                             float exponent)
{
    float primary = pow(ndoth, exponent);
    float clearcoat = primary * primary * mix(0.12, 0.38, gloss);
    float edge = 1.0 - clamp(ndotv, 0.0, 1.0);
    float edge2 = edge * edge;
    float fresnel = edge2 * edge2 * edge;
    return (primary + clearcoat) * mix(0.65, 1.35, fresnel);
}

float material_detail_factor(vec3 albedo, float roughness)
{
    float luma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    float detail = mix(0.82, 1.18, smoothstep(0.10, 0.70, luma));
    return mix(1.0, detail, 0.55 * (1.0 - roughness));
}

vec3 material_specular_tint(vec3 albedo, float reflectivity, float roughness)
{
    float luma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    vec3 chroma = clamp(albedo / max(luma, 0.08), vec3(0.45), vec3(1.65));
    chroma = mix(vec3(1.0), chroma, smoothstep(0.04, 0.20, luma));
    float gloss = 1.0 - roughness;
    float metallic = 0.55 * reflectivity * gloss * (0.70 + 0.30 * gloss);
    return mix(vec3(1.0), chroma, metallic);
}

vec3 material_bump_normal(vec3 position, vec3 normal, vec3 albedo,
                          float reflectivity, float roughness, float mask)
{
    vec3 dpdx = dFdx(position);
    vec3 dpdy = dFdy(position);
    float height = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    float dhdx = dFdx(height);
    float dhdy = dFdy(height);
    vec3 first = cross(dpdy, normal);
    vec3 second = cross(normal, dpdx);
    float determinant = dot(dpdx, first);
    float valid = step(0.000001, abs(determinant));
    float safe_determinant = determinant < 0.0 ?
        min(determinant, -0.000001) : max(determinant, 0.000001);
    vec3 gradient = (first * dhdx + second * dhdy) / safe_determinant;
    float gloss = 1.0 - roughness;
    float strength = 3.0 * rt_controls(pc.rt_params.w).x * mask *
        mix(0.35, 1.0, reflectivity) * mix(0.30, 1.0, gloss);
    gradient *= strength * valid;
    float slope = length(gradient);
    gradient *= min(1.0, 0.45 / max(slope, 0.000001));
    return normalize(normal - gradient);
}

vec3 dynamic_light(vec3 shading_normal, vec3 geometric_normal,
                   float reflectivity, float roughness, out vec3 specular,
                   out vec3 bounce, out vec3 fringe, out vec3 afterglow)
{
    vec3 light = vec3(0.0);
    specular = vec3(0.0);
    bounce = vec3(0.0);
    fringe = vec3(0.0);
    afterglow = vec3(0.0);
    vec4 controls = rt_controls(pc.rt_params.w);
    vec3 view_dir = normalize(pc.dlight.xyz - v_position);
    float gloss = 1.0 - roughness;
    float exponent = mix(10.0, 96.0, pow(gloss, 0.9));
    float lobe_normalization = mix(0.80, 1.85, gloss);
    float specular_scale = reflectivity * controls.x *
        mix(1.30, 0.68, roughness) * lobe_normalization;
    for (int i = 0; i < 3; i++) {
        float range = pc.dlight_origins[i].w;
        if (range <= 0.0)
            break;
        vec3 delta = pc.dlight_origins[i].xyz - v_position;
        float distance_to_light = length(delta);
        float falloff = max(1.0 - distance_to_light / range, 0.0);
        vec3 oriented_normal = dot(geometric_normal, delta) >= 0.0 ?
            geometric_normal : -geometric_normal;
        if (falloff <= 0.0)
            continue;
        bool cached_afterglow = pc.dlight_colors[i].w < 0.0;
        float energy = abs(pc.dlight_colors[i].w) * falloff / 255.0;
        if (max(pc.dlight_colors[i].r,
                max(pc.dlight_colors[i].g, pc.dlight_colors[i].b)) *
            energy < 0.01)
            continue;
        vec3 light_dir = delta / max(distance_to_light, 0.001);
        float contribution = max(pc.dlight_colors[i].r,
            max(pc.dlight_colors[i].g, pc.dlight_colors[i].b)) * energy;
        float bias = dynamic_ray_bias(v_position);
        vec3 ray_origin = v_position + oriented_normal * bias +
                          light_dir * 0.05;
        vec3 ray_delta = pc.dlight_origins[i].xyz - ray_origin;
        float penumbra;
        float visibility = dynamic_light_visibility(
            ray_origin, ray_delta, range, contribution, i, penumbra);
        vec3 radiance = pc.dlight_colors[i].rgb * energy * visibility;
        light += radiance;
        if (cached_afterglow)
            afterglow += radiance;
        fringe += pc.dlight_colors[i].rgb * energy * penumbra;
        if (controls.y > 0.001) {
            float color_high = max(pc.dlight_colors[i].r,
                max(pc.dlight_colors[i].g, pc.dlight_colors[i].b));
            float color_low = min(pc.dlight_colors[i].r,
                min(pc.dlight_colors[i].g, pc.dlight_colors[i].b));
            float chroma = (color_high - color_low) /
                           max(color_high, 0.001);
            float color_weight = mix(0.35, 1.0,
                smoothstep(0.08, 0.45, chroma));
            float locality = falloff * falloff * (3.0 - 2.0 * falloff);
            float facing = abs(dot(geometric_normal, light_dir));
            float wrap = 0.35 + 0.65 * (1.0 - facing) * (1.0 - facing);
            bounce += radiance * locality * wrap * color_weight;
        }
        if (specular_scale > 0.001) {
            vec3 half_dir = normalize(light_dir + view_dir);
            float ndoth = max(dot(shading_normal, half_dir), 0.0);
            float ndotl = max(dot(shading_normal, light_dir), 0.0);
            float lobe = material_specular_lobe(ndoth,
                max(dot(shading_normal, view_dir), 0.0), gloss, exponent);
            specular += radiance * lobe *
                smoothstep(0.0, 0.20, ndotl) * specular_scale;
        }
    }
    fringe *= 0.22 * controls.w;
    float fringe_luma = dot(fringe, vec3(0.2126, 0.7152, 0.0722));
    fringe *= min(1.0, 0.06 / max(fringe_luma, 0.000001));
    bounce *= 0.65 * controls.y;
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

vec3 surface_specular(vec3 normal, float reflectivity, float roughness)
{
    if (surface_info.z == 0u || reflectivity <= 0.001 ||
        rt_controls(pc.rt_params.w).x <= 0.001)
        return vec3(0.0);

    uint candidate_count = min(v_rt_data.y & 0xffu, 4u);
    vec3 view_dir = normalize(pc.dlight.xyz - v_position);
    float gloss = 1.0 - roughness;
    float exponent = mix(10.0, 96.0, pow(gloss, 0.9));
    float lobe_normalization = mix(0.80, 1.85, gloss);
    float scale = reflectivity * rt_controls(pc.rt_params.w).x *
        mix(1.20, 0.62, roughness) * lobe_normalization;
    float emissive = clamp(pc.rt_params.x, 0.0, 2.0);
    float strength_scale = pow(emissive, 0.75) * 1.5;
    vec3 specular = vec3(0.0);
    for (uint i = 0u; i < candidate_count; ++i) {
        uint index_offset = v_rt_data.x + i;
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
        float ndotl = max(dot(normal, direction), 0.0);
        vec3 half_dir = normalize(direction + view_dir);
        float ndoth = max(dot(normal, half_dir), 0.0);
        float lobe = material_specular_lobe(ndoth,
            max(dot(normal, view_dir), 0.0), gloss, exponent);
        specular += color * contribution * lobe *
            smoothstep(0.0, 0.20, ndotl) * scale;
    }
    return specular;
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

vec3 adaptive_dynamic_light(vec3 shading_normal, vec3 geometric_normal,
                            bool full_resolution, float reflectivity, float roughness,
                            out vec3 specular, out vec3 bounce,
                            out vec3 fringe, out vec3 afterglow)
{
    if (full_resolution)
        return dynamic_light(shading_normal, geometric_normal, reflectivity,
                             roughness, specular, bounce, fringe, afterglow);

    vec3 light = vec3(0.0);
    specular = vec3(0.0);
    bounce = vec3(0.0);
    fringe = vec3(0.0);
    afterglow = vec3(0.0);
    if ((gl_SubgroupInvocationID & 3u) == 0u)
        light = dynamic_light(shading_normal, geometric_normal, reflectivity,
                              roughness, specular, bounce, fringe, afterglow);
    specular = subgroupQuadBroadcast(specular, 0);
    bounce = subgroupQuadBroadcast(bounce, 0);
    fringe = subgroupQuadBroadcast(fringe, 0);
    afterglow = subgroupQuadBroadcast(afterglow, 0);
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
    if (dot(normal, pc.dlight.xyz - v_position) < 0.0)
        normal = -normal;
    vec2 material = material_params(pc.rt_params.z);
    float material_reflect = material.x;
    float material_roughness = material.y;
    vec4 material_texel = texture(tex_sampler, uv);
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
    float bump_mask = 1.0 - step(1.5, mode);
#ifdef RT_ALPHA_TEST
    bump_mask = 0.0;
#endif
#ifdef RT_GLOWMAP
    bump_mask *= 1.0 - smoothstep(0.15, 0.80, glow.a);
#endif
    vec3 shading_normal = material_bump_normal(v_position, normal,
        material_texel.rgb, material_reflect, material_roughness, bump_mask);
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
    vec3 quad_surface_light = adaptive_surface_light(shading_normal,
        full_resolution, static_lighting.rgb);
    vec3 quad_dynamic_specular;
    vec3 quad_dynamic_bounce;
    vec3 quad_dynamic_fringe;
    vec3 quad_dynamic_afterglow;
    vec3 quad_dynamic_light = adaptive_dynamic_light(
        shading_normal, normal, full_dynamic_resolution, material_reflect,
        material_roughness, quad_dynamic_specular, quad_dynamic_bounce,
        quad_dynamic_fringe, quad_dynamic_afterglow);
#endif

    vec3 bloom = vec3(0.0);
    int rt_debug = pc.rt_params.x < 0.0 ?
        int(clamp(floor(-pc.rt_params.x + 0.5), 1.0, 3.0)) :
        (pc.rt_params.y < -7.5 ?
            int(clamp(floor(-pc.rt_params.y + 0.5), 8.0, 15.0)) : 0);
    if (rt_debug >= 1 && rt_debug <= 3) {
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
        vec4 texel = material_texel;
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
        vec3 dynamic_specular;
        vec3 dynamic_bounce;
        vec3 dynamic_fringe;
        vec3 dynamic_afterglow;
        vec3 surface_lighting;
#ifdef RT_QUAD_SHARING
        ao = quad_ao;
        dynamic_lighting = quad_dynamic_light;
        dynamic_specular = quad_dynamic_specular;
        dynamic_bounce = quad_dynamic_bounce;
        dynamic_fringe = quad_dynamic_fringe;
        dynamic_afterglow = quad_dynamic_afterglow;
        surface_lighting = quad_surface_light;
#else
        if (pc.lm_scale.x >= 0.0)
            ao = ambient_visibility(normal, ao_weight, baked_occlusion);
        dynamic_lighting = dynamic_light(shading_normal, normal,
                                         material_reflect, material_roughness,
                                         dynamic_specular, dynamic_bounce,
                                         dynamic_fringe, dynamic_afterglow);
        surface_lighting = surface_light(shading_normal, static_lighting.rgb);
#endif
        vec3 static_specular = surface_specular(shading_normal,
            material_reflect, material_roughness);
        if (rt_debug == 8) {
            vec3 diagnostic = dynamic_specular * vec3(0.0, 8.0, 8.0) +
                              static_specular * vec3(8.0, 4.5, 0.0);
            out_color = vec4(clamp(diagnostic, 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        if (rt_debug == 11) {
            out_color = vec4(clamp(dynamic_fringe * 8.0, 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        if (rt_debug == 14) {
            out_color = vec4(clamp(dynamic_afterglow * 8.0, 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        surface_lighting *= mix(1.0, ao, 0.75);
#ifdef RT_GLOWMAP
        lm = mix(lm, vec3(1.0), glow.a);
        ao = mix(ao, 1.0, glow.a);
        surface_lighting *= 1.0 - glow.a;
#endif
        lm *= ao;
        vec3 base_lighting = (lm + pc.scroll.www) * pc.color.rgb;

        // Keep the raster/lightmap result authoritative, then add localized,
        // capped dynamic color spill and ray-shadowed surface-emitter pools.
        // Screen-style headroom protects bright texture detail.
        const vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);
        vec3 raster_rgb = texel.rgb *
            max(base_lighting + dynamic_lighting + dynamic_fringe, vec3(0.0));
        vec4 controls = rt_controls(pc.rt_params.w);
        vec3 caustics = underwater_caustics(v_position, normal, controls);
        float caustics_luma = dot(caustics, luma_weights);
        float caustics_cap = min(max(dot(raster_rgb, luma_weights) * 0.22,
                                     0.015), 0.08);
        caustics *= min(1.0, caustics_cap /
                        max(caustics_luma, 0.000001));
        vec3 caustics_add = caustics *
            max(vec3(1.0) - clamp(raster_rgb, 0.0, 1.0), vec3(0.0));
        float caustics_add_luma = dot(caustics_add, luma_weights);
        if (rt_debug == 10) {
            out_color = vec4(clamp(caustics_add * 8.0, 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        raster_rgb += caustics_add;
        float caustics_bloom_weight = smoothstep(0.018, 0.065,
                                                  caustics_add_luma);
        vec3 caustics_bloom = caustics_add * caustics_bloom_weight * 0.12;
        float caustics_bloom_luma = dot(caustics_bloom, luma_weights);
        caustics_bloom *= min(1.0, 0.012 /
                              max(caustics_bloom_luma, 0.000001));
        bloom += caustics_bloom;
        float bounce_luma = dot(dynamic_bounce, luma_weights);
        float bounce_cap = min(max(dot(raster_rgb, luma_weights) * 0.30,
                                   0.02), 0.10);
        dynamic_bounce *= min(1.0, bounce_cap /
                              max(bounce_luma, 0.000001));
        vec3 bounce_add = dynamic_bounce *
            max(vec3(1.0) - clamp(raster_rgb, 0.0, 1.0), vec3(0.0));
        float bounce_add_luma = dot(bounce_add, luma_weights);
        if (rt_debug == 9) {
            out_color = vec4(clamp(bounce_add * 8.0, 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        raster_rgb += bounce_add;
        float bounce_bloom_weight = smoothstep(0.018, 0.065,
                                                bounce_add_luma);
        vec3 bounce_bloom = bounce_add * bounce_bloom_weight * 0.25;
        float bounce_bloom_luma = dot(bounce_bloom, luma_weights);
        bounce_bloom *= min(1.0, 0.025 /
                            max(bounce_bloom_luma, 0.000001));
        bloom += bounce_bloom;
        // Treat the lightmap as the broad aperture signal and use baked AO
        // only as restrained secondary modulation.  The old direct AO mask
        // could remove the fill completely and expose coarse bake regions as
        // isolated dark blobs on otherwise flat surfaces.
        float sky_broad_open = mix(0.30, 1.0,
                                   smoothstep(0.04, 0.32, lm_luma));
        float sky_visibility = static_coverage > 0.5 ?
            1.0 - baked_occlusion : ao;
        float sky_visibility_weight = mix(0.65, 1.0,
                                          clamp(sky_visibility, 0.0, 1.0));
        float sky_visibility_edge = fwidth(sky_visibility);
        sky_visibility_weight = mix(sky_visibility_weight, 1.0,
            smoothstep(0.025, 0.16, sky_visibility_edge));
        float sky_openness = sky_broad_open * sky_visibility_weight;
        float sky_orientation = mix(0.22, 1.0,
                                    smoothstep(0.15, 0.85, abs(normal.z)));
        float sky_mask = 1.0;
#ifdef RT_GLOWMAP
        sky_mask = 1.0 - smoothstep(0.12, 0.72, glow.a);
#endif
        vec3 skylight_add = vec3(0.16, 0.27, 0.44) *
            material_skylight(pc.rt_params.z) * sky_openness *
            sky_orientation * sky_mask;
        float skylight_luma = dot(skylight_add, luma_weights);
        skylight_add *= min(1.0, 0.075 / max(skylight_luma, 0.000001));
        skylight_add *= max(vec3(1.0) - clamp(raster_rgb, 0.0, 1.0),
                            vec3(0.0));
        if (rt_debug == 12) {
            out_color = vec4(clamp(skylight_add * 8.0, 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        raster_rgb += skylight_add;
        vec3 environment_view = normalize(pc.dlight.xyz - v_position);
        float environment_edge = 1.0 -
            max(dot(shading_normal, environment_view), 0.0);
        float environment_edge2 = environment_edge * environment_edge;
        float environment_fresnel = 0.30 + 0.70 *
            (environment_edge2 * environment_edge2 * environment_edge);
        float environment_smoothness = 1.0 - material_roughness;
        float environment_orientation = mix(0.35, 1.0,
            smoothstep(0.10, 0.85, abs(normal.z)));
        vec3 environment_add = vec3(0.18, 0.30, 0.50) * 0.65 *
            material_environment(pc.rt_params.z) * material_reflect *
            mix(0.35, 1.0, environment_smoothness) *
            environment_fresnel * environment_orientation *
            sky_openness * sky_mask;
        environment_add *= material_specular_tint(texel.rgb,
            material_reflect, material_roughness);
        float environment_luma = dot(environment_add, luma_weights);
        environment_add *= min(1.0, 0.08 /
                               max(environment_luma, 0.000001));
        environment_add *= max(vec3(1.0) - clamp(raster_rgb, 0.0, 1.0),
                               vec3(0.0));
        if (rt_debug == 13) {
            out_color = vec4(clamp(environment_add * 8.0, 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        raster_rgb += environment_add;
        vec3 sun_direction = normalize(vec3(0.45, 0.35, 0.82));
        float sun_facing = smoothstep(0.08, 0.72,
                                      dot(normal, sun_direction));
        float sun_visibility_weight = mix(0.82, 1.0,
                                           clamp(sky_visibility, 0.0, 1.0));
        sun_visibility_weight = mix(sun_visibility_weight, 1.0,
            smoothstep(0.025, 0.16, sky_visibility_edge));
        float sun_openness = sky_broad_open * sky_broad_open *
                             sun_visibility_weight;
        vec3 sunlight_add = texel.rgb * vec3(1.0, 0.70, 0.40) * 0.42 *
            material_sunlight(pc.rt_params.z) * sun_facing *
            sun_openness * sky_mask;
        float sunlight_luma = dot(sunlight_add, luma_weights);
        sunlight_add *= min(1.0, 0.12 /
                            max(sunlight_luma, 0.000001));
        sunlight_add *= max(vec3(1.0) - clamp(raster_rgb, 0.0, 1.0),
                            vec3(0.0));
        if (rt_debug == 15) {
            out_color = vec4(clamp(sunlight_add * 8.0, 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        raster_rgb += sunlight_add;
        float reflection_surface_mask = 1.0;
#ifdef RT_GLOWMAP
        float glow_luma = dot(glow.rgb, vec3(0.2126, 0.7152, 0.0722));
        reflection_surface_mask = 1.0 - smoothstep(0.08, 0.35, glow_luma);
#endif
        if (material_reflect > 0.001 &&
            rt_controls(pc.rt_params.w).x > 0.001) {
            float surface_luma = dot(raster_rgb, luma_weights);
            vec3 highlight = (dynamic_specular + static_specular) *
                             reflection_surface_mask;
            highlight *= material_detail_factor(texel.rgb,
                                                material_roughness);
            highlight *= material_specular_tint(texel.rgb, material_reflect,
                                                material_roughness);
            float highlight_luma = dot(highlight, luma_weights);
            float cap = min(max(surface_luma * 0.22, 0.035), 0.14);
            highlight *= min(1.0, cap / max(highlight_luma, 0.000001));
            highlight_luma = dot(highlight, luma_weights);
            raster_rgb += highlight;
            float bloom_weight = smoothstep(0.01, 0.04, highlight_luma);
            bloom += highlight * bloom_weight * 0.35;
        }
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
        out_color.a *= v_color.a;
        out_color.rgb = max(raster_rgb + emissive_add, vec3(0.0));
        if (pc.intensity < 0.0) {
            out_color.rgb *= (out_color.r + out_color.g + out_color.b) / 3.0;
            out_color.rgb *= v_color.a;
        } else {
            out_color.rgb *= pc.intensity;
        }
#ifdef RT_GLOWMAP
        bloom += texel.rgb * glow.a * pc.intensity;
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
    out_bloom = vec4(bloom,
        v_mode < 4.0 ? material_output(pc.rt_params.z) : 0.0);
}
