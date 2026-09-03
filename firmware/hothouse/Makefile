# DBscreamZ firmware (Hothouse + Daisy Seed3)
#
# Layout follows the HothouseExamples convention: our sources plus the
# board file from the pinned HothouseExamples clone, built by libDaisy's
# core Makefile. Build libDaisy once first:
#   cd ../third_party/libDaisy && make -j
# Then here:
#   make            # produces build/dbscreamz_pedal.bin
#   make program-dfu  # flash over USB (Seed3 in DFU mode: hold BOOT, tap RESET)
#
# Toolchain: milestone 1 compiles with arm-none-eabi-gcc 10.3. Milestones 2+
# pull in cycfi/q (C++20) and need GCC 12+; point GCC_PATH at a newer
# toolchain's bin dir without touching the system one, e.g.
#   make GCC_PATH=$$HOME/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin

TARGET = dbscreamz_pedal

HOTHOUSE_DIR = ../third_party/HothouseExamples/src
CPP_SOURCES = main.cpp $(HOTHOUSE_DIR)/hothouse.cpp
C_INCLUDES = -I$(HOTHOUSE_DIR)

# Our DSP engine (header-only) plus cycfi/q, which it pulls in via
# pitch_tracker.hpp. The host's third_party shim dir is NOT here on purpose:
# it only exists to force q's natural_uint to 32 bits on a 64-bit host; on
# Cortex-M7 the real typedef is already uint32_t, so pitch_tracker.hpp's
# static_assert holds by construction.
#
# ../engine/q_shim IS here, and must stay AHEAD of the q include dirs: it
# shadows q/detail/count_bits.hpp with an inline SWAR popcount. q's own
# version is __builtin_popcount, which on the M7 (no POPCNT) becomes a
# libgcc __popcountsi2 call per 32-bit word, and the pitch tracker's
# autocorrelate() burst runs it ~13k times in one block at 1.1 kHz
# (FIRMWARE.md section 9). The host build uses the same shim so the tests
# cover what the pedal runs.
C_INCLUDES += -I../engine \
              -I../engine/q_shim \
              -I../third_party/q/q_lib/include \
              -I../third_party/q/infra/include

# Two grain-table sets on the pedal; 1920 covers the menu 3 grain knob at
# its 40 ms maximum (1920 samples at 48 kHz), host keeps its 4800 default.
#
# This costs RAM, not flash: the tables are .bss.
#
# 2026-09-02: the voice cap dropped from the engine default of 8 to 3.
# build_grains() fills every one of kMaxUnison voices on each rebuild, but
# to_fof_params() pins the stack to 3, so voices 3..7 were built on every
# character or formant change and never read once. Each unit of grain
# length costs 2 sets x kMaxUnison x 4 bytes, so the pair of defines below
# moved the tables from 2 x 8 x 1920 x 4 = 120 KB down to
# 2 x 3 x 1920 x 4 = 45 KB, giving back 75 KB of SRAM and making each
# rebuild (which runs on the main loop) 8/3 cheaper.
#
# .bss lands in SRAM (512 KB at 0x24000000 per the linker script, NOT the
# 128 KB DTCMRAM). Flash is the tight budget here, at ~93%, and neither of
# these defines touches it.
#
# Raising the pin in to_fof_params() above MAX_UNISON is a compile error,
# not a buffer overrun: see the static_assert in main.cpp.
# C_DEFS reaches C++ TUs via the core Makefile's CFLAGS -> CPPFLAGS chain.
C_DEFS += -DDBSCREAMZ_MAX_GRAIN_LEN=1920
C_DEFS += -DDBSCREAMZ_MAX_UNISON=3

LIBDAISY_DIR = ../third_party/libDaisy
# No DAISYSP_DIR on purpose: the DSP is ours plus cycfi/q (FIRMWARE.md sec 2).

# C++20 now so the flag never changes when the engine lands at milestone 2
# (cycfi/q requires it; GCC >= 10 accepts the flag, >= 12 needed to compile q).
CPP_STANDARD = -std=gnu++20

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
