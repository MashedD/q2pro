#version 450
layout(set = 0, binding = 0) uniform sampler2D tex_sampler;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;
void main()
{
    vec4 encoded = texture(tex_sampler, v_uv);
    // Recover small contributions without losing them to UNORM quantization.
    out_color = vec4(encoded.rgb * encoded.a, 0.0) * v_color;
}
