#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
    vec4 shadedir;
    float backlerp;
    float shellscale;
    float depthscale;
    float _pad;
    vec4 fog;
    float intensity;
    vec3 _pad2;
    vec4 height_x;
    vec4 height_y;
    vec4 height_z;
    vec4 heightfog_start;
    vec4 heightfog_end;
    vec4 heightfog_view;
    vec4 heightfog_params;
} pc;

layout(location = 0) in vec4 v_color;
layout(location = 3) in vec3 v_world_pos;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = v_color;
    if (pc.fog.a < 0.0) {
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
    } else if (pc.fog.a > 0.0) {
        float d = pc.fog.a * gl_FragCoord.z / gl_FragCoord.w;
        float fog = 1.0 - exp(-(d * d));
        out_color.rgb = mix(out_color.rgb, pc.fog.rgb, fog);
    }
    if (pc.heightfog_params.w > 0.0) {
        float frag_depth = gl_FragCoord.z / gl_FragCoord.w;
        float dir_z = normalize(v_world_pos - pc.heightfog_view.xyz).z;
        float s = sign(dir_z);
        dir_z += 0.00001 * (1.0 - s * s);
        float eye = pc.heightfog_view.z - pc.heightfog_start.w;
        float pos = v_world_pos.z - pc.heightfog_start.w;
        float density = (exp(-pc.heightfog_params.y * eye) -
                         exp(-pc.heightfog_params.y * pos)) /
            (pc.heightfog_params.y * dir_z);
        float extinction = 1.0 - clamp(exp(-density), 0.0, 1.0);
        float fraction = clamp((pos - pc.heightfog_start.w) /
                               (pc.heightfog_end.w - pc.heightfog_start.w),
                               0.0, 1.0);
        vec3 fog_color = mix(pc.heightfog_start.rgb, pc.heightfog_end.rgb,
                             fraction) * extinction;
        float fog = (1.0 - exp(-(pc.heightfog_params.x * frag_depth))) *
            extinction;
        out_color.rgb = mix(out_color.rgb, fog_color, fog);
    }
}
