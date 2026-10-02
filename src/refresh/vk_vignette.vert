#version 450

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 color;
    vec2 screen;
    vec2 pad;
    vec4 inner;
} pc;

layout(location = 0) out vec4 v_color;

void main()
{
    int indices[24] = int[](
        0, 5, 4, 0, 1, 5, 1, 6, 5, 1, 2, 6,
        6, 2, 3, 6, 3, 7, 0, 7, 3, 0, 4, 7);
    vec2 outer[4] = vec2[](
        pc.rect.xy,
        pc.rect.xy + vec2(pc.rect.z, 0.0),
        pc.rect.xy + pc.rect.zw,
        pc.rect.xy + vec2(0.0, pc.rect.w));
    vec2 inner[4] = vec2[](
        pc.inner.xy,
        pc.inner.xy + vec2(pc.inner.z, 0.0),
        pc.inner.xy + pc.inner.zw,
        pc.inner.xy + vec2(0.0, pc.inner.w));

    int index = indices[gl_VertexIndex];
    vec2 p = index < 4 ? outer[index] : inner[index - 4];
    vec2 ndc = vec2(p.x / pc.screen.x * 2.0 - 1.0,
                    p.y / pc.screen.y * 2.0 - 1.0);

    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = index < 4 ? pc.color : vec4(pc.color.rgb, 0.0);
}
