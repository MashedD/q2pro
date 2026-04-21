#version 450

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = texture(tex_sampler, v_uv) * v_color;
}
