#define SKY_VIEW_LUT 1

#include "../GlobalUniforms.h.glsl"
#include "SkyUtil.h.glsl"
#include "SkyShared.h.glsl"

FVOG_DECLARE_ARGUMENTS(PushConstants)
{
    SkyViewGpuParams pc;
};

vec3 get_multiple_scattering(vec3 world_position, float view_zenith_cos_angle)
{
    const ivec2 multiscattering_texture_size = textureSize(pc.multiscatteringTexture, 0);
    vec2 uv = clamp(vec2(view_zenith_cos_angle * 0.5 + 0.5,
                        (length(world_position) - pc.uniforms.sky.config.atmosphere_bottom) /
                        (pc.uniforms.sky.config.atmosphere_top - pc.uniforms.sky.config.atmosphere_bottom)),
                    0.0, 1.0);
    uv = vec2(from_unit_to_subuv(uv.x, multiscattering_texture_size.x),
              from_unit_to_subuv(uv.y, multiscattering_texture_size.y));

    return texture(pc.multiscatteringTexture, gLinearClampSampler, uv).rgb;
}

vec3 integrate_scattered_luminance(vec3 world_position, vec3 world_direction, vec3 sun_direction, int sample_count)
{
    vec3 planet_zero = vec3(0.0, 0.0, 0.0);
    float planet_intersection_distance = ray_sphere_intersect_nearest(
        world_position, world_direction, planet_zero, pc.uniforms.sky.config.atmosphere_bottom);
    float atmosphere_intersection_distance = ray_sphere_intersect_nearest(
        world_position, world_direction, planet_zero, pc.uniforms.sky.config.atmosphere_top);

    float integration_length;
    /* ============================= CALCULATE INTERSECTIONS ============================ */
    if ((planet_intersection_distance == -1.0) && (atmosphere_intersection_distance == -1.0))
    {
        /* ray does not intersect planet or atmosphere -> no point in raymarching*/
        return vec3(0.0, 0.0, 0.0);
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

    float cos_theta = dot(sun_direction, world_direction);
    // float mie_phase_value = klein_nishina_phase(cos_theta, 2800.0);
    float mie_phase_value = hg_draine_phase(cos_theta, 3.6);
    // float mie_phase_value = cornette_shanks_mie_phase_function(deref(settings).mie_phase_function_g, -cos_theta);
    float rayleigh_phase_value = rayleigh_phase(cos_theta);

    vec3 accum_transmittance = vec3(1.0, 1.0, 1.0);
    vec3 accum_light = vec3(0.0, 0.0, 0.0);
    /* ============================= RAYMARCH ============================ */
    for (int i = 0; i < sample_count; i++)
    {
        /* Step size computation */
        float step_0 = float(i) / sample_count;
        float step_1 = float(i + 1) / sample_count;

        /* Nonuniform step size*/
        step_0 *= step_0;
        step_1 *= step_1;

        step_0 = step_0 * integration_length;
        step_1 = step_1 > 1.0 ? integration_length : step_1 * integration_length;
        /* Sample at one third of the integrated interval -> better results for exponential functions */
        float integration_step = step_0 + (step_1 - step_0) * 0.3;
        float d_int_step = step_1 - step_0;

        /* Position shift */
        vec3 new_position = world_position + integration_step * world_direction;
        MediumSample m_sample = sample_medium(pc.uniforms.sky.config, new_position);
        vec3 medium_extinction = m_sample.medium_extinction;

        vec3 up_vector = normalize(new_position);
        TransmittanceParams transmittance_lut_params = TransmittanceParams(length(new_position), dot(sun_direction, up_vector));

        /* uv coordinates later used to sample transmittance texture */
        vec2 trans_texture_uv = transmittance_lut_to_uv(transmittance_lut_params, pc.uniforms.sky.config.atmosphere_bottom, pc.uniforms.sky.config.atmosphere_top);
        vec3 transmittance_to_sun = texture(pc.transmittanceTexture, gLinearClampSampler, trans_texture_uv).rgb;

        vec3 phase_times_scattering = m_sample.mie_scattering * mie_phase_value + m_sample.rayleigh_scattering * rayleigh_phase_value;

        float earth_intersection_distance = ray_sphere_intersect_nearest(
            new_position, sun_direction, planet_zero, pc.uniforms.sky.config.atmosphere_bottom);
        float in_earth_shadow = earth_intersection_distance == -1.0 ? 1.0 : 0.0;

        vec3 multiscattered_luminance = get_multiple_scattering(new_position, dot(sun_direction, up_vector));

        /* Light arriving from the sun to this point */
        vec3 sun_light =
            ((in_earth_shadow * transmittance_to_sun * phase_times_scattering) +
                (multiscattered_luminance * (m_sample.rayleigh_scattering + m_sample.mie_scattering))); // * deref(settings).sun_brightness;

        /* TODO: This probably should be a texture lookup*/
        vec3 trans_increase_over_integration_step = exp(-(medium_extinction * d_int_step));

        vec3 sun_light_integ = (sun_light - sun_light * trans_increase_over_integration_step) / medium_extinction;

        if (medium_extinction.r == 0.0) { sun_light_integ.r = 0.0; }
        if (medium_extinction.g == 0.0) { sun_light_integ.g = 0.0; }
        if (medium_extinction.b == 0.0) { sun_light_integ.b = 0.0; }

        accum_light += accum_transmittance * sun_light_integ;
        //accum_light += cloudShadow;
        accum_transmittance *= trans_increase_over_integration_step;
    }
    return accum_light;
}

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    const ivec2 sky_view_image_size = imageSize(pc.skyViewImage);
    if (all(lessThan(gl_GlobalInvocationID.xy, sky_view_image_size.xy)))
    {
        const vec3 z_up_world_pos = pc.uniforms.cameraPos.xzy * vec3(1.0, -1.0, 1.0);;
        const vec3 z_up_sun_dir = pc.uniforms.sky.config.sunDir.xzy * vec3(1.0, -1.0, 1.0);;

        vec3 world_position = z_up_world_pos * M_TO_KM_SCALE;
        world_position.z += pc.uniforms.sky.config.atmosphere_bottom + BASE_HEIGHT_OFFSET;
        const float camera_height = length(world_position);

        vec2 uv = vec2(gl_GlobalInvocationID.xy) / sky_view_image_size.xy;
        SkyviewParams skyview_params = uv_to_skyview_lut_params(
            uv,
            pc.uniforms.sky.config.atmosphere_bottom,
            pc.uniforms.sky.config.atmosphere_top,
            sky_view_image_size,
            camera_height);

        vec3 ray_direction = vec3( 
            cos(skyview_params.light_view_angle) * sin(skyview_params.view_zenith_angle),
            sin(skyview_params.light_view_angle) * sin(skyview_params.view_zenith_angle),
            cos(skyview_params.view_zenith_angle));

        const mat3 camera_basis = build_orthonormal_basis(world_position / camera_height);
        world_position = vec3(0, 0, camera_height);

        float sun_zenith_cos_angle = dot(vec3(0, 0, 1), z_up_sun_dir * camera_basis);

        // sin^2 + cos^2 = 1 -> sqrt(1 - cos^2) = sin
        // rotate the sun direction so that we are aligned with the y = 0 axis
        vec3 local_sun_direction = normalize(vec3(
            safe_sqrt(1.0 - sun_zenith_cos_angle * sun_zenith_cos_angle),
            0.0,
            sun_zenith_cos_angle));

        vec3 world_direction = vec3(
            cos(skyview_params.light_view_angle) * sin(skyview_params.view_zenith_angle),
            sin(skyview_params.light_view_angle) * sin(skyview_params.view_zenith_angle),
            cos(skyview_params.view_zenith_angle));

        if (!move_to_top_atmosphere(world_position, world_direction, pc.uniforms.sky.config.atmosphere_bottom, pc.uniforms.sky.config.atmosphere_top))
        {
            /* No intersection with the atmosphere */
            imageStore(pc.skyViewImage, ivec2(gl_GlobalInvocationID.xy), vec4(0.0, 0.0, 0.0, 1.0));
            return;
        }

        const vec3 luminance = integrate_scattered_luminance(world_position, world_direction, local_sun_direction, 50);
        const vec3 inv_luminance = 1.0 / max(luminance, vec3(1.0 / 1048576.0));
        const float inv_mult = min(1048576.0, max(inv_luminance.x, max(inv_luminance.y, inv_luminance.z)));
        imageStore(pc.skyViewImage, ivec2(gl_GlobalInvocationID.xy), vec4(luminance * inv_mult, 1.0/inv_mult));
    }
}