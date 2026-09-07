#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 previous_mvp;
    float backlerp;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 3) in vec3 in_old_position;
layout(location = 0) noperspective out vec2 v_current_ndc;
layout(location = 1) noperspective out vec2 v_previous_ndc;

void main()
{
    vec3 position = mix(in_position, in_old_position, pc.backlerp);
    vec4 current = pc.mvp * vec4(position, 1.0);
    vec4 previous = pc.previous_mvp * vec4(position, 1.0);
    gl_Position = current;
    v_current_ndc = current.xy / max(abs(current.w), 0.000001);
    v_previous_ndc = previous.xy / max(abs(previous.w), 0.000001);
}
