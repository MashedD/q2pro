#version 450

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 color;
    vec2 screen;
    vec4 uv;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec2 tc = v_uv + vec2(0.0625) * sin(v_uv.ts * 4.0 + pc.color.x);
    out_color = texture(tex_sampler, tc);
}
