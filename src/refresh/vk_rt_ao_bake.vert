#version 460

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_atlas_uv;
layout(location = 3) in uvec2 in_rt_data;

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;
layout(location = 2) flat out uvec2 v_rt_data;

void main()
{
    v_position = in_position;
    v_normal = in_normal;
    v_rt_data = in_rt_data;
    gl_Position = vec4(in_atlas_uv * 2.0 - 1.0, 0.0, 1.0);
}
