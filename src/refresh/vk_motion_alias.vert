#version 450
#extension GL_GOOGLE_include_directive : require
#include "vk_motion_push.glsl"
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 3) in vec3 in_old_position;
layout(location = 4) in vec3 in_previous_position;
layout(location = 5) in vec3 in_previous_old_position;
layout(location = 0) out vec4 v_current_clip;
layout(location = 1) out vec4 v_previous_clip;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out float v_alpha;
void main()
{
    vec3 current = mix(in_position, in_old_position, pc.backlerp);
    vec3 previous = mix(in_previous_position, in_previous_old_position, pc.previous_backlerp);
    v_current_clip = pc.mvp * vec4(current, 1);
    v_previous_clip = pc.previous_mvp * vec4(previous, 1);
    gl_Position = v_current_clip;
    gl_Position.xy += pc.jitter * gl_Position.w;
    gl_Position.z *= pc.viewport.y;
    v_uv = in_uv;
    v_alpha = 1;
}
