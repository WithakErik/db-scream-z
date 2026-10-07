// qspi_dump - READ-ONLY diagnostic firmware: prints the pedal's saved
// VoiceStore over USB serial, so a setting dialed in on the hardware can be
// captured on the host (first use 2026-09-28: chord mode's saved setting,
// to become factory_chord()).
//
// Flash it with `tools/flash.sh --dump`, read /dev/ttyACM0, then flash the
// real firmware back with `tools/flash.sh`. DFU writes only the Seed's
// internal flash, so the QSPI store survives both flashes untouched.
//
// It never writes QSPI. That is why it does NOT use PersistentStorage:
// PersistentStorage::Init() erases and rewrites the block whenever it does
// not recognize the stored state word. This reads the memory-mapped chip
// directly and cannot erase or write anything.
//
// Every float is printed as its raw IEEE-754 bits, so the capture is exact
// (libDaisy's logger has no %f); decode on the host.
#include <cstddef>
#include <cstring>

#include "daisy_seed.h"
#include "util/PersistentStorage.h"
#include "voice_params.hpp"

using daisy::DaisySeed;
using daisy::System;

DaisySeed hw;

// PersistentStorage<VoiceStore>'s private SaveStruct, mirrored: the state
// enum, then the settings block, at QSPI offset 0 (main.cpp's Init() uses
// the default offset).
struct SaveMirror {
  daisy::PersistentStorage<VoiceStore>::State state;
  VoiceStore store;
};
static_assert(sizeof(daisy::PersistentStorage<VoiceStore>::State) == 4,
              "state word is 4 bytes");
static_assert(offsetof(SaveMirror, store) == 4, "store follows the state");

static unsigned long bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, sizeof u);
  return u;
}

int main() {
  hw.Init();
  hw.StartLog(false);  // do not block: print forever, the host attaches late

  bool led = false;
  while (true) {
    SaveMirror m;
    std::memcpy(&m, hw.qspi.GetData(0), sizeof m);
    const ChordParams& c = m.store.chord;
    const ChargeConfig& g = m.store.charge;

    hw.PrintLine("DUMP-BEGIN state=%d version=%lu", static_cast<int>(m.state),
                 static_cast<unsigned long>(m.store.version));
    hw.PrintLine("chord vocal_vol=%08lx mix=%08lx master_vol=%08lx tone=%08lx",
                 bits(c.vocal_vol), bits(c.mix), bits(c.master_vol),
                 bits(c.tone));
    hw.PrintLine("chord sensitivity=%08lx drive=%08lx closed_vowel=%08lx "
                 "open_vowel=%08lx",
                 bits(c.sensitivity), bits(c.drive), bits(c.closed_vowel),
                 bits(c.open_vowel));
    hw.PrintLine("chord vocal_size=%08lx resonance=%08lx attack_ms=%08lx "
                 "release_ms=%08lx gate_level=%d",
                 bits(c.vocal_size), bits(c.resonance), bits(c.attack_ms),
                 bits(c.release_ms), c.gate_level);
    hw.PrintLine("charge gain=%d time=%d decay=%d pitch=%d tone=%d size=%d",
                 g.gain, g.time, g.decay, g.pitch, g.tone, g.size);
    hw.PrintLine("DUMP-END");

    led = !led;
    hw.SetLed(led);  // the Seed's own LED blinks: the dump is running
    System::Delay(1000);
  }
}
