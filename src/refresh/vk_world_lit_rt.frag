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

float trace_hit_distance(vec3 origin, vec3 direction, float distance)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xff, origin, 0.05, direction, max(distance - 0.1, 0.05));
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
                               float contribution, int light_index)
{
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
    return 0.5 * (dynamic_shadow_visibility(first_hit, first_distance) +
                  dynamic_shadow_visibility(second_hit, second_distance));
}

vec2 material_params(float packed)
{
    float bits = floor(clamp(packed, 0.0, 1.0) * 255.0 + 0.5);
    return vec2(floor(bits / 16.0), mod(bits, 16.0)) / 15.0;
}

vec4 rt_controls(float packed)
{
    uint bits = uint(clamp(floor(packed + 0.5), 0.0, 16777215.0));
    return vec4(float(bits & 255u) / 255.0,
                float((bits >> 8) & 255u) / 255.0,
                float((bits >> 16) & 63u) / 63.0,
                float((bits >> 22) & 3u));
}

vec3 underwater_caustics(vec3 position, vec3 normal, vec4 controls)
{
    float strength = controls.z;
    int kind = int(controls.w + 0.5);
    if (strength <= 0.001 || kind < 1 || kind > 3)
        return vec3(0.0);

    vec3 axis = abs(normal);
    vec2 coord = axis.z >= axis.x && axis.z >= axis.y ? position.xy :
        (axis.x >= axis.y ? position.yz : position.xz);
    float scale = kind == 2 ? 0.038 : (kind == 3 ? 0.052 : 0.045);
    float speed = kind == 2 ? 0.65 : (kind == 3 ? 1.40 : 1.0);
    vec2 p = coord * scale;
    float time = pc.dlight.w * speed;
    float wave0 = sin(dot(p, vec2(1.72, 1.13)) + time);
    float wave1 = sin(dot(p, vec2(-1.31, 1.91)) - time * 1.27);
    float wave2 = sin(dot(p, vec2(0.73, -2.08)) + time * 0.71);
    float ridge = 1.0 - abs((wave0 + wave1 + wave2) / 3.0);
    float pattern = smoothstep(kind == 3 ? 0.38 : 0.52,
                               kind == 3 ? 0.92 : 0.88, ridge);
    float orientation = 0.40 + 0.60 * axis.z;
    float distance_fade = 1.0 - smoothstep(512.0, 1152.0,
        distance(position, pc.dlight.xyz));
    vec3 tint = kind == 2 ? vec3(0.18, 0.75, 0.22) :
        (kind == 3 ? vec3(1.0, 0.28, 0.035) :
                     vec3(0.18, 0.55, 0.85));
    float energy = kind == 3 ? 0.22 : (kind == 2 ? 0.15 : 0.18);
    return tint * pattern * orientation * distance_fade * energy * strength;
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
                          float reflectivity, float roughness, float mask,
                          float specular_control)
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
    float strength = 3.0 * specular_control * mask *
        mix(0.35, 1.0, reflectivity) * mix(0.30, 1.0, gloss);
    gradient *= strength * valid;
    float slope = length(gradient);
    gradient *= min(1.0, 0.45 / max(slope, 0.000001));
    return normalize(normal - gradient);
}

vec3 liquid_ripple_normal(vec3 position, vec3 normal, vec2 uv, float time,
                          float strength)
{
    vec3 dpdx = dFdx(position);
    vec3 dpdy = dFdy(position);
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    float valid = step(0.000001, abs(determinant));
    float safe_determinant = determinant < 0.0 ?
        min(determinant, -0.000001) : max(determinant, 0.000001);
    vec3 tangent = (dpdx * duvdy.y - dpdy * duvdx.y) / safe_determinant;
    vec3 bitangent = (dpdy * duvdx.x - dpdx * duvdy.x) / safe_determinant;
    tangent = normalize(mix(cross(abs(normal.z) < 0.95 ?
        vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0), normal),
        tangent, valid));
    bitangent = normalize(mix(cross(normal, tangent), bitangent, valid));

    float phase_a = uv.x * 7.5 + uv.y * 3.0 + time * 1.35;
    float phase_b = uv.x * -4.0 + uv.y * 10.5 - time * 0.95;
    vec2 gradient = vec2(cos(phase_a) * 7.5 - cos(phase_b) * 2.2,
                         cos(phase_a) * 3.0 + cos(phase_b) * 5.775);
    gradient *= 0.032 * strength;
    return normalize(normal - tangent * gradient.x -
                     bitangent * gradient.y);
}

vec3 dynamic_light(vec3 shading_normal, vec3 geometric_normal,
                   float reflectivity, float roughness,
                   float specular_control, float bounce_control,
                   out vec3 specular, out vec3 bounce)
{
    vec3 light = vec3(0.0);
    specular = vec3(0.0);
    bounce = vec3(0.0);
    vec3 view_dir = normalize(pc.dlight.xyz - v_position);
    float gloss = 1.0 - roughness;
    float exponent = mix(10.0, 96.0, pow(gloss, 0.9));
    float lobe_normalization = mix(0.80, 1.85, gloss);
    float specular_scale = reflectivity * specular_control *
        mix(1.30, 0.68, roughness) * lobe_normalization;
    for (int i = 0; i < 3; i++) {
        float range = pc.dlight_origins[i].w;
        if (range <= 0.0)
            break;
        vec3 delta = pc.dlight_origins[i].xyz - v_position;
        float distance_to_light = length(delta);
        float falloff = max(1.0 - distance_to_light / range, 0.0);
        vec3 light_dir = delta / max(distance_to_light, 0.001);
        if (falloff > 0.0) {
            float energy = pc.dlight_colors[i].w * falloff / 255.0;
            float contribution = max(pc.dlight_colors[i].r,
                max(pc.dlight_colors[i].g, pc.dlight_colors[i].b)) * energy;
            if (contribution < 0.01)
                continue;
            vec3 oriented_normal = dot(geometric_normal, delta) >= 0.0 ?
                geometric_normal : -geometric_normal;
            float bias = dynamic_ray_bias(v_position);
            vec3 ray_origin = v_position + oriented_normal * bias +
                              light_dir * 0.05;
            vec3 ray_delta = pc.dlight_origins[i].xyz - ray_origin;
            float visibility = dynamic_light_visibility(
                ray_origin, ray_delta, range, contribution, i);
            vec3 radiance = pc.dlight_colors[i].rgb * energy * visibility;
            light += radiance;
            if (bounce_control > 0.001) {
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
                float wrap = 0.35 + 0.65 *
                    (1.0 - facing) * (1.0 - facing);
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
    }
    bounce *= 0.65 * bounce_control;
    return light;
}

void main()
{
    out_bloom = vec4(0.0);
    float mode = mod(v_mode, 4.0);
    vec2 uv = v_uv;
    float liquid_code = max(-pc.rt_params.z, 0.0);
    int liquid_kind = int(floor(liquid_code + 0.0001));
    float previous_specular = clamp(fract(liquid_code) / 0.99, 0.0, 1.0);
    bool liquid = v_mode >= 4.0 && liquid_kind >= 1 && liquid_kind <= 4;
    float liquid_strength = liquid ? clamp(pc.rt_params.w, 0.0, 1.0) : 0.0;
    if (v_mode >= 4.0) {
        uv += vec2(0.0625) * sin(uv.ts * vec2(4.0) + vec2(pc.dlight.a));
        if (liquid_strength > 0.0) {
            vec2 detail_wave = vec2(
                sin(v_uv.y * 9.0 + v_uv.x * 2.5 + pc.dlight.a * 1.15),
                cos(v_uv.x * 8.0 - v_uv.y * 3.5 - pc.dlight.a * 0.90));
            uv += detail_wave * (0.012 * liquid_strength);
        }
    }

    vec3 normal = normalize(cross(dFdx(v_position), dFdy(v_position)));
    if (dot(normal, pc.dlight.xyz - v_position) < 0.0)
        normal = -normal;
    int rt_debug = pc.rt_params.x < 0.0 ?
        int(clamp(floor(-pc.rt_params.x + 0.5), 1.0, 3.0)) :
        (pc.rt_params.y < -7.5 ?
            int(clamp(floor(-pc.rt_params.y + 0.5), 8.0, 10.0)) : 0);
    vec2 material = material_params(pc.rt_params.z);
    float material_reflect = material.x;
    float material_roughness = material.y;
    vec4 material_texel = texture(tex_sampler, uv);
    vec3 bloom = vec3(0.0);
    float bump_mask = 1.0 - step(1.5, mode);
    vec4 controls = liquid ? vec4(previous_specular, 0.0, 0.0, 0.0) :
                             rt_controls(pc.rt_params.w);
    float specular_control = liquid ? liquid_strength : controls.x;
    float bounce_control = controls.y;
    vec3 shading_normal;
    if (liquid && liquid_strength > 0.0) {
        material_reflect = 0.72 * liquid_strength;
        material_roughness = mix(0.55, 0.24, liquid_strength);
        vec3 previous_normal = material_bump_normal(v_position, normal,
            material_texel.rgb, 0.0, 0.0, bump_mask, previous_specular);
        vec3 ripple_normal = liquid_ripple_normal(v_position, normal, v_uv,
            pc.dlight.a, liquid_strength);
        shading_normal = normalize(mix(previous_normal, ripple_normal,
                                       smoothstep(0.0, 0.35,
                                                  liquid_strength)));
    } else {
        float bump_control = liquid ? previous_specular : specular_control;
        shading_normal = material_bump_normal(v_position, normal,
            material_texel.rgb, material_reflect, material_roughness,
            bump_mask, bump_control);
    }
    if (rt_debug >= 1 && rt_debug <= 3) {
        out_color = rt_debug == 1 ? vec4(1.0) : vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    if (mode > 1.5) {
        out_color = v_color;
    } else {
        out_color = material_texel;
        vec3 material_albedo = out_color.rgb;
        out_color.a *= v_color.a;
        if (pc.desaturation > 0.0) {
            float luma = dot(out_color.rgb, vec3(0.2126, 0.7152, 0.0722));
            out_color.rgb = mix(out_color.rgb, vec3(luma), pc.desaturation);
        }
        out_color.rgb *= pc.intensity;
        vec3 dynamic_specular;
        vec3 dynamic_bounce;
        vec3 dynamic = dynamic_light(shading_normal, normal, material_reflect,
                                     material_roughness, specular_control,
                                     bounce_control, dynamic_specular,
                                     dynamic_bounce);
        if (rt_debug == 8) {
            out_color = vec4(clamp(dynamic_specular *
                vec3(0.0, 8.0, 8.0), 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        out_color.rgb *= clamp(v_color.rgb + dynamic, 0.0, 1.0);
        vec3 raster_surface = out_color.rgb;
        const vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);
        vec3 caustics = underwater_caustics(v_position, normal, controls);
        float caustics_luma = dot(caustics, luma_weights);
        float caustics_cap = min(max(dot(raster_surface, luma_weights) * 0.22,
                                     0.015), 0.08);
        caustics *= min(1.0, caustics_cap /
                        max(caustics_luma, 0.000001));
        vec3 caustics_add = caustics * max(
            vec3(1.0) - clamp(raster_surface, 0.0, 1.0), vec3(0.0));
        float caustics_add_luma = dot(caustics_add, luma_weights);
        if (rt_debug == 10) {
            out_color = vec4(clamp(caustics_add * 8.0, 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        out_color.rgb += caustics_add;
        raster_surface = out_color.rgb;
        float caustics_bloom_weight = smoothstep(0.018, 0.065,
                                                  caustics_add_luma);
        vec3 caustics_bloom = caustics_add * caustics_bloom_weight * 0.12;
        float caustics_bloom_luma = dot(caustics_bloom, luma_weights);
        caustics_bloom *= min(1.0, 0.012 /
                              max(caustics_bloom_luma, 0.000001));
        out_bloom.rgb += caustics_bloom;
        float bounce_luma = dot(dynamic_bounce, luma_weights);
        float bounce_cap = min(max(dot(raster_surface, luma_weights) * 0.30,
                                   0.02), 0.10);
        dynamic_bounce *= min(1.0, bounce_cap /
                              max(bounce_luma, 0.000001));
        vec3 bounce_add = dynamic_bounce * max(
            vec3(1.0) - clamp(raster_surface, 0.0, 1.0), vec3(0.0));
        float bounce_add_luma = dot(bounce_add, luma_weights);
        if (rt_debug == 9) {
            out_color = vec4(clamp(bounce_add * 8.0, 0.0, 1.0), 1.0);
            out_bloom = vec4(0.0);
            return;
        }
        out_color.rgb += bounce_add;
        float bounce_bloom_weight = smoothstep(0.018, 0.065,
                                                bounce_add_luma);
        vec3 bounce_bloom = bounce_add * bounce_bloom_weight * 0.25;
        float bounce_bloom_luma = dot(bounce_bloom, luma_weights);
        bounce_bloom *= min(1.0, 0.025 /
                            max(bounce_bloom_luma, 0.000001));
        out_bloom.rgb += bounce_bloom;
        if (liquid && liquid_strength > 0.0) {
            vec3 view_dir = normalize(pc.dlight.xyz - v_position);
            float edge = 1.0 - max(dot(shading_normal, view_dir), 0.0);
            float edge2 = edge * edge;
            float fresnel = edge2 * edge2 * edge;
            vec3 liquid_tint = liquid_kind == 2 ? vec3(0.18, 0.62, 0.24) :
                (liquid_kind >= 3 ? vec3(1.0, 0.30, 0.055) :
                                    vec3(0.20, 0.48, 0.72));
            vec3 sheen = liquid_tint *
                (0.018 + 0.18 * fresnel) * liquid_strength;
            vec3 highlight = dynamic_specular *
                mix(0.85, 1.35, liquid_strength);
            float highlight_luma = dot(highlight, luma_weights);
            float highlight_cap = mix(0.055, 0.13, liquid_strength);
            highlight *= min(1.0, highlight_cap /
                             max(highlight_luma, 0.000001));
            out_color.rgb += (sheen + highlight) *
                max(vec3(1.0) - out_color.rgb, vec3(0.0));

            float visible_highlight = dot(sheen + highlight, luma_weights);
            float bloom_weight = smoothstep(0.025, 0.10, visible_highlight);
            bloom += (sheen + highlight) * bloom_weight * 0.28;
            if (liquid_kind == 3) {
                float lava_luma = dot(material_texel.rgb, luma_weights);
                float lava_mask = smoothstep(0.16, 0.68,
                    max(max(material_texel.r, material_texel.g),
                        material_texel.b));
                vec3 lava_emission = material_texel.rgb * lava_mask *
                    (0.075 * liquid_strength) *
                    mix(0.65, 1.0, lava_luma);
                bloom += lava_emission;
                out_color.rgb += lava_emission * 0.32 *
                    max(vec3(1.0) - out_color.rgb, vec3(0.0));
            }
            out_bloom += vec4(bloom, 0.0);
        } else if (material_reflect > 0.001 && specular_control > 0.001) {
            float surface_luma = dot(raster_surface, luma_weights);
            vec3 highlight = dynamic_specular *
                material_detail_factor(material_albedo, material_roughness);
            highlight *= material_specular_tint(material_albedo,
                                                material_reflect,
                                                material_roughness);
            float highlight_luma = dot(highlight, luma_weights);
            float cap = min(max(surface_luma * 0.22, 0.035), 0.14);
            highlight *= min(1.0, cap / max(highlight_luma, 0.000001));
            highlight_luma = dot(highlight, luma_weights);
            out_color.rgb += highlight;
            float bloom_weight = smoothstep(0.01, 0.04, highlight_luma);
            out_bloom.rgb += highlight * bloom_weight * 0.35;
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
