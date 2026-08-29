# firmware/host/Makefile
CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall -I../engine -I../hothouse -Ithird_party
QINC = -I../third_party/q/q_lib/include -I../third_party/q/infra/include

# Every header the host binaries compile against. Without these as
# prerequisites `render` is a real file that make considers up to date after a
# change to fof_engine.hpp / front_end.hpp / pitch_tracker.hpp / the q shims,
# so the metrics gate would silently re-test a stale binary.
HDRS := $(wildcard ../engine/*.hpp) $(wildcard ../hothouse/*.hpp) $(wildcard third_party/q/utility/*.hpp) wav_io.hpp

tests/bin:
	mkdir -p tests/bin

test_%: tests/test_%.cpp $(HDRS) | tests/bin
	$(CXX) $(CXXFLAGS) $(QINC) $< -o tests/bin/$@ && ./tests/bin/$@

render: render.cpp $(HDRS) | tests/bin
	$(CXX) $(CXXFLAGS) $(QINC) render.cpp -o render
