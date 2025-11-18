#include "Audio_I2S.hpp"
#include <coco/debug.hpp>
#include <coco/platform/platform.hpp>
#include <coco/platform/nvic.hpp>
#include <coco/platform/gpio.hpp>


namespace coco {

constexpr auto DEBUG_PIN = gpio::P0_3;

Audio_I2S::Audio_I2S(Loop_Queue &loop, gpio::Config sckPin, gpio::Config lrckPin, gpio::Config dataPin,
	int sampleRate, Format format, int bufferWordCount)
	: BufferDevice(State::READY)
	, loop_(loop)
{
	// debug start indicator pin
	//gpio::enableOutput(DEBUG_PIN, false);

	// configure I2S pins
	gpio::enableOutput(sckPin, false);
	gpio::enableOutput(lrckPin, false);
	gpio::enableOutput(dataPin, false);
	auto i2s = NRF_I2S;
	//i2s->PSEL.MCK = DISCONNECTED;
	i2s->PSEL.SCK = gpio::getPinPortIndex(sckPin);
	i2s->PSEL.LRCK = gpio::getPinPortIndex(lrckPin);
	//i2s->PSEL.SDIN = DISCONNECTED;
	i2s->PSEL.SDOUT = gpio::getPinPortIndex(dataPin);

	int ratio = int(format) & 0xf;

	// initialize I2S
	i2s->CONFIG.MODE = N(I2S_CONFIG_MODE_MODE, Master);
	i2s->CONFIG.RXEN = 0;
	i2s->CONFIG.TXEN = N(I2S_CONFIG_TXEN_TXEN, Enabled);
	i2s->CONFIG.MCKEN = N(I2S_CONFIG_MCKEN_MCKEN, Enabled);
	i2s->CONFIG.RATIO = ratio;
	i2s->CONFIG.SWIDTH = (int(format) >> 4) & 0xf;
	i2s->CONFIG.FORMAT = (int(format) >> 8) & 0xf;
	i2s->CONFIG.CHANNELS = (int(format) >> 12) & 0xf;

	// https://devzone.nordicsemi.com/f/nordic-q-a/391/uart-baudrate-register-values
	int clockFrequency = sampleRate * ((2 | (ratio & 1)) << 4); // *32, *48, *64, *96...
	int value = (int64_t(clockFrequency) << 32) / 32000000;
	i2s->CONFIG.MCKFREQ = (value + 0x800) & 0xFFFFF000;

	i2s->RXTXD.MAXCNT = bufferWordCount;// bufferSampleCount * channelCount >> 1/ 2;
	i2s->INTENSET = N(I2S_INTENSET_TXPTRUPD, Set);// | N(I2S_INTENSET_STOPPED, Set);
	i2s->ENABLE = N(I2S_ENABLE_ENABLE, Enabled);
}

Audio_I2S::~Audio_I2S() {
}

int Audio_I2S::getBufferCount() {
	return buffers_.count();
}

Audio_I2S::BufferBase &Audio_I2S::getBuffer(int index) {
	return buffers_.get(index);
}

void Audio_I2S::update() {
	//gpio::setOutput(DEBUG_PIN, false);

	if (transfer_ != nullptr) {
		// current transfer is ready: pass buffer to event loop so that app gets notified (via BufferBase::handle())
		loop_.push(*transfer_);
		transfer_ = nullptr;
	}

	// start next buffer
	if (transfers_.pop(
		[this](BufferBase &buffer) {
			// set as current transfer
			transfer_ = &buffer;
			return true;
		},
		[](BufferBase &next) {
			// start next buffer
			NRF_I2S->TXD.PTR = uintptr_t(next.data_);
		}) == -1)
	{
		// no more buffers: stop I2S
		NRF_I2S->TASKS_STOP = TRIGGER;
		debug::setRed();
	}
}


// BufferBase

Audio_I2S::BufferBase::BufferBase(uint8_t *data, int capacity, Audio_I2S &device)
	: coco::Buffer(data, capacity, BufferBase::State::READY), device_(device)
{
	device.buffers_.add(*this);
}

Audio_I2S::BufferBase::~BufferBase() {
}

bool Audio_I2S::BufferBase::start(Op op) {
	if (st.state != State::READY) {
		assert(st.state != State::BUSY);
		return false;
	}
	auto &device = device_;

	// check if WRITE flag is set
	assert((op & Op::WRITE) != 0);

	// add to list of pending transfers and start immediately if list was empty
	nvic::disable(I2S_IRQn);
	if (device.transfers_.push(*this)) {
		NRF_I2S->TXD.PTR = uintptr_t(data_);

		// start I2S if necessary
		if (device.transfer_ == nullptr)
			NRF_I2S->TASKS_START = TRIGGER;
	}
	nvic::enable(I2S_IRQn);

	// set state
	setBusy();

	return true;
}

bool Audio_I2S::BufferBase::cancel() {
	if (st.state != State::BUSY)
		return false;
	auto &device = device_;

	// remove from pending transfers if not yet started, otherwise complete normally
	if (device.transfers_.remove(nvic::Guard(I2S_IRQn), *this, false) == 1)
		setReady(0);
	return true;
}

void Audio_I2S::BufferBase::handle() {
	setReady();
}

} // namespace coco
