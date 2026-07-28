#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 shadedir;
    float backlerp;
    float shellscale;
    float depthscale;
    float _pad;
    vec4 fog;
    float intensity;
    float desaturation;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_old_position;
layout(location = 4) in vec3 in_normal;
layout(location = 5) in vec3 in_old_normal;
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) flat out float v_mode;

void main()
{
    vec3 position = mix(in_position, in_old_position, pc.backlerp);
    vec3 normal = normalize(mix(in_normal, in_old_normal, pc.backlerp));
    if (pc.shellscale != 0.0) {
        position += normal * pc.shellscale;
    }
    gl_Position = pc.mvp * vec4(position, 1.0);
    gl_Position.z *= pc.depthscale;
    v_color = in_color * pc.color;
    if (pc.shadedir.w != 0.0) {
        float d = dot(normal, pc.shadedir.xyz);
        if (d < 0.0) {
            d *= 0.3;
        }
        v_color.rgb *= d + 1.0;
    }
    v_uv = in_uv;
    v_mode = 0.0;
}
