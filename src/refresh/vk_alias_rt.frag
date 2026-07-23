#version 460
#extension GL_EXT_ray_query : require

layout(push_constant) uniform Push {
    mat4 mvp; vec4 color; vec4 shadedir; float backlerp; float shellscale;
    float depthscale; float _pad; vec4 fog; float intensity; float desaturation;
    vec4 rt_light_origin; vec4 rt_light_color; vec4 rt_entity_origin;
    float rt_enabled; vec3 _rt_pad;
} pc;
layout(set=0,binding=0) uniform sampler2D tex_sampler;
layout(set=1,binding=0) uniform accelerationStructureEXT scene;
layout(location=0) in vec4 v_color;
layout(location=1) in vec2 v_uv;
layout(location=2) flat in float v_mode;
layout(location=3) flat in vec4 v_rt_light_origin;
layout(location=4) flat in vec4 v_rt_light_color;
layout(location=5) flat in vec4 v_rt_entity_origin;
layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_bloom;

void main() {
    vec4 texel = texture(tex_sampler, v_uv);
#ifdef RT_ALPHA_TEST
    if (texel.a < 0.5) discard;
#endif
    vec3 rgb = texel.rgb * v_color.rgb * max(pc.intensity, 0.0);
    if (pc.rt_enabled > 0.5 && v_rt_light_color.w > 0.001) {
        vec3 to_light = v_rt_light_origin.xyz - v_rt_entity_origin.xyz;
        float dist = length(to_light);
        if (dist > 0.001 && dist <= v_rt_light_origin.w) {
            vec3 dir = to_light / dist;
            rayQueryEXT q;
            rayQueryInitializeEXT(q, scene, gl_RayFlagsOpaqueEXT,
                                  0xff, v_rt_entity_origin.xyz + dir * 2.0,
                                  0.0, dir, max(dist - 4.0, 0.0));
            while (rayQueryProceedEXT(q)) {}
            if (rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT)
                rgb *= 0.28;
        }
    }
    out_color = vec4(rgb, texel.a * v_color.a);
    if (pc.fog.a < 0.0) out_color.rgb = mix(out_color.rgb, pc.fog.rgb, -pc.fog.a);
    else if (pc.fog.a > 0.0) out_color.rgb = mix(out_color.rgb, pc.fog.rgb, 1.0-exp(-pow(pc.fog.a*gl_FragCoord.z/gl_FragCoord.w, 2.0)));
    out_bloom = out_color;
}
