#version 450

layout(push_constant) uniform Push {
    mat4 mvp; vec4 color; vec4 shadedir; float backlerp; float shellscale;
    float depthscale; float _pad; vec4 fog; float intensity; float desaturation;
    vec2 _rt_align;
    vec4 rt_light_origin; vec4 rt_light_color; vec4 rt_entity_origin;
    vec4 rt_local_light; vec4 rt_local_view;
    vec4 rt_control;
} pc;
layout(set=0,binding=0) uniform sampler2D tex_sampler;
layout(location=0) in vec4 v_color;
layout(location=1) in vec2 v_uv;
layout(location=2) flat in float v_mode;
layout(location=3) flat in vec4 v_rt_light_origin;
layout(location=4) flat in vec4 v_rt_light_color;
layout(location=5) flat in vec4 v_rt_entity_origin;
layout(location=6) in vec3 v_rt_normal;
layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_bloom;

bool finite_float(float value)
{
    return !isnan(value) && !isinf(value);
}

bool finite_vec3(vec3 value)
{
    return !any(isnan(value)) && !any(isinf(value));
}

bool safe_normalize(vec3 value, out vec3 normalized)
{
    float length_squared = dot(value, value);
    if (!finite_float(length_squared) || length_squared <= 0.000001) {
        normalized = vec3(0.0);
        return false;
    }

    normalized = value * inversesqrt(length_squared);
    if (!finite_vec3(normalized)) {
        normalized = vec3(0.0);
        return false;
    }
    return true;
}

float entity_specular_lobe(vec3 normal, vec3 light_dir, vec3 view_dir)
{
    // Alias models are rendered without face culling and legacy model normals
    // are not consistently oriented, so their highlight must be two-sided.
    float ndotl = abs(dot(normal, light_dir));
    vec3 half_vector = light_dir + view_dir;
    vec3 half_dir;
    if (!finite_float(ndotl) || ndotl <= 0.0 ||
        !safe_normalize(half_vector, half_dir))
        return 0.0;

    float ndoth = abs(dot(normal, half_dir));
    if (!finite_float(ndoth))
        return 0.0;
    float primary = pow(ndoth, 4.0);
    float clearcoat = pow(ndoth, 24.0) * 0.30;
    float lobe = (primary + clearcoat) * smoothstep(0.0, 0.20, ndotl);
    return finite_float(lobe) ? max(lobe, 0.0) : 0.0;
}

void main() {
    vec4 texel = texture(tex_sampler, v_uv);
#ifdef RT_ALPHA_TEST
    if (texel.a <= 0.666) discard;
#endif

    if (pc.desaturation > 0.0) {
        float luma = dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722));
        texel.rgb = mix(texel.rgb, vec3(luma), pc.desaturation);
    }
    if (pc.intensity < 0.0) {
        texel.rgb *= (texel.r + texel.g + texel.b) / 3.0;
        texel.rgb *= v_color.a;
    } else {
        texel.rgb *= pc.intensity;
    }
    texel *= v_color;
    vec3 rgb = texel.rgb;

    float specular_strength = finite_float(pc.rt_local_view.w) ?
        clamp(abs(pc.rt_local_view.w), 0.0, 1.0) : 0.0;
    float tint_peak = max(max(pc.color.r, pc.color.g), pc.color.b);
    vec3 model_tint = tint_peak > 0.001 && finite_vec3(pc.color.rgb) ?
        pc.color.rgb / tint_peak : vec3(1.0);
    vec3 effect_tint = mix(vec3(1.0), model_tint, 0.35);
    float albedo_luma = dot(texel.rgb,
        vec3(0.2126, 0.7152, 0.0722));
    float material_visibility = finite_float(albedo_luma) ?
        smoothstep(0.05, 0.60, albedo_luma) : 0.0;
    // A small material-driven floor keeps malformed legacy normals from
    // making the complete overlay visually empty.
    vec3 highlight = effect_tint * material_visibility * 0.10;
    vec3 bloom_seed = vec3(0.0);
    vec3 normal;
    vec3 view_dir;
    if (specular_strength > 0.001 &&
        safe_normalize(v_rt_normal, normal) &&
        safe_normalize(pc.rt_local_view.xyz, view_dir)) {
        float view_alignment = abs(dot(normal, view_dir));
        float facing = pow(view_alignment, 3.0);
        if (finite_float(facing)) {
            float broad_sheen = 0.35 + 0.65 * view_alignment * view_alignment;
            highlight += effect_tint * broad_sheen * 0.35;
            bloom_seed = effect_tint * facing * specular_strength * 0.35;
        }
        vec3 static_dir;
        if (finite_float(pc.shadedir.w) && pc.shadedir.w != 0.0 &&
            safe_normalize(pc.shadedir.xyz, static_dir)) {
            float static_lobe = entity_specular_lobe(normal, static_dir,
                                                      view_dir);
            vec3 key_tint = effect_tint;
            float key_facing = pow(abs(dot(normal, static_dir)), 3.0);
            bloom_seed = max(bloom_seed,
                key_tint * key_facing * specular_strength * 0.35);
            highlight += key_tint * static_lobe * 0.85;
        }
        vec3 dynamic_dir;
        if (safe_normalize(pc.rt_local_light.xyz, dynamic_dir) &&
            finite_vec3(v_rt_light_color.rgb) &&
            finite_float(v_rt_light_color.w)) {
            float dynamic_lobe = entity_specular_lobe(normal, dynamic_dir,
                                                       view_dir);
            highlight += v_rt_light_color.rgb * v_rt_light_color.w *
                         dynamic_lobe * 1.10;
        }

    }

    float detail = mix(0.75, 1.0,
        smoothstep(0.08, 0.65, max(albedo_luma, 0.0)));
    highlight *= detail * specular_strength;
    vec3 headroom = max(vec3(1.0) - clamp(rgb, 0.0, 1.0), vec3(0.35));
    highlight *= headroom;
    float highlight_luma = dot(highlight,
        vec3(0.2126, 0.7152, 0.0722));
    highlight *= min(1.0, 0.60 /
                     max(highlight_luma, 0.000001));

    if (!finite_vec3(highlight))
        highlight = vec3(0.0);
    else
        highlight = max(highlight, vec3(0.0));
    if (!finite_vec3(bloom_seed))
        bloom_seed = vec3(0.0);
    else
        bloom_seed = max(bloom_seed, vec3(0.0));

    if (pc.rt_local_light.w > 0.5) {
        out_color = vec4(clamp(highlight * 8.0, 0.0, 1.0), 0.0);
        out_bloom = vec4(0.0);
        return;
    }

    float fog_visibility = 1.0;
    if (pc.fog.a < 0.0) {
        fog_visibility = clamp(1.0 + pc.fog.a, 0.0, 1.0);
    } else if (pc.fog.a > 0.0) {
        float fog_distance = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        fog_visibility = exp(-(fog_distance * fog_distance));
    }
    if (!finite_float(fog_visibility))
        fog_visibility = 0.0;
    highlight *= fog_visibility;
    out_color = vec4(highlight, 0.0);
    vec3 lit_material = clamp(rgb, 0.0, 1.0);
    float skin_luma = dot(lit_material,
        vec3(0.2126, 0.7152, 0.0722));
    float skin_glint = smoothstep(0.10, 0.65, skin_luma) *
        specular_strength;
    vec3 material_bloom = lit_material * skin_glint * 0.35;
    vec3 coverage_bloom = mix(vec3(1.0), model_tint, 0.45) *
        specular_strength * 0.25;
    vec3 bloom = max(max(max(highlight * 8.0, bloom_seed),
                        material_bloom), coverage_bloom);
    if (!finite_vec3(bloom))
        bloom = vec3(0.0);
    else
        bloom = max(bloom, vec3(0.0));
    bloom *= fog_visibility;
    float bloom_luma = dot(bloom, vec3(0.2126, 0.7152, 0.0722));
    bloom *= min(1.0, 0.45 / max(bloom_luma, 0.000001));
    out_bloom = vec4(bloom, 0.0);
}
