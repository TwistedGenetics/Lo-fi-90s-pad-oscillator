#pragma once
/*
 * Dusty Pads - lo-fi 90s pad oscillator (DSP core)
 *
 * One engine, two builds:
 *   DustyPads1  = PadEngine<float>  one voice  (NTS-1 mkII, scalar Cortex-M7)
 *   DustyPads4  = PadEngine<v4f>    four voices, one per SIMD lane
 *                                   (microKORG 2: NEON on the Cortex-A7)
 * Both run exactly the same code, so the two synths sound the same.
 *
 * Per voice:
 *
 *   two ensemble copies (detuned, drifting)
 *     each: STRINGS saw | ANALOG saw + PWM pulse | ORGAN drawbar sines
 *           | CHOIR narrow pulse | GLASS 2:1 phase-mod sine
 *           Knob A (TIMBRE) crossfades between neighbours
 *   CHOIR -> fixed "ah" formant filter (three band-passes)
 *   TONE tilt (one-pole split, dark <-> bright)
 *   + HISS (high-passed noise, "in the sample")
 *   sampler emulation: sample-and-hold rate reduction + bit reduction,
 *     RESAMPLE (-12 / -24: sampled high, played low) and KEY TRACK
 *     (the hold rate follows the note, like a variable-clock sampler)
 *     -> two-pole output smoothing, crossfaded in as GRIT comes up
 *   DC blocker, output level, hard ceiling
 *
 * CPU rules (the microKORG 2 runs up to 8 voices of this):
 *   - everything slow (pitch, drift, detune, weights, filter coefficients,
 *     crusher settings) is worked out once per buffer per voice; the sample
 *     loop only steps linear ramps
 *   - the per-sample work runs in passes, short loops over the buffer that
 *     each fit in the NEON registers (one big loop spilled to the stack on
 *     the Cortex-A7 and crackled: v1.0.0)
 *   - no `?:` min/max patterns or stored compare masks in the sample loops:
 *     GCC 10 turns those into four scalar compares on ARM
 *   - saws are DPW (differentiated parabola), sines are refined parabolas,
 *     no tables, no divides in the sample loop, no memory allocation
 *   - a timbre whose weight is zero on every lane is not computed at all,
 *     and the second ensemble copy is skipped while ENSEMBLE is 0
 */

#include <stdint.h>
#include <string.h>

namespace dpads {

typedef float v4f __attribute__((vector_size(16)));
typedef int v4i __attribute__((vector_size(16)));  // type of a float-vector comparison
typedef unsigned int v4u __attribute__((vector_size(16)));

#define DP_INLINE inline __attribute__((always_inline))

constexpr float kSR = 48000.f;
constexpr float kInvSR = 1.f / 48000.f;
constexpr float kW0C4 = 261.6255653f / 48000.f;  // middle C (MIDI 60), cycles per sample

// ---------------------------------------------------------------------------
// Scalar helpers (control rate)
// ---------------------------------------------------------------------------

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float maxf(float a, float b) { return a > b ? a : b; }

// 2^x, about 1e-6 relative error over the range used here
static inline float exp2f_fast(float x) {
  x = clampf(x, -60.f, 60.f);
  const float fl = (float)(int32_t)(x < 0.f ? x - 1.f : x);
  const float xi = (x == fl + 1.f) ? x : fl;  // exact integers stay exact
  const float f = x - xi;
  const float p = 1.f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
  union { float f; int32_t i; } u;
  u.i = ((int32_t)xi + 127) << 23;
  return p * u.f;
}

// 1 / sqrt(q) for 0.5 <= q <= 1, Newton steps (no libm: keeps the NTS-1 unit
// free of errno / abort / malloc pulled in by sqrtf)
static inline float rsqrtHalfToOne(float q) {
  q = clampf(q, 0.5f, 1.f);
  float y = 1.5f - 0.5f * q;
  for (int i = 0; i < 4; ++i) y = y * (1.5f - 0.5f * q * y * y);
  return y;
}

// one-pole low-pass coefficient for a cutoff in Hz
static inline float onePoleCoef(float hz) {
  const float x = 6.2831853f * hz * kInvSR;
  return 1.f - exp2f_fast(-1.44269504f * x);
}

// tan(x) for 0 <= x < 0.3 (fixed formant filters)
static inline float tanSmall(float x) {
  const float x2 = x * x;
  return x * (1.f + x2 * (0.33333333f + x2 * (0.13333333f + x2 * 0.05396825f)));
}

struct Rng {
  uint32_t s;
  void seed(uint32_t v) { s = v ? v : 0x6D2B79F5u; }
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  float u01() { return (next() >> 8) * (1.f / 16777216.f); }
  float u11() { return u01() * 2.f - 1.f; }
};

// ---------------------------------------------------------------------------
// Lane helpers: the same names for float (1 lane) and v4f (4 lanes)
// ---------------------------------------------------------------------------

template <class V> struct LaneInfo;
template <> struct LaneInfo<float> { enum { N = 1 }; typedef uint32_t U; };
template <> struct LaneInfo<v4f> { enum { N = 4 }; typedef v4u U; };

static DP_INLINE float lget(float x, int) { return x; }
static DP_INLINE float lget(v4f x, int j) { return x[j]; }
static DP_INLINE void lset(float &x, int, float v) { x = v; }
static DP_INLINE void lset(v4f &x, int j, float v) { x[j] = v; }

template <class V> static DP_INLINE V splat(float v);
template <> DP_INLINE float splat<float>(float v) { return v; }
template <> DP_INLINE v4f splat<v4f>(float v) { return (v4f){v, v, v, v}; }

static DP_INLINE float sel(bool m, float a, float b) { return m ? a : b; }
static DP_INLINE v4f sel(v4i m, v4f a, v4f b) { return m ? a : b; }

static DP_INLINE float absv(float x) { return __builtin_fabsf(x); }
static DP_INLINE v4f absv(v4f x) { return (v4f)((v4u)x & 0x7FFFFFFFu); }

// truncate toward zero
static DP_INLINE float truncv(float x) { return (float)(int32_t)x; }
static DP_INLINE v4f truncv(v4f x) {
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 9)
  return __builtin_convertvector(__builtin_convertvector(x, v4i), v4f);  // vcvt on NEON
#else
  v4f r;
  for (int j = 0; j < 4; ++j) r[j] = (float)(int32_t)x[j];
  return r;
#endif
}

// uniform noise in [-1, 1) from 32 random bits (mantissa trick, no int->float)
static DP_INLINE float bitsToNoise(uint32_t u) {
  u = (u >> 9) | 0x3F800000u;
  float f;
  memcpy(&f, &u, 4);
  return 2.f * f - 3.f;
}
static DP_INLINE v4f bitsToNoise(v4u u) {
  u = (u >> 9) | 0x3F800000u;
  v4f f;
  memcpy(&f, &u, 16);
  return 2.f * f - 3.f;
}

// clamp to [-1, 1] with two abs (no compares): 0.5 * (|x + 1| - |x - 1|)
template <class V> static DP_INLINE V clamp1(V x) { return 0.5f * (absv(x + 1.f) - absv(x - 1.f)); }

template <class V> static DP_INLINE V clampv(V x, float lo, float hi) {
  x = sel(x < lo, splat<V>(lo), x);
  return sel(x > hi, splat<V>(hi), x);
}
// wrap into [0, 1) for -1 <= t < 2
template <class V> static DP_INLINE V wrap1(V t) {
  t = sel(t >= 1.f, t - 1.f, t);
  return sel(t < 0.f, t + 1.f, t);
}
// fractional part for t >= 0
template <class V> static DP_INLINE V fracp(V t) { return t - truncv(t); }

// sin(2 pi t) for t in [0, 1): refined parabola (~0.1 % error)
template <class V> static DP_INLINE V sin2pi(V t) {
  const V y = 2.f * t - 1.f;
  const V s0 = 4.f * y - 4.f * y * absv(y);
  const V s = 0.225f * (s0 * absv(s0) - s0) + s0;
  return -s;
}

// DPW saw: rising saw from the parabola's first difference.
// prev holds the last parabola value; inv4w = 1 / (4 * phase increment).
template <class V> static DP_INLINE V dpwSaw(V t, V &prev, V inv4w) {
  const V s = 2.f * t - 1.f;
  const V p = s * s;
  const V y = (p - prev) * inv4w;
  prev = p;
  return y;
}
template <class V> static DP_INLINE V parab(V t) {
  const V s = 2.f * t - 1.f;
  return s * s;
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

struct Params {
  enum Id {
    kTimbre = 0,  // 0..1023  STRINGS > ANALOG > ORGAN > CHOIR > GLASS
    kGrit,        // 0..1023  clean > 12-bit / 22 kHz > 8-bit / 6 kHz
    kEnsemble,    // 0..100 % detuned second copy, 0..25 cents
    kDrift,       // 0..100 % slow random pitch wander per copy, 0..10 cents
    kTone,        // -100..100 dark <-> bright tilt
    kResample,    // OFF, -12, -24
    kKeyTrack,    // OFF, ON
    kHiss,        // 0..100 % noise floor
    kNumParams
  };
  enum { kNumTimbres = 5 };
  enum { kTStrings = 0, kTAnalog, kTOrgan, kTChoir, kTGlass };

  static const char *resampleName(int v) {
    static const char *const n[] = {"OFF", "-12", "-24"};
    return n[clampi(v, 0, 2)];
  }
  static const char *keyTrackName(int v) { return v ? "ON" : "OFF"; }
};

// Overall output level (before the hard ceiling)
constexpr float kOutLevel = 0.75f;

// Level of each timbre, so a TIMBRE sweep stays at about the same loudness
// (set from the desktop tests' RMS measurements, see test/render.cc)
struct TimbreGain {
  static constexpr float kStrings = 0.62f;
  static constexpr float kAnalog = 0.47f;
  static constexpr float kOrgan = 0.376f;
  static constexpr float kChoir = 1.33f;
  static constexpr float kGlass = 0.50f;
};

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

template <class V>
class PadEngine {
 public:
  enum { N = LaneInfo<V>::N };
  typedef typename LaneInfo<V>::U U;

  void init(uint32_t seed) {
    rng_.seed(seed * 2654435761u + 0x9E3779B9u);
    p_timbre_ = 180.f / 1023.f;
    p_grit_ = 300.f / 1023.f;
    p_ens_ = 0.6f;
    p_drift_ = 0.35f;
    p_tone_ = 0.f;
    p_hiss_ = 0.15f;
    resample_ = 0;
    keytrack_ = 0;
    first_ = true;

    // fixed "ah" formants: F1 730 Hz, F2 1090 Hz, F3 2440 Hz
    static const float fhz[3] = {730.f, 1090.f, 2440.f};
    static const float fq[3] = {5.f, 6.f, 8.f};
    for (int f = 0; f < 3; ++f) {
      const float g = tanSmall(3.14159265f * fhz[f] * kInvSR);
      const float k = 1.f / fq[f];
      const float a1 = 1.f / (1.f + g * (g + k));
      sv_a1_[f] = a1;
      sv_a2_[f] = g * a1;
      sv_a3_[f] = g * g * a1;
      sv_k_[f] = k;
    }

    for (int j = 0; j < N; ++j) {
      w0_[j] = 220.f * kInvSR;
      mod_timbre_[j] = mod_grit_[j] = mod_tone_[j] = mod_ens_[j] = 0.f;
      reseed_[j] = true;
      pw_lfo_[j] = rng_.u01();
      pw_rate_[j] = 0.35f + 0.15f * rng_.u01();
      for (int c = 0; c < 2; ++c) {
        drift_v_[j][c] = rng_.u11() * 0.5f;
        drift_tgt_[j][c] = rng_.u11();
        drift_ph_[j][c] = rng_.u01();
      }
      lset(hacc_, j, 1.f);
      noise_s_[j] = rng_.next() | 1u;
    }
    for (int c = 0; c < 2; ++c)
      for (int j = 0; j < N; ++j) lset(ph_[c], j, rng_.u01());
    resetState();
  }

  // Clear filter / hold state on every lane
  void resetState() {
    const V z = splat<V>(0.f);
    for (int f = 0; f < 3; ++f) sv1_[f] = sv2_[f] = z;
    tone_lp_ = hiss_lp_ = hval_ = sm1_ = sm2_ = dcx_ = dcy_ = z;
    hacc_ = splat<V>(1.f);
    for (int j = 0; j < N; ++j) reseed_[j] = true;
  }

  void setParam(int id, int32_t v) {
    switch (id) {
      case Params::kTimbre: p_timbre_ = clampi(v, 0, 1023) * (1.f / 1023.f); break;
      case Params::kGrit: p_grit_ = clampi(v, 0, 1023) * (1.f / 1023.f); break;
      case Params::kEnsemble: p_ens_ = clampi(v, 0, 100) * 0.01f; break;
      case Params::kDrift: p_drift_ = clampi(v, 0, 100) * 0.01f; break;
      case Params::kTone: p_tone_ = clampi(v, -100, 100) * 0.01f; break;
      case Params::kResample: resample_ = clampi(v, 0, 2); break;
      case Params::kKeyTrack: keytrack_ = clampi(v, 0, 1); break;
      case Params::kHiss: p_hiss_ = clampi(v, 0, 100) * 0.01f; break;
      default: break;
    }
  }

  // lane 0..N-1; w0 = note frequency in cycles per sample
  void setPitch(int lane, float w0) { w0_[lane] = w0; }

  // Per-voice modulation, added to the normalised parameter (Tone: -1..1 range)
  void setMod(int lane, float timbre, float grit, float tone, float ens) {
    mod_timbre_[lane] = timbre;
    mod_grit_[lane] = grit;
    mod_tone_[lane] = tone;
    mod_ens_[lane] = ens;
  }

  // New note. fresh = the voice was silent: new random phases (chords never
  // start phase-locked) and clean filters. A stolen / legato voice keeps
  // everything so it doesn't click.
  void noteOn(int lane, bool fresh) {
    if (!fresh) return;
    for (int c = 0; c < 2; ++c) lset(ph_[c], lane, rng_.u01());
    for (int f = 0; f < 3; ++f) {
      lset(sv1_[f], lane, 0.f);
      lset(sv2_[f], lane, 0.f);
    }
    lset(tone_lp_, lane, 0.f);
    lset(hiss_lp_, lane, 0.f);
    lset(hval_, lane, 0.f);
    lset(sm1_, lane, 0.f);
    lset(sm2_, lane, 0.f);
    lset(dcx_, lane, 0.f);
    lset(dcy_, lane, 0.f);
    lset(hacc_, lane, 1.f);
    reseed_[lane] = true;
  }

  // Render n samples (any n). out[i] holds one sample per lane.
  // live[j] == false: that lane outputs exactly 0.
  void render(const bool *live, V *out, uint32_t n) {
    while (n > 0) {
      const uint32_t len = n < 64u ? n : 64u;
      renderBlock(live, out, len);
      out += len;
      n -= len;
    }
  }

 private:
  // ---- control rate -------------------------------------------------------
  struct Ctl {
    V w[Params::kNumTimbres];  // timbre weights (gain folded in)
    V ca, cb;                  // ensemble copy gains
    V lg, hg;                  // tone low / high gains
    V cm;                      // crusher mix
  };

  void computeEnd(int j, float dt, Ctl &e, float *inc, float *step, float *smc, float *tonec, float *wsub, float *inv4w,
                  float *pw) {
    // timbre weights
    const float pos = clampf(p_timbre_ + mod_timbre_[j], 0.f, 1.f) * 4.f;
    static const float gains[Params::kNumTimbres] = {TimbreGain::kStrings, TimbreGain::kAnalog, TimbreGain::kOrgan,
                                                     TimbreGain::kChoir, TimbreGain::kGlass};
    for (int k = 0; k < Params::kNumTimbres; ++k) {
      const float d = pos - (float)k;
      const float wk = 1.f - (d < 0.f ? -d : d);
      lset(e.w[k], j, wk > 0.f ? wk * gains[k] : 0.f);
    }

    // ensemble: copy B fades in over the first 20 %, power-normalised
    const float ens = clampf(p_ens_ + mod_ens_[j], 0.f, 1.f);
    const float m = clampf(ens * 5.f, 0.f, 1.f);
    const float a = 1.f - 0.5f * m, b = 0.5f * m;
    const float norm = rsqrtHalfToOne(a * a + b * b);
    lset(e.ca, j, a * norm);
    lset(e.cb, j, b * norm);

    // drift: smoothed random walk per copy (new target ~0.4 times a second)
    const float drift_c = p_drift_ * 10.f;
    for (int c = 0; c < 2; ++c) {
      float &ph = drift_ph_[j][c];
      ph += dt * 0.4f;
      if (ph >= 1.f) {
        ph -= 1.f;
        drift_tgt_[j][c] = rng_.u11();
      }
      drift_v_[j][c] += (drift_tgt_[j][c] - drift_v_[j][c]) * clampf(dt * 1.2f, 0.f, 1.f);
    }
    const float spread = ens * 25.f;  // cents between the copies
    const float cents_a = -0.5f * spread + drift_c * drift_v_[j][0];
    const float cents_b = 0.5f * spread + drift_c * drift_v_[j][1];
    const float w0 = clampf(w0_[j], 1e-5f, 0.45f);
    // phase runs at half the note (so the organ's 16' sine shares it)
    const float wa = 0.5f * w0 * exp2f_fast(cents_a * (1.f / 1200.f));
    const float wb = 0.5f * w0 * exp2f_fast(cents_b * (1.f / 1200.f));
    wsub[0] = wa;
    wsub[1] = wb;
    inv4w[0] = 0.25f / clampf(2.f * wa, 1e-5f, 0.45f);
    inv4w[1] = 0.25f / clampf(2.f * wb, 1e-5f, 0.45f);

    // PWM for ANALOG: slow sine per voice
    pw_lfo_[j] += dt * pw_rate_[j];
    if (pw_lfo_[j] >= 1.f) pw_lfo_[j] -= 1.f;
    {
      const float t = pw_lfo_[j];
      const float y = 2.f * t - 1.f;
      const float s = -(4.f * y - 4.f * y * (y < 0.f ? -y : y));
      *pw = 0.5f + 0.3f * s;
    }

    // tone tilt
    const float tone = clampf(p_tone_ + mod_tone_[j], -1.f, 1.f);
    float fc = 900.f, lg = 1.f, hg = 1.f;
    if (tone < 0.f) {
      fc = 900.f * exp2f_fast(1.5f * tone);
      hg = 1.f + 0.9f * tone;
      lg = 1.f - 0.15f * tone;
    } else {
      hg = 1.f + 1.2f * tone;
      lg = 1.f - 0.3f * tone;
    }
    *tonec = onePoleCoef(fc);
    lset(e.lg, j, lg);
    lset(e.hg, j, hg);

    // sampler emulation
    const float g = clampf(p_grit_ + mod_grit_[j], 0.f, 1.f);
    float oct = -3.f * g - (float)resample_;
    if (keytrack_) {
      float r = w0 * (1.f / kW0C4);
      r = clampf(r, 0.125f, 2.f);
      // log2 of r for the rate: rate scales with the note like a sampler
      union { float f; int32_t i; } u;
      u.f = r;
      const float e2 = (float)(((u.i >> 23) & 0xFF) - 127);
      u.i = (u.i & 0x007FFFFF) | 0x3F800000;
      const float mm = u.f;  // 1..2
      const float l2 = e2 + (mm - 1.f) * (1.3466f - 0.3466f * mm);
      oct += l2;
    }
    const float rate = clampf(kSR * exp2f_fast(oct), 1000.f, kSR);
    inc[0] = rate * kInvSR;
    const float bits = 16.f - 8.f * g;
    step[0] = exp2f_fast(1.f - bits);
    *smc = onePoleCoef(clampf(rate * 0.42f, 400.f, 18000.f));
    lset(e.cm, j, clampf((1.f - inc[0]) * 6.f + g * 3.f, 0.f, 1.f));
  }

  // ---- one buffer (n <= 64) ------------------------------------------------
  void renderBlock(const bool *live, V *out, uint32_t n) {
    const float dt = (float)n * kInvSR;
    const float inv_n = 1.f / (float)n;

    Ctl e;
    V wsubA, wsubB, invA, invB, pwE, toneA, incV, stepV, invStepV, smA;
    V liveV;
    for (int j = 0; j < N; ++j) {
      float inc, step, smc, tonec, wsub[2], inv4w[2], pw;
      computeEnd(j, dt, e, &inc, &step, &smc, &tonec, wsub, inv4w, &pw);
      lset(wsubA, j, wsub[0]);
      lset(wsubB, j, wsub[1]);
      lset(invA, j, inv4w[0]);
      lset(invB, j, inv4w[1]);
      lset(pwE, j, pw);
      lset(toneA, j, tonec);
      lset(incV, j, inc);
      lset(stepV, j, step);
      lset(invStepV, j, 1.f / step);
      lset(smA, j, smc);
      lset(liveV, j, live[j] ? 1.f : 0.f);
      if (first_ || reseed_[j]) {
        // new voice: no ramp from whatever this lane did before
        for (int k = 0; k < Params::kNumTimbres; ++k) lset(z_.w[k], j, lget(e.w[k], j));
        lset(z_.ca, j, lget(e.ca, j));
        lset(z_.cb, j, lget(e.cb, j));
        lset(z_.lg, j, lget(e.lg, j));
        lset(z_.hg, j, lget(e.hg, j));
        lset(z_.cm, j, lget(e.cm, j));
        lset(pw_z_, j, pw);
        reseed_[j] = false;
      }
    }
    first_ = false;

    // which parts run at all this buffer (uniform across the lanes)
    bool act[Params::kNumTimbres];
    for (int k = 0; k < Params::kNumTimbres; ++k) {
      float mx = 0.f;
      for (int j = 0; j < N; ++j) mx = maxf(mx, maxf(lget(e.w[k], j), lget(z_.w[k], j)));
      act[k] = mx > 0.f;
    }
    float cbmax = 0.f;
    for (int j = 0; j < N; ++j) cbmax = maxf(cbmax, maxf(lget(e.cb, j), lget(z_.cb, j)));
    const bool runB = cbmax > 0.f;
    const bool needSaw = act[Params::kTStrings] || act[Params::kTAnalog] || act[Params::kTChoir];
    const bool needAna = act[Params::kTAnalog];
    const bool needCho = act[Params::kTChoir];
    const bool needOrg = act[Params::kTOrgan];
    const bool needGls = act[Params::kTGlass];
    const bool needHiss = p_hiss_ > 0.f;
    // sampler section: skipped while it's fully bypassed on every lane
    float cmmax = 0.f;
    for (int j = 0; j < N; ++j) cmmax = maxf(cmmax, maxf(lget(e.cm, j), lget(z_.cm, j)));
    const bool needCrush = cmmax > 0.f;

    // ---- per sample, in passes over the buffer ---------------------------
    // Each pass is a short loop that keeps its few values in NEON registers.
    // (One big loop doing everything ran out of registers on the Cortex-A7 and
    // spent most of its time spilling to the stack, which crackled.)
    const V zero = splat<V>(0.f);
    V *const acc = buf_acc_;
    V *const tb = buf_t_;
    V *const phb = buf_ph_;
    V *const sawb = buf_saw_;
    V *const cho = buf_cho_;
    for (uint32_t s = 0; s < n; ++s) acc[s] = zero;
    if (needCho)
      for (uint32_t s = 0; s < n; ++s) cho[s] = zero;

    for (int c = 0; c < 2; ++c) {
      if (c == 1 && !runB) break;
      const V wsub = c == 0 ? wsubA : wsubB;
      const V inv = c == 0 ? invA : invB;
      const V g0 = c == 0 ? z_.ca : z_.cb;
      const V g1 = c == 0 ? e.ca : e.cb;

      // DPW history: the parabola one sample back (the last sample of the
      // previous buffer). Primed every buffer, so it's always valid, even for
      // a part that was skipped last buffer.
      V tPrev;
      {
        const V ph = ph_[c];
        tPrev = wrap1(2.f * ph - sel(ph >= 0.5f, splat<V>(1.f), splat<V>(0.f)));
      }

      // pass: phase. ph runs at half the note (the organ's 16'), t at the note
      {
        V ph = ph_[c];
        for (uint32_t s = 0; s < n; ++s) {
          ph = ph + wsub;
          ph = sel(ph >= 1.f, ph - 1.f, ph);
          phb[s] = ph;
          V t = 2.f * ph;
          tb[s] = sel(t >= 1.f, t - 1.f, t);
        }
        ph_[c] = ph;
      }

      // pass: falling DPW saw (STRINGS; kept in sawb for ANALOG and CHOIR).
      // Falling, so its fundamental is in phase with the sines and crossfades
      // between neighbouring timbres don't cancel.
      if (needSaw) {
        V k = g0 * z_.w[Params::kTStrings];
        const V dk = (g1 * e.w[Params::kTStrings] - k) * inv_n;
        V prev = parab(tPrev);
        for (uint32_t s = 0; s < n; ++s) {
          const V p = parab(tb[s]);
          const V saw = (prev - p) * inv;
          prev = p;
          sawb[s] = saw;
          acc[s] = acc[s] + k * saw;
          k = k + dk;
        }
      }

      // pass: ANALOG = saw + PWM pulse (saw minus a phase-shifted saw)
      if (needAna) {
        V k = g0 * z_.w[Params::kTAnalog];
        const V dk = (g1 * e.w[Params::kTAnalog] - k) * inv_n;
        V pw = pw_z_;
        const V dpw = (pwE - pw_z_) * inv_n;
        V prev = parab(wrap1(tPrev + pw));
        for (uint32_t s = 0; s < n; ++s) {
          const V p = parab(wrap1(tb[s] + pw));
          const V s2 = (prev - p) * inv;
          prev = p;
          acc[s] = acc[s] + k * (1.1f * sawb[s] - 0.5f * s2);
          k = k + dk;
          pw = pw + dpw;
        }
      }

      // pass: CHOIR source = narrow pulse, summed over both copies, formants later
      if (needCho) {
        V k = g0;
        const V dk = (g1 - g0) * inv_n;
        V prev = parab(wrap1(tPrev + 0.25f));
        for (uint32_t s = 0; s < n; ++s) {
          const V p = parab(wrap1(tb[s] + 0.25f));
          const V s3 = (prev - p) * inv;
          prev = p;
          cho[s] = cho[s] + k * (sawb[s] - s3);
          k = k + dk;
        }
      }

      // pass: ORGAN, drawbars 16' 8' 4' 2 2/3'
      if (needOrg) {
        V k = g0 * z_.w[Params::kTOrgan];
        const V dk = (g1 * e.w[Params::kTOrgan] - k) * inv_n;
        for (uint32_t s = 0; s < n; ++s) {
          const V t = tb[s];
          V t4 = 2.f * t;
          t4 = sel(t4 >= 1.f, t4 - 1.f, t4);
          V t3 = 3.f * t;
          t3 = sel(t3 >= 1.f, t3 - 1.f, t3);
          t3 = sel(t3 >= 1.f, t3 - 1.f, t3);
          const V org = 0.7f * sin2pi(phb[s]) + sin2pi(t) + 0.55f * sin2pi(t4) + 0.4f * sin2pi(t3);
          acc[s] = acc[s] + k * org;
          k = k + dk;
        }
      }

      // pass: GLASS, 2:1 phase-mod sine + 4th harmonic
      if (needGls) {
        V k = g0 * z_.w[Params::kTGlass];
        const V dk = (g1 * e.w[Params::kTGlass] - k) * inv_n;
        for (uint32_t s = 0; s < n; ++s) {
          const V t = tb[s];
          V t2 = 2.f * t;
          t2 = sel(t2 >= 1.f, t2 - 1.f, t2);
          V t4 = 2.f * t2;
          t4 = sel(t4 >= 1.f, t4 - 1.f, t4);
          const V pm = sin2pi(wrap1(t + 0.22f * sin2pi(t2)));
          acc[s] = acc[s] + k * (0.8f * pm + 0.3f * sin2pi(t4));
          k = k + dk;
        }
      }
    }

    // keep copy B's phase moving while it's skipped, so it re-enters freely
    if (!runB) ph_[1] = fracp(ph_[1] + (float)n * wsubB);

    // pass: CHOIR formants, one band-pass at a time, into tb (free by now)
    if (needCho) {
      for (uint32_t s = 0; s < n; ++s) tb[s] = 0.12f * cho[s];
      static const float fg[3] = {1.f, 0.75f, 0.35f};
      for (int f = 0; f < 3; ++f) {
        const float a1 = sv_a1_[f], a2 = sv_a2_[f], a3 = sv_a3_[f], gk = fg[f] * sv_k_[f];
        V s1 = sv1_[f], s2 = sv2_[f];
        for (uint32_t s = 0; s < n; ++s) {
          const V v3 = cho[s] - s2;
          const V v1 = a1 * s1 + a2 * v3;
          const V v2 = s2 + a2 * s1 + a3 * v3;
          s1 = 2.f * v1 - s1;
          s2 = 2.f * v2 - s2;
          tb[s] = tb[s] + gk * v1;
        }
        sv1_[f] = s1;
        sv2_[f] = s2;
      }
      V k = z_.w[Params::kTChoir];
      const V dk = (e.w[Params::kTChoir] - k) * inv_n;
      for (uint32_t s = 0; s < n; ++s) {
        acc[s] = acc[s] + k * tb[s];
        k = k + dk;
      }
    }

    // pass: TONE tilt
    {
      V tlp = tone_lp_;
      V lg = z_.lg, hg = z_.hg;
      const V dlg = (e.lg - z_.lg) * inv_n, dhg = (e.hg - z_.hg) * inv_n;
      for (uint32_t s = 0; s < n; ++s) {
        const V x = acc[s];
        tlp = tlp + toneA * (x - tlp);
        acc[s] = lg * tlp + hg * (x - tlp);
        lg = lg + dlg;
        hg = hg + dhg;
      }
      tone_lp_ = tlp;
    }

    // pass: HISS, "in the sample" (before the sampler)
    if (needHiss) {
      const V amt = splat<V>(p_hiss_ * p_hiss_ * 0.12f);
      U ns;
      for (int j = 0; j < N; ++j) lset_u(ns, j, noise_s_[j]);
      V hlp = hiss_lp_;
      for (uint32_t s = 0; s < n; ++s) {
        ns ^= ns << 13;
        ns ^= ns >> 17;
        ns ^= ns << 5;
        const V wn = bitsToNoise(ns);
        hlp = hlp + 0.25f * (wn - hlp);
        acc[s] = acc[s] + amt * (wn - hlp);
      }
      hiss_lp_ = hlp;
      for (int j = 0; j < N; ++j) noise_s_[j] = lget_u(ns, j);
    }

    // pass: SAMPLER, hold + bits, two-pole output smoothing, crossfaded in
    if (needCrush) {
      V hacc = hacc_, hval = hval_, sm1 = sm1_, sm2 = sm2_;
      V cm = z_.cm;
      const V dcm = (e.cm - z_.cm) * inv_n;
      for (uint32_t s = 0; s < n; ++s) {
        const V x = acc[s];
        V r = x * invStepV;
        r = truncv(r + sel(r >= 0.f, splat<V>(0.5f), splat<V>(-0.5f)));
        const V h = hacc + incV;
        hval = sel(h >= 1.f, r * stepV, hval);
        hacc = sel(h >= 1.f, h - 1.f, h);
        sm1 = sm1 + smA * (hval - sm1);
        sm2 = sm2 + smA * (sm1 - sm2);
        acc[s] = x + cm * (sm2 - x);
        cm = cm + dcm;
      }
      hacc_ = hacc;
      hval_ = hval;
      sm1_ = sm1;
      sm2_ = sm2;
    }

    // pass: DC blocker (~8 Hz), output level, hard ceiling (normal levels peak
    // well under it, so it only catches extreme settings), idle lanes silent
    {
      V dcx = dcx_, dcy = dcy_;
      const V gain = kOutLevel * liveV;
      for (uint32_t s = 0; s < n; ++s) {
        const V x = acc[s];
        dcy = (x - dcx) + 0.999f * dcy;
        dcx = x;
        out[s] = clamp1(gain * dcy);
      }
      dcx_ = dcx;
      dcy_ = dcy;
    }

    z_ = e;
    pw_z_ = pwE;
  }

  static DP_INLINE void lset_u(uint32_t &x, int, uint32_t v) { x = v; }
  static DP_INLINE void lset_u(v4u &x, int j, uint32_t v) { x[j] = v; }
  static DP_INLINE uint32_t lget_u(uint32_t x, int) { return x; }
  static DP_INLINE uint32_t lget_u(v4u x, int j) { return x[j]; }

  // parameters
  float p_timbre_, p_grit_, p_ens_, p_drift_, p_tone_, p_hiss_;
  int resample_, keytrack_;
  bool first_;

  // per lane, control rate
  float w0_[N];
  float mod_timbre_[N], mod_grit_[N], mod_tone_[N], mod_ens_[N];
  bool reseed_[N];
  float drift_v_[N][2], drift_tgt_[N][2], drift_ph_[N][2];
  float pw_lfo_[N], pw_rate_[N];
  uint32_t noise_s_[N];
  Rng rng_;

  // formant filter coefficients
  float sv_a1_[3], sv_a2_[3], sv_a3_[3], sv_k_[3];

  // per lane, audio rate
  V ph_[2];
  V sv1_[3], sv2_[3];
  V tone_lp_, hiss_lp_, hval_, hacc_, sm1_, sm2_, dcx_, dcy_;
  Ctl z_;
  V pw_z_;

  // per-buffer work space (one buffer is at most 64 samples)
  V buf_acc_[64], buf_t_[64], buf_ph_[64], buf_saw_[64], buf_cho_[64];
};

typedef PadEngine<float> DustyPads1;
typedef PadEngine<v4f> DustyPads4;

}  // namespace dpads
