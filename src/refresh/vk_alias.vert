#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    float backlerp;
    float shellscale;
    float depthscale;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_old_position;
layout(location = 4) in vec3 in_normal;
layout(location = 5) in vec3 in_old_normal;
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

void main()
{
    vec3 position = mix(in_position, in_old_position, pc.backlerp);
    if (pc.shellscale != 0.0) {
        vec3 normal = normalize(mix(in_normal, in_old_normal, pc.backlerp));
        position += normal * pc.shellscale;
    }
    gl_Position = pc.mvp * vec4(position, 1.0);
    gl_Position.z *= pc.depthscale;
    v_color = in_color * pc.color;
    v_uv = in_uv;
}
