#version 450

layout(location = 0) noperspective in vec2 v_current_ndc;
layout(location = 1) noperspective in vec2 v_previous_ndc;
layout(location = 0) out vec2 out_motion;
layout(location = 1) out float out_reactive;

layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 previous_mvp;
    float reactive;
} pc;

void main()
{
    /* FSR2 multiplies this normalized screen-space delta by the render
     * dimensions through motionVectorScale. */
    out_motion = (v_current_ndc - v_previous_ndc) * 0.5;
    out_reactive = pc.reactive;
}
