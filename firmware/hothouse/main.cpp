// DBscreamZ - Milestone 5: control surface, voice memory, post chain.
// Spec: docs/superpowers/specs/2026-08-26-milestone5-controls-design.md
// (approved 2026-08-26; replaces the PROVISIONAL milestones 2-4 map).
//
// Control map (physical, facing the pedal; FOOTSWITCH_1/LED_1 = LEFT,
// FOOTSWITCH_2/LED_2 = RIGHT, verified against the Hothouse PCB netlists):
//   Knobs 1-6 (menu 1): vocal vol, mix, master vol, tone, glide, vocal size
//   RIGHT hold ~1 s   : latch menu 2 (F1/F2 formants), right LED blinks
//   LEFT hold ~1 s    : latch menu 3 (F3, vibrato, detune), left LED
//                       blinks
//   Stomp taps        : recall/engage slots, tap again = bypass; while a
//                       menu is latched the OTHER stomp's tap = SAVE
//   Toggle 1: octave +1/0/-1   Toggle 2: page Set1/Freeform/Set2
//   Toggle 3: gate high/med/low (kGateLevels, milestone 3 calibration TBD)
//   Both stomps together (engaged, no menu): CHARGE MODE power-up ramp,
//   LEDs alternate and accelerate; release decays. Config via toggles
//   while a menu is latched. Spec:
//   docs/superpowers/specs/2026-08-27-charge-mode-design.md
//   Both stomps held THROUGH power-up: beast mode, the hidden bank
//   (beast_presets.hpp). Session only, never written to QSPI.
// Bypass = milestone 1 passthrough; engage ramps in over 10 ms.

#include <cmath>   // std::isfinite, guarding the CPU report against NaN

#include "daisy_seed.h"
#include "hothouse.h"
#include "util/PersistentStorage.h"
#include "util/CpuLoadMeter.h"

#include "fof_engine.hpp"
#include "post_chain.hpp"
#include "charge.hpp"
#include "ui_controller.hpp"
#include "beast_presets.hpp"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::CpuLoadMeter;
using daisy::Led;
using daisy::PersistentStorage;
using daisy::SaiHandle;
using daisy::System;

Hothouse hw;
Led led_left, led_right;  // LED_1 = physical LEFT, LED_2 = physical RIGHT
FofEngine* eng = nullptr;
PostChain* post = nullptr;
PersistentStorage<VoiceStore>* storage = nullptr;
UiController ui;

// ---- CPU load instrumentation (2026-09-02) --------------------------------
// FIRMWARE.md section 9 has said since the start that the per-block cost
// budget "was never measured with a CpuLoadMeter" and that "that measurement
// remains open". Three fixes have now been aimed at a high-note crackle on
// the strength of that unmeasured estimate. This closes it.
//
// The number that matters is NOT the average: at kBlockSize 128 / 48 kHz each
// callback has a 2.67 ms deadline (1 ms at the 48 it measured at first), and
// ONE block over budget is one audible dropout. So the worst block is logged, along with the tracked f0 at the
// moment it happened, because the whole hypothesis is that cost rises with
// pitch. If the crackle is a deadline miss, worst-block load will sit near
// or above 100% exactly when f0 is high.
//
// Cost when idle: two System::GetTick() reads and a handful of floats per
// block. The USB serial log is written from the MAIN LOOP only, never from
// the audio IRQ.
CpuLoadMeter cpu_meter;
// Written by the audio IRQ, read and cleared by the main loop. A torn read
// across that boundary would cost one wrong diagnostic line and nothing
// else, and both are naturally-aligned 32-bit scalars on Cortex-M7, so no
// locking. Deliberately not volatile-qualified beyond that.
volatile float cpu_worst_load = 0.0f;
volatile float cpu_worst_f0 = 0.0f;

// The grain tables are trimmed on this target (Makefile: MAX_GRAIN_LEN).
// Grain length is no longer knob-driven: the knob (and map_grain, and its
// 4..40 ms sweep) were retired on 2026-09-02, and to_fof_params() now pins
// grain to a constant 20 ms (960 samples at 48 kHz). 1920 is kept as a
// deliberately conservative bound rather than shrunk to the new 960-sample
// requirement: it is double the pin's actual need, costs nothing at this
// target's flash budget, and leaves headroom if the pin is ever loosened
// again without a second look at this assert. Checked here because this is
// the translation unit that carries the -D override; the host builds keep
// their roomier 4800 default and clear it either way.
static_assert(kMaxGrainLen >= 1920,
              "kMaxGrainLen must cover the 20 ms grain pin at 48 kHz, kept "
              "at 2x headroom");

// The pinned voice count, named so the assert below can tie it to the grain
// tables. build_grains() fills all kMaxUnison voices on every rebuild and
// process_block only ever reads the first p.unison of them, so the two
// numbers being equal is what stops the pedal building tables it never
// reads: that was 5 of 8 voices (75 KB of SRAM) until 2026-09-02.
//
// fof_engine.hpp:170 clamps the requested stack to kMaxUnison, so exceeding
// it could never overrun the tables; it would silently give you fewer voices
// than the pin asks for, which is worse. This turns that into a build error.
static constexpr int kPinnedUnison = 3;
static_assert(kPinnedUnison <= kMaxUnison,
              "to_fof_params pins more voices than the grain tables hold; "
              "raise DBSCREAMZ_MAX_UNISON in the Makefile to match");

// 128, not the libDaisy/Hothouse default of 48 (2026-09-03). The pitch
// tracker's autocorrelate() burst runs inside ONE sample once per 688-sample
// window, so it lands in at most one block whatever the block size; at 48
// samples it had a 1 ms deadline and the CpuLoadMeter measured the block
// it landed in at 100-132% above ~650 Hz (FIRMWARE.md section 9), which was
// the high-note crackle. At 128 the same burst has 2.67 ms to fit in. The
// per-sample work scales with the block and the burst does not, so this
// buys margin rather than merely moving the line. Costs ~1.7 ms of
// latency. 128 is also the block the lab reference and host/render.cpp
// use, and the top of process_block's supported range (n <= 128).
//
// Everything else in the callback is per-sample (the engage ramp) or
// millisecond-timed (UI gestures, charge ramp, switch debounce), and
// Hothouse::SetAudioBlockSize re-derives the knob smoothing rate, so the
// coarser tick changes nothing but latency.
static constexpr size_t kBlockSize = 128;
// Engage/bypass crossfade over 10 ms: click-free transition (spec sec 7).
static constexpr float kRampStep = 1.0f / (0.010f * 48000.0f);
static float ramp = 0.0f;  // 0 = bypass output, 1 = processed output

static TogglePos to_pos(Hothouse::ToggleswitchPosition p) {
  if (p == Hothouse::TOGGLESWITCH_UP) return TogglePos::Up;
  if (p == Hothouse::TOGGLESWITCH_DOWN) return TogglePos::Down;
  return TogglePos::Middle;
}

static UiInputs read_inputs() {
  UiInputs in;
  in.left_down = hw.switches[Hothouse::FOOTSWITCH_1].Pressed();
  in.right_down = hw.switches[Hothouse::FOOTSWITCH_2].Pressed();
  in.t_octave = to_pos(hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1));
  in.t_page = to_pos(hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2));
  in.t_gate = to_pos(hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3));
  for (int i = 0; i < 6; i++) in.knobs[i] = hw.knobs[i].Value();
  in.now_ms = System::GetNow();
  return in;
}

// Edit buffer -> engine params. Everything not listed keeps the v12
// defaults baked into FofParams (FIRMWARE.md section 5).
static FofParams to_fof_params(const VoiceParams& v) {
  FofParams p;
  p.f1 = v.f1; p.f2 = v.f2; p.f3 = v.f3;
  p.bw1 = v.bw1; p.bw2 = v.bw2; p.bw3 = v.bw3;
  p.a1 = v.a1; p.a2 = v.a2; p.a3 = v.a3;
  // Unison is pinned for the same reason aspiration is: fof_engine.hpp is a
  // transcription of the frozen lab engine and keeps its unison support, so
  // the pedal switches it off here rather than cutting the engine.
  //
  // 3 is the engine's own long-standing default (FofParams::unison) and what
  // every character ran before the menu 3 knob existed; it is also what
  // tools/ref_render.js and host/render.cpp render at, so the golden
  // reference renders stay valid. Stacks above 3 were the "ringmod" heard on
  // hardware 2026-09-01, audible even at mix 0 where the voice path is
  // multiplied by zero, which is what ruled the voice path out and left
  // per-block cost as the cause.
  //
  // 2026-09-02: briefly dropped to 1 chasing the high-note crackle, then put
  // back. Three separate causes were ruled OUT on the host that day, none of
  // them the stack: peak output never exceeds 0.25 anywhere from 100 Hz to
  // 2 kHz and does NOT rise with pitch (so the voice is not clipping);
  // libDaisy builds -mfpu=fpv5-d16 -mfloat-abi=hard (so the doubles in the
  // synth loop are hardware, not emulated); and the BACF tracker holds f0
  // with zero deviation and zero octave jumps from 110 Hz to 1320 Hz. The
  // engine renders clean at every pitch, so the artifact is not in this
  // math. 1 also silently killed knob 6, because detune multiplies `spread`
  // and a single voice sits at spread 0 (fof_engine.hpp:209).
  //
  // What actually shrank on 2026-09-02 was the grain TABLES, not the stack:
  // DBSCREAMZ_MAX_UNISON trims them from 8 voices to kPinnedUnison, which is
  // where the 75 KB and the 8/3 cheaper rebuild came from. See the Makefile.
  //
  // The per-block cost theory behind all of this has never been measured.
  // The CpuLoadMeter below is that measurement.
  p.unison = kPinnedUnison;
  p.detune_cents = v.detune_cents;
  // Vibrato. The store and the knobs are in CENTS, FofParams::vib_depth is
  // in SEMITONES: this division is the only place that boundary is crossed
  // anywhere in the firmware.
  p.vib_rate = v.vib_rate_hz;
  p.vib_depth = v.vib_depth_cents / 100.0;
  // Grain length is pinned for the same reason unison and aspiration are:
  // the engine keeps its support and the pedal stops driving it. 20 ms is
  // the engine's own default (FofParams::grain_ms), and every character and
  // beast already stored exactly 20, so no voice changed when the knob was
  // retired on 2026-09-02. Knob 6 is detune now, and the knob detune left
  // is vibrato depth.
  p.grain_ms = 20.0;
  // Aspiration is pinned rather than deleted: grain.hpp is a verbatim
  // transcription of the frozen lab engine (FIRMWARE.md section 9 gotcha 6)
  // and must keep its breath branch, so the pedal switches it off here
  // instead. Its two 4-5 kHz
  // sinusoids, retriggered once per pitch period, were the "static"
  // heard on hardware on 2026-09-01: measured at +22.6 dB in the 3-6 kHz
  // band at Piccolo's old 0.3 with no change in broadband level, which
  // is why no volume knob touched it.
  p.aspiration = 0.0;
  // Vocal size: the whole vocal tract scaled down means a physically bigger
  // creature. Grain-affecting, which is why param_map and charge both
  // quantise their inputs (design spec section 8).
  p.formant_scale = formant_scale_from(v.vocal_size);
  p.glide_ms = v.glide_ms;
  p.octave_shift = v.octave;
  p.gate = kGateLevels[v.gate_level];
  p.follow_pitch = true;  // milestone 5 has no fixed-pitch mode
  p.input_gain = 1.0;     // hardware analog gain replaces the lab's x4
  p.gain = 1.0;           // output levels live in the post chain now
  return p;
}

// Closes out the CPU measurement for this block. Called on EVERY exit path
// from AudioCallback, including the bypass early-return: a block that skips
// the engine is the cheap baseline this whole measurement is compared
// against, so dropping it would bias the numbers toward the engine.
static inline void end_block_metering() {
  cpu_meter.OnBlockEnd();
  // GetMaxCpuLoad() is monotonically non-decreasing between Reset() calls, so
  // it growing means THIS block is the new worst one and the f0 read below is
  // the pitch that produced it. NaN (the post-Reset value, before any block
  // has completed) fails this compare and is skipped, which is what we want.
  const float mx = cpu_meter.GetMaxCpuLoad();
  if (mx > cpu_worst_load) {
    cpu_worst_load = mx;
    cpu_worst_f0 = static_cast<float>(eng->live_f0());
  }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  cpu_meter.OnBlockStart();
  hw.ProcessAllControls();
  ui.tick(read_inputs());

  // Charge overlay (charge spec sections 3 and 5): a pure copy, the
  // edit buffer itself is never modified.
  const VoiceParams vp = apply_charge(ui.edit_buffer(), ui.charge_config(),
                                      ui.charge_level(), ui.charging());
  eng->set_params(to_fof_params(vp));
  post->set(vp.tone, vp.vocal_vol, vp.mix, vp.master_vol);

  if (ui.take_engage_edge()) {
    // Burp fix + held-f0 preservation (spec sec 7, MILESTONE0.md sec 7):
    // clear the overlap buffer and front end, never the tracker.
    eng->clear_output_state();
    post->reset();
  }

  const float target = ui.engaged() ? 1.0f : 0.0f;

  if (target == 0.0f && ramp <= 0.0f) {
    // Milestone 1 passthrough, verbatim: the bypass branch.
    for (size_t i = 0; i < size; ++i) out[0][i] = out[1][i] = in[0][i];
    end_block_metering();
    return;
  }

  static float mono[kBlockSize];  // matches SetAudioBlockSize below
  const size_t n = size > kBlockSize ? kBlockSize : size;
  eng->process_block(in[0], mono, static_cast<int>(n));
  for (size_t i = 0; i < n; ++i) {
    if (ramp < target) {
      ramp += kRampStep;
      if (ramp > 1.0f) ramp = 1.0f;
    } else if (ramp > target) {
      ramp -= kRampStep;
      if (ramp < 0.0f) ramp = 0.0f;
    }
    const float dry = in[0][i];
    const float wet = post->process(dry, mono[i]);
    out[0][i] = out[1][i] = dry + ramp * (wet - dry);
  }
  for (size_t i = n; i < size; ++i)
    out[0][i] = out[1][i] = in[0][i];  // dead when n == size (m2-4 note)
  end_block_metering();
}

int main() {
  hw.Init(true);  // 480 MHz boost, budgeted in FIRMWARE.md section 8
  hw.SetAudioBlockSize(kBlockSize);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  led_left.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led_right.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  // Voice memory: QSPI-backed, factory characters on first boot or on a
  // schema version mismatch (spec section 6). Writes ONLY on save.
  static PersistentStorage<VoiceStore> store_obj(hw.seed.qspi);
  store_obj.Init(factory_store());
  if (store_obj.GetSettings().version != kVoiceStoreVersion)
    store_obj.RestoreDefaults();
  storage = &store_obj;

  static FofEngine engine(48000.0);  // static: engine buffers live in .bss
  static PostChain post_obj(48000.0f);
  eng = &engine;
  post = &post_obj;

  // Let the ADC and the AnalogControl smoothing settle so the boot knob
  // captures (pickup references) are real positions, not zeros.
  hw.StartAdc();
  for (int i = 0; i < 50; i++) {
    hw.ProcessAllControls();
    hw.DelayMs(1);
  }
  // Beast mode (beast_presets.hpp): both stomps held THROUGH power-up
  // swap the four characters for the animal bank, for this session only.
  // The settle loop above has already debounced the switches, so the read
  // is real, and no OTHER boot-time reader competes for the gesture.
  //
  // It does overlap one RUNTIME gesture, and the overlap has to be handled
  // rather than reasoned away: the pedal boots BYPASSED, which is exactly
  // when the Hothouse DFU escape is armed, and that escape fires after both
  // stomps have been held 2000 ms (hothouse.cpp CheckResetToBootloader,
  // HOLD_THRESHOLD_MS). Left alone, holding both a beat too long would load
  // the bank and then reboot straight into the bootloader. See boot_grip_
  // below, which swallows the DFU check until the entry grip is released.
  //
  // The swap is a pointer: ui_controller.hpp holds a const VoiceStore* and
  // only ever reads it, so pointing it at a static beast store is the
  // whole mechanism. Nothing here touches QSPI, so the saved characters
  // survive untouched and a power cycle brings them back.
  const UiInputs boot_in = read_inputs();
  const bool beast_mode = boot_in.left_down && boot_in.right_down;
  static VoiceStore beast = beast_store();
  VoiceStore& active = beast_mode ? beast : store_obj.GetSettings();
  ui.init(&active, boot_in);

  // True only when the entry grip is still held; cleared on first release.
  bool boot_grip = beast_mode;

  eng->set_params(to_fof_params(ui.edit_buffer()));
  eng->rebuild_grains_if_dirty();  // first tables built before audio starts

  // CPU metering. Init BEFORE StartAudio so the first callback already has
  // its tick scaling. StartLog(false) does NOT wait for a host to attach, so
  // a pedal with nothing plugged into the USB port boots and plays exactly as
  // before; the lines are simply discarded.
  cpu_meter.Init(48000.0f, static_cast<int>(kBlockSize));
  hw.seed.StartLog(false);

  hw.StartAudio(AudioCallback);
  uint32_t cpu_log_ms = System::GetNow();

  while (true) {
    // Grain rebuilds ONLY here, never in the audio IRQ (FIRMWARE.md
    // gotcha 2).
    if (eng->grains_dirty()) eng->rebuild_grains_if_dirty();

    // Both saves write the ACTIVE store and skip the QSPI commit in beast
    // mode. Outside beast mode `active` IS storage->GetSettings(), so this
    // is the same write it always was. Inside it, the save chord still
    // works as a session sandbox: tweak a cow, save it, keep it until you
    // pull the plug. What it can never do is overwrite a character.
    if (ui.save_pending()) {
      active.slots[ui.save_slot()] = ui.save_snapshot();
      // blocking QSPI erase+write; audio keeps running
      if (!beast_mode) storage->Save();
      ui.save_done(System::GetNow());
    }

    if (ui.config_save_pending()) {
      // Silent charge-config write on menu exit (charge spec sec 7);
      // the gesture guard covers the blocking window, no LED blink.
      active.charge = ui.charge_config();
      if (!beast_mode) storage->Save();
      ui.config_save_done();
    }

    // CPU report, once a second, from the main loop (never the audio IRQ).
    // Printed as integer tenths of a percent on purpose: libDaisy's logger
    // has no %f, it offers the FLT_FMT/FLT_VAR decomposition macros instead
    // (hid/logger.h), and plain integers are less to get wrong.
    //
    // Reading this: "avg" is the smoothed steady-state load and "worst" is
    // the single most expensive block since the previous line. Sustained
    // worst near or above 1000 (100.0%) IS the dropout, and "worst@" is the
    // f0 it happened at. If worst climbs with pitch, the overload hypothesis
    // is confirmed and the cost is pitch-driven; if worst is flat and well
    // under budget at the pitches that crackle, the cause is NOT CPU and the
    // search moves to the post chain and the codec.
    const uint32_t now_ms = System::GetNow();
    if (now_ms - cpu_log_ms >= 1000) {
      cpu_log_ms = now_ms;
      // GetAvgCpuLoad() is NAN between a Reset() and the next completed
      // block, and casting a NaN to int is undefined. In practice a second
      // of audio has always run by here, but this is diagnostic code that
      // must never be the thing that takes the pedal down.
      const float raw_avg = cpu_meter.GetAvgCpuLoad();
      const float avg = std::isfinite(raw_avg) ? raw_avg : 0.0f;
      const float worst = cpu_worst_load;
      hw.seed.PrintLine("cpu avg %d.%d%%  worst %d.%d%%  worst@ %d Hz",
                        static_cast<int>(avg * 100.0f),
                        static_cast<int>(avg * 1000.0f) % 10,
                        static_cast<int>(worst * 100.0f),
                        static_cast<int>(worst * 1000.0f) % 10,
                        static_cast<int>(cpu_worst_f0));
      // Clear both so the next line's worst is that second's worst, not the
      // session's. Without this the peak latches on the first bad block and
      // the log stops telling you anything.
      cpu_meter.Reset();
      cpu_worst_load = 0.0f;
    }

    const LedState l = ui.leds(System::GetNow());
    led_left.Set(l.left ? 1.0f : 0.0f);
    led_right.Set(l.right ? 1.0f : 0.0f);
    led_left.Update();
    led_right.Update();

    hw.DelayMs(5);
    // Bootloader gesture armed ONLY while bypassed (spec section 7), so
    // menu/save gestures can never reboot the pedal mid-song. BOOT+RESET
    // on the Seed remains the hard fallback.
    //
    // Beast mode enters on both stomps held through power-up, and the pedal
    // boots bypassed, so that grip is still down when this loop starts and
    // would trip the 2 s DFU hold within about two seconds. The entry
    // gesture CONSUMES it: the check is swallowed until both stomps have
    // been seen released once. After that the escape behaves exactly as it
    // always has, including in a beast session, so nothing is lost but the
    // trap. Skipping the call outright also leaves Hothouse's
    // dfu_start_time_ at 0, so no partial hold is banked while we wait.
    if (boot_grip) {
      if (!hw.switches[Hothouse::FOOTSWITCH_1].Pressed() &&
          !hw.switches[Hothouse::FOOTSWITCH_2].Pressed())
        boot_grip = false;
    } else if (ui.bootloader_armed()) {
      hw.CheckResetToBootloader();
    }
  }
  return 0;
}
