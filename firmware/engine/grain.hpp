// grain.hpp
#pragma once
#include "fof_params.hpp"
#include <cmath>
#include <algorithm>

// Verbatim transcription of buildGrain(), fof-processor.js lines 619-669.
// Writes kMaxUnison grains of identical length into `grains` (row-major
// [voice][sample]); returns grain length n = max(16, round(sr*grain_ms/1000)).
inline int build_grains(const FofParams& p, double sr,
                         float grains[kMaxUnison][kMaxGrainLen]) {
  // Clamped to kMaxGrainLen (fixed-size C++ buffer); JS is immune since
  // Float32Array resizes per-call. Where this clamp bites depends on the
  // build: host default kMaxGrainLen=4800 (100 ms @ 48k) diverges only for
  // grain_ms > 100 ms, which the lab UI's slider (capped at 40 ms) never
  // produces; firmware's -DDBSCREAMZ_MAX_GRAIN_LEN=1920 diverges above
  // 40 ms (1920 samples @ 48k), which is exactly where menu 3 knob 6 tops
  // out (param_map.hpp map_grain). So neither build can reach its clamp
  // through the controls, and the two stay in agreement everywhere the
  // pedal can go.
  const int n = std::min(kMaxGrainLen, std::max(16, (int)jsRound(sr * p.grain_ms * 0.001)));

  // cosine window, matching voice.c build_window_and_decay()
  const int aLen = std::max(1, (int)jsRound(sr * 0.0018));
  const int rStart = std::min(n - 1, (int)jsRound(sr * 0.013 * (p.grain_ms / 20.0)));
  const int rLen = std::max(1, (int)jsRound(sr * 0.007 * (p.grain_ms / 20.0)));

  const double F[3] = { p.f1 * p.formant_scale, p.f2 * p.formant_scale, p.f3 * p.formant_scale };
  const double BW[3] = { p.bw1, p.bw2, p.bw3 };
  const double A[3] = { p.a1, p.a2, p.a3 };

  const double TAU = 2 * M_PI;
  for (int u = 0; u < kMaxUnison; u++) {
    for (int i = 0; i < n; i++) {
      const double t = i / sr;
      double s = 0;
      for (int k = 0; k < 3; k++) {
        // Deterministic per-voice, per-formant phase scatter (golden-ratio
        // sequence). Voice 0 keeps zero phase so unison=1 is the exact
        // MonkSynth grain. No runtime randomness, no noise.
        double ph = 0;
        if (u != 0) {
          double x = (u * 0.61803398875 + k * 0.3819660113) * (k + 1);
          ph = TAU * (x - std::floor(x));
        }
        // damped sinusoid: exp(-pi*BW*i/sr) * sin(2*pi*F*t + phase)
        s += A[k] * std::sin(TAU * F[k] * t + ph) * std::exp(-M_PI * BW[k] * i / sr);
      }
      // MonkSynth aspiration: two INHARMONIC sinusoids, not white noise.
      if (p.aspiration > 0) {
        const double p1 = sr * 0.000202, p2 = sr * 0.000263;
        const double d1 = std::exp(-M_PI * 50 * (i * 3.0) / sr);
        const double d2 = std::exp(-M_PI * 50 * (i * 3.5) / sr);
        s += p.aspiration * (std::sin(TAU * i / p1) * d1 + std::sin(TAU * i / p2) * d2);
      }
      // window
      double w = 1.0;
      if (i < aLen) w = 0.5 * (1 - std::cos(M_PI * i / aLen));
      if (i >= rStart) w = std::max(0.0, 0.5 * (1 - std::cos(M_PI * (rLen + i) / rLen)));
      // MonkSynth's release deliberately stops short of zero ("open tail"),
      // so every grain ends on a step. Optional final taper to zero.
      if (p.taper_end) {
        const int tail = std::max(1, (int)jsRound(n * 0.12));
        if (i >= n - tail) w *= 0.5 * (1 + std::cos(M_PI * (i - (n - tail)) / tail));
      }
      grains[u][i] = (float)(s * w);
    }
  }
  return n;
}
