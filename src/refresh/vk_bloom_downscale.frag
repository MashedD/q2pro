#version 450

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 color;
    vec2 screen;
    vec4 uv;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main()
{
    vec2 step_uv = pc.color.xy * 0.25;
    vec3 color = texture(tex_sampler, v_uv + vec2(-step_uv.x, -step_uv.y)).rgb;
    color += texture(tex_sampler, v_uv + vec2(-step_uv.x,  step_uv.y)).rgb;
    color += texture(tex_sampler, v_uv + vec2( step_uv.x, -step_uv.y)).rgb;
    color += texture(tex_sampler, v_uv + vec2( step_uv.x,  step_uv.y)).rgb;
    out_color = vec4(color * 0.25, 1.0);
}
