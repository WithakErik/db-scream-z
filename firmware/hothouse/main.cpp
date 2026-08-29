// DBscreamZ - Milestone 5: control surface, voice memory, post chain.
// Spec: docs/superpowers/specs/2026-08-26-milestone5-controls-design.md
// (approved 2026-08-26; replaces the PROVISIONAL milestones 2-4 map).
//
// Control map (physical, facing the pedal; FOOTSWITCH_1/LED_1 = LEFT,
// FOOTSWITCH_2/LED_2 = RIGHT, verified against the Hothouse PCB netlists):
//   Knobs 1-6 (menu 1): mix, glide, master vol, vocal vol, drive, tone
//   RIGHT hold ~1 s   : latch menu 2 (F1/F2 formants), right LED blinks
//   LEFT hold ~1 s    : latch menu 3 (F3 + vibrato), left LED blinks
//   Stomp taps        : recall/engage slots, tap again = bypass; while a
//                       menu is latched the OTHER stomp's tap = SAVE
//   Toggle 1: octave +1/0/-1   Toggle 2: page Set1/Freeform/Set2
//   Toggle 3: gate high/med/low (kGateLevels, milestone 3 calibration TBD)
//   Both stomps together (engaged, no menu): CHARGE MODE power-up ramp,
//   LEDs alternate and accelerate; release decays. Config via toggles
//   while a menu is latched. Spec:
//   docs/superpowers/specs/2026-08-27-charge-mode-design.md
// Bypass = milestone 1 passthrough; engage ramps in over 10 ms.

#include "daisy_seed.h"
#include "hothouse.h"
#include "util/PersistentStorage.h"

#include "fof_engine.hpp"
#include "post_chain.hpp"
#include "charge.hpp"
#include "ui_controller.hpp"

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
  p.vib_rate = v.vib_rate;
  p.vib_depth = v.vib_depth;
  p.vib_jitter = v.vib_jitter;
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
  post->set(vp.drive, vp.tone, vp.vocal_vol, vp.mix, vp.master_vol);

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
  ui.init(&store_obj.GetSettings(), read_inputs());

  eng->set_params(to_fof_params(ui.edit_buffer()));
  eng->rebuild_grains_if_dirty();  // first tables built before audio starts

  hw.StartAudio(AudioCallback);

  while (true) {
    // Grain rebuilds ONLY here, never in the audio IRQ (FIRMWARE.md
    // gotcha 2).
    if (eng->grains_dirty()) eng->rebuild_grains_if_dirty();

    if (ui.save_pending()) {
      VoiceStore& s = storage->GetSettings();
      s.slots[ui.save_slot()] = ui.save_snapshot();
      storage->Save();  // blocking QSPI erase+write; audio keeps running
      ui.save_done(System::GetNow());
    }

    if (ui.config_save_pending()) {
      // Silent charge-config write on menu exit (charge spec sec 7);
      // the gesture guard covers the blocking window, no LED blink.
      storage->GetSettings().charge = ui.charge_config();
      storage->Save();
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
    if (ui.bootloader_armed()) hw.CheckResetToBootloader();
  }
  return 0;
}
