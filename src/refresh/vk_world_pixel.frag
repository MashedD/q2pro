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
    vec2 lm_scale;
    vec2 lm_offset;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;
layout(set = 1, binding = 0) uniform sampler2D lm_sampler;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in float v_mode;
layout(location = 3) in vec2 v_lmuv;
layout(location = 4) in vec3 v_position;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_bloom;

layout(constant_id = 0) const bool fast_path = false;

vec3 dynamic_light()
{
    if (fast_path)
        return vec3(0.0);
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
    out_bloom = vec4(0.0);
    float mode = fast_path ? 0.0 : mod(v_mode, 4.0);
    vec2 uv = v_uv;

    if (!fast_path && v_mode >= 4.0) {
        uv += vec2(0.0625) * sin(uv.ts * vec2(4.0) + vec2(pc.dlight.a));
    }

    if (mode > 1.5) {
        out_color = v_color;
    } else {
        vec4 texel = texture(tex_sampler, uv);
        if (pc.desaturation > 0.0) {
            float luma = dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722));
            texel.rgb = mix(texel.rgb, vec3(luma), pc.desaturation);
        }
        vec3 lm = pc.lm_scale.x < 0.0 ? vec3(1.0) : texture(lm_sampler, v_lmuv).rgb;
        out_color = texel;
        out_color.rgb *= (lm + pc.scroll.www) * pc.color.rgb + dynamic_light();
        out_color.a *= v_color.a;
        if (pc.intensity < 0.0) {
            out_color.rgb *= (out_color.r + out_color.g + out_color.b) / 3.0;
            out_color.rgb *= v_color.a;
        } else {
            out_color.rgb *= pc.intensity;
        }
    }

    if (pc.fog.a < 0.0) {
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
    } else if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        float fog = 1.0 - exp(-(d * d));
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, fog);
    }
}
