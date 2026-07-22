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
    vec2 rt_padding;
    float rt_enabled;
    float _pad;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;
layout(set = 1, binding = 0) uniform accelerationStructureEXT scene;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in float v_mode;
layout(location = 3) in vec3 v_position;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_bloom;

bool occluded(vec3 origin, vec3 direction, float distance)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xff, origin, 0.05, direction, max(distance - 0.1, 0.05));
    while (rayQueryProceedEXT(query)) { }
    return rayQueryGetIntersectionTypeEXT(query, true) !=
        gl_RayQueryCommittedIntersectionNoneEXT;
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
        if (falloff > 0.0 &&
            (pc.rt_enabled < 0.5 ||
             !occluded(v_position + normal * 0.05,
                      delta / max(distance_to_light, 0.001),
                      distance_to_light))) {
            light += pc.dlight_colors[i].rgb *
                (pc.dlight_colors[i].w * falloff / 255.0);
        }
    }
    return light;
}

void main()
{
    out_bloom = vec4(0.0);
    float mode = mod(v_mode, 4.0);
    vec2 uv = v_uv;
    if (v_mode >= 4.0)
        uv += vec2(0.0625) * sin(uv.ts * vec2(4.0) + vec2(pc.dlight.a));

    vec3 normal = normalize(cross(dFdx(v_position), dFdy(v_position)));
    if (dot(normal, pc.dlight.xyz - v_position) < 0.0)
        normal = -normal;

    if (mode > 1.5) {
        out_color = v_color;
    } else {
        out_color = texture(tex_sampler, uv);
        if (pc.desaturation > 0.0) {
            float luma = dot(out_color.rgb, vec3(0.2126, 0.7152, 0.0722));
            out_color.rgb = mix(out_color.rgb, vec3(luma), pc.desaturation);
        }
        out_color.rgb *= pc.intensity;
        out_color.rgb *= clamp(v_color.rgb + dynamic_light(normal), 0.0, 1.0);
        out_color.a *= v_color.a;
    }

    if (pc.fog.a < 0.0) {
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
    } else if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        float fog = 1.0 - exp(-(d * d));
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, fog);
    }
}
