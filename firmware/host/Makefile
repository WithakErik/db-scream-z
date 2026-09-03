# firmware/host/Makefile
CXX = g++
# Two shim dirs sit AHEAD of q on the include path: third_party (host-only,
# pins q's bitset word to 32 bits) and ../engine/q_shim (shared with the
# firmware build, the inline popcount). Order matters: -I is searched
# first-to-last and each shim carries q's own include guard.
CXXFLAGS = -std=c++20 -O2 -Wall -I../engine -I../hothouse -Ithird_party -I../engine/q_shim
QINC = -I../third_party/q/q_lib/include -I../third_party/q/infra/include

# Every header the host binaries compile against. Without these as
# prerequisites `render` is a real file that make considers up to date after a
# change to fof_engine.hpp / front_end.hpp / pitch_tracker.hpp / the q shims,
# so the metrics gate would silently re-test a stale binary.
HDRS := $(wildcard ../engine/*.hpp) $(wildcard ../hothouse/*.hpp) $(wildcard third_party/q/utility/*.hpp) $(wildcard ../engine/q_shim/q/detail/*.hpp) wav_io.hpp

tests/bin:
	mkdir -p tests/bin

test_%: tests/test_%.cpp $(HDRS) | tests/bin
	$(CXX) $(CXXFLAGS) $(QINC) $< -o tests/bin/$@ && ./tests/bin/$@

render: render.cpp $(HDRS) | tests/bin
	$(CXX) $(CXXFLAGS) $(QINC) render.cpp -o render
