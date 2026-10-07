# qspi_dump: read-only diagnostic firmware that prints the saved VoiceStore
# over USB serial (see main.cpp). Build and flash with tools/flash.sh --dump;
# flash the real pedal firmware back afterwards with tools/flash.sh.

TARGET = qspi_dump

CPP_SOURCES = main.cpp
# voice_params.hpp (the VoiceStore layout) and the presets.hpp it includes.
C_INCLUDES = -I../hothouse -I../engine

LIBDAISY_DIR = ../third_party/libDaisy
CPP_STANDARD = -std=gnu++20

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
