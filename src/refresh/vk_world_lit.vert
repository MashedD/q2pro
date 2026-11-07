#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 scroll;
    vec4 dlight;
    vec4 dlight_origins[3];
    vec4 dlight_colors[3];
    vec4 fog;
    float intensity;
    float desaturation;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) flat out float v_mode;
layout(location = 3) out vec3 v_position;

void main()
{
    float mode = mod(pc.scroll.z, 4.0);
    vec3 lit = clamp((in_color.rgb + vec3(pc.scroll.w)) * pc.color.rgb, 0.0, 1.0);
    vec3 color = mode > 0.5 && mode < 1.5 ? vec3(1.0) : lit;

    gl_Position = pc.mvp * vec4(in_position, 1.0);
    v_color = vec4(color, in_color.a * pc.color.a);
    v_uv = in_uv + pc.scroll.xy;
    v_mode = pc.scroll.z;
    v_position = in_position;
}
