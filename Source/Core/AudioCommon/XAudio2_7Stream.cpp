// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// Note that this file *and this file only* must also have %DXSDK_DIR%/Include prepended
// to its include path in order fetch dxsdkver.h and XAudio2.h from the DXSDK
// instead of other possible places. This may be accomplished by adding the path to
// the AdditionalIncludeDirectories for this file via msbuild.

#include "AudioCommon/AudioCommon.h"
#include "AudioCommon/XAudio2_7Stream.h"
#include "Common/Event.h"
#include "Core/Core.h"

#ifdef HAVE_DXSDK
#include <dxsdkver.h>
#if (_DXSDK_PRODUCT_MAJOR == 9) && (_DXSDK_PRODUCT_MINOR == 29) && (_DXSDK_BUILD_MAJOR == 1962) && (_DXSDK_BUILD_MINOR == 0)
#define HAVE_DXSDK_JUNE_2010
#else
#pragma message("You have DirectX SDK installed, but it is not the expected version (June 2010). Update it to build this module.")
#endif
#endif

#ifdef HAVE_DXSDK_JUNE_2010
#include <XAudio2.h>
struct StreamingVoiceContext2_7 : public IXAudio2VoiceCallback
{
private:
	CMixer* const m_mixer;
	IXAudio2SourceVoice* m_source_voice;
	u32 volatile m_bufferReady[SOUND_BUFFER_COUNT];
	PBYTE m_bufferAddress[SOUND_BUFFER_COUNT];
	u32 m_NextBuffer;
	std::unique_ptr<BYTE[]> m_xaudio_buffer;
	void SubmitBuffer(u32 index, u32 sizeinbytes);
	bool m_useSurround;
	s32 m_framesizeinbytes;
	s32 m_samplesizeinBytes;
public:
	StreamingVoiceContext2_7(IXAudio2 *pXAudio2, CMixer *pMixer, bool useSurround);

	~StreamingVoiceContext2_7();

	void StreamingVoiceContext2_7::Stop();
	void StreamingVoiceContext2_7::Play();
	void StreamingVoiceContext2_7::WriteFrame(s16* src, u32 numsamples);
	bool StreamingVoiceContext2_7::BufferReady();
	STDMETHOD_(void, OnVoiceError) (THIS_ void* pBufferContext, HRESULT Error) {}
	STDMETHOD_(void, OnVoiceProcessingPassStart) (UINT32) {}
	STDMETHOD_(void, OnVoiceProcessingPassEnd) () {}
	STDMETHOD_(void, OnBufferStart) (void*) {}
	STDMETHOD_(void, OnLoopEnd) (void*) {}
	STDMETHOD_(void, OnStreamEnd) () {}

	STDMETHOD_(void, OnBufferEnd) (void* context);
};

void StreamingVoiceContext2_7::SubmitBuffer(u32 index, u32 sizeinbytes)
{
	m_bufferReady[index] = 0;
	XAUDIO2_BUFFER buf = {};
	buf.AudioBytes = sizeinbytes;
	buf.pContext = (void*)index;
	buf.pAudioData = m_bufferAddress[index];

	m_source_voice->SubmitSourceBuffer(&buf);
}

StreamingVoiceContext2_7::StreamingVoiceContext2_7(IXAudio2 *pXAudio2, CMixer *pMixer, bool useSurround)
	: m_mixer(pMixer), m_useSurround(useSurround), m_xaudio_buffer(new BYTE[SOUND_BUFFER_COUNT * (useSurround ? SOUND_SURROUND_FRAME_SIZE_BYTES : SOUND_STEREO_FRAME_SIZE_BYTES)])
{
	WAVEFORMATEXTENSIBLE wfx = {};
	m_framesizeinbytes = m_useSurround ? SOUND_SURROUND_FRAME_SIZE_BYTES : SOUND_STEREO_FRAME_SIZE_BYTES;
	m_samplesizeinBytes = (m_useSurround ? SOUND_SAMPLES_SURROUND : SOUND_SAMPLES_STEREO) * sizeof(s16);
	wfx.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
	wfx.Format.nSamplesPerSec  = m_mixer->GetSampleRate();
	wfx.Format.nChannels = m_useSurround ? 6 : 2;
	wfx.Format.wBitsPerSample  = 16;
	wfx.Format.nBlockAlign     = wfx.Format.nChannels*wfx.Format.wBitsPerSample / 8;
	wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
	wfx.Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	wfx.Samples.wValidBitsPerSample = 16;
	wfx.dwChannelMask = m_useSurround ? SPEAKER_5POINT1_SURROUND : SPEAKER_STEREO;
	wfx.SubFormat              = KSDATAFORMAT_SUBTYPE_PCM;

	// create source voice
	HRESULT hr;
	if (FAILED(hr = pXAudio2->CreateSourceVoice(&m_source_voice, &wfx.Format, XAUDIO2_VOICE_NOSRC, 1.0f, this)))
	{
		PanicAlertT("XAudio2_7 CreateSourceVoice failed: %#X", hr);
		return;
	}
	m_source_voice->Start();
	BYTE* buff = m_xaudio_buffer.get();
	// Initialize the filling loop with the first buffer
	memset(buff, 0, SOUND_BUFFER_COUNT * m_framesizeinbytes);
	m_NextBuffer = 0;
	// start buffers with silence
	for (int i = 0; i != SOUND_BUFFER_COUNT; ++i)
	{
		m_bufferAddress[i] = buff + (i * m_framesizeinbytes);
		SubmitBuffer(i, m_framesizeinbytes);
	}
}

StreamingVoiceContext2_7::~StreamingVoiceContext2_7()
{
	if (m_source_voice)
	{
		m_source_voice->Stop();
		m_source_voice->DestroyVoice();
	}
}

void StreamingVoiceContext2_7::Stop()
{
	if (m_source_voice)
		m_source_voice->Stop();
}

void StreamingVoiceContext2_7::Play()
{
	if (m_source_voice)
		m_source_voice->Start();
}

void StreamingVoiceContext2_7::OnBufferEnd(void* context)
{
	if (!m_source_voice)
		return;
	u32 index = (u32)context;
	m_bufferReady[index] = 1;
}

void StreamingVoiceContext2_7::WriteFrame(s16* src, u32 numsamples)
{
	memcpy(m_bufferAddress[m_NextBuffer], src, numsamples * m_samplesizeinBytes);
	SubmitBuffer(m_NextBuffer, numsamples * m_samplesizeinBytes);
	m_NextBuffer++;
	m_NextBuffer = m_NextBuffer % SOUND_BUFFER_COUNT;
}

bool StreamingVoiceContext2_7::BufferReady()
{
	return m_bufferReady[m_NextBuffer] == 1;
}

HMODULE XAudio2_7::m_xaudio2_dll = nullptr;

void XAudio2_7::ReleaseIXAudio2(IXAudio2* ptr)
{
	ptr->Release();
}

bool XAudio2_7::InitLibrary()
{
	if (m_xaudio2_dll)
	{
		return true;
	}

	m_xaudio2_dll = ::LoadLibrary(TEXT("xaudio2_7.dll"));

	return m_xaudio2_dll != nullptr;
}

XAudio2_7::XAudio2_7(CMixer *mixer)
	: SoundStream(mixer)
	, m_mastering_voice(nullptr)
	, m_volume(1.0f)
	, m_cleanup_com(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
{
	m_enablesoundloop = true;
}

XAudio2_7::~XAudio2_7()
{
	Stop();
	if (m_cleanup_com)
		CoUninitialize();
}

bool XAudio2_7::Start()
{
	HRESULT hr;
	IXAudio2* xaudptr;
	if (FAILED(hr = XAudio2Create(&xaudptr, 0, XAUDIO2_ANY_PROCESSOR)))
	{
		PanicAlertT("XAudio2_7 init failed: %#X", hr);
		Stop();
		return false;
	}
	m_xaudio2 = std::unique_ptr<IXAudio2, Releaser>(xaudptr);

	// XAudio2 master voice
	// XAUDIO2_DEFAULT_CHANNELS instead of 2 for expansion?
	if (FAILED(hr = m_xaudio2->CreateMasteringVoice(&m_mastering_voice, 2, m_mixer->GetSampleRate())))
	{
		PanicAlertT("XAudio2_7 master voice creation failed: %#X", hr);
		Stop();
		return false;
	}

	// Volume
	m_mastering_voice->SetVolume(m_volume);

	m_voice_context = std::unique_ptr<StreamingVoiceContext2_7>
		(new StreamingVoiceContext2_7(m_xaudio2.get(), m_mixer, SupportSurroundOutput()));

	return SoundStream::Start();
}

void XAudio2_7::InitializeSoundLoop()
{

}
u32 XAudio2_7::SamplesNeeded()
{
	return m_voice_context->BufferReady() ? SOUND_FRAME_SIZE : 0;
}

void XAudio2_7::WriteSamples(s16 *src, u32 numsamples)
{
	m_voice_context->WriteFrame(src, numsamples);
}

bool XAudio2_7::SupportSurroundOutput()
{
	bool surround_capable = Core::g_CoreStartupParameter.bDPL2Decoder;
	return surround_capable;
}

void XAudio2_7::SetVolume(int volume)
{
	//linear 1- .01
	m_volume = (float)volume / 100.f;

	if (m_mastering_voice)
		m_mastering_voice->SetVolume(m_volume);
}

void XAudio2_7::Update()
{
	
}

void XAudio2_7::Clear(bool mute)
{
	SoundStream::Clear(mute);
	if (m_voice_context)
	{
		if (m_muted)
			m_voice_context->Stop();
		else
			m_voice_context->Play();
	}
}

void XAudio2_7::Stop()
{
	SoundStream::Stop();
	m_voice_context.reset();

	if (m_mastering_voice)
	{
		m_mastering_voice->DestroyVoice();
		m_mastering_voice = nullptr;
	}

	m_xaudio2.reset(); // release interface

	if (m_xaudio2_dll)
	{
		::FreeLibrary(m_xaudio2_dll);
		m_xaudio2_dll = nullptr;
	}
}

#else

struct StreamingVoiceContext2_7 {};
struct IXAudio2 {};
struct IXAudio2MasteringVoice {};
void XAudio2_7::ReleaseIXAudio2(IXAudio2* ptr) {}

XAudio2_7::XAudio2_7(CMixer *mixer)
	: SoundStream(mixer)
	, m_mastering_voice(nullptr)
	, m_volume(1.0f)
	, m_cleanup_com(false)
{}

XAudio2_7::~XAudio2_7() {}

bool XAudio2_7::Start() { return SoundStream::Start(); }
void XAudio2_7::Stop() {}
void XAudio2_7::Update() {}
void XAudio2_7::Clear(bool mute) {}
void XAudio2_7::SetVolume(int volume) {}
bool XAudio2_7::InitLibrary() { return false; }
bool XAudio2_7::SupportSurroundOutput() { return false;}
void XAudio2_7::InitializeSoundLoop(){}
u32  XAudio2_7::SamplesNeeded() { return 0; }
void XAudio2_7::WriteSamples(s16 *src, u32 numsamples){}
#endif