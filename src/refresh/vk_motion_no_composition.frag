#version 450
#extension GL_GOOGLE_include_directive : require
#include "vk_motion_push.glsl"
layout(set = 0, binding = 0) uniform sampler2D skin;
layout(location = 0) in vec4 v_current_clip;
layout(location = 1) in vec4 v_previous_clip;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in float v_alpha;
layout(location = 0) out vec2 out_motion;
layout(location = 1) out float out_reactive;
void main()
{
    float coverage = texture(skin, v_uv).a * pc.alpha * v_alpha;
    if (coverage <= max(pc.viewport.x, 0.01)) discard;
    bool valid = v_current_clip.w > 0.000001 && v_previous_clip.w > 0.000001;
    out_motion = valid ? (v_previous_clip.xy / v_previous_clip.w -
                          v_current_clip.xy / v_current_clip.w) * pc.motion_scale : vec2(0);
    out_reactive = valid ? clamp(pc.reactive * coverage, 0, 1) : 1;
}
