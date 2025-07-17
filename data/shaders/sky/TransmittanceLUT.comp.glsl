#include "../GlobalUniforms.h.glsl"
#include "SkyUtil.h.glsl"
#include "SkyShared.h.glsl"

vec3 integrate_transmittance(vec3 world_position, vec3 world_direction, uint sample_count)
{
    /* The length of ray between position and nearest atmosphere top boundary */
    float integration_length = ray_sphere_intersect_nearest(
        world_position,
        world_direction,
        vec3(0.0, 0.0, 0.0),
        uniforms.sky.atmosphere_top);

    float integration_step = integration_length / float(sample_count);

    /* Result of the integration */
    vec3 optical_depth = vec3(0.0, 0.0, 0.0);

    for (int i = 0; i < sample_count; i++)
    {
        /* Move along the world direction ray to new position */
        vec3 new_pos = world_position + i * integration_step * world_direction;
        vec3 atmosphere_extinction = sample_medium(uniforms.sky, new_pos).medium_extinction;
        optical_depth += atmosphere_extinction * integration_step;
    }
    return optical_depth;
}

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    const ivec2 transmittance_image_size = imageSize(transmittanceImage).xy;

    if (all(lessThan(gl_GlobalInvocationID.xy, transmittance_image_size)))
    {
        vec2 uv = vec2(gl_GlobalInvocationID.xy) / vec2(transmittance_image_size);

        TransmittanceParams mapping = uv_to_transmittance_lut_params(uv, uniforms.sky.atmosphere_bottom, uniforms.sky.atmosphere_top);

        vec3 world_position = vec3(0.0, 0.0, mapping.height);
        vec3 world_direction = vec3(
            safe_sqrt(1.0 - mapping.zenith_cos_angle * mapping.zenith_cos_angle),
            0.0,
            mapping.zenith_cos_angle);

        vec3 transmittance = exp(-integrate_transmittance(world_position, world_direction, 2));
        imageStore(transmittanceImage, ivec2(gl_GlobalInvocationID.xy), vec4(transmittance, 0.0));
    }
}