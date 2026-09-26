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
//                       menu is latched the OTHER stomp's tap switches
//                       menus and the blinking side's own tap exits
//   Save              : hold one stomp past ~1 s, press the other; the
//                       held side is the slot (menus never save)
//   Toggle 1: octave +1/0/-1   Toggle 2: page Set1/Set2/Set3
//   Toggle 3: gate high/med/low (kGateLevels, milestone 3 calibration TBD)
//   Both stomps together (engaged, no menu): CHARGE MODE power-up ramp,
//   LEDs alternate and accelerate; release decays. Config via toggles
//   while a menu is latched. Spec:
//   docs/superpowers/specs/2026-08-27-charge-mode-design.md
//   Both stomps held THROUGH power-up: CHORD MODE for this session
//   (chord_engine.hpp, chord_ui.hpp; chord mode design spec
//   2026-09-24). Left tap on/off, left hold = chord menu, right held =
//   open mouth, both = charge. Its one setting auto-saves to QSPI.
// Bypass = milestone 1 passthrough; engage ramps in over 10 ms.

// The CPU load report (see "CPU load instrumentation" below) is compiled OUT
// by default. Its USB serial log pulls libDaisy's whole USB CDC stack plus
// printf into the image, about 6 KB, and with chord mode the firmware no
// longer fits the 128 KB internal flash with it in (2026-09-25: 5616 bytes
// over). Build with `CPU_LOG=1` (Makefile) for a diagnostic image, which
// will only link once the image is back under the limit without it.
#ifndef DBSCREAMZ_CPU_LOG
#define DBSCREAMZ_CPU_LOG 0
#endif

#if DBSCREAMZ_CPU_LOG
#include <cmath>   // std::isfinite, guarding the CPU report against NaN
#endif

#include "daisy_seed.h"
#include "hothouse.h"
#include "util/PersistentStorage.h"
#if DBSCREAMZ_CPU_LOG
#include "util/CpuLoadMeter.h"
#endif

#include "fof_engine.hpp"
#include "post_chain.hpp"
#include "charge.hpp"
#include "ui_controller.hpp"
#include "chord_engine.hpp"
#include "chord_map.hpp"
#include "chord_ui.hpp"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
#if DBSCREAMZ_CPU_LOG
using daisy::CpuLoadMeter;
#endif
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
ChordEngine* chord_eng = nullptr;
ChordUiController chord_ui;
// Decided once at boot (both stomps held through power-up) and never
// changed: the callback runs exactly one engine and one controller.
bool chord_mode = false;

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
//
// Compiled in only with DBSCREAMZ_CPU_LOG=1 (see the top of this file).
#if DBSCREAMZ_CPU_LOG
CpuLoadMeter cpu_meter;
// Written by the audio IRQ, read and cleared by the main loop. A torn read
// across that boundary would cost one wrong diagnostic line and nothing
// else, and both are naturally-aligned 32-bit scalars on Cortex-M7, so no
// locking. Deliberately not volatile-qualified beyond that.
volatile float cpu_worst_load = 0.0f;
volatile float cpu_worst_f0 = 0.0f;
#endif

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
  // the engine's own default (FofParams::grain_ms), and every character
  // already stored exactly 20, so no voice changed when the knob was
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
// A no-op unless DBSCREAMZ_CPU_LOG is on.
static inline void end_block_metering() {
#if DBSCREAMZ_CPU_LOG
  cpu_meter.OnBlockEnd();
  // GetMaxCpuLoad() is monotonically non-decreasing between Reset() calls, so
  // it growing means THIS block is the new worst one and the f0 read below is
  // the pitch that produced it. NaN (the post-Reset value, before any block
  // has completed) fails this compare and is skipped, which is what we want.
  const float mx = cpu_meter.GetMaxCpuLoad();
  if (mx > cpu_worst_load) {
    cpu_worst_load = mx;
    // In chord mode the grain engine is idle and its held f0 is meaningless,
    // so the report logs 0 there.
    cpu_worst_f0 = chord_mode ? 0.0f : static_cast<float>(eng->live_f0());
  }
#endif
}

// Bypass passthrough, engage ramp, post chain and metering, shared by both
// engines. `render` fills `mono` with the engine's output for the block.
template <typename Render>
static void run_output(AudioHandle::InputBuffer in,
                       AudioHandle::OutputBuffer out, size_t size,
                       bool engaged, Render render) {
  const float target = engaged ? 1.0f : 0.0f;

  if (target == 0.0f && ramp <= 0.0f) {
    // Milestone 1 passthrough, verbatim: the bypass branch.
    for (size_t i = 0; i < size; ++i) out[0][i] = out[1][i] = in[0][i];
    end_block_metering();
    return;
  }

  static float mono[kBlockSize];  // matches SetAudioBlockSize below
  const size_t n = size > kBlockSize ? kBlockSize : size;
  render(in[0], mono, static_cast<int>(n));
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

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
#if DBSCREAMZ_CPU_LOG
  cpu_meter.OnBlockStart();
#endif
  hw.ProcessAllControls();

  if (chord_mode) {
    chord_ui.tick(read_inputs());
    // Charge overlay: a pure copy, chord_ui's setting is never charged,
    // so an auto-save can never persist a charge (spec section 4).
    const ChordParams cp = apply_charge_chord(
        chord_ui.chord(), chord_ui.charge_config(), chord_ui.charge_level());
    // input_gain 1.0: hardware analog gain replaces the lab's x4, as in
    // to_fof_params().
    chord_eng->set_params(
        to_chord_engine_params(cp, chord_ui.mouth_open(), 1.0));
    post->set(cp.tone, cp.vocal_vol, cp.mix, cp.master_vol);
    if (chord_ui.take_engage_edge()) {
      // Burp fix, as in normal mode (spec sec 7): clear the filters, front
      // end and mouth so bypass-era state never bursts out on engage.
      chord_eng->clear_output_state();
      post->reset();
    }
    run_output(in, out, size, chord_ui.engaged(),
               [](const float* x, float* y, int n) {
                 chord_eng->process_block(x, y, n);
               });
    return;
  }

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

  run_output(in, out, size, ui.engaged(),
             [](const float* x, float* y, int n) {
               eng->process_block(x, y, n);
             });
}

int main() {
  hw.Init(true);  // 480 MHz boost, budgeted in FIRMWARE.md section 8
  hw.SetAudioBlockSize(kBlockSize);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  led_left.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led_right.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  // Voice memory: QSPI-backed, factory characters on first boot or on an
  // unmigratable schema version (spec section 6). Writes ONLY on save,
  // chord mode's auto-save, and once after a v8 or v7 migration.
  static PersistentStorage<VoiceStore> store_obj(hw.seed.qspi);
  store_obj.Init(factory_store());
  // v9 grew the slots to six; a v8 or v7 image keeps its four characters
  // and charge config, and a v8 image its chord setting too
  // (voice_params.hpp migrate_store, three-banks spec section 4).
  switch (migrate_store(store_obj.GetSettings())) {
    case StoreLoad::Current: break;
    case StoreLoad::Migrated: store_obj.Save(); break;
    case StoreLoad::Reset: store_obj.RestoreDefaults(); break;
  }
  storage = &store_obj;

  static FofEngine engine(48000.0);  // static: engine buffers live in .bss
  static PostChain post_obj(48000.0f);
  eng = &engine;
  post = &post_obj;
  static ChordEngine chord_engine(48000.0);  // static: .bss, like the above
  chord_eng = &chord_engine;

  // Let the ADC and the AnalogControl smoothing settle so the boot knob
  // captures (pickup references) are real positions, not zeros.
  hw.StartAdc();
  for (int i = 0; i < 50; i++) {
    hw.ProcessAllControls();
    hw.DelayMs(1);
  }
  // Chord mode (chord_engine.hpp, chord_ui.hpp): both stomps held THROUGH
  // power-up run the chord engine and its controller instead of the
  // characters, for this session. The settle loop above has already
  // debounced the switches, so the read is real, and no OTHER boot-time
  // reader competes for the gesture.
  //
  // It does overlap one RUNTIME gesture, and the overlap has to be handled
  // rather than reasoned away: the pedal boots BYPASSED, which is exactly
  // when the Hothouse DFU escape is armed, and that escape fires after both
  // stomps have been held 2000 ms (hothouse.cpp CheckResetToBootloader,
  // HOLD_THRESHOLD_MS). Left alone, holding both a beat too long would enter
  // chord mode and then reboot straight into the bootloader. See boot_grip
  // below, which swallows the DFU check until the entry grip is released.
  //
  // The choice is made once here and never revisited: the callback runs
  // exactly one engine and one controller. Chord mode reads and writes only
  // the store's chord block (its auto-save); the characters are untouched.
  const UiInputs boot_in = read_inputs();
  chord_mode = boot_in.left_down && boot_in.right_down;
  VoiceStore& active = store_obj.GetSettings();
  if (chord_mode)
    chord_ui.init(active.chord, active.charge, boot_in);
  else
    ui.init(&active, boot_in);

  // True only when the entry grip is still held; cleared on first release.
  bool boot_grip = chord_mode;

  // In chord mode `ui` is never initialised and the grain engine never
  // plays, so no grain table is built for it.
  if (!chord_mode) {
    eng->set_params(to_fof_params(ui.edit_buffer()));
    eng->rebuild_grains_if_dirty();  // first tables built before audio starts
  }

  // CPU metering. Init BEFORE StartAudio so the first callback already has
  // its tick scaling. StartLog(false) does NOT wait for a host to attach, so
  // a pedal with nothing plugged into the USB port boots and plays exactly as
  // before; the lines are simply discarded. DBSCREAMZ_CPU_LOG builds only.
#if DBSCREAMZ_CPU_LOG
  cpu_meter.Init(48000.0f, static_cast<int>(kBlockSize));
  hw.seed.StartLog(false);
#endif

  hw.StartAudio(AudioCallback);
#if DBSCREAMZ_CPU_LOG
  uint32_t cpu_log_ms = System::GetNow();
#endif

  while (true) {
    // Grain rebuilds ONLY here, never in the audio IRQ (FIRMWARE.md
    // gotcha 2).
    if (!chord_mode && eng->grains_dirty()) eng->rebuild_grains_if_dirty();

    if (chord_mode) {
      // Chord mode auto-save (spec section 4): chord_ui raises it 3 s after
      // the last change or on leaving the menu, and only for a real change.
      // Same blocking QSPI erase+write the save chord does; audio runs on.
      if (chord_ui.save_pending()) {
        active.chord = chord_ui.save_snapshot();
        storage->Save();
        chord_ui.save_done();
      }
    } else {
      if (ui.save_pending()) {
        active.slots[ui.save_slot()] = ui.save_snapshot();
        storage->Save();  // blocking QSPI erase+write; audio keeps running
        ui.save_done(System::GetNow());
      }

      if (ui.config_save_pending()) {
        // Silent charge-config write on menu exit (charge spec sec 7);
        // the gesture guard covers the blocking window, no LED blink.
        active.charge = ui.charge_config();
        storage->Save();
        ui.config_save_done();
      }
    }

#if DBSCREAMZ_CPU_LOG
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
#endif

    const LedState l = chord_mode ? chord_ui.leds(System::GetNow())
                                  : ui.leds(System::GetNow());
    led_left.Set(l.left ? 1.0f : 0.0f);
    led_right.Set(l.right ? 1.0f : 0.0f);
    led_left.Update();
    led_right.Update();

    hw.DelayMs(5);
    // Bootloader gesture armed ONLY while bypassed (spec section 7), so
    // menu/save gestures can never reboot the pedal mid-song. BOOT+RESET
    // on the Seed remains the hard fallback.
    //
    // Chord mode enters on both stomps held through power-up, and the pedal
    // boots bypassed, so that grip is still down when this loop starts and
    // would trip the 2 s DFU hold within about two seconds. The entry
    // gesture CONSUMES it: the check is swallowed until both stomps have
    // been seen released once. After that the escape behaves exactly as it
    // always has, including in a chord session (armed only while chord
    // mode is bypassed), so nothing is lost but the trap. Skipping the call
    // outright also leaves Hothouse's dfu_start_time_ at 0, so no partial
    // hold is banked while we wait.
    if (boot_grip) {
      if (!hw.switches[Hothouse::FOOTSWITCH_1].Pressed() &&
          !hw.switches[Hothouse::FOOTSWITCH_2].Pressed())
        boot_grip = false;
    } else if (chord_mode ? chord_ui.bootloader_armed()
                          : ui.bootloader_armed()) {
      hw.CheckResetToBootloader();
    }
  }
  return 0;
}
