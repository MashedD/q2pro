#version 450
layout(set = 0, binding = 0) uniform sampler2D source;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;
void main()
{
    vec4 value = texture(source, v_uv);
    if (v_color.x < 1.5)
        out_color = vec4(clamp(vec3(0.5 + value.xy * 16.0, 0.5), 0.0, 1.0), 1.0);
    else if (v_color.x > 3.5)
        out_color = vec4(vec3(pow(clamp(value.r, 0.0, 1.0), 32.0)), 1.0);
    else
        out_color = vec4(vec3(value.r), 1.0);
}
