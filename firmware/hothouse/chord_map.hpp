// chord_map.hpp - chord mode's knob layout, the ChordParams ->
// ChordEngineParams bridge, and its charge overlay (chord mode design
// spec section 3). Host-testable: no libDaisy types.
#pragma once
#include <cmath>

#include "chord_engine.hpp"
#include "charge.hpp"
#include "param_map.hpp"
#include "voice_params.hpp"

// Chord mode has two layers: menu 1 (normal play) and the chord menu (left
// hold). There is no menu 3.
enum class ChordLayer : unsigned char { Main, Menu };

// Drive pre-gain, 1x..40x on a log taper, so the middle of the knob is
// about 6.3x rather than 20x. Computed in double on purpose: the float
// overload calls powf, a new libm symbol (~1.6 KB) the firmware does not
// otherwise link at ~94% flash, while double pow is already linked (final
// review, 2026-09-25). Same for map_bw_scale.
inline float map_drive(float t) {
  return static_cast<float>(std::pow(40.0, static_cast<double>(t)));
}

// Resonance 0..1 to the bandwidth multiplier, 2.0 (soft) .. 0.35 (sharp)
// on a log taper.
inline float map_bw_scale(float resonance) {
  return static_cast<float>(
      2.0 * std::pow(0.35 / 2.0, static_cast<double>(resonance)));
}

// Knob k (0..5, same physical order as apply_knob) at position t (0..1).
// Menu 1 knobs 1-4 deliberately reuse normal mode's maps so the level
// knobs feel identical in both modes.
inline void apply_chord_knob(ChordLayer layer, int k, float t, ChordParams& c) {
  if (layer == ChordLayer::Main) {
    switch (k) {
      case 0: c.vocal_vol   = map_lin(t, 0.0f, 2.0f); break;
      case 1: c.mix         = map_lin(t, 0.0f, 1.0f); break;
      case 2: c.master_vol  = map_lin(t, 0.0f, 2.0f); break;
      case 3: c.tone        = map_tone(t); break;
      case 4: c.sensitivity = map_cube(t, 0.0f, 8.0f); break;
      case 5: c.drive       = map_drive(t); break;
    }
    return;
  }
  switch (k) {
    case 0: c.closed_vowel = map_lin(t, 0.0f, 4.0f); break;
    case 1: c.open_vowel   = map_lin(t, 0.0f, 4.0f); break;
    case 2: c.vocal_size   = map_vocal_size(t); break;
    case 3: c.resonance    = t; break;
    case 4: c.attack_ms    = map_cube(t, 1.0f, 50.0f); break;
    case 5: c.release_ms   = map_cube(t, 20.0f, 500.0f); break;
  }
}

// input_gain: 1.0 on the pedal (hardware analog gain), 4.0 in the host
// render and the emulator (the lab's digital stand-in the gate thresholds
// were tuned against). glide_ms is the shift glide apply_charge_chord()
// returns; 0 (the default) lands toggle moves at once.
inline ChordEngineParams to_chord_engine_params(const ChordParams& c,
                                                bool mouth_open,
                                                double input_gain,
                                                double glide_ms = 0.0) {
  ChordEngineParams p;
  p.input_gain = input_gain;
  p.gate = kGateLevels[c.gate_level];
  p.drive = c.drive;
  p.sensitivity = c.sensitivity;
  p.closed_vowel = c.closed_vowel;
  p.open_vowel = c.open_vowel;
  p.formant_scale = formant_scale_from(c.vocal_size);
  p.bw_scale = map_bw_scale(c.resonance);
  p.attack_ms = c.attack_ms;
  p.release_ms = c.release_ms;
  p.mouth_open = mouth_open;
  p.gain = 1.0;
  p.octave = c.octave;
  p.glide_ms = glide_ms;
  return p;
}

// The charged chord setting plus the shift glide the pitch row asks for.
// The glide is never stored, so it has no home in ChordParams and rides
// alongside.
struct ChargedChord {
  ChordParams params;
  float glide_ms;
};

// Charge in chord mode (chord mode spec section 3; chord octave spec,
// 2026-10-06): the gain, tone, size AND pitch rows use apply_charge()'s
// formulas exactly, by running them on a VoiceParams carrying those
// fields, so the two modes can never drift. Pitch is therefore regular
// mode's two-octave sweep from the toggle octave: a 2000 ms glide while
// charging, the decay time on the way home. Chord mode has no glide knob,
// so its base glide is 0. A pure copy: the chord setting itself is never
// written, so a charge can never be auto-saved.
//
// Inherited, not fixed here: once the level reaches 0, apply_charge() stops
// overriding the glide, so any return glide still in flight finishes at the
// base glide (0, at once). Regular mode does the same. If it is audible,
// fix it once in apply_charge() for both modes.
inline ChargedChord apply_charge_chord(const ChordParams& base,
                                       const ChargeConfig& c, float level,
                                       bool charging) {
  if (level <= 0.0f) return {base, 0.0f};
  VoiceParams v{};
  v.vocal_vol = base.vocal_vol;
  v.tone = base.tone;
  v.vocal_size = base.vocal_size;
  v.octave = base.octave;
  v.glide_ms = 0.0f;
  const VoiceParams r = apply_charge(v, c, level, charging);
  ChordParams out = base;
  out.vocal_vol = r.vocal_vol;
  out.tone = r.tone;
  out.vocal_size = r.vocal_size;
  out.octave = r.octave;
  return {out, r.glide_ms};
}
