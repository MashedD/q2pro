#version 450

// Snow cover (vk_snow.c). Each point grows on its own schedule: in_growth
// holds its full height, the snow amount at which it starts to grow and how
// long it takes. Points wait two units under the floor, and once they start
// to grow they are at least a thin layer above it, so snow never lies level
// with the floor where it would fight it for depth.

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
    float h = in_growth.x;
    if (h <= 0.0)
        position.z += (h + 2.0) * g - 2.0;     // stays under the floor
    else if (g > 0.0)
        position.z += max(h * g, min(h, 0.75));
    else
        position.z -= 2.0;
    gl_Position = pc.mvp * vec4(position, 1.0);
    v_color = vec4(in_color.rgb * pc.color.rgb, 1.0);
}
