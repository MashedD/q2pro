#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 scroll;
    vec4 dlight;
    vec4 dlight_origins[3];
    vec4 dlight_colors[3];
    vec4 fog;
    float intensity;
    float desaturation;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in float v_mode;
layout(location = 3) in vec3 v_position;
layout(location = 0) out vec4 out_color;

vec3 dynamic_light()
{
    vec3 light = vec3(0.0);
    for (int i = 0; i < 3; i++) {
        float range = pc.dlight_origins[i].w;
        if (range <= 0.0)
            break;
        float falloff = max(1.0 - distance(v_position, pc.dlight_origins[i].xyz) / range,
                            0.0);
        light += pc.dlight_colors[i].rgb * (pc.dlight_colors[i].w * falloff / 255.0);
    }
    return light;
}

void main()
{
    float mode = mod(v_mode, 4.0);
    vec2 uv = v_uv;
    if (v_mode >= 4.0)
        uv += vec2(0.0625) * sin(uv.ts * vec2(4.0) + vec2(pc.dlight.a));

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

    out_color = texture(tex_sampler, uv);
    if (out_color.a <= 0.666)
        discard;
    if (pc.desaturation > 0.0) {
        float luma = dot(out_color.rgb, vec3(0.2126, 0.7152, 0.0722));
        out_color.rgb = mix(out_color.rgb, vec3(luma), pc.desaturation);
    }
    out_color.rgb *= pc.intensity;
    out_color.rgb *= clamp(v_color.rgb + dynamic_light(), 0.0, 1.0);
    out_color.a *= v_color.a;

    if (pc.fog.a < 0.0) {
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
    } else if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        float fog = 1.0 - exp(-(d * d));
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, fog);
    }
}
