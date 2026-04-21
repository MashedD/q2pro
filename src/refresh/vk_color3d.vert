#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) out vec4 v_color;

void main()
{
    vec3 pos[3] = vec3[](
        vec3(-24.0, -16.0, -96.0),
        vec3( 24.0, -16.0, -96.0),
        vec3(  0.0,  24.0, -96.0));

    gl_Position = pc.mvp * vec4(pos[gl_VertexIndex], 1.0);
    v_color = pc.color;
}
