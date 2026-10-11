layout(push_constant) uniform Push {
    vec4 projection;
    vec4 control; // reflection strength, specular strength, inverse scene size
    vec4 view_up;
    vec4 camera;
    vec4 view_to_world[3];
    vec4 viewport; // normalized scene viewport
} pc;
layout(set = 0, binding = 0) uniform sampler2D source_sampler;
layout(set = 0, binding = 1) uniform sampler2D depth_sampler;
layout(set = 0, binding = 2) uniform sampler2D material_sampler;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

vec2 pixel_uv(vec2 uv)
{
    vec2 size = vec2(textureSize(depth_sampler, 0));
    return (clamp(floor(uv * size), vec2(0.0), size - 1.0) + 0.5) / size;
}
float depth_at(vec2 uv)
{
    ivec2 size = textureSize(depth_sampler, 0);
    return texelFetch(depth_sampler, clamp(ivec2(uv * vec2(size)),
        ivec2(0), size - 1), 0).r;
}
vec2 material_at(vec2 uv)
{
    ivec2 size = textureSize(material_sampler, 0);
    float packed = texelFetch(material_sampler,
        clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1), 0).a;
    uint bits = uint(floor(clamp(packed, 0.0, 1.0) * 255.0 + 0.5));
    return vec2(bits >> 4u, bits & 15u) / 15.0;
}
vec3 view_position(vec2 uv, float depth)
{
    float z = -pc.projection.w / min(depth + pc.projection.z, -0.00001);
    vec2 ndc = (uv - pc.viewport.xy) / pc.viewport.zw * 2.0 - 1.0;
    return vec3(-ndc.x * z / pc.projection.x,
                -ndc.y * z / pc.projection.y, z);
}
vec3 normal_at(vec2 uv, vec3 p)
{
    vec2 dx = vec2(pc.control.z, 0.0);
    vec2 dy = vec2(0.0, pc.control.w);
    vec3 xp = view_position(uv + dx, depth_at(uv + dx)) - p;
    vec3 xm = p - view_position(uv - dx, depth_at(uv - dx));
    vec3 yp = view_position(uv + dy, depth_at(uv + dy)) - p;
    vec3 ym = p - view_position(uv - dy, depth_at(uv - dy));
    vec3 x = abs(xp.z) < abs(xm.z) ? xp : xm;
    vec3 y = abs(yp.z) < abs(ym.z) ? yp : ym;
    if (max(abs(x.z), abs(y.z)) > max(16.0, abs(p.z) * 0.08))
        return vec3(0.0);
    vec3 cross_xy = cross(x, y);
    float length2 = dot(cross_xy, cross_xy);
    if (length2 < 0.00000001)
        return vec3(0.0);
    vec3 n = cross_xy * inversesqrt(length2);
    return dot(n, -p) < 0.0 ? -n : n;
}
vec3 world_vector(vec3 v)
{
    return pc.view_to_world[0].xyz * v.x +
           pc.view_to_world[1].xyz * v.y +
           pc.view_to_world[2].xyz * v.z;
}
float receiver_weight(vec3 n, float roughness)
{
    return smoothstep(0.62, 0.88, dot(n, pc.view_up.xyz)) *
        mix(1.0, 0.35, roughness);
}
