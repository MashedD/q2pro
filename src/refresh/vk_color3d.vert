#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 v_color;

void main()
{
    gl_Position = pc.mvp * vec4(in_position, 1.0);
    v_color = in_color * pc.color;
}
