#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 scroll;
    vec4 dlight;
    vec4 fog;
    float intensity;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in float v_mode;
layout(location = 0) out vec4 out_color;

void main()
{
    float mode = mod(v_mode, 4.0);
    vec2 uv = v_uv;

    if (v_mode >= 4.0) {
        uv += vec2(0.0625) * sin(uv.ts * vec2(4.0) + vec2(pc.dlight.a));
    }

    if (mode > 1.5) {
        out_color = v_color;
        if (pc.fog.a < 0.0) {
            out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
        } else if (pc.fog.a > 0.0) {
            float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
            float fog = 1.0 - exp(-(d * d));
            out_color.rgb = mix(out_color.rgb, pc.fog.rgb, fog);
        }
        return;
    }

    vec4 texel = texture(tex_sampler, uv);
    if (texel.a <= 0.666) {
        discard;
    }
    out_color = texel;
    if (pc.intensity < 0.0) {
        out_color.rgb *= (out_color.r + out_color.g + out_color.b) / 3.0;
        out_color.rgb *= v_color.a;
    } else {
        out_color.rgb *= pc.intensity;
    }
    out_color *= v_color;
    if (pc.fog.a < 0.0) {
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
    } else if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        float fog = 1.0 - exp(-(d * d));
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, fog);
    }
}
