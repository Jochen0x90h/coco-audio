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
	, loop(loop), sampleRate(sampleRate), channelCount(channelCount), format(format)
	, callback(makeCallback<Audio_Win32, &Audio_Win32::poll>(this))
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
        eRender, eConsole, &this->device);
	enumerator->Release();
	if (result != S_OK)
		return;

	// get audio client
	result = this->device->Activate(
        IID_IAudioClient, CLSCTX_ALL,
        nullptr, (void**)&this->audioClient);
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
	/*this->sampleSize =*/ waveFormat.Format.nBlockAlign = channelCount * info.byteCount;
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
 	result = this->audioClient->Initialize(shareMode, streamFlags, bufferDuration, 0,
		&waveFormat.Format, nullptr);
	if (result != S_OK)
		return;

	// get the actual size of the allocated buffer
	UINT32 bufferFrameCount;
    result = this->audioClient->GetBufferSize(&bufferFrameCount);
	if (result != S_OK)
		return;

	result = this->audioClient->GetService(
        IID_IAudioRenderClient,
        (void**)&this->renderClient);
	if (result != S_OK)
		return;

	// start playing
	result = audioClient->Start();
	if (result != S_OK)
		return;


	this->st.state = State::READY;
}

Audio_Win32::~Audio_Win32() {
	if (this->renderClient != nullptr)
		this->renderClient->Release();
	if (this->audioClient != nullptr)
		this->audioClient->Release();
	if (this->device != nullptr)
		this->device->Release();
}

int Audio_Win32::getBufferCount() {
	return this->buffers.count();
}

Audio_Win32::Buffer &Audio_Win32::getBuffer(int index) {
	return this->buffers.get(index);
}

void Audio_Win32::poll() {
	this->polling = true;

	// get number of valid frames that are still in the buffer
	UINT32 validFrameCount;
	HRESULT result = this->audioClient->GetCurrentPadding(&validFrameCount);

	// get current position
	int position = this->position - validFrameCount;

	// set elapsed buffers to ready state
	while (!this->transfers.empty()) {
		auto it = this->transfers.begin();
		int d = it->position - position;
		Milliseconds<> duration = (d * 1000ms) / this->sampleRate;
		if (duration.value <= 0) {
			it->remove2();
			it->setReady();
		} else {
			// calc duration in milliseconds until buffer elapses
			std::cout << "invoke in " << duration.value << "ms" << std::endl;
			this->loop.invoke(this->callback, duration);
			return;
		}
	}

	this->polling = false;
}


// Buffer

Audio_Win32::Buffer::Buffer(Audio_Win32 &device, int capacity)
	: coco::Buffer(new uint8_t[capacity], capacity, device.st.state)
	, device(device)
{
	device.buffers.add(*this);
}

Audio_Win32::Buffer::~Buffer() {
	delete [] this->p.data;
}

bool Audio_Win32::Buffer::start(Op op) {
	if (this->st.state != State::READY) {
		assert(this->st.state != State::BUSY);
		return false;
	}

	// check if READ or WRITE flag is set
	assert((op & Op::READ_WRITE) != 0);

	// add to list of pending transfers
	this->device.transfers.add(*this);

	// start if device is ready
	if (this->device.st.state == Device::State::READY)
		start();

	// set state
	setBusy();

	return true;
}

bool Audio_Win32::Buffer::cancel() {
	if (this->st.state != State::BUSY)
		return false;

	remove2();

	return true;
}

void Audio_Win32::Buffer::start() {
	auto &device = this->device;

	auto info = infos[int(device.format)];
	int frameCount = this->p.size / info.byteCount;

	// get buffer
	BYTE *bytes;
	HRESULT result = device.renderClient->GetBuffer(frameCount, &bytes);

	// copy samples
	if (device.format == Format::INT32_24) {
		auto src = reinterpret_cast<const int32_t *>(this->p.data);
		auto dst = reinterpret_cast<int32_t *>(bytes);
		for (int i = 0; i < frameCount; ++i) {
			dst[i] = src[i] << 8;
		}
	} else {
		memcpy(bytes, this->p.data, frameCount * info.byteCount);
	}
	//const float *src = reinterpret_cast<float *>(this->p.data);
	//float *dst = reinterpret_cast<float *>(bytes);
	//for (int i = 0; i < frameCount; ++i) {
	//	dst[i] = src[i];
	//}

	// update stream position and end position of this buffer
	this->position = (device.position += frameCount);

	// release buffer
	DWORD flags = 0;
	result = device.renderClient->ReleaseBuffer(frameCount, flags);

	if (!device.polling)
		device.poll();
}

} // namespace coco
