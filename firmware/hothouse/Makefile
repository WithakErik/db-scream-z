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
# pitch_tracker.hpp. No host shim dir here on purpose: the shim only exists to
# force q's natural_uint to 32 bits on a 64-bit host; on Cortex-M7 the real
# typedef is already uint32_t, so pitch_tracker.hpp's static_assert holds by
# construction.
C_INCLUDES += -I../engine \
              -I../third_party/q/q_lib/include \
              -I../third_party/q/infra/include

# Two grain-table sets on the pedal; 1024 covers grainMs 20 (960 samples)
# with headroom, host keeps its 4800 default.
# C_DEFS reaches C++ TUs via the core Makefile's CFLAGS -> CPPFLAGS chain.
C_DEFS += -DDBSCREAMZ_MAX_GRAIN_LEN=1024

LIBDAISY_DIR = ../third_party/libDaisy
# No DAISYSP_DIR on purpose: the DSP is ours plus cycfi/q (FIRMWARE.md sec 2).

# C++20 now so the flag never changes when the engine lands at milestone 2
# (cycfi/q requires it; GCC >= 10 accepts the flag, >= 12 needed to compile q).
CPP_STANDARD = -std=gnu++20

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
