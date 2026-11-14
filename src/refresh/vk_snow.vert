#version 450

// Snow cover (vk_snow.c). Each point grows on its own schedule: in_growth
// holds its full height, the snow amount at which it starts to grow and how
// long it takes. Points start two units under the floor and rise through it,
// so snow never lies flat on the floor where it would fight it for depth.

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;     // rgb tint, a = snow amount 0..1
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec3 in_growth;
layout(location = 0) out vec4 v_color;

void main()
{
    float g = clamp((pc.color.a - in_growth.y) / max(in_growth.z, 0.001), 0.0, 1.0);
    g = g * g * (3.0 - 2.0 * g);
    vec3 position = in_position;
    position.z += (in_growth.x + 2.0) * g - 2.0;
    gl_Position = pc.mvp * vec4(position, 1.0);
    v_color = vec4(in_color.rgb * pc.color.rgb, 1.0);
}
