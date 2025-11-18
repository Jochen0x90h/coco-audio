#include "Audio_Win32.hpp"
#include <filesystem>
#include <iostream>


namespace coco {

// https://learn.microsoft.com/en-us/windows/win32/coreaudio/rendering-a-stream

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);


struct FormatInfo {
    uint8_t byteCount;
    uint8_t validBits;
};
static const FormatInfo infos[] = {{1, 8}, {2, 16}, {3, 24}, {4, 24}, {4, 32}};

Audio_Win32::Audio_Win32(Loop_Win32 &loop, int sampleRate, int channelCount, Format format)
    : BufferDevice(State::DISABLED)
    , loop_(loop), sampleRate_(sampleRate), channelCount_(channelCount), format_(format)
    , callback_(makeCallback<Audio_Win32, &Audio_Win32::poll>(this))
{
    HRESULT result;

    result = CoInitialize(nullptr);
    if (result != S_OK)
        return;

    IMMDeviceEnumerator *enumerator;
    result = CoCreateInstance(
        CLSID_MMDeviceEnumerator, nullptr,
        CLSCTX_ALL, IID_IMMDeviceEnumerator,
        (void**)&enumerator);
    if (result != S_OK)
        return;

    // get default endpoint
    result = enumerator->GetDefaultAudioEndpoint(
        eRender, eConsole, &device_);
    enumerator->Release();
    if (result != S_OK)
        return;

    // get audio client
    result = device_->Activate(
        IID_IAudioClient, CLSCTX_ALL,
        nullptr, (void**)&audioClient_);
    if (result != S_OK)
        return;

    // get mix format
    //WAVEFORMATEXTENSIBLE *mixFormat;
    //result = audioClient->GetMixFormat(reinterpret_cast<WAVEFORMATEX **>(&mixFormat));
    //if (result != S_OK)
    //	return;

    // get format info
    auto info = infos[int(format)];

    // define sample format
    WAVEFORMATEXTENSIBLE waveFormat;
    waveFormat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    waveFormat.Format.nChannels = channelCount;
    waveFormat.Format.nSamplesPerSec = sampleRate;
    waveFormat.Format.nAvgBytesPerSec = sampleRate * channelCount * info.byteCount;
    waveFormat.Format.nBlockAlign = channelCount * info.byteCount; // sample size
    waveFormat.Format.wBitsPerSample = info.byteCount * 8;
    waveFormat.Format.cbSize = 22;
    waveFormat.Samples.wValidBitsPerSample = info.validBits;
    waveFormat.dwChannelMask = channelCount == 1 ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    if (format != Format::FLOAT32)
        waveFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    else
        waveFormat.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    //CoTaskMemFree(mixFormat);

    // initialize audio client
    AUDCLNT_SHAREMODE shareMode = AUDCLNT_SHAREMODE_SHARED;
    //AUDCLNT_SHAREMODE shareMode = AUDCLNT_SHAREMODE_EXCLUSIVE;
    DWORD streamFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
    REFERENCE_TIME bufferDuration = 1000 * 10000; // buffer duration in 100-nanosecond units
     result = audioClient_->Initialize(shareMode, streamFlags, bufferDuration, 0,
        &waveFormat.Format, nullptr);
    if (result != S_OK)
        return;

    // get the actual size of the allocated buffer
    UINT32 bufferFrameCount;
    result = audioClient_->GetBufferSize(&bufferFrameCount);
    if (result != S_OK)
        return;

    result = audioClient_->GetService(
        IID_IAudioRenderClient,
        (void**)&renderClient_);
    if (result != S_OK)
        return;

    // start playing
    result = audioClient_->Start();
    if (result != S_OK)
        return;


    st.state = State::READY;
}

Audio_Win32::~Audio_Win32() {
    if (renderClient_ != nullptr)
        renderClient_->Release();
    if (audioClient_ != nullptr)
        audioClient_->Release();
    if (device_ != nullptr)
        device_->Release();
}

int Audio_Win32::getBufferCount() {
    return buffers_.count();
}

Audio_Win32::Buffer &Audio_Win32::getBuffer(int index) {
    return buffers_.get(index);
}

void Audio_Win32::poll() {
    polling_ = true;

    // get number of valid frames that are still in the buffer
    UINT32 validFrameCount;
    HRESULT result = audioClient_->GetCurrentPadding(&validFrameCount);

    // get current position
    int position = position_ - validFrameCount;

    // set elapsed buffers to ready state
    while (!transfers_.empty()) {
        auto it = transfers_.begin();
        int d = it->position_ - position;
        Milliseconds<> duration = (d * 1000ms) / sampleRate_;
        if (duration.value <= 0) {
            it->remove2();
            it->setReady();
        } else {
            // calc duration in milliseconds until buffer elapses
            std::cout << "invoke in " << duration.value << "ms" << std::endl;
            loop_.invoke(callback_, duration);
            return;
        }
    }

    polling_ = false;
}


// Buffer

Audio_Win32::Buffer::Buffer(Audio_Win32 &device, int capacity)
    : coco::Buffer(new uint8_t[capacity], capacity, device.st.state)
    , device_(device)
{
    device.buffers_.add(*this);
}

Audio_Win32::Buffer::~Buffer() {
    delete [] data_;
}

bool Audio_Win32::Buffer::start(Op op) {
    if (st.state != State::READY) {
        assert(st.state != State::BUSY);
        return false;
    }

    // check if READ or WRITE flag is set
    assert((op & Op::READ_WRITE) != 0);

    // add to list of pending transfers
    device_.transfers_.add(*this);

    // start if device is ready
    if (device_.st.state == Device::State::READY)
        start();

    // set state
    setBusy();

    return true;
}

bool Audio_Win32::Buffer::cancel() {
    if (st.state != State::BUSY)
        return false;

    remove2();

    return true;
}

void Audio_Win32::Buffer::start() {
    auto &device = device_;

    auto info = infos[int(device_.format_)];
    int frameCount = size_ / info.byteCount;

    // get buffer
    BYTE *bytes;
    HRESULT result = device.renderClient_->GetBuffer(frameCount, &bytes);

    // copy samples
    if (device.format_ == Format::INT32_24) {
        auto src = reinterpret_cast<const int32_t *>(data_);
        auto dst = reinterpret_cast<int32_t *>(bytes);
        for (int i = 0; i < frameCount; ++i) {
            dst[i] = src[i] << 8;
        }
    } else {
        memcpy(bytes, data_, frameCount * info.byteCount);
    }

    // update stream position and end position of this buffer
    position_ = (device.position_ += frameCount);

    // release buffer
    DWORD flags = 0;
    result = device.renderClient_->ReleaseBuffer(frameCount, flags);

    if (!device.polling_)
        device.poll();
}

} // namespace coco
