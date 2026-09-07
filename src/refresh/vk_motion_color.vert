#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 previous_mvp;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 0) noperspective out vec2 v_current_ndc;
layout(location = 1) noperspective out vec2 v_previous_ndc;

void main()
{
    vec4 current = pc.mvp * vec4(in_position, 1.0);
    vec4 previous = pc.previous_mvp * vec4(in_position, 1.0);
    gl_Position = current;
    v_current_ndc = current.xy / max(abs(current.w), 0.000001);
    v_previous_ndc = previous.xy / max(abs(previous.w), 0.000001);
}
