#version 460
#extension GL_GOOGLE_include_directive : require
#include "vk_rt_reflection_common.glsl"

void main()
{
    out_color = vec4(0.0);
    if (any(lessThan(v_uv, pc.viewport.xy)) ||
        any(greaterThanEqual(v_uv, pc.viewport.xy + pc.viewport.zw)))
        return;
    vec2 uv = pixel_uv(v_uv);
    float depth = depth_at(uv);
    vec2 material = material_at(uv);
    if (depth >= 0.9999 || depth <= 0.00001 || material.x <= 0.001)
        return;
    vec3 p = view_position(uv, depth);
    vec3 n = normal_at(uv, p);
    float receiver = receiver_weight(n, material.y);
    if (receiver <= 0.001)
        return;
    vec2 raw_texel = 1.0 / vec2(textureSize(source_sampler, 0));
    vec4 center = texture(source_sampler, uv);
    float rough2 = material.y * material.y;
    float projected_distance = center.a * 1536.0 * abs(pc.projection.x) /
        max(-p.z, 1.0) / raw_texel.x * 0.5;
    float radius = mix(0.7, clamp(projected_distance * 0.25, 3.0, 12.0), rough2);
    vec3 sum = vec3(0.0);
    float weight_sum = 0.0;
    float distance_sum = 0.0;
    float hit_weight = 0.0;
    // A small texture gather replaces dozens of full-resolution AS traversals.
    // Bilinear taps blur silhouettes; misses remain black, not excluded.
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            vec2 offset = vec2(x, y);
            vec2 q = pixel_uv(clamp(uv + offset * radius * raw_texel * 0.5,
                                   pc.viewport.xy + pc.control.zw * 0.5,
                                   pc.viewport.xy + pc.viewport.zw - pc.control.zw * 0.5));
            float qdepth = depth_at(q);
            vec2 qm = material_at(q);
            if (qdepth >= 0.9999 || qm.x <= 0.001)
                continue;
            vec3 qp = view_position(q, qdepth);
            vec3 qn = normal_at(q, qp);
            float plane_error = abs(dot(n, qp - p));
            float plane_limit = max(0.75, abs(p.z) * 0.002);
            float weight = exp(-0.5 * dot(offset, offset)) *
                (1.0 - smoothstep(plane_limit, plane_limit * 3.0, plane_error)) *
                smoothstep(0.90, 0.995, dot(n, qn)) *
                (1.0 - smoothstep(0.07, 0.25, abs(material.x - qm.x))) *
                (1.0 - smoothstep(0.07, 0.25, abs(material.y - qm.y)));
            vec4 reflection = texture(source_sampler, q);
            sum += reflection.rgb * 4.0 * weight;
            weight_sum += weight;
            if (reflection.a > 0.0) {
                distance_sum += reflection.a * 1536.0 * weight;
                hit_weight += weight;
            }
        }
    }
    if (weight_sum <= 0.00001 || hit_weight <= 0.00001)
        return;
    float nv = clamp(dot(n, normalize(-p)), 0.0, 1.0);
    float alpha = max(rough2, 0.001);
    float masking = 2.0 * nv / max(nv +
        sqrt(alpha * alpha + (1.0 - alpha * alpha) * nv * nv), 0.00001);
    float fresnel = (0.04 + 0.96 * pow(1.0 - nv, 5.0)) * masking;
    float distance_fade = 1.0 - smoothstep(512.0, 1536.0, distance_sum / hit_weight);
    vec3 reflection = sum / weight_sum * receiver * material.x * fresnel *
        distance_fade * mix(1.0, 0.5, material.y) *
        pc.control.x * pc.control.y * 2.0;
    float luma = dot(reflection, vec3(0.2126, 0.7152, 0.0722));
    reflection *= min(1.0, 0.15 / max(luma, 0.000001));
    // Keep dim reflections above the UNORM quantization floor. Alpha carries
    // the scale, decoded by the additive composite; brighter colors stay valid.
    float scale = clamp(max(reflection.r, max(reflection.g, reflection.b)), 0.25, 1.0);
    out_color = vec4(reflection / scale, scale);
}
