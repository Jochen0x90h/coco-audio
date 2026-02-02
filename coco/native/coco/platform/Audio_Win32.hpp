#include <coco/BufferDevice.hpp>
#include <coco/Frequency.hpp>
#include <coco/IntrusiveList.hpp>
#include <coco/platform/Loop_native.hpp> // includes Windows.h
#include <mmdeviceapi.h>
#include <Audioclient.h>


namespace coco {

/// @brief Audio implementation using Windows Audio Session API (WASAPI)
/// https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi
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
    /// @brief Constructor
    /// @param loop event loop
    /// @param sampleRate sample rate
    /// @param channelCount number of channels
    Audio_Win32(Loop_Win32 &loop, Hertz<> sampleRate, int channelCount, Format format)
        : Audio_Win32(loop, sampleRate.value, channelCount, format) {}

    ~Audio_Win32() override;


    /// @brief Buffer for transferring data to/from audio device
    ///
    class Buffer : public coco::Buffer, public IntrusiveListNode, public IntrusiveListNode2 {
        friend class Audio_Win32;
    public:
        Buffer(Audio_Win32 &device, int size);
        ~Buffer() override;

        bool start(Op op) override;
        bool cancel() override;

    protected:
        void start();

        Audio_Win32 &device_;

        // end position in stream
        int position_;
    };


    // BufferDevice methods
    int getBufferCount() override;
    Buffer &getBuffer(int index) override;

protected:
    void poll();

    Loop_Win32 &loop_;

    // audio client
    IMMDevice *device_ = nullptr;
    IAudioClient *audioClient_ = nullptr;
    IAudioRenderClient *renderClient_ = nullptr;

    int sampleRate_;
    int channelCount_;
    Format format_;

    // polling callback
    TimedTask<Callback> callback_;
    bool polling_ = false;

    // list of buffers
    IntrusiveList<Buffer> buffers_;

    // pending transfers
    IntrusiveList2<Buffer> transfers_;

    // accumulated stream position
    int position_ = 0;
};

} // namespace coco
