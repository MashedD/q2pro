#version 450
#extension GL_GOOGLE_include_directive : require
#include "vk_motion_push.glsl"
layout(location = 0) flat out mat4 reprojection;
layout(location = 4) flat out mat4 sky_reprojection;
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2 - 1, 0, 1);
    mat4 inv = inverse(pc.mvp);
    reprojection = pc.previous_mvp * inv;
    // Strip camera translation from reconstructed homogeneous positions.
    vec3 camera = inv[2].xyz / inv[2].w;
    for (int i = 0; i < 4; i++) {
        inv[i].xyz -= camera * inv[i].w;
        inv[i].w = 0;
    }
    sky_reprojection = pc.previous_mvp * inv;
}
