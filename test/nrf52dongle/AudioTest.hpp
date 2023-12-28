#pragma once

#include <coco/platform/Audio_I2S.hpp>


using namespace coco;

// MAX98357A: https://www.analog.com/media/en/technical-documentation/data-sheets/max98357a-max98357b.pdf

// not supported by MAX98357A
//using Sample = int8_t;
//constexpr auto FORMAT = Audio_I2S::Format::MONO_I2S_INT8;
//constexpr auto SCALE = 127.0;

using Sample = int16_t;
constexpr auto FORMAT = Audio_I2S::Format::MONO_I2S_INT16;
constexpr auto SCALE = 32767.0;

//using Sample = int32_t;
//constexpr auto FORMAT = Audio_I2S::Format::MONO_I2S_INT32_24;
//constexpr auto SCALE = 8388607.0;

constexpr int SAMPLE_COUNT = 4096;
constexpr int WORD_COUNT = SAMPLE_COUNT * sizeof(Sample) / 4;


// drivers for AudioTest
struct Drivers {
	Loop_RTC0 loop{Loop_RTC0::Mode::POLL};
	Audio_I2S audio{loop,
		gpio::P0(20), // SCK
		gpio::P0(21), // LRCK
		gpio::P0(19), // data
		48000Hz, FORMAT, WORD_COUNT};
	Audio_I2S::Buffer<SAMPLE_COUNT * sizeof(Sample)> buffer1{audio};
	Audio_I2S::Buffer<SAMPLE_COUNT * sizeof(Sample)> buffer2{audio};
};

Drivers drivers;

extern "C" {
void I2S_IRQHandler() {
	drivers.audio.I2S_IRQHandler();
}
}
