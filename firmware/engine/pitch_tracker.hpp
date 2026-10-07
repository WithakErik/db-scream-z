#pragma once
#include <q/support/literals.hpp>
#include <q/pitch/pitch_detector.hpp>

// The BACF analysis window is word-size dependent (bacf_period_detector
// rounds its window up to a whole number of cycfi::q::bitset<> words). The
// v12 JS reference hardcodes 32-bit words; firmware/host/third_party/q's
// bitset shim pins natural_uint to uint32_t to match on the host build (see
// FIRMWARE.md gotcha 7 and MILESTONE0.md section 1). This assert is the
// authoritative, header-level guard: any TU that includes this header (the
// host build, tests, and the target firmware alike) fails to compile
// instead of silently tracking a different, wrong period than the
// ear-validated v12 reference.
static_assert(cycfi::q::bitset<>::value_size == 32,
              "BACF word size must be 32 bits to match the v12 reference (FIRMWARE.md gotcha 7)");

// FIRMWARE.md section 3 item 1: pitch_detector(70_Hz, 1300_Hz, sps, -45_dB),
// fed the FULL-RATE post-inputGain sample, get_frequency() per sample,
// hold last value when it returns 0.
//
// Behavioral reference: the line-faithful JS port at the top of
// dbscreamz_lab/static/fof-processor.js (lines 19-443):
//   QPitchDetector(70, 1300, sr, -45), .process(x), .frequency, .periodicity()
//
// 2026-09-24: the detector still reads the RAW sample. A lowpass on the
// tracker's copy was built, measured and NOT shipped:
//
//   - q's dynamic_smoother (the lowpass stage of q's signal_conditioner,
//     685 Hz base for 70-1300 Hz) trimmed the autocorrelate() burst 17-33%
//     (worst window in ACF calls on guitar_long.wav as recorded / +1 / +2
//     octaves, x1 input gain: 75 / 208 / 539 -> 51 / 144 / 360; the 99th
//     percentile at +2 octaves barely moved, 359 -> 342), but moved the
//     render's octave-class disagreement with the v12 trace from 0.29%
//     (gate only) to ~9%. A spectral check found it right more often than
//     the old tracker on weak-fundamental low notes (the 87.6 Hz low F at
//     25.0 s of guitar_long.wav) and wrong in new ways (two octaves low at
//     21.8 s). The user's A/B listening test on 2026-09-24 preferred the
//     gate alone, so only OctaveGate and the silence reset below shipped.
//   - The full signal_conditioner was worse still: its compressor's makeup
//     gain lifts decaying tails over the -45 dB hysteresis (x4 worst window
//     at +2 octaves 646 -> 1005), its noise gate is redundant below that
//     hysteresis and its -24 dB onset would deafen the tracker to soft
//     swells, its 70 Hz highpass doubled octave jumps (24% octave-class vs
//     v12), and its lin_to_db / fast_tanh would be new ISR transcendentals
//     against a ~94% flash budget.

// OctaveGate: a confidence gate on top of pitch_detector's output, fed ONE
// reading per analysis window (every window/2 = 688 samples, 14.3 ms, at
// 48 kHz), never per sample, so it costs a few compares ~70 times a second
// and no transcendentals: the octave and same-note bands are precomputed
// ratios, not log2() of anything.
//
//   - A reading is only considered when periodicity >= kMinPeriodicity. 0.8
//     is q's own pitch_detector::min_periodicity, the bar q applies to its
//     own frequency shifts. Measured at 0.7 / 0.8 / 0.9 on guitar_long.wav
//     (with the since-dropped smoother in front, see above):
//     octave jumps 40 / 40 / 40 summed over six gain/speed cases, so the
//     threshold is not what stops octave errors, while f0 updates over the
//     render fell 830 / 762 / 574: 0.9 starts ignoring bends and finger
//     vibrato. q's value stays.
//   - A jump within 50 cents of an octave, up or down, must repeat for
//     kOctaveHoldWindows consecutive confident windows (~43 ms) before it
//     is taken. Any window that does not repeat it restarts the count,
//     unconfident ones included, so a harmonic or sub-harmonic blip that
//     comes back is never heard. A real octave leap costs 43 ms.
//   - Every other change is a note change and is taken at once.
//   - The first confident reading after unvoiced() (silence, see
//     PitchTracker) is taken at once whatever it is: after a gap there is
//     no current note for it to be an octave error of.
class OctaveGate {
 public:
  static constexpr float kMinPeriodicity = 0.8f;
  static constexpr int kOctaveHoldWindows = 3;

  void window(float f, float periodicity) {
    if (f <= 0.0f || periodicity < kMinPeriodicity) {
      pending_n_ = 0;
      return;
    }
    if (!voiced_) {
      voiced_ = true;
      accept(f);
      return;
    }
    const double r = f / f0_;
    const bool octave = (r > kOctLo && r < kOctHi) || (r > 1.0 / kOctHi && r < 1.0 / kOctLo);
    if (!octave) {
      accept(f);
      return;
    }
    // The same candidate again? Within 50 cents, so a bend still counts.
    const double rp = f / pending_f_;
    pending_n_ = (pending_n_ > 0 && rp > kSameLo && rp < kSameHi) ? pending_n_ + 1 : 1;
    pending_f_ = f;
    if (pending_n_ >= kOctaveHoldWindows) accept(f);
  }

  // Silence: keep holding f0, but the next confident reading is a new note.
  void unvoiced() {
    voiced_ = false;
    pending_n_ = 0;
  }

  double f0() const { return f0_; }

 private:
  void accept(float f) {
    f0_ = f;
    pending_n_ = 0;
  }

  static constexpr double kOctLo = 1.94251;    // 2 * 2^(-50/1200)
  static constexpr double kOctHi = 2.05917;    // 2 * 2^(+50/1200)
  static constexpr double kSameLo = 0.971532;  // 2^(-50/1200)
  static constexpr double kSameHi = 1.029302;  // 2^(+50/1200)

  double f0_ = 200.0;  // JS liveF0 init, line 567
  double pending_f_ = 1.0;
  int pending_n_ = 0;
  bool voiced_ = false;
};

// PitchTracker: pitch_detector -> OctaveGate.
//
// Silence (2026-09-24). q's pitch_detector never forgets: its frequency
// stays at the last note through any gap, and on the next note its bias()
// harmonic snap folds anything near a multiple of that old frequency back
// onto it. Measured: A2, 0.5 s of silence, then A3 read 110 Hz for as long
// as A3 rang, at periodicity 1.0 (test_pitch_tracker case 1b). So when no
// analysis window has completed for 1.5 windows (2064 samples, 43 ms; on a
// sustained 70 Hz note windows are at most one window, 1376 samples,
// apart), the tracker calls pd_.reset(), which zeroes q's frequency so its
// next lock is a fresh one (periodicity >= 0.9, q's max_deviation), and
// tells the gate the next reading is a new note. f0() holds the last note
// throughout, as it always has.
class PitchTracker {
 public:
  explicit PitchTracker(float sps)
      : pd_(kLowest, kHighest, sps, cycfi::q::dB(-45.0)),
        silence_after_(pd_.edges().window_size() * 3 / 2) {}

  void process(float x) {
    const bool ready = pd_(x);
    periodicity_ = pd_.periodicity();
    if (ready) {
      since_ready_ = 0;
      gate_.window(pd_.get_frequency(), periodicity_);
    } else if (++since_ready_ == silence_after_) {
      gate_.unvoiced();
      pd_.reset();
    }
  }

  double f0() const { return gate_.f0(); }
  double periodicity() const { return periodicity_; }

 private:
  static constexpr cycfi::q::frequency kLowest{70.0f};
  static constexpr cycfi::q::frequency kHighest{1300.0f};

  cycfi::q::pitch_detector pd_;
  OctaveGate gate_;
  std::size_t silence_after_;
  std::size_t since_ready_ = 0;
  double periodicity_ = 0.0;
};
