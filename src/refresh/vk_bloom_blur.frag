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

// The same Gaussian as the OpenGL renderer's bloom blur: weights
// exp(-i^2 / sigma^2) over a radius of 2 * sigma texels, sampling every texel.
// A fixed kernel with stretched offsets skipped texels and left a grid of
// the source pattern in the glow.
void main()
{
    vec2 step_uv = pc.color.xy;     // one texel along the blur direction
    float sigma = max(pc.color.z, 0.0);
    int radius = min(int(sigma * 2.0 + 0.5), 50);
    vec3 color = vec3(0.0);
    float sum = 0.0;

    for (int i = -radius; i <= radius; i++) {
        float w = radius > 0 ? exp(-float(i * i) / (sigma * sigma)) : 1.0;
        color += texture(tex_sampler, v_uv + step_uv * float(i)).rgb * w;
        sum += w;
    }

    out_color = vec4(color / sum, 1.0) * v_color.wwww;
}
