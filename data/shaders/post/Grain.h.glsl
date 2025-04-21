// Courtesy of Sam
// https://sam-izdat.github.io/lsl-editor/get-gist?q=07162faa32528ca0098328758a181d5b
#ifndef GRAIN_H
#define GRAIN_H

#include "../Hash.h.glsl"

vec3 FilmAlign(vec3 im, mat3 mat_in, float iso_model, float mid_gray_model, float exposure_bias,
              float softness_low, float softness_high, float eps);
vec3 FilmExpose(vec3 fexp);
vec3 FilmProject(
  vec3 trans,
  vec3 proj_cmy,
  float proj_intensity,
  int apply_grain,
  float zoom,
  uvec2 size,
  float clumping,
  int seed,
  vec2 fragCoordXy);
vec3 FilmPrint(vec3 col);
vec3 FilmFinalize(vec3 im, mat3 mat_out);
float xy_to_rand_seed(ivec2 xy, int seed);
float beta_approx_mixture(float a, float b, ivec2 xy, int idx);

vec3 grain_ColorPass(
  in int sim,
  in int show_neg,
  in int apply_grain,
  in float exp_bias,
  in float zoom,
  in float clumping,
  in int animate,
  in int frame_idx,
  in uvec2 size,
  vec2 fragCoordXy,
  in vec3 in_color)
{
  float eps = 1e-6;
  int seed = animate > 0 ? frame_idx : 1;
  vec2 uv = fragCoordXy / vec2(min(size.x, size.y));

  vec3 incol = in_color;

  const float iso_model = 100.0;
  const float mid_gray_film = 0.18;
  const mat3 mat_in = mat3(
    0.79226088, 0.06094367, 0.04125177,
    0.09362342, 0.8905935,  0.0932336,
    0.04533828, 0.05952694, 0.88673362);
  const mat3 mat_out = mat3(
     1.27501146, -0.08387444, -0.05049602,
    -0.12811011,  1.13921999, -0.11382116,
    -0.05658992, -0.07218802,  1.13795781);

  vec3 proj_cmy = vec3(0.5, 0.5, 0.5);
  float proj_intensity = 1.0;

  float soft_low = 0.0;
  float soft_high = 0.0;
  if (sim != 0)
  {
    vec3 exposure = FilmAlign(incol.rgb,
      mat_in,
      iso_model,
      mid_gray_film,
      exp_bias,
      soft_low,
      soft_high,
      eps);
    vec3 transmittance = FilmExpose(exposure);
    vec3 projection = FilmProject(
      transmittance,
      proj_cmy,
      proj_intensity,
      apply_grain,
      zoom,
      size,
      clumping, 
      seed,
      fragCoordXy);
    if (show_neg != 0)
    {
      return projection;
    }
    else
    {
      projection = FilmPrint(projection);
      vec3 film = FilmFinalize(projection, mat_out);
      return film;
    }
  }
  else
  {
    return incol.rgb * pow(2.0, exp_bias);
  }
}

struct FilmProfile
{
  float c_ct_compress;
  float c_iso_film;
  float c_mid_gray_film;
  vec3 c_prn_cmy_bal;
  float c_prn_exp_bias;
  vec2 c_wp_film;
  vec3 d_cap_cdl_o;
  vec3 d_cap_cdl_p;
  vec3 d_cap_cdl_s;
  vec3 d_cap_mlp_b0;
  vec3 d_cap_mlp_b1;
  vec3 d_cap_mlp_b2;
  mat3 d_cap_mlp_m0;
  mat3 d_cap_mlp_m1;
  mat3 d_cap_mlp_m2;
  vec2 d_cap_plateau_b;
  vec2 d_cap_plateau_g;
  vec2 d_cap_plateau_r;
  vec2 d_cap_shoulder_b;
  vec2 d_cap_shoulder_g;
  vec2 d_cap_shoulder_r;
  vec2 d_cap_toe_b;
  vec2 d_cap_toe_g;
  vec2 d_cap_toe_r;
  vec3 d_prn_cmy_bal;
  float d_prn_exp_bias;
  vec2 d_prn_plateau_b;
  vec2 d_prn_plateau_g;
  vec2 d_prn_plateau_r;
  vec2 d_prn_shoulder_b;
  vec2 d_prn_shoulder_g;
  vec2 d_prn_shoulder_r;
  vec2 d_prn_toe_b;
  vec2 d_prn_toe_g;
  vec2 d_prn_toe_r;
  vec3 d_ptb_rmsgc_bc_b;
  vec3 d_ptb_rmsgc_bc_g;
  vec3 d_ptb_rmsgc_bc_r;
  vec3 d_ptb_rmsgc_bg_b;
  vec3 d_ptb_rmsgc_bg_g;
  vec3 d_ptb_rmsgc_bg_r;
  vec3 d_ptb_rmsgc_bw_b;
  vec3 d_ptb_rmsgc_bw_g;
  vec3 d_ptb_rmsgc_bw_r;
  float f_cap_d_max;
  float f_exp_max;
  float f_exp_min;
  float f_prn_d_max;
  float f_ptb_rmsgc_bn;
};

FilmProfile prof = FilmProfile(
    /* c_ct_compress    */ 0.20000000,
    /* c_iso_film       */ 400.00000000,
    /* c_mid_gray_film  */ 0.18000000,
    /* c_prn_cmy_bal    */ vec3(0.00000000, 0.00000000, 0.00000000),
    /* c_prn_exp_bias   */ 0.00000000,
    /* c_wp_film        */ vec2(0.33240000, 0.34740000),
    /* d_cap_cdl_o      */ vec3(-0.10900000, -0.17659999, -0.15060000),
    /* d_cap_cdl_p      */ vec3(0.84130001, 0.79369998, 1.54929996),
    /* d_cap_cdl_s      */ vec3(1.30449998, 0.99409997, 0.84560001),
    /* d_cap_mlp_b0     */ vec3(-0.73689997, 0.00440000, -0.23130000),
    /* d_cap_mlp_b1     */ vec3(0.41610000, -0.07580000, 0.27350000),
    /* d_cap_mlp_b2     */ vec3(-0.02440000, -0.04230000, 0.01090000),
    /* d_cap_mlp_m0     */ mat3(1.12469995, 0.45510000, 0.90380001, 1.07210004, 0.81800002, -0.19149999, 0.59240001, 1.36469996, 0.49149999),
    /* d_cap_mlp_m1     */ mat3(1.95480001, -0.04120000, 0.27599999, 0.75680000, 0.91310000, 0.23230000, -0.34760001, 0.34670001, 1.57070005),
    /* d_cap_mlp_m2     */ mat3(-0.04130000, -0.59020001, -0.14920001, 0.91409999, 0.93349999, -0.23180000, -0.58999997, 0.21320000, 0.26069999),
    /* d_cap_plateau_b  */ vec2(0.31500000, 0.89600003),
    /* d_cap_plateau_g  */ vec2(0.22800000, 0.82900000),
    /* d_cap_plateau_r  */ vec2(0.05600000, 0.68000001),
    /* d_cap_shoulder_b */ vec2(0.81699997, 0.84700000),
    /* d_cap_shoulder_g */ vec2(0.79699999, 0.80800003),
    /* d_cap_shoulder_r */ vec2(0.77100003, 0.77999997),
    /* d_cap_toe_b      */ vec2(0.28700000, 0.07800000),
    /* d_cap_toe_g      */ vec2(0.30399999, 0.08100000),
    /* d_cap_toe_r      */ vec2(0.33399999, 0.10500000),
    /* d_prn_cmy_bal    */ vec3(-0.42546248, 0.00000000, -0.13860039),
    /* d_prn_exp_bias   */ 1.92890000,
    /* d_prn_plateau_b  */ vec2(0.00000000, 0.81699997),
    /* d_prn_plateau_g  */ vec2(0.00000000, 0.81699997),
    /* d_prn_plateau_r  */ vec2(0.00000000, 0.81699997),
    /* d_prn_shoulder_b */ vec2(0.42399999, 0.71200001),
    /* d_prn_shoulder_g */ vec2(0.49500000, 0.71799999),
    /* d_prn_shoulder_r */ vec2(0.56500000, 0.75400001),
    /* d_prn_toe_b      */ vec2(0.36600000, 0.40549999),
    /* d_prn_toe_g      */ vec2(0.41100001, 0.32699999),
    /* d_prn_toe_r      */ vec2(0.47200000, 0.29100001),
    /* d_ptb_rmsgc_bc_b */ vec3(0.21120000, 0.27160001, 0.80710000),
    /* d_ptb_rmsgc_bc_g */ vec3(0.25990000, 0.47400001, 0.95349997),
    /* d_ptb_rmsgc_bc_r */ vec3(0.26670000, 0.51279998, 0.64429998),
    /* d_ptb_rmsgc_bg_b */ vec3(0.06990000, 0.24680001, 0.22280000),
    /* d_ptb_rmsgc_bg_g */ vec3(0.09400000, 0.19930001, 0.09050000),
    /* d_ptb_rmsgc_bg_r */ vec3(0.10760000, 0.10300000, 0.11110000),
    /* d_ptb_rmsgc_bw_b */ vec3(0.05910000, 0.37500000, 0.42429999),
    /* d_ptb_rmsgc_bw_g */ vec3(0.07000000, 0.48629999, 0.29159999),
    /* d_ptb_rmsgc_bw_r */ vec3(0.07060000, 0.28610000, 1.29799998),
    /* f_cap_d_max      */ 10.00000000,
    /* f_exp_max        */ 8.00000000,
    /* f_exp_min        */ -8.00000000,
    /* f_prn_d_max      */ 16.60964000,
    /* f_ptb_rmsgc_bn   */ 3.00000000
);

const float def_eps = 1e-6;

vec3 SoftClip(vec3 v, float exp_min, float exp_max, float softness_low, float softness_high, float eps)
{
  float r_low = exp_min + softness_low;
  float r_high = exp_max - softness_high;

  vec3 toe = mix(v, v + pow(r_low - v, vec3(2.0)) / max(r_high - v + softness_low, vec3(eps)), lessThan(v, vec3(r_low)));
  vec3 shoulder = mix(toe, v - pow(v - r_high, vec3(2.0)) / max(v - r_high + softness_high, vec3(eps)), greaterThan(v, vec3(r_high)));

  return mix(shoulder, toe, lessThan(v, vec3(0.0)));
}

float SoftClip(float v, float exp_min, float exp_max, float softness_low, float softness_high, float eps)
{
  float r_low = exp_min + softness_low;
  float r_high = exp_max - softness_high;
  float toe = (v < r_low) 
    ? v + pow(r_low - v, 2.0) / max(r_high - v + softness_low, eps)
    : v;
  float shoulder = (v > r_high) 
    ? v - pow(v - r_high, 2.0) / max(v - r_high + softness_high, eps)
    : v;
  return (v < 0.0) ? toe : shoulder;
}

float SuperSigmoid(float v, vec2 toe, vec2 shoulder, vec2 plateaus, float eps)
{
  v = clamp(v, eps, 1.0 - eps);
  toe = clamp(toe, eps, 1.0 - eps);
  shoulder = clamp(shoulder, eps, 1.0 - eps);

  float toe_x = toe.x;
  float toe_y = toe.y + eps;
  float shoulder_x = shoulder.x;
  float shoulder_y = shoulder.y - eps;

  float slope = (shoulder_y - toe_y) / (shoulder_x - toe_x);
  float toe_section = toe_y * pow(clamp(v / toe_x, eps, 1.0), slope * toe_x / toe_y);
  float linear_section = slope * (v - toe_x) + toe_y;

  float shoulder_inv_x = 1.0 - shoulder_x;
  float shoulder_inv_y = 1.0 - shoulder_y;
  float shoulder_section = 1.0 - pow(clamp((1.0 - v) / shoulder_inv_x, eps, 1.0), slope * shoulder_inv_x / shoulder_inv_y) * shoulder_inv_y;

  float result = (v < toe_x) ? toe_section : ((v < shoulder_x) ? linear_section : shoulder_section);
  return clamp(mix(plateaus.x, plateaus.y, result), 0.0, 1.0);
}

const int NUM_BANDS = 3;

float BandScale(float v, int num_bands, float band_centers[NUM_BANDS], float band_widths[NUM_BANDS], float band_gains[NUM_BANDS])
{
  float total = 0.0;
  for (int i = 0; i < num_bands; ++i)
  {
    float dist = (v - band_centers[i]) / band_widths[i];
    float weight = exp(-dist * dist);
    total += weight * band_gains[i];
  }
    return clamp(total, 0.0, 1.0);
}

vec3 Softsign(vec3 x)
{
  return x / (1.0 + abs(x));
}

float RemapTo01(float x, float min_val, float max_val)
{
  return clamp((x - min_val) / (max_val - min_val), 0.0, 1.0);
}

vec3 RemapTo01(vec3 x, float min_val, float max_val)
{
  return clamp((x - min_val) / (max_val - min_val), 0.0, 1.0);
}

vec3 FilmAlign(vec3 im, mat3 mat_in, float iso_model, float mid_gray_model, float exposure_bias,
              float softness_low, float softness_high, float eps)
{
  float iso_scale = (iso_model == 0.0 || prof.c_iso_film == iso_model) ? 1.0 : prof.c_iso_film / iso_model;
  float gray_delta = (mid_gray_model == prof.c_mid_gray_film) ? 0.0 :
                    log2(mid_gray_model) - log2(prof.c_mid_gray_film);

  vec3 im_scene = mat_in * im;
  vec3 im_scaled = max(im_scene * iso_scale, vec3(eps));
  vec3 exp_log = log2(im_scaled) + exposure_bias - gray_delta;

  vec3 exp_clipped = exp_log;
  if (softness_low > 0.0 || softness_high > 0.0)
  {
    exp_clipped = SoftClip(exp_clipped, prof.f_exp_min, prof.f_exp_max, softness_low, softness_high, def_eps);
  }
  else
  {
    exp_clipped = clamp(exp_clipped, prof.f_exp_min, prof.f_exp_max);
  }

  return RemapTo01(exp_clipped, prof.f_exp_min, prof.f_exp_max);
}

vec3 FilmExpose(vec3 fexp)
{
  vec3 resp;
  resp.r = SuperSigmoid(fexp.r, prof.d_cap_toe_r, prof.d_cap_shoulder_r, prof.d_cap_plateau_r, def_eps);
  resp.g = SuperSigmoid(fexp.g, prof.d_cap_toe_g, prof.d_cap_shoulder_g, prof.d_cap_plateau_g, def_eps);
  resp.b = SuperSigmoid(fexp.b, prof.d_cap_toe_b, prof.d_cap_shoulder_b, prof.d_cap_plateau_b, def_eps);

  vec3 r_att = pow(resp, prof.d_cap_cdl_p) * prof.d_cap_cdl_s + prof.d_cap_cdl_o;

  vec3 x0 = prof.d_cap_mlp_m0 * r_att + prof.d_cap_mlp_b0;
  vec3 x1 = prof.d_cap_mlp_m1 * Softsign(x0) + prof.d_cap_mlp_b1;
  vec3 x2 = prof.d_cap_mlp_m2 * Softsign(x1) + prof.d_cap_mlp_b2;

  resp = max(resp + x2, vec3(0.0));
  return pow(vec3(2.0), -resp * prof.f_cap_d_max);
}

vec3 Granulate(vec3 trans, float zoom, uvec2 size, float clumping, int seed, vec2 fragCoordXy)
{
  float eps = 1e-6;
  vec3 opacity = 1.0 - trans;

  float S = 8000.0 / pow(2.0, 2.0 * (zoom - 1.0));
  float noise = xy_to_rand_seed(ivec2(fragCoordXy), seed);
  float step_size = (clumping * 25.0) / sqrt(S);
  
  vec2 uv = fragCoordXy / vec2(min(size.x, size.y));
  vec2 snapped_uv = (clumping > 0.0 && zoom > 1.0)
    ? ceil(max(vec2(eps), floor(fragCoordXy / step_size + noise)) * step_size)
    : fragCoordXy;

  vec3 alpha = max(vec3(eps), S * opacity);
  vec3 beta_val = max(vec3(eps), S - S * opacity);

  vec3 approx = vec3(
    beta_approx_mixture(alpha.r, beta_val.r, ivec2(snapped_uv), seed),
    beta_approx_mixture(alpha.g, beta_val.g, ivec2(snapped_uv), seed+1),
    beta_approx_mixture(alpha.b, beta_val.b, ivec2(snapped_uv), seed+2)
  );
  return 1. - approx;
}

const vec3 cyan = vec3(0.0, 1.0, 1.0);
const vec3 magenta = vec3(1.0, 0.0, 1.0);
const vec3 yellow = vec3(1.0, 1.0, 0.0);

vec3 FilmProject(
  vec3 trans,
  vec3 proj_cmy,
  float proj_intensity,
  int apply_grain,
  float zoom,
  uvec2 size,
  float clumping,
  int seed,
  vec2 fragCoordXy)
{
  vec3 proj_light = cyan * proj_cmy.r + magenta * proj_cmy.g + yellow * proj_cmy.b;
  if (apply_grain != 0)
  {
    trans = Granulate(trans, zoom, size, clumping, seed, fragCoordXy);
  }
  return trans * proj_light;
}

vec3 FilmPrint(vec3 col)
{
  vec3 col_log = log2(max(col, vec3(1e-6)))
                + prof.c_prn_exp_bias + prof.c_prn_cmy_bal
                + prof.d_prn_exp_bias + prof.d_prn_cmy_bal;

  vec3 exp_clipped = clamp(col_log, prof.f_exp_min, prof.f_exp_max);
  vec3 exp_final = RemapTo01(exp_clipped, prof.f_exp_min, prof.f_exp_max);

  vec3 resp;
  resp.r = SuperSigmoid(exp_final.r, prof.d_prn_toe_r, prof.d_prn_shoulder_r, prof.d_prn_plateau_r, def_eps);
  resp.g = SuperSigmoid(exp_final.g, prof.d_prn_toe_g, prof.d_prn_shoulder_g, prof.d_prn_plateau_g, def_eps);
  resp.b = SuperSigmoid(exp_final.b, prof.d_prn_toe_b, prof.d_prn_shoulder_b, prof.d_prn_plateau_b, def_eps);

  return pow(vec3(2.0), -resp * prof.f_prn_d_max);
}

vec3 FilmFinalize(vec3 im, mat3 mat_out)
{
  return max(mat_out * im, vec3(0.0));
}

int hash(int x)
{    
  x ^= x >> 15;
  x ^= (x * x) | 1;
  x ^= x >> 17;
  x *= 0x9E3779B9;
  x ^= x >> 13;
  return x;
}

float xy_to_rand_seed(ivec2 xy, int seed)
{
  const float GR = 1.61803398874989484820459;
  vec2 xyf = vec2(xy) * GR;
  float seedf = fract(abs(float(seed%50)+GR)*GR);
  float r = fract(abs(tan(distance(xyf*GR, xyf)*seedf))*xyf.x);
  return clamp(r, 1e-7, 1.0 - 1e-7);
}

float beta_approx_mixture(float a, float b, ivec2 xy, int idx)
{
  float w_unimodal = max(0.0, a - 1.0 + 1.0) * max(0.0, b - 1.0 + 1.0);
  float w_bimodal = max(0.0, 1.0 - a + 2.0) * max(0.0, 1.0 - b + 2.0);
  float w_power_a = max(0.0, 1.0 - a + 1.0) * max(0.0, b - 1.0);
  float w_power_b = max(0.0, a - 1.0) * max(0.0, 1.0 - b + 1.0);
  float w_uniform = exp(-(abs(a - 1.0) + abs(b - 1.0))) * pow(min(1.0, min(a, b)), 2.0) * 5.0;

  float sum = w_uniform + w_unimodal + w_power_a + w_power_b + w_bimodal;
  float u1 = xy_to_rand_seed(xy, idx++);
  float u2 = xy_to_rand_seed(xy, idx++);
  float u3 = xy_to_rand_seed(xy, idx++);
  float u4 = xy_to_rand_seed(xy, idx++);

  float u = u3;

  if (u < w_uniform/sum)
  {
    return u4;
  }
  u -= w_uniform/sum;
  if (u < w_unimodal/sum)
  {
    float sum_ab = a + b;
    float mu = a / sum_ab;
    float sigma = sqrt((a * b) / (sum_ab * sum_ab * (sum_ab + 1.0)));
    float bm = sqrt(-2.0 * log(u1)) * cos(6.28318530718 * u2);
    return abs(mod((mu + sigma * bm) * 0.5 + 0.5, 1.0) * 2.0 - 1.0);
  }
  u -= w_unimodal/sum;
  if (u < w_power_a/sum)
  {
    float ratio = min(a, b) / max(a, b);
    return 1.0 - pow(u1, ratio);
  }
  u -= w_power_a/sum;
  if (u < w_power_b/sum)
  {
    float ratio = min(a, b) / max(a, b);
    return pow(u1, ratio);
  }
  float x = pow(u1, 1.0 / a);
  float y = pow(u2, 1.0 / b);
  return clamp(x / (x + y), 0.0, 1.0);
}
#endif // GRAIN_H
