#define MULTISCATTERING_LUT 1

#include "../GlobalUniforms.h.glsl"
#include "SkyUtil.h.glsl"
#include "SkyShared.h.glsl"

#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_arithmetic : require

/* This number should match the number of local threads -> z dimension */
const float SPHERE_SAMPLES = 64.0;
const float GOLDEN_RATIO = 1.6180339;
const float uniformPhase = 1.0 / (4.0 * M_PI);

layout(local_size_x = 1, local_size_y = 1, local_size_z = uint(SPHERE_SAMPLES)) in;

shared vec3 multiscatt_shared[2];
shared vec3 luminance_shared[2];

struct RaymarchResult
{
    vec3 luminance;
    vec3 multiscattering;
};

RaymarchResult integrate_scattered_luminance(vec3 world_position, vec3 world_direction, vec3 sun_direction, float sample_count)
{
    RaymarchResult result = RaymarchResult(vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 0.0));
    vec3 planet_zero = vec3(0.0, 0.0, 0.0);
    float planet_intersection_distance = ray_sphere_intersect_nearest(
        world_position, world_direction, planet_zero, uniforms.sky.atmosphere_bottom);
    float atmosphere_intersection_distance = ray_sphere_intersect_nearest(
        world_position, world_direction, planet_zero, uniforms.sky.atmosphere_top);

    float integration_length;
    /* ============================= CALCULATE INTERSECTIONS ============================ */
    if ((planet_intersection_distance == -1.0) && (atmosphere_intersection_distance == -1.0))
    {
        /* ray does not intersect planet or atmosphere -> no point in raymarching*/
        return result;
    }
    else if ((planet_intersection_distance == -1.0) && (atmosphere_intersection_distance > 0.0))
    {
        /* ray intersects only atmosphere */
        integration_length = atmosphere_intersection_distance;
    }
    else if ((planet_intersection_distance > 0.0) && (atmosphere_intersection_distance == -1.0))
    {
        /* ray intersects only planet */
        integration_length = planet_intersection_distance;
    }
    else
    {
        /* ray intersects both planet and atmosphere -> return the first intersection */
        integration_length = min(planet_intersection_distance, atmosphere_intersection_distance);
    }
    float integration_step = integration_length / float(sample_count);

    /* stores accumulated transmittance during the raymarch process */
    vec3 accum_transmittance = vec3(1.0, 1.0, 1.0);
    /* stores accumulated light contribution during the raymarch process */
    vec3 accum_light = vec3(0.0, 0.0, 0.0);
    float old_ray_shift = 0;

    /* ============================= RAYMARCH ==========================================  */
    for (int i = 0; i < sample_count; i++)
    {
        /* Sampling at 1/3rd of the integration step gives better results for exponential
           functions */
        float new_ray_shift = integration_length * (float(i) + 0.3) / sample_count;
        integration_step = new_ray_shift - old_ray_shift;
        vec3 new_position = world_position + new_ray_shift * world_direction;
        old_ray_shift = new_ray_shift;

        /* Raymarch shifts the angle to the sun a bit recalculate */
        vec3 up_vector = normalize(new_position);
        TransmittanceParams transmittance_lut_params = TransmittanceParams(length(new_position), dot(sun_direction, up_vector));

        /* uv coordinates later used to sample transmittance texture */
        vec2 trans_texture_uv = transmittance_lut_to_uv(transmittance_lut_params, uniforms.sky.atmosphere_bottom, uniforms.sky.atmosphere_top);

        vec3 transmittance_to_sun = texture(transmittanceTexture, transmittanceSampler, trans_texture_uv).rgb;

        MediumSample m_sample = sample_medium(uniforms.sky, new_position);
        vec3 medium_scattering = m_sample.mie_scattering + m_sample.rayleigh_scattering;
        vec3 medium_extinction = m_sample.medium_extinction;

        /* TODO: This probably should be a texture lookup altho might be slow*/
        vec3 trans_increase_over_integration_step = exp(-(medium_extinction * integration_step));
        /* Check if current position is in earth's shadow */
        float earth_intersection_distance = ray_sphere_intersect_nearest(
            new_position, sun_direction, planet_zero + PLANET_RADIUS_OFFSET * up_vector, uniforms.sky.atmosphere_bottom);
        float in_earth_shadow = earth_intersection_distance == -1.0 ? 1.0 : 0.0;

        /* Light arriving from the sun to this point */
        vec3 sunLight = in_earth_shadow * transmittance_to_sun * medium_scattering * uniformPhase;
        vec3 multiscattered_cont_int = (medium_scattering - medium_scattering * trans_increase_over_integration_step) / medium_extinction;
        vec3 inscatteredContInt = (sunLight - sunLight * trans_increase_over_integration_step) / medium_extinction;

        if (medium_extinction.r == 0.0)
        {
            multiscattered_cont_int.r = 0.0;
            inscatteredContInt.r = 0.0;
        }
        if (medium_extinction.g == 0.0)
        {
            multiscattered_cont_int.g = 0.0;
            inscatteredContInt.g = 0.0;
        }
        if (medium_extinction.b == 0.0)
        {
            multiscattered_cont_int.b = 0.0;
            inscatteredContInt.b = 0.0;
        }

        result.multiscattering += accum_transmittance * multiscattered_cont_int;
        accum_light += accum_transmittance * inscatteredContInt;
        accum_transmittance *= trans_increase_over_integration_step;
    }
    result.luminance = accum_light;
    return result;
    /* TODO: Check for bounced light off the earth */
}

layout(local_size_x = 1, local_size_y = 1, local_size_z = 64) in;
void main()
{
    const float sample_count = 20;

    const ivec2 multiscattering_image_size = imageSize(multiscatteringImage);
    vec2 uv = (vec2(gl_GlobalInvocationID.xy) + vec2(0.5, 0.5)) / vec2(multiscattering_image_size);
    uv = vec2(from_subuv_to_unit(uv.x, multiscattering_image_size.x),
              from_subuv_to_unit(uv.y, multiscattering_image_size.y));

    /* Mapping uv to multiscattering LUT parameters
    TODO -> Is the range from 0.0 to -1.0 really needed? */
    float sun_cos_zenith_angle = uv.x * 2.0 - 1.0;
    vec3 sun_direction = vec3(
        0.0,
        safe_sqrt(clamp(1.0 - sun_cos_zenith_angle * sun_cos_zenith_angle, 0.0, 1.0)),
        sun_cos_zenith_angle);

    float view_height =
        uniforms.sky.atmosphere_bottom +
        clamp(uv.y + PLANET_RADIUS_OFFSET, 0.0, 1.0) *
             (uniforms.sky.atmosphere_top - uniforms.sky.atmosphere_bottom - PLANET_RADIUS_OFFSET);

    vec3 world_position = vec3(0.0, 0.0, view_height);

    float sample_idx = gl_LocalInvocationID.z;
    // local thread dependent raymarch
#define USE_HILL_SAMPLING 0
#if USE_HILL_SAMPLING
#define SQRTSAMPLECOUNT 8
    const float sqrt_sample = float(SQRTSAMPLECOUNT);
    float i = 0.5 + float(sample_idx / SQRTSAMPLECOUNT);
    float j = 0.5 + mod(sample_idx, SQRTSAMPLECOUNT);
    float randA = i / sqrt_sample;
    float randB = j / sqrt_sample;

    float theta = 2.0 * M_PI * randA;
    float phi = M_PI * randB;
#else
    /* Fibbonaci lattice -> http://extremelearning.com.au/how-to-evenly-distribute-points-on-a-sphere-more-effectively-than-the-canonical-fibonacci-lattice/ */
    float theta = acos(1.0 - 2.0 * (sample_idx + 0.5) / SPHERE_SAMPLES);
    float phi = (2 * M_PI * sample_idx) / GOLDEN_RATIO;
#endif

    vec3 world_direction = vec3(cos(theta) * sin(phi), sin(theta) * sin(phi), cos(phi));
    RaymarchResult result = integrate_scattered_luminance(world_position, world_direction, sun_direction, sample_count);

    // Multiscattering LUT shoots 64 rays per pixel.
    // Now we need to sum and average them all up into a single value.
    // First we perform reduction inside each subgroup.
    result.multiscattering = subgroupAdd(result.multiscattering);
    result.luminance = subgroupAdd(result.luminance);

    groupMemoryBarrier();
    barrier();

    // One thread from each subgroup then writes its reduction into shared memory.
    if(subgroupElect())
    {
        const uint index = gl_SubgroupID;
        multiscatt_shared[index] = result.multiscattering;
        luminance_shared[index] = result.luminance;
    }
    groupMemoryBarrier();
    barrier();

    // And finally, a single thread sums the values from shared memory.
    if (gl_LocalInvocationID.z == 0)
    {
        vec3 multiscattering_sum = vec3(0.0f);
        vec3 luminance_sum = vec3(0.0f);

        // Add entries from each subgroup.
        const uint num_subgroups = uint(SPHERE_SAMPLES) / gl_SubgroupSize;
        for(int subgroup_entry = 0; subgroup_entry < num_subgroups; ++subgroup_entry)
        {
            multiscattering_sum += multiscatt_shared[subgroup_entry];
            luminance_sum += luminance_shared[subgroup_entry];
        }

        multiscattering_sum /= SPHERE_SAMPLES;
        luminance_sum /= SPHERE_SAMPLES;

        const vec3 r = multiscattering_sum;
        const vec3 sum_of_all_multiscattering_events_contribution = vec3(1.0 / (1.0 - r.x), 1.0 / (1.0 - r.y), 1.0 / (1.0 - r.z));
        vec3 lum = luminance_sum * sum_of_all_multiscattering_events_contribution;

        imageStore(multiscatteringImage, ivec2(gl_GlobalInvocationID.xy), vec4(lum, 1.0));
    }
}