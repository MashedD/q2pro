#version 450

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 color;
    vec2 screen;
    vec4 uv;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main()
{
    vec2 step_uv = pc.color.xy;
    float sigma = max(pc.color.z, 0.1);
    int radius = clamp(int(sigma * 2.0 + 0.5), 1, 50);
    float weight_sum = 0.0;
    for (int i = -radius; i <= radius; ++i)
        weight_sum += exp(-float(i * i) / (sigma * sigma));

    vec3 color = texture(tex_sampler, v_uv).rgb / weight_sum;
    // Combine adjacent taps via bilinear filtering, as in OpenGL's Gaussian.
    for (int i = 1; i <= radius; i += 2) {
        float w0 = exp(-float(i * i) / (sigma * sigma));
        float w1 = i + 1 <= radius ?
            exp(-float((i + 1) * (i + 1)) / (sigma * sigma)) : 0.0;
        float offset = float(i) + w1 / (w0 + w1);
        vec2 delta = step_uv * offset;
        color += (texture(tex_sampler, v_uv + delta).rgb +
                  texture(tex_sampler, v_uv - delta).rgb) *
                 ((w0 + w1) / weight_sum);
    }
    out_color = vec4(color, 1.0);
}
