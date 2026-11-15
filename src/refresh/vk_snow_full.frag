#version 450

// Snow cover (vk_snow.c) once it has fully grown: nothing is left out, so
// the depth test can run before this shader.

layout(early_fragment_tests) in;

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_bloom;

// Same layout as vk_snow_push_t.
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 craters;
    vec4 fog;       // rgb color, a density; the same as for the world
} pc;

vec3 apply_fog(vec3 color)
{
    if (pc.fog.a < 0.0)
        return mix(color, pc.fog.rgb, -pc.fog.a);
    if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        return mix(color, pc.fog.rgb, 1.0 - exp(-(d * d)));
    }
    return color;
}

void main()
{
    out_color = v_color;
    out_color.rgb = apply_fog(out_color.rgb);
    out_bloom = out_color;
}
