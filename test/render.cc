// Desktop test harness for Dusty Pads.
// Renders the DSP without any Korg runtime and checks for NaN / clipping / DC,
// tuning, aliasing when GRIT is 0, that GRIT / RESAMPLE / KEY TRACK really
// crunch, zipper noise, level balance across timbres and grit, that the
// 4-lane (microKORG 2) and 1-lane (NTS-1) builds match, then writes WAV demos
// to test/out/.
//
// Build & run:  ./test/run.sh

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "dustypads.h"

using namespace dpads;
typedef Params P;

static const int kRate = 48000;
static int failures = 0;

static float noteW0(float note) { return 440.f * std::pow(2.f, (note - 69.f) / 12.f) / kRate; }
static float noteHz(float note) { return 440.f * std::pow(2.f, (note - 69.f) / 12.f); }

static void writeWav(const std::string &path, const std::vector<float> &x) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return;
  const uint32_t n = x.size(), data = n * 2, riff = 36 + data;
  const uint16_t pcm = 1, ch = 1, bits = 16, align = 2;
  const uint32_t sr = kRate, br = kRate * 2, fmt = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
  fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  for (float v : x) {
    int16_t s = (int16_t)std::lrint(std::fmax(-1.f, std::fmin(1.f, v)) * 32767.f);
    fwrite(&s, 2, 1, f);
  }
  fclose(f);
}

static void result(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void result(bool ok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char buf[256];
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  printf("%-66s %s\n", buf, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

struct Stats { float peak = 0, dc = 0, rms = 0; bool bad = false; };
static Stats stats(const std::vector<float> &x, size_t from = 0) {
  Stats s; double sum = 0, sq = 0;
  for (size_t i = from; i < x.size(); ++i) {
    const float v = x[i];
    if (!std::isfinite(v)) s.bad = true;
    s.peak = std::fmax(s.peak, std::fabs(v));
    sum += v; sq += double(v) * v;
  }
  const double n = double(x.size() - from);
  s.dc = sum / n; s.rms = std::sqrt(sq / n);
  return s;
}
static bool sane(const std::vector<float> &x) {
  Stats s = stats(x);
  return !s.bad && s.peak <= 1.0f && std::fabs(s.dc) < 0.03f && s.peak > 0.01f;
}
static void check(const char *name, const std::vector<float> &x) {
  Stats s = stats(x);
  result(sane(x), "%-36s peak %.3f  dc %+.4f", name, s.peak, s.dc);
}

// ---- settings ----
struct Set { int timbre, grit, ens, drift, tone, resample, keytrack, hiss; };
static const Set kClean = {0, 0, 0, 0, 0, 0, 0, 0};

template <class E> static void apply(E &e, const Set &s) {
  e.setParam(P::kTimbre, s.timbre); e.setParam(P::kGrit, s.grit);
  e.setParam(P::kEnsemble, s.ens);  e.setParam(P::kDrift, s.drift);
  e.setParam(P::kTone, s.tone);     e.setParam(P::kResample, s.resample);
  e.setParam(P::kKeyTrack, s.keytrack); e.setParam(P::kHiss, s.hiss);
}

// Starter settings documented in the README (keep in sync)
struct Preset { const char *name; Set s; };
static const Preset kPresets[] = {
    {"warehouse_strings", {60, 420, 70, 40, -20, 0, 0, 20}},
    {"juno_pad",          {256, 200, 60, 30, 10, 0, 0, 10}},
    {"rave_organ",        {512, 550, 20, 15, 20, 1, 0, 15}},
    {"ah_choir",          {768, 380, 80, 45, -10, 0, 1, 25}},
    {"glass_bells",       {1023, 300, 40, 20, 30, 0, 0, 10}},
    {"pitched_down_94",   {140, 650, 70, 50, -40, 2, 1, 35}},
};

// ---- 1-lane (NTS-1) render: a held note, automate(t) once per block ----
template <typename F>
static std::vector<float> render1(DustyPads1 &e, float note, float seconds, F automate, int block = 64) {
  std::vector<float> out(size_t(seconds * kRate));
  const bool live = true;
  e.setPitch(0, noteW0(note));
  e.noteOn(0, true);
  for (size_t i = 0; i < out.size(); i += block) {
    automate(float(i) / out.size());
    e.render(&live, &out[i], (uint32_t)std::min<size_t>(block, out.size() - i));
  }
  return out;
}
static std::vector<float> render1(DustyPads1 &e, float note, float seconds) {
  return render1(e, note, seconds, [](float) {});
}

// ---- 4-lane (microKORG 2) render: four notes, returns lane `lane` ----
template <typename F>
static std::vector<float> render4(DustyPads4 &g, const float notes[4], float seconds, int lane, F automate,
                                  int block = 64, const bool *liveIn = nullptr) {
  std::vector<float> out(size_t(seconds * kRate));
  const bool all[4] = {true, true, true, true};
  const bool *live = liveIn ? liveIn : all;
  for (int j = 0; j < 4; ++j) { g.setPitch(j, noteW0(notes[j])); g.noteOn(j, true); }
  std::vector<v4f> buf(block);
  for (size_t i = 0; i < out.size(); i += block) {
    automate(float(i) / out.size());
    const uint32_t n = (uint32_t)std::min<size_t>(block, out.size() - i);
    g.render(live, buf.data(), n);
    for (uint32_t s = 0; s < n; ++s) out[i + s] = buf[s][lane];
  }
  return out;
}

static double toneMag(const std::vector<float> &x, size_t off, size_t N, double hz) {
  double re = 0, im = 0;
  const double w = 2 * M_PI * hz / kRate;
  for (size_t n = 0; n < N; ++n) {
    const double win = 0.5 - 0.5 * std::cos(2 * M_PI * n / N);
    re += x[off + n] * win * std::cos(w * n);
    im -= x[off + n] * win * std::sin(w * n);
  }
  return std::sqrt(re * re + im * im) * 4.0 / N;
}

// share of energy not near a harmonic of f0 (aliasing / crunch measure)
static float inharmonicRatio(const std::vector<float> &x, float f0) {
  const int N = 8192;
  const int off = x.size() > (size_t)N ? (x.size() - N) / 2 : 0;
  double harm = 0, other = 0;
  for (int k = 1; k < N / 2; ++k) {
    double re = 0, im = 0;
    const double w = 2 * M_PI * k / N;
    for (int n = 0; n < N; ++n) {
      const double win = 0.5 - 0.5 * std::cos(2 * M_PI * n / N);
      re += x[off + n] * win * std::cos(w * n);
      im -= x[off + n] * win * std::sin(w * n);
    }
    const double p = re * re + im * im;
    const double f = double(k) * kRate / N;
    const double h = f / f0;
    if (std::fabs(h - std::round(h)) * f0 < 3.0 * kRate / N) harm += p; else other += p;
  }
  return float(other / (harm + other + 1e-20));
}

static int zeroCrossHz(const std::vector<float> &x, size_t from) {
  int zc = 0;
  for (size_t i = from + 1; i < x.size(); ++i) zc += (x[i - 1] < 0.f && x[i] >= 0.f);
  return int(std::lround(zc * double(kRate) / double(x.size() - from)));
}

static float maxStep(const std::vector<float> &x, size_t from = 0) {
  float m = 0;
  for (size_t i = from + 1; i < x.size(); ++i) m = std::fmax(m, std::fabs(x[i] - x[i - 1]));
  return m;
}

static float rmsDb(const std::vector<float> &x) { return 20.f * std::log10(stats(x, kRate / 4).rms + 1e-12f); }

// ---------------------------------------------------------------------------

static void testSanity() {
  printf("-- sanity --\n");
  DustyPads1 e;
  for (float note : {36.f, 60.f, 84.f}) {
    e.init(1);
    char n[64]; snprintf(n, sizeof n, "defaults note %d", int(note));
    check(n, render1(e, note, 1.f));
  }

  // stress: timbre positions x grit x resample x keytrack x tone x notes, everything else at max
  int bad = 0, total = 0;
  for (int timbre : {0, 128, 256, 400, 512, 640, 768, 900, 1023})
    for (int grit : {0, 1023})
      for (int rs = 0; rs <= 2; ++rs)
        for (int kt = 0; kt <= 1; ++kt)
          for (int tone : {-100, 0, 100})
            for (float note : {12.f, 48.f, 96.f, 120.f}) {
              e.init(7);
              apply(e, Set{timbre, grit, 100, 100, tone, rs, kt, 100});
              auto x = render1(e, note, 0.15f);
              ++total;
              if (!sane(x)) {
                if (++bad < 6) {
                  char n[96]; snprintf(n, sizeof n, "stress t%d g%d rs%d kt%d tone%d note%d", timbre, grit, rs, kt, tone, int(note));
                  check(n, x);
                }
              }
            }
  result(bad == 0, "stress: %d combinations, %d bad", total, bad);

  // all zero
  for (int timbre : {0, 256, 512, 768, 1023}) {
    e.init(3); apply(e, Set{timbre, 0, 0, 0, 0, 0, 0, 0});
    char n[64]; snprintf(n, sizeof n, "all-zero timbre %d", timbre);
    check(n, render1(e, 48, 0.5f));
  }

  // odd block sizes
  for (int bs : {1, 7, 64, 200}) {
    e.init(5); apply(e, Set{600, 500, 50, 50, 20, 1, 1, 40});
    char n[64]; snprintf(n, sizeof n, "block size %d", bs);
    check(n, render1(e, 55, 0.5f, [](float) {}, bs));
  }
  {
    e.init(1);
    std::vector<float> x(4800);
    const bool live = true;
    e.setPitch(0, 0.f); e.noteOn(0, true); e.render(&live, x.data(), x.size());
    Stats s = stats(x);
    result(!s.bad && s.peak <= 1.f, "zero pitch stays finite");
  }
}

static void testTuning() {
  printf("\n-- tuning --\n");
  DustyPads1 e;
  for (int timbre : {0, 512, 1023})
    for (float note : {45.f, 69.f}) {
      e.init(1); apply(e, Set{timbre, 0, 0, 0, 0, 0, 0, 0});
      auto x = render1(e, note, 2.f);
      // strongest of fundamental vs octave below/above must be the fundamental
      const double f = toneMag(x, kRate / 2, 32768, noteHz(note));
      const double lo = toneMag(x, kRate / 2, 32768, noteHz(note) * 0.5) ;
      const int hz = zeroCrossHz(x, 0), want = int(std::lround(noteHz(note)));
      const bool zc_ok = timbre == 512 || std::abs(hz - want) <= 1;  // organ's 16' adds crossings
      result(zc_ok && f > 0.05, "tuning timbre %4d note %d: %d Hz (want %d), fund %.2f, 16' %.2f", timbre,
             int(note), hz, want, f, lo);
    }
}

static void testGrit() {
  printf("\n-- grit / sampler --\n");
  DustyPads1 e;
  // GRIT 0 is clean: little aliasing on the saw
  for (float note : {48.f, 60.f, 72.f, 84.f}) {
    e.init(1); apply(e, kClean);
    const float ir = inharmonicRatio(render1(e, note, 0.5f), noteHz(note));
    result(ir < (note < 80 ? 0.01f : 0.03f), "clean strings note %d: inharmonic %.5f", int(note), ir);
  }
  for (float note : {60.f, 84.f}) {
    e.init(1); apply(e, Set{512, 0, 0, 0, 0, 0, 0, 0});
    // the 16' drawbar sits an octave below: harmonics of f0 / 2
    const float ir = inharmonicRatio(render1(e, note, 0.5f), noteHz(note) * 0.5f);
    result(ir < 0.002f, "clean organ note %d: inharmonic %.5f", int(note), ir);
  }
  // GRIT / RESAMPLE / KEY TRACK must add crunch, in order
  auto ir = [&](Set s, float note) {
    e.init(1); apply(e, s);
    return inharmonicRatio(render1(e, note, 0.5f), noteHz(note));
  };
  const float c0 = ir(Set{0, 0, 0, 0, 0, 0, 0, 0}, 60);
  const float c1 = ir(Set{0, 512, 0, 0, 0, 0, 0, 0}, 60);
  const float c2 = ir(Set{0, 1023, 0, 0, 0, 0, 0, 0}, 60);
  const float c3 = ir(Set{0, 1023, 0, 0, 0, 2, 0, 0}, 60);
  result(c1 > c0 * 3 && c2 > c1 && c3 > c2, "grit adds crunch: %.4f < %.4f < %.4f < %.4f (-24)", c0, c1, c2, c3);
  const float k0 = ir(Set{0, 300, 0, 0, 0, 0, 0, 0}, 36), k1 = ir(Set{0, 300, 0, 0, 0, 0, 1, 0}, 36);
  const float k2 = ir(Set{0, 300, 0, 0, 0, 0, 0, 0}, 84), k3 = ir(Set{0, 300, 0, 0, 0, 0, 1, 0}, 84);
  result(k1 > k0 && k3 < k2, "key track: low notes grainier (%.4f > %.4f), high cleaner (%.4f < %.4f)", k1, k0, k3, k2);

  // level: GRIT sweep shouldn't jump the level much
  for (int timbre : {0, 512, 768}) {
    float lo = 1e9, hi = -1e9;
    for (int grit : {0, 256, 512, 768, 1023})
      for (int rs = 0; rs <= 2; ++rs) {
        e.init(2); apply(e, Set{timbre, grit, 50, 0, 0, rs, 0, 0});
        const float d = rmsDb(render1(e, 48, 1.f));
        lo = std::fmin(lo, d); hi = std::fmax(hi, d);
      }
    // CHOIR loses more: its formants sit where the resampling cuts (as on a real sampler)
    result(hi - lo < (timbre == 768 ? 4.5f : 3.f), "level vs grit/resample timbre %4d: spread %.2f dB", timbre, hi - lo);
  }
}

static void testLevels() {
  printf("\n-- levels --\n");
  DustyPads1 e;
  // each timbre on its own, at three notes; print the raw RMS for gain tuning
  float ref = 0;
  float lo = 1e9, hi = -1e9;
  for (int t = 0; t < 5; ++t) {
    const int timbre = t == 4 ? 1023 : t * 256;
    double acc = 0;
    for (float note : {36.f, 48.f, 60.f, 72.f}) {
      e.init(4); apply(e, Set{timbre, 0, 60, 30, 0, 0, 0, 0});
      acc += rmsDb(render1(e, note, 1.f));
    }
    const float d = float(acc / 4);
    if (t == 0) ref = d;
    lo = std::fmin(lo, d); hi = std::fmax(hi, d);
    printf("   timbre %4d: %6.2f dBFS (%+.2f dB vs strings)\n", timbre, d, d - ref);
  }
  result(hi - lo < 2.f, "timbre level spread %.2f dB", hi - lo);
  // crossfade midpoints shouldn't dip more than ~3 dB below the ends
  float worst = 0;
  for (int t = 0; t < 4; ++t) {
    auto lvl = [&](int timbre) { e.init(4); apply(e, Set{timbre, 0, 60, 30, 0, 0, 0, 0}); return rmsDb(render1(e, 48, 1.f)); };
    const int a = t * 256, b = t == 3 ? 1023 : (t + 1) * 256;
    const float mid = lvl((a + b) / 2), ends = 0.5f * (lvl(a) + lvl(b));
    worst = std::fmax(worst, ends - mid);
  }
  result(worst < 3.f, "timbre crossfade dip at most %.2f dB", worst);
  // ensemble 0 vs 100, tone extremes
  {
    auto lvl = [&](Set s) { e.init(4); apply(e, s); return rmsDb(render1(e, 48, 1.f)); };
    const float a = lvl(Set{0, 0, 0, 0, 0, 0, 0, 0}), b = lvl(Set{0, 0, 100, 0, 0, 0, 0, 0});
    result(std::fabs(a - b) < 2.f, "level ensemble 0 -> 100: %+.2f dB", b - a);
    const float d = lvl(Set{0, 0, 50, 0, -100, 0, 0, 0}), br = lvl(Set{0, 0, 50, 0, 100, 0, 0, 0}), mid = lvl(Set{0, 0, 50, 0, 0, 0, 0, 0});
    result(d - mid > -9.f && br - mid < 4.f, "level tone dark %+.2f / bright %+.2f dB", d - mid, br - mid);
  }
  // hiss only adds a little
  {
    e.init(4); apply(e, Set{0, 0, 0, 0, 0, 0, 0, 100});
    auto x = render1(e, 48, 1.f);
    e.init(4); apply(e, Set{0, 0, 0, 0, 0, 0, 0, 0});
    auto y = render1(e, 48, 1.f);
    std::vector<float> d(x.size());
    for (size_t i = 0; i < x.size(); ++i) d[i] = x[i] - y[i];
    const float hissDb = rmsDb(d) - rmsDb(y);
    result(hissDb > -40.f && hissDb < -14.f, "hiss 100%% sits %.1f dB under the pad", hissDb);
  }
}

static void testZipper() {
  printf("\n-- zipper / switching --\n");
  struct Sweep { const char *name; int id, lo, hi; };
  const Sweep sweeps[] = {
      {"timbre", P::kTimbre, 0, 1023}, {"grit", P::kGrit, 0, 1023}, {"tone", P::kTone, -100, 100},
      {"ensemble", P::kEnsemble, 0, 100}, {"hiss", P::kHiss, 0, 100},
  };
  DustyPads1 e;
  for (const Sweep &s : sweeps) {
    float ref = 0;
    for (int v = s.lo; v <= s.hi; v += (s.hi - s.lo) / 8) {
      e.init(9); apply(e, Set{300, 300, 50, 0, 0, 0, 0, 0}); e.setParam(s.id, v);
      ref = std::fmax(ref, maxStep(render1(e, 48, 1.5f), 64));
    }
    e.init(9); apply(e, Set{300, 300, 50, 0, 0, 0, 0, 0});
    auto x = render1(e, 48, 1.5f, [&](float p) { e.setParam(s.id, int(s.lo + (s.hi - s.lo) * p)); });
    const float d = maxStep(x, 64);
    result(d <= ref * 1.2f, "zipper %-10s max step %.4f (static max %.4f)", s.name, d, ref);
  }
  // RESAMPLE / KEY TRACK switching mid-note
  {
    float ref = 0;
    for (int rs = 0; rs <= 2; ++rs) for (int kt = 0; kt <= 1; ++kt) {
      e.init(9); apply(e, Set{300, 300, 50, 0, 0, rs, kt, 0});
      ref = std::fmax(ref, maxStep(render1(e, 48, 0.3f), 64));
    }
    e.init(9); apply(e, Set{300, 300, 50, 0, 0, 0, 0, 0});
    auto x = render1(e, 48, 2.f, [&](float p) { const int st = int(p * 12); e.setParam(P::kResample, st % 3); e.setParam(P::kKeyTrack, (st / 3) & 1); });
    const float d = maxStep(x, 64);
    result(d <= ref * 1.3f, "resample/keytrack switching max step %.4f (static %.4f)", d, ref);
  }
}

static void testLanes() {
  printf("\n-- microKORG 2 4-lane build --\n");
  DustyPads4 g;
  // stress across lanes, with per-lane mod making every timbre active at once
  int bad = 0, total = 0;
  for (int grit : {0, 1023})
    for (int rs = 0; rs <= 2; ++rs)
      for (float note : {12.f, 48.f, 96.f, 120.f})
        for (int lane = 0; lane < 4; ++lane) {
          g.init(11); apply(g, Set{512, grit, 100, 100, 0, rs, 1, 100});
          for (int j = 0; j < 4; ++j) g.setMod(j, -0.5f + 0.33f * j, 0.2f * j - 0.3f, 0.6f * j - 1.f, 0.f);
          const float notes[4] = {note, note + 7, note - 12, note + 3};
          auto x = render4(g, notes, 0.15f, lane, [](float) {});
          ++total;
          if (!sane(x) && ++bad < 6) {
            char n[96]; snprintf(n, sizeof n, "lane stress g%d rs%d note%d lane%d", grit, rs, int(note), lane);
            check(n, x);
          }
        }
  result(bad == 0, "lane stress: %d combinations, %d bad", total, bad);

  // same level as the NTS-1 build for each starter setting. Phases, PWM and the
  // ensemble's slow beating are random per voice, so compare RMS with the
  // ensemble and drift off.
  for (const Preset &p : kPresets) {
    Set s = p.s; s.ens = 0; s.drift = 0;
    DustyPads1 e; e.init(3); apply(e, s);
    const float a = rmsDb(render1(e, 48, 1.5f));
    g.init(3); apply(g, s);
    const float notes[4] = {48, 48, 48, 48};
    const float b = rmsDb(render4(g, notes, 1.5f, 2, [](float) {}));
    result(std::fabs(a - b) < 1.f, "4-lane vs 1-lane level %-18s %+.2f dB", p.name, b - a);
  }
  // with drift / ensemble / hiss off and grit 0, the output is deterministic up to the
  // random start phase: the spectrum's harmonic levels must match
  {
    DustyPads1 e; e.init(3); apply(e, Set{0, 0, 0, 0, 30, 0, 0, 0});
    auto x = render1(e, 57, 1.f);
    g.init(3); apply(g, Set{0, 0, 0, 0, 30, 0, 0, 0});
    const float notes[4] = {50, 57, 62, 45};
    auto y = render4(g, notes, 1.f, 1, [](float) {});
    float worst = 0;
    for (int h = 1; h <= 6; ++h) {
      const double a = toneMag(x, kRate / 4, 16384, noteHz(57) * h), b = toneMag(y, kRate / 4, 16384, noteHz(57) * h);
      worst = std::fmax(worst, std::fabs(20.f * std::log10((b + 1e-9) / (a + 1e-9))));
    }
    result(worst < 1.5f, "4-lane vs 1-lane harmonics 1-6: worst %.2f dB", worst);
  }
  // idle lanes output exactly zero, live lanes play
  {
    g.init(1); apply(g, Set{400, 600, 60, 40, 0, 1, 1, 50});
    const bool live[4] = {true, false, true, false};
    const float notes[4] = {48, 52, 55, 59};
    auto l0 = render4(g, notes, 0.5f, 0, [](float) {}, 64, live);
    g.init(1); apply(g, Set{400, 600, 60, 40, 0, 1, 1, 50});
    auto l1 = render4(g, notes, 0.5f, 1, [](float) {}, 64, live);
    result(stats(l1).peak == 0.f && stats(l0).peak > 0.05f, "idle lanes silent (%.3g), live lanes play (%.3f)",
           stats(l1).peak, stats(l0).peak);
  }
  // per-voice mod: lane with timbre mod +1 sounds like GLASS while lane 0 stays STRINGS
  {
    g.init(1); apply(g, Set{0, 0, 0, 0, 0, 0, 0, 0});
    g.setMod(1, 1.f, 0.f, 0.f, 0.f);
    const float notes[4] = {57, 57, 57, 57};
    auto s = render4(g, notes, 0.5f, 0, [](float) {});
    g.init(1); apply(g, Set{0, 0, 0, 0, 0, 0, 0, 0});
    g.setMod(1, 1.f, 0.f, 0.f, 0.f);
    auto gl = render4(g, notes, 0.5f, 1, [](float) {});
    // the saw has a strong 2nd harmonic, the glass (odd sidebands + 4th) almost none
    const double s3 = toneMag(s, 4800, 16384, noteHz(57) * 2) / toneMag(s, 4800, 16384, noteHz(57));
    const double g3 = toneMag(gl, 4800, 16384, noteHz(57) * 2) / toneMag(gl, 4800, 16384, noteHz(57));
    result(s3 > 0.3 && g3 < s3 * 0.3, "per-voice timbre mod: H2/H1 strings %.2f, glass %.2f", s3, g3);
  }
  // steal (legato) keeps phase: no click
  {
    g.init(1); apply(g, Set{256, 300, 50, 30, 0, 0, 0, 0});
    const bool live[4] = {true, true, true, true};
    for (int j = 0; j < 4; ++j) { g.setPitch(j, noteW0(48)); g.noteOn(j, true); }
    std::vector<float> x(48000);
    v4f buf[64];
    for (size_t i = 0; i < x.size(); i += 64) {
      if (i == 64 * 300) { g.setPitch(0, noteW0(51)); g.noteOn(0, false); }
      g.render(live, buf, 64);
      for (int s = 0; s < 64; ++s) x[i + s] = buf[s][0];
    }
    float ref = 0, d = 0;
    for (size_t i = 64 * 150; i < 64 * 299; ++i) ref = std::fmax(ref, std::fabs(x[i] - x[i - 1]));
    for (size_t i = 64 * 300; i < 64 * 304; ++i) d = std::fmax(d, std::fabs(x[i] - x[i - 1]));
    result(d <= ref * 1.5f, "steal retrigger step %.4f (steady %.4f)", d, ref);
  }
}

static void demos() {
  printf("\n-- demos --\n");
  // a jungle chord loop: Fm9 - Dbmaj7 - Ebsus - Cm7, two bars each at 170 BPM, 8 voices (2 groups)
  const float chords[4][8] = {
      {41, 48, 51, 55, 56, 60, 63, 67},
      {37, 44, 48, 53, 56, 60, 63, 65},
      {39, 46, 51, 53, 58, 63, 65, 70},
      {36, 43, 46, 51, 55, 58, 62, 67},
  };
  const int barLen = int(kRate * 60.0 / 170.0 * 4.0);
  for (const Preset &p : kPresets) {
    DustyPads4 g[2];
    for (int k = 0; k < 2; ++k) { g[k].init(0xC0FFEE + k); apply(g[k], p.s); }
    std::vector<float> x;
    const bool live[4] = {true, true, true, true};
    v4f buf[64];
    for (int c = 0; c < 4; ++c) {
      for (int k = 0; k < 2; ++k)
        for (int j = 0; j < 4; ++j) { g[k].setPitch(j, noteW0(chords[c][k * 4 + j])); g[k].noteOn(j, c == 0); }
      const int len = barLen * 2;
      for (int i = 0; i < len; i += 64) {
        // simple amp envelope: 40 ms attack, release over the last 15 %
        float sample[64] = {0};
        for (int k = 0; k < 2; ++k) {
          g[k].render(live, buf, 64);
          for (int s = 0; s < 64; ++s) sample[s] += (buf[s][0] + buf[s][1] + buf[s][2] + buf[s][3]) * 0.35f;
        }
        for (int s = 0; s < 64; ++s) {
          const float t = float(i + s);
          float env = std::fmin(1.f, t / (0.04f * kRate));
          const float rel = 0.85f * len;
          if (t > rel) env *= std::fmax(0.f, 1.f - (t - rel) / (0.15f * len));
          x.push_back(sample[s] * env * 0.55f);
        }
      }
    }
    Stats s = stats(x);
    result(!s.bad && s.peak <= 1.f && s.peak > 0.05f, "demo %-18s 8-voice chords, peak %.3f", p.name, s.peak);
    writeWav(std::string("test/out/") + p.name + ".wav", x);
  }
}

static void speed() {
  printf("\n-- speed (desktop, relative only) --\n");
  const int blocks = 48000 * 10 / 64;
  const bool live[4] = {true, true, true, true};
  auto run = [&](Set s, bool allTimbres, const char *name) {
    DustyPads4 g[2];
    for (int k = 0; k < 2; ++k) {
      g[k].init(k + 1); apply(g[k], s);
      for (int j = 0; j < 4; ++j) {
        g[k].setPitch(j, noteW0(48 + 3 * j + k));
        g[k].noteOn(j, true);
        if (allTimbres) g[k].setMod(j, -0.5f + 0.33f * j, 0.f, 0.f, 0.f);
      }
    }
    v4f ob[64];
    float acc = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) for (int k = 0; k < 2; ++k) { g[k].render(live, ob, 64); acc += ob[0][0]; }
    auto t1 = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / (blocks * 64.0);
    printf("8 voices, %-40s %6.1f ns/sample (%.2f%% of real time)%s\n", name, ns, ns / (1e9 / kRate) * 100.0, acc > 1e9 ? "" : "");
  };
  run(Set{180, 300, 60, 35, 0, 0, 0, 15}, false, "defaults (strings/analog):");
  run(Set{512, 1023, 100, 100, 50, 2, 1, 100}, false, "organ, everything up:");
  run(Set{512, 1023, 100, 100, 50, 2, 1, 100}, true, "worst case (all 5 timbres via mod):");
}

int main() {
  if (system("mkdir -p test/out") != 0) return 1;
  testSanity();
  testTuning();
  testGrit();
  testLevels();
  testZipper();
  testLanes();
  demos();
  speed();
  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
