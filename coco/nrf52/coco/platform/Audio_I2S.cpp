#include "Audio_I2S.hpp"
#include <coco/debug.hpp>
#include <coco/platform/platform.hpp>
#include <coco/platform/nvic.hpp>
#include <coco/platform/gpio.hpp>


namespace coco {

constexpr int DEBUG_PIN = gpio::P0(3);

Audio_I2S::Audio_I2S(Loop_RTC0 &loop, int sckPin, int lrckPin, int dataPin, int sampleRate, Format format,
	int bufferWordCount)
	: loop(loop)
{
	// debug start indicator pin
	gpio::configureOutput(DEBUG_PIN, false);

	// configure I2S pins
	auto i2s = NRF_I2S;
	//i2s->PSEL.MCK = DISCONNECTED;
	i2s->PSEL.SCK = sckPin;
	i2s->PSEL.LRCK = lrckPin;
	//i2s->PSEL.SDIN = DISCONNECTED;
	i2s->PSEL.SDOUT = dataPin;

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
/*
	int clockFrequency;
	switch (format) {
	case Format::INT8:
		clockFrequency = sampleRate * 32;
		i2s->CONFIG.RATIO = N(I2S_CONFIG_RATIO_RATIO, 32X);
		i2s->CONFIG.SWIDTH = N(I2S_CONFIG_SWIDTH_SWIDTH, 8Bit);
		break;
	case Format::INT16:
		clockFrequency = sampleRate * 32;
		i2s->CONFIG.RATIO = N(I2S_CONFIG_RATIO_RATIO, 32X);
		i2s->CONFIG.SWIDTH = N(I2S_CONFIG_SWIDTH_SWIDTH, 16Bit);
		break;
	default:
		clockFrequency = sampleRate * 48;
		i2s->CONFIG.RATIO = N(I2S_CONFIG_RATIO_RATIO, 48X);
		i2s->CONFIG.SWIDTH = N(I2S_CONFIG_SWIDTH_SWIDTH, 24Bit);
		break;
	}
	//i2s->CONFIG.ALIGN = N(I2S_CONFIG_ALIGN_ALIGN, Left);
	i2s->CONFIG.FORMAT = N(I2S_CONFIG_FORMAT_FORMAT, I2S);
	if (channelCount == 1)
		i2s->CONFIG.CHANNELS = N(I2S_CONFIG_CHANNELS_CHANNELS, Left);
	else
		i2s->CONFIG.CHANNELS = N(I2S_CONFIG_CHANNELS_CHANNELS, Stereo);
*/

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

BufferDevice::State Audio_I2S::state() {
	return State::READY;
}

Awaitable<> Audio_I2S::stateChange(int waitFlags) {
	if ((waitFlags & (1 << int(State::READY))) == 0)
		return {};
	return {this->stateTasks};
}

int Audio_I2S::getBufferCount() {
	return this->buffers.count();
}

Audio_I2S::BufferBase &Audio_I2S::getBuffer(int index) {
	return this->buffers.get(index);
}

void Audio_I2S::update() {
	//gpio::setOutput(DEBUG_PIN, false);

	if (this->transfer != nullptr) {
		// current transfer is ready: pass buffer to event loop so that app gets notified (via BufferBase::handle())
		this->loop.push(*this->transfer);
		this->transfer = nullptr;
	}

	// start next buffer
	if (this->transfers.pop(
		[this](BufferBase &buffer) {
			// set as current transfer
			this->transfer = &buffer;
			return true;
		},
		[](BufferBase &next) {
			// start next buffer
			NRF_I2S->TXD.PTR = uintptr_t(next.p.data);
		}) == -1)
	{
		// no more buffers: stop I2S
		NRF_I2S->TASKS_STOP = TRIGGER;
		debug::setRed();
	}
}


// BufferBase

Audio_I2S::BufferBase::BufferBase(uint8_t *data, int capacity, Audio_I2S &device)
	: BufferImpl(data, capacity, BufferBase::State::READY), device(device)
{
	device.buffers.add(*this);
}

Audio_I2S::BufferBase::~BufferBase() {
}

bool Audio_I2S::BufferBase::start(Op op) {
	if (this->p.state != State::READY) {
		assert(this->p.state != State::BUSY);
		return false;
	}
	auto &device = this->device;

	// check if WRITE flag is set
	assert((op & Op::WRITE) != 0);

	// add to list of pending transfers and start immediately if list was empty
	if (device.transfers.push(I2S_IRQn, *this)) {
		NRF_I2S->TXD.PTR = uintptr_t(this->p.data);

		// start I2S
		if (device.transfer == nullptr) {
			NRF_I2S->TASKS_START = TRIGGER;
		}
	}

	// set state
	setBusy();

	return true;
}

bool Audio_I2S::BufferBase::cancel() {
	if (this->p.state != State::BUSY)
		return false;
	auto &device = this->device;

	// remove from pending transfers if not yet started, otherwise complete normally
	if (device.transfers.remove(I2S_IRQn, *this, false) == 1)
		setReady(0);
	return true;
}

void Audio_I2S::BufferBase::handle() {
	setReady();
}

} // namespace coco
