#pragma once

#include <coco/platform/Audio_native.hpp>


using namespace coco;

using Sample = int16_t;
constexpr auto FORMAT = Audio_native::Format::INT16;
constexpr int SCALE = 32767.0;

//using Sample = int32_t;
//constexpr auto FORMAT = Audio_native::Format::INT32_24;
//constexpr auto SCALE = 8388607.0;

constexpr int SAMPLE_COUNT = 4096;


// drivers for AudioTest
struct Drivers {
	Loop_native loop;
	Audio_native audio{loop, 48000Hz, 1, FORMAT};
	Audio_native::Buffer buffer1{audio, SAMPLE_COUNT * sizeof(Sample)};
	Audio_native::Buffer buffer2{audio, SAMPLE_COUNT * sizeof(Sample)};
};

Drivers drivers;
