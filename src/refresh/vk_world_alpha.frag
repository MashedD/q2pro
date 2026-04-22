#version 450

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in float v_mode;
layout(location = 0) out vec4 out_color;

void main()
{
    if (v_mode > 1.5) {
        out_color = v_color;
        return;
    }

    vec4 texel = texture(tex_sampler, v_uv);
    if (texel.a < 0.5) {
        discard;
    }
    out_color = texel * v_color;
}
