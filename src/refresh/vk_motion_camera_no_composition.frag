#version 450
#extension GL_GOOGLE_include_directive : require
#include "vk_motion_push.glsl"
layout(set = 0, binding = 0) uniform sampler2D scene_depth;
layout(location = 0) flat in mat4 reprojection;
layout(location = 4) flat in mat4 sky_reprojection;
layout(location = 0) out vec2 out_motion;
layout(location = 1) out float out_reactive;
void main()
{
    float depth = texelFetch(scene_depth, ivec2(gl_FragCoord.xy), 0).r;
    vec2 current = (gl_FragCoord.xy - pc.viewport.xy) / pc.viewport.zw * 2 - 1;
    current -= pc.jitter;
    vec4 clip = vec4(current, depth, 1);
    vec4 previous = depth >= 1 ? sky_reprojection * clip : reprojection * clip;
    bool valid = previous.w > 0.000001;
    out_motion = valid ? (previous.xy / previous.w - current) * pc.motion_scale : vec2(0);
    out_reactive = valid ? 0 : 1;
}
