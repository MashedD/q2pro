#version 450

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 color;
    vec2 screen;
    vec4 uv;
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main()
{
    vec2 pos[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0));
    vec2 p = pc.rect.xy + pos[gl_VertexIndex] * pc.rect.zw;
    vec2 ndc = vec2(p.x / pc.screen.x * 2.0 - 1.0,
                    p.y / pc.screen.y * 2.0 - 1.0);

    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = mix(pc.uv.xy, pc.uv.zw, pos[gl_VertexIndex]);
    v_color = pc.color;
}
