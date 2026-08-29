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
// Behavioural reference: the line-faithful JS port at the top of
// dbscreamz_lab/static/fof-processor.js (lines 19-443):
//   QPitchDetector(70, 1300, sr, -45), .process(x), .frequency, .periodicity()
class PitchTracker {
 public:
  explicit PitchTracker(float sps)
      : pd_(cycfi::q::frequency(70.0f), cycfi::q::frequency(1300.0f), sps,
            cycfi::q::dB(-45.0)) {}

  void process(float x) {
    pd_(x);
    float f = pd_.get_frequency();
    if (f > 0.0f) f0_ = f;  // hold last on unvoiced (get_frequency() == 0)
    periodicity_ = pd_.periodicity();
  }

  double f0() const { return f0_; }
  double periodicity() const { return periodicity_; }

 private:
  cycfi::q::pitch_detector pd_;
  double f0_ = 200.0;  // JS liveF0 init, line 567
  double periodicity_ = 0.0;
};
