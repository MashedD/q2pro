#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 scroll;
    vec4 dlight;
    vec4 fog;
    float intensity;
    vec2 lm_scale;
    vec2 lm_offset;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec2 in_lmuv;
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) flat out float v_mode;
layout(location = 3) out vec2 v_lmuv;

void main()
{
    float mode = mod(pc.scroll.z, 4.0);
    vec3 lit = clamp((in_color.rgb + vec3(pc.scroll.w)) * pc.color.rgb + pc.dlight.rgb, 0.0, 1.0);
    vec3 color = mode > 0.5 && mode < 1.5 ? vec3(1.0) : lit;

    gl_Position = pc.mvp * vec4(in_position, 1.0);
    v_color = vec4(color, in_color.a * pc.color.a);
    v_uv = in_uv + pc.scroll.xy;
    v_mode = pc.scroll.z;
    v_lmuv = in_lmuv * pc.lm_scale + pc.lm_offset;
}
