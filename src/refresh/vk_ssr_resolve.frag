#version 450

layout(set = 0, binding = 0) uniform sampler2D ssr_sampler;
layout(set = 0, binding = 1) uniform sampler2D depth_sampler;
layout(set = 0, binding = 2) uniform sampler2D material_sampler;

layout(push_constant) uniform Push {
    vec4 projection;
    vec4 control;
    vec4 view_up;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

vec2 material_params(float packed)
{
    float bits = floor(clamp(packed, 0.0, 1.0) * 255.0 + 0.5);
    return vec2(floor(bits / 16.0), mod(bits, 16.0)) / 15.0;
}

float view_z(float depth)
{
    return -pc.projection.w / min(depth + pc.projection.z, -0.00001);
}

void main()
{
    int debug_mode = int(pc.control.y + 0.5);
    if (debug_mode >= 4 && debug_mode <= 6) {
        out_color = texture(ssr_sampler, v_uv);
        return;
    }

    float depth = texture(depth_sampler, v_uv).r;
    vec2 material = material_params(texture(material_sampler, v_uv).a);
    if (depth >= 0.9999 || material.x <= 0.001) {
        out_color = vec4(0.0);
        return;
    }

    vec2 raw_size = vec2(textureSize(ssr_sampler, 0));
    vec2 raw_texel = 1.0 / max(raw_size, vec2(1.0));
    vec2 raw_pixel = v_uv * raw_size - 0.5;
    vec2 base_pixel = floor(raw_pixel);
    vec2 fraction = fract(raw_pixel);
    float current_z = view_z(depth);
    float footprint = mix(1.30, 0.78, 1.0 - material.y);

    vec3 color_sum = vec3(0.0);
    float alpha_sum = 0.0;
    float weight_sum = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            vec2 corner = vec2(x, y);
            vec2 sample_uv = (base_pixel + corner + 0.5) * raw_texel;
            sample_uv = mix(v_uv, sample_uv, footprint);
            sample_uv = clamp(sample_uv, vec2(0.001), vec2(0.999));

            vec4 reflection = texture(ssr_sampler, sample_uv);
            float sample_depth = texture(depth_sampler, sample_uv).r;
            vec2 sample_material = material_params(
                texture(material_sampler, sample_uv).a);
            if (sample_depth >= 0.9999 || sample_material.x <= 0.001)
                continue;

            float sample_z = view_z(sample_depth);
            float near_limit = max(1.5, abs(current_z) * 0.006);
            float far_limit = max(5.0, abs(current_z) * 0.025);
            float depth_weight = 1.0 - smoothstep(near_limit, far_limit,
                                                   abs(sample_z - current_z));
            float material_weight = 1.0 - smoothstep(0.08, 0.30,
                abs(sample_material.x - material.x));
            vec2 bilinear_axis = mix(vec2(1.0) - fraction, fraction, corner);
            float bilinear_weight = bilinear_axis.x * bilinear_axis.y;
            float weight = depth_weight * material_weight *
                max(bilinear_weight, 0.04);
            color_sum += reflection.rgb * weight;
            alpha_sum += reflection.a * weight;
            weight_sum += weight;
        }
    }

    if (weight_sum <= 0.0001) {
        out_color = vec4(0.0);
        return;
    }
    vec3 resolved_color = color_sum / weight_sum;
    float resolved_alpha = alpha_sum / weight_sum;
    if (debug_mode == 7)
        out_color = vec4(resolved_color * resolved_alpha, 1.0);
    else
        out_color = vec4(resolved_color, resolved_alpha);
}
