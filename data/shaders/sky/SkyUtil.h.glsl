#ifndef SKY_UTIL_H
#define SKY_UTIL_H

#include "../GlobalUniforms.h.glsl"

/* Return sqrt clamped to 0 */
float safe_sqrt(float x)
{
    return sqrt(max(0.0f, x));
}

/// Return distance of the first daxa_i32ersection between ray and sphere
/// @param r0 - ray origin
/// @param rd - normalized ray direction
/// @param s0 - sphere center
/// @param sR - sphere radius
/// @return return distance of daxa_i32ersection or -1.0 if there is no daxa_i32ersection
float ray_sphere_intersect_nearest(vec3 r0, vec3 rd, vec3 s0, float sR)
{
    float a = dot(rd, rd);
    vec3 s0_r0 = r0 - s0;
    float b = 2.0 * dot(rd, s0_r0);
    float c = dot(s0_r0, s0_r0) - (sR * sR);
    float delta = b * b - 4.0 * a * c;
    if (delta < 0.0 || a == 0.0)
    {
        return -1.0;
    }
    float sol0 = (-b - safe_sqrt(delta)) / (2.0 * a);
    float sol1 = (-b + safe_sqrt(delta)) / (2.0 * a);
    if (sol0 < 0.0 && sol1 < 0.0)
    {
        return -1.0;
    }
    if (sol0 < 0.0)
    {
        return max(0.0, sol1);
    }
    else if (sol1 < 0.0)
    {
        return max(0.0, sol0);
    }
    return max(0.0, min(sol0, sol1));
}

struct TransmittanceParams
{
    float height;
    float zenith_cos_angle;
};

/// Transmittance LUT uses not uniform mapping -> transfer from uv to this mapping
/// @param uv - uv in the range [0,1]
/// @param atmosphere_bottom - bottom radius of the atmosphere in km
/// @param atmosphere_top - top radius of the atmosphere in km
/// @return - TransmittanceParams structure
TransmittanceParams uv_to_transmittance_lut_params(vec2 uv, float atmosphere_bottom, float atmosphere_top)
{
    TransmittanceParams params;
    float H = safe_sqrt(atmosphere_top * atmosphere_top - atmosphere_bottom * atmosphere_bottom.x);

    float rho = H * uv.y;
    params.height = safe_sqrt(rho * rho + atmosphere_bottom * atmosphere_bottom);

    float d_min = atmosphere_top - params.height;
    float d_max = rho + H;
    float d = d_min + uv.x * (d_max - d_min);

    params.zenith_cos_angle = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * params.height * d);
    params.zenith_cos_angle = clamp(params.zenith_cos_angle, -1.0, 1.0);

    return params;
}

float sample_profile_density(DensityProfileLayer[PROFILE_LAYER_COUNT] profile, float above_surface_height)
{
    int layer_index = -1;
    float curr_layer_end = 0.0;
    for(int i = 0; i < PROFILE_LAYER_COUNT; i++)
    {
        curr_layer_end += profile[i].layer_width;
        if(above_surface_height < curr_layer_end)
        {
            layer_index = i;
            break;
        }
    }
    // Not in any layer
    if(layer_index == -1) { return 0.0; }
    return profile[layer_index].exp_term * exp(profile[layer_index].exp_scale * above_surface_height) +
           profile[layer_index].lin_term * above_surface_height +
           profile[layer_index].const_term;
}

struct MediumSample
{
    vec3 mie_scattering;
    vec3 rayleigh_scattering;
    vec3 medium_extinction;
};
/// @param params - buffer reference to the atmosphere parameters buffer
/// @param position - position in the world where the sample is to be taken
/// @return atmosphere extinction at the desired position
MediumSample sample_medium(SkyParameters params, vec3 position)
{
    const float above_surface_height = length(position) - params.atmosphere_bottom;

    const float density_mie = max(0.0, sample_profile_density(params.mie_density, above_surface_height));
    const float density_ray = max(0.0, sample_profile_density(params.rayleigh_density, above_surface_height));
    const float density_ozo = max(0.0, sample_profile_density(params.absorption_density, above_surface_height));

    const vec3 mie_extinction = params.mie_extinction * density_mie;
    const vec3 ray_extinction = params.rayleigh_scattering * density_ray;
    const vec3 ozo_extinction = params.absorption_extinction * density_ozo;
    const vec3 medium_extinction = mie_extinction + ray_extinction + ozo_extinction;

    const vec3 mie_scattering = params.mie_scattering * density_mie;
    const vec3 ray_scattering = params.rayleigh_scattering * density_ray;

    return MediumSample(mie_scattering, ray_scattering, medium_extinction);
}
#endif //SKY_UTIL_H