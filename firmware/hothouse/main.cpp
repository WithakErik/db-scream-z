// DBscreamZ - Milestone 5: control surface, voice memory, post chain.
// Spec: docs/superpowers/specs/2026-08-26-milestone5-controls-design.md
// (approved 2026-08-26; replaces the PROVISIONAL milestones 2-4 map).
//
// Control map (physical, facing the pedal; FOOTSWITCH_1/LED_1 = LEFT,
// FOOTSWITCH_2/LED_2 = RIGHT, verified against the Hothouse PCB netlists):
//   Knobs 1-6 (menu 1): vocal vol, mix, master vol, tone, glide, vocal size
//   RIGHT hold ~1 s   : latch menu 2 (F1/F2 formants), right LED blinks
//   LEFT hold ~1 s    : latch menu 3 (F3, voices, detune, grain), left LED
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

#include "daisy_seed.h"
#include "hothouse.h"
#include "util/PersistentStorage.h"

#include "fof_engine.hpp"
#include "post_chain.hpp"
#include "charge.hpp"
#include "ui_controller.hpp"
#include "beast_presets.hpp"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
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

// The grain tables are trimmed on this target (Makefile: MAX_GRAIN_LEN) and
// menu 3 knob 6 now reaches 40 ms, so the trim has to cover the top of that
// travel or a full-clockwise grain would silently clamp short. Checked here
// because this is the translation unit that carries the -D override; the
// host builds keep their roomier 4800 default and clear it either way.
static_assert(kMaxGrainLen >= 1920,
              "kMaxGrainLen must cover map_grain's 40 ms maximum at 48 kHz");

static constexpr size_t kBlockSize = 48;
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
  p.unison = static_cast<int>(v.unison);
  p.detune_cents = v.detune_cents;
  p.grain_ms = v.grain_ms;
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

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
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

  hw.StartAudio(AudioCallback);

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
