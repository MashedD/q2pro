#version 450

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main()
{
    vec3 color = texture(tex_sampler, v_uv).rgb;
    float bright = max(max(color.r, color.g), color.b);
    float amount = smoothstep(0.55, 1.0, bright);
    out_color = vec4(color * amount, 1.0) * v_color;
}
