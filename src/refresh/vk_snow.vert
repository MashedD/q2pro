#version 450

// Snow cover (vk_snow.c). Each point grows on its own schedule: in_growth
// holds its full height, the snow amount at which it starts to grow and how
// long it takes. Snow is drawn at least a thin layer above the floor, so it
// never lies level with it where it would fight it for depth; the fragment
// shader leaves out what hasn't grown yet. Points with no snow at full
// growth (the tapered edges of drifts) stay under the floor.
//
// Explosions blow snow away: in_crater holds when, and how strongly at this
// point. That sets the point back on its growth schedule, so the crater has
// the same ragged edge as growing snow and fills in again over
// pc.craters.y seconds.

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;     // rgb tint, a = snow amount 0..1
    vec4 craters;   // x = time, y = seconds for a crater to fill again
    vec4 fog;       // used by the fragment shaders
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec3 in_growth;
layout(location = 3) in vec2 in_crater;
layout(location = 0) out vec4 v_color;
layout(location = 1) out float v_front;
layout(location = 2) out vec2 v_xy;

void main()
{
    float front = (pc.color.a - in_growth.y) / max(in_growth.z, 0.001);
    float refill = clamp((pc.craters.x - in_crater.x) / pc.craters.y, 0.0, 1.0);
    front -= 3.0 * in_crater.y * (1.0 - refill);
    float g = clamp(front, 0.0, 1.0);
    g = g * g * (3.0 - 2.0 * g);
    float h = in_growth.x;
    vec3 position = in_position;
    if (h <= 0.0)
        position.z += (h + 2.0) * g - 2.0;
    else
        position.z += max(h * g, min(h, 0.75));
    gl_Position = pc.mvp * vec4(position, 1.0);
    v_color = vec4(in_color.rgb * pc.color.rgb, 1.0);
    v_front = front;
    v_xy = in_position.xy;
}
