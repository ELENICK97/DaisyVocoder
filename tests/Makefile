# Desktop tests with a native g++/clang++ (Linux/macOS/MSYS2).
# Usage: make -C tests        (Windows + MSVC: tests\build_tests.bat)
HOSTCXX ?= c++
SRC = test_main.cpp ../Controller.cpp ../Engine.cpp ../MidiMapper.cpp ../MidiParser.cpp \
      ../Params.cpp ../SettingsStorage.cpp ../Synth.cpp ../Vocoder.cpp

build/vocoder_tests: $(SRC) $(wildcard ../*.h)
	mkdir -p build
	$(HOSTCXX) -std=c++14 -O2 -Wall -Wextra -o $@ $(SRC) -lm

test: build/vocoder_tests
	./build/vocoder_tests

.DEFAULT_GOAL := test
.PHONY: test
