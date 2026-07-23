#version 450

layout(set = 0, binding = 0) uniform sampler2D scene_sampler;
layout(set = 0, binding = 1) uniform sampler2D depth_sampler;
layout(set = 0, binding = 2) uniform sampler2D material_sampler;

layout(push_constant) uniform Push {
    // Projection matrix entries P00, P11, P22 and P32.
    vec4 projection;
    // Strength, debug mode, inverse scene width and inverse scene height.
    vec4 control;
    // World-up direction transformed into view space.
    vec4 view_up;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

vec3 view_position(vec2 uv, float depth)
{
    float z = -pc.projection.w / min(depth + pc.projection.z, -0.00001);
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(-ndc.x * z / pc.projection.x,
                -ndc.y * z / pc.projection.y, z);
}

vec2 project_uv(vec3 p)
{
    vec2 ndc = vec2(pc.projection.x * p.x, pc.projection.y * p.y) / -p.z;
    return ndc * 0.5 + 0.5;
}

void main()
{
    float depth = texture(depth_sampler, v_uv).r;
    if (depth >= 0.9999 || depth <= 0.00001) {
        out_color = vec4(0.0);
        return;
    }
    float material_reflect = texture(scene_sampler, v_uv).a;
    if (material_reflect <= 0.001) {
        out_color = vec4(0.0);
        return;
    }
    float material_roughness = clamp(texture(material_sampler, v_uv).a, 0.0, 1.0);

    vec3 p = view_position(v_uv, depth);
    vec2 texel = pc.control.zw * 2.0;
    float dx = texture(depth_sampler, v_uv + vec2(texel.x, 0.0)).r;
    float dy = texture(depth_sampler, v_uv + vec2(0.0, texel.y)).r;
    vec3 px = view_position(v_uv + vec2(texel.x, 0.0), dx);
    vec3 py = view_position(v_uv + vec2(0.0, texel.y), dy);
    // Depth discontinuities produce unstable normals and false floor hits.
    // Keep the finite difference local to a continuous surface.
    if (abs(px.z - p.z) > max(10.0, abs(p.z) * 0.12) ||
        abs(py.z - p.z) > max(10.0, abs(p.z) * 0.12)) {
        out_color = vec4(0.0);
        return;
    }
    vec3 n = normalize(cross(px - p, py - p));
    vec3 view_dir = normalize(-p);
    if (dot(n, view_dir) < 0.0)
        n = -n;

    float upness = dot(n, normalize(pc.view_up.xyz));
    float floor_mask = smoothstep(0.62, 0.88, upness);
    float surface_mask = floor_mask;
    if (surface_mask < 0.01) {
        out_color = vec4(0.0);
        return;
    }
    float fresnel = 0.18 + 0.82 * pow(1.0 - max(dot(n, view_dir), 0.0), 3.0);

    vec3 ray_origin = p + n * max(0.75, -p.z * 0.0025);
    vec3 ray_dir = normalize(reflect(-view_dir, n));
    float max_distance = min(max(-p.z * 0.5, 64.0), 512.0);
    vec3 hit_color = vec3(0.0);
    float hit = 0.0;
    float hit_fade = 0.0;
    vec2 hit_uv = v_uv;
    float previous_delta = 0.0;
    float previous_t = 0.0;

    for (int i = 1; i <= 8; ++i) {
        float t = max_distance * (float(i) / 8.0);
        vec3 sample_pos = ray_origin + ray_dir * t;
        if (sample_pos.z > -1.0)
            break;
        vec2 uv = project_uv(sample_pos);
        if (any(lessThan(uv, vec2(0.01))) || any(greaterThan(uv, vec2(0.99))))
            break;
        float sample_depth = texture(depth_sampler, uv).r;
        if (sample_depth >= 0.9999)
            continue;
        vec3 depth_pos = view_position(uv, sample_depth);
        float delta = sample_pos.z - depth_pos.z;
        float thickness = 2.0 + t * 0.018;
        if (delta <= 0.0 && delta > -thickness &&
            (i == 1 || previous_delta > 0.0)) {
            // Interpolate between the previous and current samples. This
            // removes the large eight-step bands from the half-resolution pass.
            float denom = previous_delta - delta;
            float refine = denom > 0.001 ? clamp(previous_delta / denom, 0.0, 1.0) : 0.0;
            float refined_t = mix(previous_t, t, refine);
            vec3 refined_pos = ray_origin + ray_dir * refined_t;
            vec2 refined_uv = project_uv(refined_pos);
            if (any(lessThan(refined_uv, vec2(0.02))) ||
                any(greaterThan(refined_uv, vec2(0.98))))
                break;
            float refined_depth = texture(depth_sampler, refined_uv).r;
            if (refined_depth >= 0.9999)
                break;
            hit_uv = refined_uv;
            hit_color = texture(scene_sampler, refined_uv).rgb;
            hit = 1.0;
            hit_fade = (1.0 - float(i - 1) / 8.0) *
                       smoothstep(0.0, 0.35, surface_mask);
            break;
        }
        previous_delta = delta;
        previous_t = t;
    }

    float edge = smoothstep(0.01, 0.12, hit_uv.x) *
                 smoothstep(0.01, 0.12, hit_uv.y) *
                 smoothstep(0.01, 0.12, 1.0 - hit_uv.x) *
                 smoothstep(0.01, 0.12, 1.0 - hit_uv.y);
    float roughness = mix(0.72, 0.38, fresnel) *
                      mix(1.0, 0.35, material_roughness);
    float amount = hit * surface_mask * fresnel * hit_fade * edge *
                   roughness * material_reflect * pc.control.x;
    float luminance = dot(hit_color, vec3(0.2126, 0.7152, 0.0722));
    hit_color *= min(1.0, 1.15 / max(luminance, 0.001));

    int debug_mode = int(pc.control.y + 0.5);
    if (debug_mode == 4)
        out_color = vec4(vec3(pc.control.x), 1.0);
    else if (debug_mode == 5)
        out_color = vec4(mix(vec3(0.12, 0.03, 0.01), vec3(0.05, 0.9, 0.15), floor_mask), 1.0);
    else if (debug_mode == 6)
        out_color = vec4(hit > 0.5 ? vec3(0.1, 0.35, 1.0) : vec3(0.35, 0.12, 0.02), 1.0);
    else if (debug_mode == 7)
        out_color = vec4(hit_color * amount, 1.0);
    else
        out_color = vec4(hit_color * amount, amount);
}
