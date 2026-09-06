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
    vec2 step_uv = pc.color.xy;
    // Keep a center tap so narrow highlights and one-pixel glow sources are
    // not lost between the four diagonal samples at quarter resolution.
    vec3 color = texture(tex_sampler, v_uv).rgb * 0.50;
    color += texture(tex_sampler,
                     v_uv + vec2(-step_uv.x, -step_uv.y)).rgb * 0.125;
    color += texture(tex_sampler,
                     v_uv + vec2(-step_uv.x,  step_uv.y)).rgb * 0.125;
    color += texture(tex_sampler,
                     v_uv + vec2( step_uv.x, -step_uv.y)).rgb * 0.125;
    color += texture(tex_sampler,
                     v_uv + vec2( step_uv.x,  step_uv.y)).rgb * 0.125;

    out_color = vec4(color, 1.0);
}
