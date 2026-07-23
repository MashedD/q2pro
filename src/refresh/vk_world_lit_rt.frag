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
    vec4 rt_params;
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
            (pc.rt_params.x < 0.5 ||
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
    int rt_debug = int(clamp(floor(pc.rt_params.w + 0.5), 0.0, 7.0));
    vec3 debug_view = normalize(pc.dlight.xyz - v_position);
    float debug_ndotv = max(dot(normal, debug_view), 0.0);
    float debug_fresnel = pow(1.0 - debug_ndotv, 1.5);
    vec3 reflected_view = normalize(reflect(-debug_view, normal));
    float reflection_key = pow(max(dot(reflected_view,
        normalize(vec3(0.45, 0.30, 0.84))), 0.0), 6.0);
    float reflection_fill = pow(max(dot(reflected_view,
        normalize(vec3(-0.55, -0.12, 0.83))), 0.0), 5.0);
    float debug_response = 0.05 + 0.34 * reflection_key +
        0.20 * reflection_fill + 0.08 * debug_fresnel;
    if (rt_debug == 4) {
        out_color = vec4(vec3(pc.rt_params.z), 1.0);
        return;
    }
    if (rt_debug == 5) {
        out_color = vec4(vec3(debug_response * pc.rt_params.z * 4.0), 1.0);
        return;
    }
    if (rt_debug == 6) {
        out_color = vec4(vec3(debug_response * pc.rt_params.z * 4.0), 1.0);
        return;
    }

    if (mode > 1.5) {
        out_color = v_color;
    } else {
        out_color = texture(tex_sampler, uv);
        if (pc.desaturation > 0.0) {
            float luma = dot(out_color.rgb, vec3(0.2126, 0.7152, 0.0722));
            out_color.rgb = mix(out_color.rgb, vec3(luma), pc.desaturation);
        }
        out_color.rgb *= pc.intensity;
        vec3 dynamic = dynamic_light(normal);
        out_color.rgb *= clamp(v_color.rgb + dynamic, 0.0, 1.0);
        vec3 raster_surface = out_color.rgb;
        if (pc.rt_params.z > 0.001) {
            const vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);
            float surface_luma = dot(raster_surface, luma_weights);
            vec3 metallic_surface = mix(raster_surface, vec3(surface_luma), 0.55);
            vec3 environment_tint = vec3(0.10, 0.16, 0.25) * reflection_key +
                                    vec3(0.18, 0.12, 0.08) * reflection_fill;
            vec3 tint = clamp(metallic_surface * 0.78 + environment_tint +
                               max(dynamic, vec3(0.0)), vec3(0.0), vec3(1.0));
            vec3 highlight = tint * (pc.rt_params.z * debug_response * 0.75);
            float highlight_luma = dot(highlight, luma_weights);
            float cap = max(surface_luma * 0.20, 0.035);
            highlight *= min(1.0, cap / max(highlight_luma, 0.000001));
            out_color.rgb += highlight;
            float bloom_weight = smoothstep(0.01, 0.04, highlight_luma);
            out_bloom = vec4(highlight * bloom_weight * 0.35, out_color.a);
            if (rt_debug == 7) {
                out_color = vec4(0.08 + clamp(highlight * 6.0, 0.0, 0.92), 1.0);
                out_bloom = vec4(0.0);
                return;
            }
        }
        if (rt_debug == 7) {
            out_color = vec4(0.08, 0.08, 0.08, 1.0);
            return;
        }
        // Scene alpha carries the material reflection allowlist value;
        // the secondary MRT alpha carries roughness for SSR.
        out_color.a = pc.rt_params.z;
        out_bloom.a = v_color.a;
    }

    if (pc.fog.a < 0.0) {
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
    } else if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        float fog = 1.0 - exp(-(d * d));
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, fog);
    }
}
