#ifndef SKY_UTIL_H
#define SKY_UTIL_H

#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
//#include "SkyParams.shared.h"

#define PLANET_RADIUS_OFFSET (0.01f)
#define BASE_HEIGHT_OFFSET (3.0f)
#define M_TO_KM_SCALE (0.001f)

/* Return sqrt clamped to 0 */
float safe_sqrt(float x)
{
    return sqrt(max(0.0f, x));
}

// Building an Orthonormal Basis, Revisited
// http://jcgt.org/published/0006/01/01/
mat3x3 build_orthonormal_basis(vec3 n) {
    vec3 b1;
    vec3 b2;

    if (n.z < 0.0) {
        const float a = 1.0 / (1.0 - n.z);
        const float b = n.x * n.y * a;
        b1 = vec3(1.0 - n.x * n.x * a, -b, n.x);
        b2 = vec3(b, n.y * n.y * a - 1.0, -n.y);
    } else {
        const float a = 1.0 / (1.0 + n.z);
        const float b = -n.x * n.y * a;
        b1 = vec3(1.0 - n.x * n.x * a, b, -n.x);
        b2 = vec3(b, 1.0 - n.y * n.y * a, -n.y);
    }
    return mat3x3(b1, b2, n);
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

float from_subuv_to_unit(float u, float resolution)
{
    return (u - 0.5 / resolution) * (resolution / (resolution - 1.0));
}

float from_unit_to_subuv(float u, float resolution)
{
    return (u + 0.5 / resolution) * (resolution / (resolution + 1.0));
}

/// Moves to the nearest intersection with top of the atmosphere in the direction specified in world_direction
/// @param world_position - current world position -> will be changed to new pos at the top of the atmosphere if there exists such intersection
/// @param world_direction - the direction in which the shift will be done
/// @param atmosphere_bottom - bottom of the atmosphere in km
/// @param atmosphere_top - top of the atmosphere in km

bool move_to_top_atmosphere(inout vec3 world_position, vec3 world_direction, float atmosphere_bottom, float atmosphere_top)
{
    vec3 planet_origin = vec3(0.0, 0.0, 0.0);
    /* Check if the world_position is outside of the atmosphere */
    if (length(world_position) > atmosphere_top)
    {
        float dist_to_top_atmo_intersection = ray_sphere_intersect_nearest(
            world_position, world_direction, planet_origin, atmosphere_top);

        /* No intersection with the atmosphere */
        if (dist_to_top_atmo_intersection == -1.0) { return false; }
        else
        {
            // bias the world position to be slightly inside the sphere
            const float BIAS = uintBitsToFloat(0x3f800040); // uintBitsToFloat(0x3f800040) == 1.00000762939453125
            world_position += world_direction * (dist_to_top_atmo_intersection * BIAS);
            vec3 up_offset = normalize(world_position) * -PLANET_RADIUS_OFFSET;
            world_position += up_offset;
        }
    }
    /* Position is in or at the top of the atmosphere */
    return true;
}

struct TransmittanceParams
{
    float height;
    float zenith_cos_angle;
};

///	Transmittance LUT uses not uniform mapping -> transfer from mapping to texture uv
///	@param parameters
/// @param atmosphere_bottom - bottom radius of the atmosphere in km
/// @param atmosphere_top - top radius of the atmosphere in km
///	@return - uv of the corresponding texel
vec2 transmittance_lut_to_uv(TransmittanceParams parameters, float atmosphere_bottom, float atmosphere_top)
{
    const float H = safe_sqrt(atmosphere_top * atmosphere_top - atmosphere_bottom * atmosphere_bottom);
    const float rho = safe_sqrt(parameters.height * parameters.height - atmosphere_bottom * atmosphere_bottom);

    const float discriminant = parameters.height * parameters.height *
                               (parameters.zenith_cos_angle * parameters.zenith_cos_angle - 1.0) +
                                atmosphere_top * atmosphere_top;
    /* Distance to top atmosphere boundary */
    const float d = max(0.0, (-parameters.height * parameters.zenith_cos_angle + safe_sqrt(discriminant)));

    const float d_min = atmosphere_top - parameters.height;
    const float d_max = rho + H;
    const float mu = (d - d_min) / (d_max - d_min);
    const float r = rho / H;

    return vec2(mu, r);
}

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

struct SkyviewParams
{
    float view_zenith_angle;
    float light_view_angle;
};

/// Get skyview LUT uv from skyview parameters
/// @param intersects_ground - true if ray intersects ground false otherwise
/// @param params - SkyviewParams structure
/// @param atmosphere_bottom - bottom of the atmosphere in km
/// @param atmosphere_top - top of the atmosphere in km
/// @param skyview_dimensions - skyViewLUT dimensions
/// @param view_height - view_height in world coordinates -> distance from planet center
/// @return - uv for the skyview LUT sampling
vec2 skyview_lut_params_to_uv(bool intersects_ground, SkyviewParams params, float atmosphere_bottom, float atmosphere_top, vec2 skyview_dimensions, float view_height)
{
    vec2 uv;
    if(view_height < atmosphere_top)
    {
        float beta = asin(atmosphere_bottom / view_height);
        float zenith_horizon_angle = M_PI - beta;

        if (!intersects_ground)
        {
            float coord = params.view_zenith_angle / zenith_horizon_angle;
            coord = (1.0 - safe_sqrt(1.0 - coord)) / 2.0;
            uv.y = coord;
        }
        else
        {
            float coord = (params.view_zenith_angle - zenith_horizon_angle) / beta;
            coord = (safe_sqrt(coord) + 1.0) / 2.0;
            uv.y = coord;
        }
    } else {
        float beta = asin(atmosphere_top / view_height);
        float zenith_horizon_angle = M_PI - beta;
        float coord = (params.view_zenith_angle - zenith_horizon_angle) / beta;
        coord = safe_sqrt(coord);
        uv.y = coord;
    }
    uv.x = safe_sqrt(params.light_view_angle / M_PI);
    uv = vec2(from_unit_to_subuv(uv.x, skyview_dimensions.x),
              from_unit_to_subuv(uv.y, skyview_dimensions.y));
    return uv;
}

/// Get parameters used for skyview LUT computation from uv coords
/// @param uv - texel uv in the range [0,1]
/// @param atmosphere_bottom - bottom of the atmosphere in km
/// @param atmosphere_top - top of the atmosphere in km
/// @param skyview dimensions
/// @param view_height - view_height in world coordinates -> distance from planet center
/// @return - SkyviewParams structure
SkyviewParams uv_to_skyview_lut_params(vec2 uv, float atmosphere_bottom, float atmosphere_top, vec2 skyview_dimensions, float view_height) 
{
    /* Constrain uvs to valid sub texel range
    (avoid zenith derivative issue making LUT usage visible) */
    uv = vec2(from_subuv_to_unit(uv.x, skyview_dimensions.x),
              from_subuv_to_unit(uv.y, skyview_dimensions.y));

    float view_zenith_angle;
    float light_view_angle;
    if(view_height < atmosphere_top)
    {
        float beta = asin(atmosphere_bottom / view_height);
        float zenith_horizon_angle = M_PI - beta;

        /* Nonuniform mapping near the horizon to avoid artefacts */
        if (uv.y < 0.5)
        {
            float coord = 1.0 - (1.0 - 2.0 * uv.y) * (1.0 - 2.0 * uv.y);
            view_zenith_angle = zenith_horizon_angle * coord;
        }
        else
        {
            float coord = (uv.y * 2.0 - 1.0) * (uv.y * 2.0 - 1.0);
            view_zenith_angle = zenith_horizon_angle + beta * coord;
        }
    } else {
        float beta = asin(atmosphere_top / view_height);
        float zenith_horizon_angle = M_PI - beta;
        float coord = uv.y * uv.y;
        view_zenith_angle = zenith_horizon_angle + beta * coord;
    }
    light_view_angle = (uv.x * uv.x) * M_PI;
    return SkyviewParams(view_zenith_angle, light_view_angle);
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
MediumSample sample_medium(SkyConfig params, vec3 position)
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

vec3 Sky_GetTransmittanceAlongRay(SkyData sky, vec3 ray, vec3 world_position)
{
    const vec3 z_up_pos = world_position.xzy * vec3(1.0, -1.0, 1.0) * M_TO_KM_SCALE + vec3(0.0, 0.0, sky.config.atmosphere_bottom + BASE_HEIGHT_OFFSET);
    const float height = length(z_up_pos);
    
    const vec3 z_up_ray = ray.xzy * vec3(1.0, -1.0, 1.0);

    const float zenith_cos_angle = dot(z_up_ray, normalize(z_up_pos));
    const TransmittanceParams transmittance_lut_params = TransmittanceParams(height, zenith_cos_angle);

    const vec2 transmittance_texture_uv = transmittance_lut_to_uv(
            transmittance_lut_params,
            sky.config.atmosphere_bottom,
            sky.config.atmosphere_top
        );
        
    const vec3 transmittance_to_sun = texture(sky.luts.transmittanceLut, gLinearClampSampler, transmittance_texture_uv).rgb;
    return transmittance_to_sun;
}

vec3 Sky_GetScatteringAlongRay(SkyData sky, vec3 ray, vec3 world_position)
{
    const ivec2 sky_view_texture_size = textureSize(sky.luts.skyViewLut, 0);
    const vec3 z_up_sun_dir = sky.config.sunDir.xzy * vec3(1.0, -1.0, 1.0);
    const vec3 z_up_ray = ray.xzy * vec3(1.0, -1.0, 1.0);
    const vec3 z_up_pos = world_position.xzy * vec3(1.0, -1.0, 1.0) * M_TO_KM_SCALE + vec3(0.0, 0.0, sky.config.atmosphere_bottom + BASE_HEIGHT_OFFSET);

    const float height = length(z_up_pos);
    const mat3x3 basis = build_orthonormal_basis(z_up_pos / height);
    const vec3 rotated_ray = basis * z_up_ray;
    const vec3 sun_direction = basis * z_up_sun_dir;

    const vec3 world_up = vec3(0.0, 0.0, 1.0);
    const float view_zenith_angle = acos(dot(rotated_ray, world_up));
    const float light_view_angle = acos(clamp(dot(
        normalize(vec2(sun_direction.xy)),
        normalize(vec2(rotated_ray.xy))
        ),-1.0, 1.0)
    );

    const float bottom_atmosphere_intersection_distance = ray_sphere_intersect_nearest(
        vec3(0.0, 0.0, height),
        rotated_ray,
        vec3(0.0),
        sky.config.atmosphere_bottom
    );

    const float top_atmosphere_intersection_distance = ray_sphere_intersect_nearest(
        vec3(0.0, 0.0, height),
        rotated_ray,
        vec3(0.0),
        sky.config.atmosphere_top
    );

    const bool intersects_ground = bottom_atmosphere_intersection_distance >= 0.0;
    const bool intersects_sky = top_atmosphere_intersection_distance >= 0.0;

    vec3 atmosphere_transmittance = vec3(1.0);
    vec3 atmosphere_scattering_illuminance = vec3(0.0);

    if(intersects_sky)
    {
        vec2 sky_uv = skyview_lut_params_to_uv(
            intersects_ground,
            SkyviewParams(view_zenith_angle, light_view_angle),
            sky.config.atmosphere_bottom,
            sky.config.atmosphere_top,
            sky_view_texture_size,
            height
        );

        const vec4 unitless_atmosphere_illuminance_mult = texture(sky.luts.skyViewLut, gLinearClampSampler, sky_uv).rgba;
        const vec3 unitless_atmosphere_illuminance = unitless_atmosphere_illuminance_mult.rgb * unitless_atmosphere_illuminance_mult.a;
        const vec3 sun_color_weighed_atmosphere_illuminance = sky.config.sunColor * unitless_atmosphere_illuminance * 0.5;
        atmosphere_scattering_illuminance = sun_color_weighed_atmosphere_illuminance * sky.config.sunBrightness;
        //atmosphere_scattering_illuminance = vec3(sky_uv.y, 0.0f, 0.0f);
    }

    return atmosphere_scattering_illuminance;
}

/* ============================= PHASE FUNCTIONS ============================ */
float cornette_shanks_mie_phase_function(float g, float cos_theta)
{
    float k = 3.0 / (8.0 * M_PI) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + cos_theta * cos_theta) / pow(1.0 + g * g - 2.0 * g * -cos_theta, 1.5);
}
float klein_nishina_phase(float cos_theta, float e)
{
    const float TAU = 2 * M_PI;
    return e / (TAU * (e * (1.0 - cos_theta) + 1.0) * log(2.0 * e + 1.0));
}

float rayleigh_phase(float cos_theta)
{
    float factor = 3.0 / (16.0 * M_PI);
    return factor * (1.0 + cos_theta * cos_theta);
}
// https://research.nvidia.com/labs/rtr/approximate-mie/publications/approximate-mie.pdf
float draine_phase(float alpha, float g, float cos_theta)
{
    return (1.0 / (4.0 * M_PI)) *
           ((1.0 - (g * g)) / pow((1.0 + (g * g) - (2.0 * g * cos_theta)), 3.0 / 2.0)) *
           ((1.0 + (alpha * cos_theta * cos_theta)) / (1.0 + (alpha * (1.0 / 3.0) * (1.0 + (2.0 * g * g)))));
}

float hg_draine_phase(float cos_theta, float diameter)
{
    const float g_hg = exp(-(0.0990567 / (diameter - 1.67154)));
    const float g_d = exp(-(2.20679 / (diameter + 3.91029)) - 0.428934);
    const float alpha = exp(3.62489 - (0.599085 / (diameter + 5.52825)));
    const float w_d = exp(-(0.599085 / (diameter - 0.641583)) - 0.665888);
    return (1 - w_d) * draine_phase(0, g_hg, cos_theta) + w_d * draine_phase(alpha, g_d, cos_theta);
}
/* ========================================================================== */
#endif //SKY_UTIL_H