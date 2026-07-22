#version 460
#extension GL_EXT_ray_query : require

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
    SurfaceLight lights[];
};

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in float v_mode;
layout(location = 3) in vec2 v_lmuv;
layout(location = 4) in vec3 v_position;
layout(location = 5) flat in uvec4 v_rt_lights;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_bloom;

bool occluded(vec3 origin, vec3 direction, float distance_to_light)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xff, origin, 0.05, direction, max(distance_to_light - 0.1, 0.05));
    while (rayQueryProceedEXT(query)) { }
    return rayQueryGetIntersectionTypeEXT(query, true) !=
        gl_RayQueryCommittedIntersectionNoneEXT;
}

vec3 direct_light(vec3 normal)
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
    for (uint i = 0; i < min(v_rt_lights.x, 2u); i++) {
        SurfaceLight source = lights[i == 0u ? v_rt_lights.y : v_rt_lights.z];
        vec3 delta = source.origin_range.xyz - v_position;
        float distance_to_light = length(delta);
        vec3 light_direction = delta / max(distance_to_light, 0.001);
        float falloff = max(1.0 - distance_to_light / source.origin_range.w, 0.0);
        vec3 oriented_normal = dot(normal, delta) >= 0.0 ? normal : -normal;
        float source_cosine = max(dot(source.normal.xyz, -light_direction), 0.0);
        float receiver_cosine = abs(dot(normal, light_direction));
        float angular = source_cosine * (0.25 + 0.75 * receiver_cosine);
        if (source.color_strength.w > 0.5 && falloff * angular > 0.002 &&
            !occluded(v_position + oriented_normal * 0.05,
                      light_direction,
                      distance_to_light)) {
            light += source.color_strength.rgb *
                (source.color_strength.w * falloff * angular / 255.0);
        }
    }
    return light;
}

float ambient_visibility(vec3 normal)
{
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) :
        vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    // The phase is constant across a BSP face. Neighboring fragments therefore
    // trace coherently instead of producing texture-like stochastic noise.
    float seed = (float(v_rt_lights.w) + 0.5) / 256.0;
    float phi = seed * 6.2831853;
    const float radius = 0.45;
    vec3 direction = normalize(tangent * (cos(phi) * radius) +
        bitangent * (sin(phi) * radius) + normal * sqrt(1.0 - radius * radius));
    return occluded(v_position + normal * 0.05, direction, 48.0) ? 0.88 : 1.0;
}

void main()
{
    float mode = mod(v_mode, 4.0);
    vec2 uv = v_uv;
    if (v_mode >= 4.0)
        uv += vec2(0.0625) * sin(uv.ts * vec2(4.0) + vec2(pc.dlight.a));

    vec3 normal = normalize(cross(dFdx(v_position), dFdy(v_position)));

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
        float ao = pc.lm_scale.x < 0.0 ? 1.0 : ambient_visibility(normal);
#ifdef RT_GLOWMAP
        vec4 glow = texture(glow_sampler, uv);
        lm = mix(lm, vec3(1.0), glow.a);
        ao = mix(ao, 1.0, glow.a);
#endif
        lm *= ao;
        out_color = texel;
        out_color.rgb *= (lm + pc.scroll.www) * pc.color.rgb +
            direct_light(normal);
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
