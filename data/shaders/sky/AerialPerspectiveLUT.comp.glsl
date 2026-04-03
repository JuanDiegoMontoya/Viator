#define AERIAL_PERSPECTIVE_LUT
#include "SkyShared.h.glsl"
#include "SkyUtil.h.glsl"
#include "../Math.h.glsl"

FVOG_DECLARE_ARGUMENTS(PushConstants)
{
  AerialPerspectiveGpuParams pc;
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

vec3 integrate_scattered_luminance(vec3 world_position, vec3 world_direction, vec3 sun_direction, int sample_count, float max_integration_length, out vec3 transmittance)
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

    integration_length = min(integration_length, max_integration_length);

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
        vec3 transmittance_to_sun = textureLod(pc.transmittanceTexture, gLinearClampSampler, trans_texture_uv, 0).rgb;

        vec3 phase_times_scattering = m_sample.mie_scattering * mie_phase_value + m_sample.rayleigh_scattering * rayleigh_phase_value;

        float earth_intersection_distance = ray_sphere_intersect_nearest(
            new_position, sun_direction, planet_zero, pc.uniforms.sky.config.atmosphere_bottom);
        float in_earth_shadow = earth_intersection_distance == -1.0 ? 1.0 : 0.0;

        vec3 multiscattered_luminance = get_multiple_scattering(new_position, dot(sun_direction, up_vector));

        /* Light arriving from the sun to this point */
        vec3 sun_light =
            ((in_earth_shadow * transmittance_to_sun * phase_times_scattering) +
                (multiscattered_luminance * (m_sample.rayleigh_scattering + m_sample.mie_scattering))); // * deref(settings).sun_brightness;

        vec3 shadowPos = new_position - vec3(0, pc.uniforms.sky.config.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0);
        shadowPos /= M_TO_KM_SCALE;
        shadowPos *= vec3(1, -1, 1);
        shadowPos = shadowPos.xzy; // Y-up conversion
        float cloudShadow = 1;
        //cloudShadow = SampleCascadedBeerShadowMap(shadowPos, pc.uniforms.beerShadowMap);
        // also sample the regular CSM here to get shadows from opaque stuff
        sun_light *= cloudShadow * cloudShadow;

        /* TODO: This probably should be a texture lookup*/
        vec3 trans_increase_over_integration_step = exp(-(medium_extinction * d_int_step));

        vec3 sun_light_integ = (sun_light - sun_light * trans_increase_over_integration_step) / medium_extinction;

        if (medium_extinction.r == 0.0) { sun_light_integ.r = 0.0; }
        if (medium_extinction.g == 0.0) { sun_light_integ.g = 0.0; }
        if (medium_extinction.b == 0.0) { sun_light_integ.b = 0.0; }

        accum_light += accum_transmittance * sun_light_integ;
        accum_transmittance *= trans_increase_over_integration_step;
    }

    transmittance = accum_transmittance;
    return accum_light;
}

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
void main()
{
  const ivec3 gid = ivec3(gl_GlobalInvocationID.xyz);
  const ivec3 outResolution = imageSize(pc.aerialPerspectiveScattering);

  if (any(greaterThanEqual(gid, outResolution)))
  {
    return;
  }

  const vec3 uv                      = (gid + 0.5) / outResolution;
  const float z                      = Sky_EncodeAerialPerspectiveNdcZ(pc.uniforms.sky, uv.z);
  const vec3 rayEndPos               = UnprojectUV_ZO(z, uv.xy, pc.uniforms.sky.ae_world_from_clip);
  const vec3 rayStartPos             = pc.uniforms.cameraPos.xyz;
  const vec3 rayDir                  = normalize(rayEndPos - rayStartPos);
  const vec3 sunDir                  = pc.uniforms.sky.config.sunDir;
  const int sampleCount              = 50;
  const float max_integration_length = distance(rayStartPos, rayEndPos) * M_TO_KM_SCALE;

  vec3 transmittance;
  vec3 scattering = integrate_scattered_luminance(rayStartPos * M_TO_KM_SCALE + vec3(0, pc.uniforms.sky.config.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0),
    rayDir,
    sunDir,
    sampleCount,
    max_integration_length,
    transmittance);

  scattering *= pc.uniforms.sky.config.sunColor * pc.uniforms.sky.config.sunBrightness;

  imageStore(pc.aerialPerspectiveTransmittance, gid, vec4(transmittance, 0));
  imageStore(pc.aerialPerspectiveScattering, gid, vec4(scattering, 0));
}