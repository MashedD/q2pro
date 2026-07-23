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

vec2 material_params(float packed)
{
    float bits = floor(clamp(packed, 0.0, 1.0) * 255.0 + 0.5);
    return vec2(floor(bits / 16.0), mod(bits, 16.0)) / 15.0;
}

vec3 dynamic_light(vec3 normal, float reflectivity, float roughness,
                   out vec3 specular)
{
    vec3 light = vec3(0.0);
    specular = vec3(0.0);
    vec3 view_dir = normalize(pc.dlight.xyz - v_position);
    float exponent = mix(96.0, 8.0, roughness);
    float specular_scale = reflectivity * pc.rt_params.w *
        mix(0.72, 0.22, roughness);
    for (int i = 0; i < 3; i++) {
        float range = pc.dlight_origins[i].w;
        if (range <= 0.0)
            break;
        vec3 delta = pc.dlight_origins[i].xyz - v_position;
        float distance_to_light = length(delta);
        float falloff = max(1.0 - distance_to_light / range, 0.0);
        vec3 light_dir = delta / max(distance_to_light, 0.001);
        if (falloff > 0.0 && !occluded(v_position + normal * 0.05,
                                      light_dir, distance_to_light)) {
            float energy = pc.dlight_colors[i].w * falloff / 255.0;
            vec3 radiance = pc.dlight_colors[i].rgb * energy;
            light += radiance;
            if (specular_scale > 0.001) {
                vec3 half_dir = normalize(light_dir + view_dir);
                float ndoth = max(dot(normal, half_dir), 0.0);
                float ndotl = max(dot(normal, light_dir), 0.0);
                float grazing = 0.12 + 0.88 * pow(1.0 -
                    max(dot(normal, view_dir), 0.0), 3.0);
                specular += radiance * pow(ndoth, exponent) *
                    smoothstep(0.0, 0.20, ndotl) * specular_scale *
                    (0.65 + 0.35 * grazing);
            }
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
    int rt_debug = pc.rt_params.x < 0.0 ?
        int(clamp(floor(-pc.rt_params.x + 0.5), 1.0, 3.0)) : 0;
    vec2 material = material_params(pc.rt_params.z);
    float material_reflect = material.x;
    float material_roughness = material.y;
    if (rt_debug != 0) {
        out_color = rt_debug == 1 ? vec4(1.0) : vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    if (mode > 1.5) {
        out_color = v_color;
    } else {
        out_color = texture(tex_sampler, uv);
        out_color.a *= v_color.a;
        if (pc.desaturation > 0.0) {
            float luma = dot(out_color.rgb, vec3(0.2126, 0.7152, 0.0722));
            out_color.rgb = mix(out_color.rgb, vec3(luma), pc.desaturation);
        }
        out_color.rgb *= pc.intensity;
        vec3 dynamic_specular;
        vec3 dynamic = dynamic_light(normal, material_reflect,
                                     material_roughness, dynamic_specular);
        out_color.rgb *= clamp(v_color.rgb + dynamic, 0.0, 1.0);
        vec3 raster_surface = out_color.rgb;
        if (material_reflect > 0.001 && pc.rt_params.w > 0.001) {
            const vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);
            float surface_luma = dot(raster_surface, luma_weights);
            vec3 highlight = dynamic_specular;
            float highlight_luma = dot(highlight, luma_weights);
            float cap = min(max(surface_luma * 0.16, 0.025), 0.10);
            highlight *= min(1.0, cap / max(highlight_luma, 0.000001));
            highlight_luma = dot(highlight, luma_weights);
            out_color.rgb += highlight;
            float bloom_weight = smoothstep(0.01, 0.04, highlight_luma);
            out_bloom = vec4(highlight * bloom_weight * 0.35, out_color.a);
        }
        // Preserve scene alpha for normal translucency. Material eligibility
        // is carried separately in the secondary MRT alpha.
        out_bloom.a = v_mode < 4.0 ? pc.rt_params.z : 0.0;
    }

    if (pc.fog.a < 0.0) {
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
    } else if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        float fog = 1.0 - exp(-(d * d));
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, fog);
    }
}
