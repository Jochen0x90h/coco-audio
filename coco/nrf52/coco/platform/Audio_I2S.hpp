#pragma once

#include <coco/BufferDevice.hpp>
#include <coco/BufferImpl.hpp>
#include <coco/Frequency.hpp>
#include <coco/platform/Loop_RTC0.hpp>
#include <coco/platform/gpio.hpp>
#include <coco/platform/nvic.hpp>


namespace coco {

/**
	Implementation of Audio interface on nrf52 using I2S.

	Reference manual:
		https://infocenter.nordicsemi.com/topic/ps_nrf52840/i2s.html?cp=5_0_0_5_10
	Resources:
		I2S
*/
class Audio_I2S : public BufferDevice {
public:
	enum class Format {
		// stereo, 2x8 bit I2S frames, sample stored in 8 bit signed integer
		MONO_I2S_INT8 = 0x1000,

		// stereo, 2x8 bit aligned frames, sample stored in 8 bit signed integer
		MONO_ALIGNED_INT8 = 0x1100,

		// stereo, 2x16 bit I2S frames, sample stored in 16 bit signed integer
		MONO_I2S_INT16 = 0x1010,

		// stereo, 2x16 bit aligned frames, sample stored in 16 bit signed integer
		MONO_ALIGNED_INT16 = 0x1110,

		// stereo, 2x24 bit I2S frames, sample stored in lower part of 32 bit signed integer
		MONO_I2S_INT32_24 = 0x1021,

		// stereo, 2x24 bit aligned frames, sample stored in lower 24 bits of 32 bit signed integer
		MONO_ALIGNED_INT32_24 = 0x1121,


		// stereo, 2x8 bit I2S frames, sample stored in 8 bit signed integer
		STEREO_I2S_INT8 = 0x0000,

		// stereo, 2x8 bit aligned frames, sample stored in 8 bit signed integer
		STEREO_ALIGNED_INT8 = 0x0100,

		// stereo, 2x16 bit I2S frames, sample stored in 16 bit signed integer
		STEREO_I2S_INT16 = 0x0010,

		// stereo, 2x16 bit aligned frames, sample stored in 16 bit signed integer
		STEREO_ALIGNED_INT16 = 0x0110,

		// stereo, 2x24 bit I2S frames, sample stored in lower part of 32 bit signed integer
		STEREO_I2S_INT32_24 = 0x0021,

		// stereo, 2x24 bit aligned frames, sample stored in lower part of 32 bit signed integer
		STEREO_ALIGNED_INT32_24 = 0x0121
	};

protected:
	Audio_I2S(Loop_RTC0 &loop, int sckPin, int lrckPin, int dataPin, int sampleRate, Format format, int bufferWordCount);

public:
	/**
		Constructor
		@param loop event loop
		@param sckPin i2s sck pin
		@param lrckPin i2s lrck pin
		@param dataPin i2s data pin
		@param sampleRate sample rate
		@param format sample format
		@param bufferWordCount number of 32 bit words in each buffer
	*/
	Audio_I2S(Loop_RTC0 &loop, int sckPin, int lrckPin, int dataPin, Hertz<> sampleRate, Format format, int bufferWordCount)
		: Audio_I2S(loop, sckPin, lrckPin, dataPin, sampleRate.value, format, bufferWordCount) {}

	~Audio_I2S() override;

	class BufferBase;

	State state() override;
	[[nodiscard]] Awaitable<> stateChange(int waitFlags = -1) override;
	int getBufferCount();
	BufferBase &getBuffer(int index);


	// internal buffer base class, derives from IntrusiveListNode for the list of active transfers and Loop_RTC0::Handler2 to be notified from the event loop
	class BufferBase : public BufferImpl, public IntrusiveListNode, public Loop_RTC0::Handler2 {
		friend class Audio_I2S;
	public:
		/**
			Constructor
			@param data data of the buffer
			@param capacity capacity of the buffer
			@param device audio device to attach to
		*/
		BufferBase(uint8_t *data, int capacity, Audio_I2S &device);
		~BufferBase() override;

		bool start(Op op) override;
		bool cancel() override;

	protected:
		void handle() override;

		Audio_I2S &device;
	};

	/**
		Buffer for transferring data to LED strip.
		@tparam C capacity of buffer
	*/
	template <int C>
	class Buffer : public BufferBase {
	public:
		Buffer(Audio_I2S &device) : BufferBase(data, C, device) {}

	protected:
		alignas(4) uint8_t data[C];
	};

	// needs to be called from I2S interrupt handler
	void I2S_IRQHandler() {
		// check if tx pointer has been read
		if (NRF_I2S->EVENTS_TXPTRUPD) {
			NRF_I2S->EVENTS_TXPTRUPD = 0;
			update();
		}
	}

protected:
	void update();

	Loop_RTC0 &loop;

	// dummy (state is always READY)
	CoroutineTaskList<> stateTasks;

	// list of buffers
	IntrusiveList<BufferBase> buffers;

	// list of active transfers
	BufferBase *transfer = nullptr;
	nvic::Queue<BufferBase> transfers;
};

} // namespace coco
