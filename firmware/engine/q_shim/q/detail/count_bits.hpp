// count_bits.hpp - DBscreamZ shim for cycfi/q's q/detail/count_bits.hpp.
//
// Shadows the vendored header: this directory (firmware/engine/q_shim) is
// on the include path AHEAD of q in both the firmware and host Makefiles,
// and the include guard below is q's own, so the original is never seen.
// Same mechanism as firmware/host/third_party/q/utility/bitset.hpp.
//
// Why: q's version is `__builtin_popcount`, which on Cortex-M7 (no POPCNT
// instruction) GCC lowers to a call to libgcc's __popcountsi2, a byte-table
// loop with four loads plus call/return, per 32-bit word. bitstream_acf
// calls it for every word of every autocorrelation, and the pitch
// tracker's once-per-window autocorrelate() burst was measured at up to
// 12,920 words for one real-guitar window at ~1.1 kHz (FIRMWARE.md
// section 9): that call was the single most expensive thing in the audio
// ISR's worst block. The branch-free SWAR count below compiles to 12
// inline ALU instructions on the M7 (verified against arm-none-eabi-g++
// 13.3 -O2: GCC does NOT fold this idiom back into the builtin on a
// target without a popcount insn).
//
// Bit-exact by construction: a popcount is a popcount. The host test
// test_count_bits.cpp checks every 16-bit value against the builtin, and
// the host build uses this same shim so the tests exercise what the pedal
// runs.
#if !defined(CYCFI_Q_COUNT_BITS_HPP_MARCH_12_2018)
#define CYCFI_Q_COUNT_BITS_HPP_MARCH_12_2018

#include <cstdint>

// Lets a test assert that THIS header, not q's, was the one included.
#define DBSCREAMZ_COUNT_BITS_SHIM 1

namespace cycfi::q::detail
{
   inline std::uint32_t count_bits(std::uint32_t x)
   {
      x = x - ((x >> 1) & 0x55555555u);
      x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
      x = (x + (x >> 4)) & 0x0F0F0F0Fu;
      return (x * 0x01010101u) >> 24;
   }

   inline std::uint64_t count_bits(std::uint64_t x)
   {
      return count_bits(static_cast<std::uint32_t>(x)) +
             count_bits(static_cast<std::uint32_t>(x >> 32));
   }
}

#endif
