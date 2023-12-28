#include <coco/BufferDevice.hpp>
#include <coco/Frequency.hpp>
#include <coco/IntrusiveList.hpp>
#include <coco/platform/Loop_native.hpp> // includes Windows.h
#include <mmdeviceapi.h>
#include <Audioclient.h>


namespace coco {

/**
	Audio implementation using Windows Audio Session API (WASAPI)
    https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi
*/
class Audio_Win32 : public BufferDevice {
public:
	enum class Format {
		UINT8,
		INT16,
		INT24,
		INT32_24,
		FLOAT32,
	};

protected:
	Audio_Win32(Loop_Win32 &loop, int sampleRate, int channelCount, Format format);

public:
	/**
		Constructor
		@param loop event loop
		@param sampleRate sample rate
		@param channelCount number of channels
	*/
	Audio_Win32(Loop_Win32 &loop, Hertz<> sampleRate, int channelCount, Format format)
		: Audio_Win32(loop, sampleRate.value, channelCount, format) {}

	~Audio_Win32() override;


	/**
		Buffer for transferring data to/from audio device
	*/
	class Buffer : public coco::Buffer, public IntrusiveListNode, public IntrusiveListNode2 {
		friend class Audio_Win32;
	public:
		Buffer(Audio_Win32 &device, int size);
		~Buffer() override;

		bool start(Op op) override;
		bool cancel() override;

	protected:
		void start();

		Audio_Win32 &device;

		// end position in stream
		int position;
	};


	// BufferDevice methods
	int getBufferCount() override;
	Buffer &getBuffer(int index) override;

protected:
	void poll();

	Loop_Win32 &loop;

	// audio client
	IMMDevice *device = nullptr;
	IAudioClient *audioClient = nullptr;
	IAudioRenderClient *renderClient = nullptr;

	int sampleRate;
	int channelCount;
	//int sampleSize;
	Format format;

	// polling callback
	TimedTask<Callback> callback;
	bool polling = false;

	// list of buffers
	IntrusiveList<Buffer> buffers;

	// pending transfers
	IntrusiveList2<Buffer> transfers;

	// accumulated stream position
	int position = 0;
};

} // namespace coco
