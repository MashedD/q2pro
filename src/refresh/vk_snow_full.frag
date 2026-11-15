#version 450

// Snow cover (vk_snow.c) once it has fully grown: nothing is left out, so
// the depth test can run before this shader.

layout(early_fragment_tests) in;

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_bloom;

void main()
{
    out_color = v_color;
    out_bloom = out_color;
}
