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
    vec3 color = texture(tex_sampler, v_uv).rgb * 0.227027;
    color += texture(tex_sampler, v_uv + step_uv * 1.384615).rgb * 0.316216;
    color += texture(tex_sampler, v_uv - step_uv * 1.384615).rgb * 0.316216;
    color += texture(tex_sampler, v_uv + step_uv * 3.230769).rgb * 0.070270;
    color += texture(tex_sampler, v_uv - step_uv * 3.230769).rgb * 0.070270;
    out_color = vec4(color, 1.0) * v_color.wwww;
}
