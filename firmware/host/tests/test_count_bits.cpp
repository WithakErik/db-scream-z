// The popcount shim (firmware/engine/q_shim/q/detail/count_bits.hpp) must
// be the header that q's bitstream_acf actually picks up, and must agree
// with the compiler builtin on every input. Includes bitstream_acf.hpp
// rather than the shim directly, so the test proves the include-path
// shadowing works, not just that the shim is correct in isolation.
#include <q/utility/bitstream_acf.hpp>
#include <cassert>
#include <cstdint>
#include <cstdio>

#ifndef DBSCREAMZ_COUNT_BITS_SHIM
#error "q's own count_bits.hpp was included: the q_shim dir is not ahead of q on the include path"
#endif

int main() {
  using cycfi::q::detail::count_bits;
  // Every 16-bit pattern in the low half, the high half, and both halves.
  for (std::uint32_t v = 0; v < 0x10000u; v++) {
    assert(count_bits(v) == static_cast<std::uint32_t>(__builtin_popcount(v)));
    assert(count_bits(v << 16) == static_cast<std::uint32_t>(__builtin_popcount(v << 16)));
    assert(count_bits(v | (v << 16)) == static_cast<std::uint32_t>(__builtin_popcount(v | (v << 16))));
  }
  // Edges and a deterministic LCG sweep of full 32-bit words.
  assert(count_bits(0u) == 0u);
  assert(count_bits(0xFFFFFFFFu) == 32u);
  std::uint32_t s = 0x9E3779B9u;
  for (int i = 0; i < 1000000; i++) {
    s = s * 1664525u + 1013904223u;
    assert(count_bits(s) == static_cast<std::uint32_t>(__builtin_popcount(s)));
  }
  // 64-bit overload (unused on the pedal, kept API-complete).
  assert(count_bits(std::uint64_t{0xFFFFFFFFFFFFFFFFull}) == 64u);
  assert(count_bits(std::uint64_t{0x8000000000000001ull}) == 2u);
  std::puts("test_count_bits OK");
  return 0;
}
