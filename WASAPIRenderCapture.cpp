#include "pch.h"
#include "WASAPIRenderCapture.h"


using namespace Windows::System::Threading;
using namespace SDKTemplate::WASAPIAudio;

#define BITS_PER_BYTE 8

WASAPIRenderCapture::WASAPIRenderCapture() :
	m_RenderInit(false),
	m_BufferFrames(0),
	m_DeviceStateChanged(nullptr),
	m_AudioClient(nullptr),
	m_CaptureAudioClient(nullptr),
	m_AudioRenderClient(nullptr),
	m_SampleReadyAsyncResult(nullptr),
	m_dwQueueID(0),
	m_CaptureDeviceStateChanged(nullptr),
	m_AudioCaptureClient(nullptr),
	m_Data(nullptr),
	m_CaptureSource(nullptr)
{

	m_SampleReadyEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
	if (nullptr == m_SampleReadyEvent)
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}
	if (!InitializeCriticalSectionEx(&m_CritSec, 0, 0))
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}
	m_DeviceStateChanged = ref new DeviceChangedEvent();
	m_CaptureDeviceStateChanged = ref new DeviceChangedEvent();
	if (nullptr == m_DeviceStateChanged)
	{
		ThrowIfFailed(E_OUTOFMEMORY);
	}
	HRESULT hr = S_OK;
	DWORD dwTaskID = 0;
	hr = MFLockSharedWorkQueue(L"Capture", 0, &dwTaskID, &m_dwQueueID);
	if (FAILED(hr))
	{
		ThrowIfFailed(hr);
	}
	m_xSampleReady.SetQueueID(m_dwQueueID);

}

WASAPIRenderCapture::~WASAPIRenderCapture() {
	SAFE_RELEASE(m_AudioClient);
	SAFE_RELEASE(m_CaptureAudioClient);
	SAFE_RELEASE(m_AudioRenderClient);
	SAFE_RELEASE(m_AudioCaptureClient);
	SAFE_RELEASE(m_SampleReadyAsyncResult);

	if (INVALID_HANDLE_VALUE != m_SampleReadyEvent)
	{
		CloseHandle(m_SampleReadyEvent);
		m_SampleReadyEvent = INVALID_HANDLE_VALUE;
	}
	MFUnlockWorkQueue(m_dwQueueID);
	
	m_DeviceStateChanged = nullptr;
	m_CaptureDeviceStateChanged = nullptr;
	DeleteCriticalSection(&m_CritSec);
}

HRESULT WASAPIRenderCapture::InitAudioDeviceAsync() {
	IActivateAudioInterfaceAsyncOperation * asyncOp;
	HRESULT hr = S_OK;

	m_DeviceIdString = MediaDevice::GetDefaultAudioRenderId(Windows::Media::Devices::AudioDeviceRole::Default);

	hr = ActivateAudioInterfaceAsync(m_DeviceIdString->Data(), __uuidof(IAudioClient3), nullptr, this, &asyncOp);

	if (FAILED(hr))
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
		m_CaptureDeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
	}
	SAFE_RELEASE(asyncOp);
	return hr;
}

HRESULT WASAPIRenderCapture::ActivateCompleted(IActivateAudioInterfaceAsyncOperation * operation) {
	HRESULT hr = CaptureActivateCompleted(operation);
	if (FAILED(hr))
	{
		return hr;
	}
	if (m_RenderInit)
	{
		HRESULT hr = RenderActivateCompleted(operation);
	}
	
	return hr;
}

HRESULT WASAPIRenderCapture::SetProperties(DEVICEPROPS props)
{
	m_DeviceProps = props;
	return S_OK;
}


HRESULT WASAPIRenderCapture::SetVolumeOnSession(UINT32 volume)
{
	if (volume > 100)
	{
		return E_INVALIDARG;
	}

	HRESULT hr = S_OK;
	ISimpleAudioVolume *SessionAudioVolume = nullptr;
	float ChannelVolume = 0.0;
	hr = m_AudioClient->GetService(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&SessionAudioVolume));
	if (FAILED(hr))
	{
		goto exit;
	}
	ChannelVolume = volume / (float) 100.0;
	hr = SessionAudioVolume->SetMasterVolume(ChannelVolume, nullptr);

exit:
	SAFE_RELEASE(SessionAudioVolume);
	return hr;
}



HRESULT WASAPIRenderCapture::ConfigureDeviceInternal()
{
	if (m_DeviceStateChanged->GetState() != DeviceState::DeviceStateActivated)
	{
		return E_NOT_VALID_STATE;
	}

	HRESULT  hr = S_OK;
	AudioClientProperties audioProps = { 0 };
	audioProps.cbSize = sizeof(AudioClientProperties);
	audioProps.bIsOffload = m_DeviceProps.IsHWOffload;
	audioProps.eCategory = AudioCategory_Media;

	if (m_DeviceProps.IsRawChosen && m_DeviceProps.IsRawSupported)
	{
		audioProps.Options = AUDCLNT_STREAMOPTIONS_RAW;
	}

	hr = m_AudioClient->SetClientProperties(&audioProps);
	if (FAILED(hr))
	{
		return hr;
	}

	hr = m_AudioClient->GetMixFormat(&m_MixFormat);
	if (FAILED(hr))
	{
		return hr;
	}

	hr = m_AudioClient->GetSharedModeEnginePeriod(m_MixFormat, &m_DefaultPeriodInFrames, &m_FundamentalPeriodInFrames, &m_MinPeriodInFrames, &m_MaxPeriodInFrames);
	if (FAILED(hr))
	{
		return hr;
	}

	hr = ValidateBufferValue();
	return hr;


}

HRESULT WASAPIRenderCapture::ValidateBufferValue()
{
	HRESULT hr = S_OK;
	if (!m_DeviceProps.IsHWOffload)
	{
		m_DeviceProps.hnsBufferDuration = 0;
		return hr;
	}
	REFERENCE_TIME hnsMinBufferDuration = 0;
	REFERENCE_TIME hnsMaxBufferDuration = 0;

	hr = m_AudioClient->GetBufferSizeLimits(m_MixFormat, true, &hnsMinBufferDuration, &hnsMaxBufferDuration);
	if (SUCCEEDED(hr))
	{
		if (m_DeviceProps.hnsBufferDuration < hnsMinBufferDuration)
		{
			m_DeviceProps.hnsBufferDuration = hnsMinBufferDuration;
		}
		else if (m_DeviceProps.hnsBufferDuration > hnsMaxBufferDuration)
		{
			m_DeviceProps.hnsBufferDuration = hnsMaxBufferDuration;
		}
	}
	return hr;
}



//HRESULT WASAPIRender::ConfigureSource()
//{
//	HRESULT hr = S_OK;
//	UINT32 FramesPerPeriod = GetBufferFramesPerPeriod();
//	if (m_DeviceProps.IsTonePlayback)
//	{
//		m_ToneSource = new ToneSampleGenerator();
//		if (m_ToneSource)
//		{
//			hr = m_ToneSource->GenerateSampleBuffer(m_DeviceProps.Frequency, FramesPerPeriod, m_MixFormat);
//		}
//		else
//		{
//			hr = E_OUTOFMEMORY;
//		}
//	}
//#ifdef MF
//	else
//	{
//
//		m_MFSource = new MFSampleGenerator();
//		if (m_MFSource)
//		{
//			hr = m_MFSource->Initialize(m_DeviceProps.ContentStream, FramesPerPeriod, m_MixFormat);
//		}
//		else
//		{
//			hr = E_OUTOFMEMORY;
//		}
//
//
//
//	}
//#endif // MF
//	return hr;
//}

UINT32 WASAPIRenderCapture::GetBufferFramesPerPeriod()
{
	REFERENCE_TIME defaultDevicePeriod = 0;
	REFERENCE_TIME minimumDevicePeriod = 0;
	if (m_DeviceProps.IsHWOffload)
	{
		return m_BufferFrames;
	}

	HRESULT hr = m_AudioClient->GetDevicePeriod(&defaultDevicePeriod, &minimumDevicePeriod);
	if (FAILED(hr))
	{
		return 0;
	}
	double devicePeriodInSec = 0.0;
	if (m_DeviceProps.IsLowLatency)
	{
		devicePeriodInSec = minimumDevicePeriod / (10000.0 * 1000.0);
	}
	else
	{
		devicePeriodInSec = defaultDevicePeriod / (10000.0 * 1000.0);
	}

	return static_cast<UINT32>(m_MixFormat->nSamplesPerSec * devicePeriodInSec + 0.5);
}



HRESULT WASAPIRenderCapture::StartPlaybackAsync()
{
	HRESULT hr = S_OK;

	if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStateStopped
		|| m_DeviceStateChanged->GetState() == DeviceState::DeviceStateInitialized)
	{
		
		//hr = ConfigureSource();
		/*if (FAILED(hr))
		{
			m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
			return hr;
		}*/
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateStarting, hr, true);
		return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartPlayback, nullptr);

	}
	else if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStatePaused)
	{
		return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartPlayback, nullptr);
	}
	return E_FAIL;
}

HRESULT WASAPIRenderCapture::OnStartPlayback(IMFAsyncResult * pResult)
{
	HRESULT hr = S_OK;

	hr = OnAudioSampleRequested(true);
	if (FAILED(hr))
	{
		goto exit;
	}

	hr = m_AudioClient->Start();
	if (SUCCEEDED(hr))
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStatePlaying, S_OK, true);
		hr = MFPutWaitingWorkItem(m_SampleReadyEvent, 0, m_SampleReadyAsyncResult, &m_SampleReadyKey);

	}

exit:
	if (FAILED(hr))
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
	}
	return S_OK;
}
HRESULT WASAPIRenderCapture::StopPlaybackAsync()
{
	if ((m_DeviceStateChanged->GetState() != DeviceState::DeviceStatePlaying) &&
		(m_DeviceStateChanged->GetState() != DeviceState::DeviceStatePaused) &&
		(m_DeviceStateChanged->GetState() != DeviceState::DeviceStateInError))
	{
		return E_NOT_VALID_STATE;
	}
	m_DeviceStateChanged->SetState(DeviceState::DeviceStateStopping, S_OK, true);
	return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStopPlayback, nullptr);
}

HRESULT WASAPIRenderCapture::OnStopPlayback(IMFAsyncResult * pResult)
{
	if (0 != m_SampleReadyKey)
	{
		MFCancelWorkItem(m_SampleReadyKey);
		m_SampleReadyKey = 0;
	}

	OnAudioSampleRequested(true);
	m_AudioClient->Stop();
	SAFE_RELEASE(m_SampleReadyAsyncResult);

	/*if (m_DeviceProps.IsTonePlayback)
	{
		m_ToneSource->Flush();
	}*/

	m_DeviceStateChanged->SetState(DeviceState::DeviceStateStopped, S_OK, true);
	return S_OK;
}

HRESULT WASAPIRenderCapture::PausePlaybackAsync()
{
	if ((m_DeviceStateChanged->GetState() != DeviceState::DeviceStatePlaying) &&
		(m_DeviceStateChanged->GetState() != DeviceState::DeviceStateInError))
	{
		return E_NOT_VALID_STATE;
	}

	m_DeviceStateChanged->SetState(DeviceState::DeviceStatePausing, S_OK, true);
	return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xPausePlayback, nullptr);
}

HRESULT SDKTemplate::WASAPIAudio::WASAPIRenderCapture::StartCaptureAsync()
{
	HRESULT hr = S_OK;
	if (m_CaptureDeviceStateChanged->GetState() == DeviceState::DeviceStateInitialized)
	{
		m_CaptureDeviceStateChanged->SetState(DeviceState::DeviceStateCapturing, hr, true);
		return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartCapture, nullptr);
	}
	return E_NOT_VALID_STATE;
}

HRESULT SDKTemplate::WASAPIAudio::WASAPIRenderCapture::StopCaptureAsync()
{
	if ((m_CaptureDeviceStateChanged->GetState() != DeviceState::DeviceStateCapturing) &&
		(m_CaptureDeviceStateChanged->GetState() != DeviceState::DeviceStateInError))
	{
		return E_NOT_VALID_STATE;
	}

	m_CaptureDeviceStateChanged->SetState(DeviceState::DeviceStateStopping, S_OK, true);
	return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStopCapture, nullptr);
}

HRESULT SDKTemplate::WASAPIAudio::WASAPIRenderCapture::FinishCaptureAsync()
{
	if (m_CaptureDeviceStateChanged->GetState() == DeviceState::DeviceStateFlushing)
	{
		return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xFinishCapture, nullptr);
	}

	// We are in the wrong state
	return E_NOT_VALID_STATE;
}




HRESULT WASAPIRenderCapture::OnPausePlayback(IMFAsyncResult * pResult)
{
	return S_OK;
}


HRESULT WASAPIRenderCapture::OnSampleReady(IMFAsyncResult * pResult)
{
	HRESULT hr = S_OK;
	hr = OnAudioCaptureSampleRequested(false);
	if (SUCCEEDED(hr))
	{
		if (m_CaptureDeviceStateChanged->GetState() == DeviceState::DeviceStateCapturing)
		{
			hr = MFPutWaitingWorkItem(m_SampleReadyEvent, 0, m_SampleReadyAsyncResult, &m_SampleReadyKey);
		}
	}
	else
	{
		m_CaptureDeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
		return hr;
	}
	hr = OnAudioSampleRequested(false);
	if (SUCCEEDED(hr))
	{
		if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStatePlaying)
		{
			hr = MFPutWaitingWorkItem(m_SampleReadyEvent, 0, m_SampleReadyAsyncResult, &m_SampleReadyKey);
		}
	}
	else
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
	}
	return hr;
}

HRESULT WASAPIRenderCapture::OnAudioSampleRequested(Platform::Boolean IsSilence /*= false*/)
{
	HRESULT hr = S_OK;
	UINT32 PaddingFrames = 0;
	UINT32 FramesAvailable = 0;

	EnterCriticalSection(&m_CritSec);

	// Get padding in existing buffer
	hr = m_AudioClient->GetCurrentPadding(&PaddingFrames);
	if (FAILED(hr))
	{
		goto exit;
	}

	// Audio frames available in buffer
	if (m_DeviceProps.IsHWOffload)
	{
		// In HW mode, GetCurrentPadding returns the number of available frames in the 
		// buffer, so we can just use that directly
		FramesAvailable = PaddingFrames;
	}
	else
	{
		// In non-HW shared mode, GetCurrentPadding represents the number of queued frames
		// so we can subtract that from the overall number of frames we have
		FramesAvailable = m_BufferFrames - PaddingFrames;
	}

	// Only continue if we have buffer to write data
	if (FramesAvailable > 0)
	{
		if (IsSilence)
		{
			BYTE *Data;

			// Fill the buffer with silence
			hr = m_AudioRenderClient->GetBuffer(FramesAvailable, &Data);
			if (FAILED(hr))
			{
				goto exit;
			}

			hr = m_AudioRenderClient->ReleaseBuffer(FramesAvailable, AUDCLNT_BUFFERFLAGS_SILENT);
			goto exit;
		}

		// Even if we cancel a work item, this may still fire due to the async
		// nature of things.  There should be a queued work item already to handle
		// the process of stopping or stopped
		if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStatePlaying)
		{
			hr = GetCaptureSample(FramesAvailable);
			
			// Fill the buffer with a playback sample
//			if (m_DeviceProps.IsTonePlayback)
//			{
//				hr = GetToneSample(FramesAvailable);
//			}
//#ifdef MF
//
//			else
//			{
//				hr = GetMFSample(FramesAvailable);
//			}
//#endif // MF

		}
	}

exit:
	LeaveCriticalSection(&m_CritSec);
	if (AUDCLNT_E_RESOURCES_INVALIDATED == hr)
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateUnInitialized, hr, false);
		SAFE_RELEASE(m_AudioClient);
		SAFE_RELEASE(m_AudioRenderClient);
		SAFE_RELEASE(m_SampleReadyAsyncResult);

		hr = InitAudioDeviceAsync();

	}
	return hr;
}

HRESULT SDKTemplate::WASAPIAudio::WASAPIRenderCapture::OnAudioCaptureSampleRequested(Platform::Boolean IsSilence)
{
	HRESULT hr = S_OK;
	UINT32 FramesAvailable = 0;
	BYTE * Data = nullptr;
	DWORD dwCaptureFlags;
	uint64 u64DevicePosition = 0;
	UINT64 u64QPCPosition = 0;
	DWORD cbBytesToCapture = 0;
	EnterCriticalSection(&m_CritSec);
	if ((m_DeviceStateChanged->GetState() == DeviceState::DeviceStateStopping) ||
		(m_DeviceStateChanged->GetState() == DeviceState::DeviceStateFlushing))
	{
		goto exit;
	}
	for (hr = m_AudioCaptureClient->GetNextPacketSize(&FramesAvailable); SUCCEEDED(hr) && FramesAvailable > 0; hr = m_AudioCaptureClient->GetNextPacketSize(&FramesAvailable))
	{
		cbBytesToCapture = FramesAvailable * m_MixFormat->nBlockAlign;
		if (m_cbDataSize + cbBytesToCapture < m_cbDataSize)
		{
			StopCaptureAsync();
			goto exit;
		}

		hr = m_AudioCaptureClient->GetBuffer(&Data, &FramesAvailable, &dwCaptureFlags, &u64DevicePosition, &u64QPCPosition);
		if (FAILED(hr))
		{
			goto exit;
		}

		if (dwCaptureFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
		{
			m_DeviceStateChanged->SetState(DeviceState::DeviceStateDiscontinuity, S_OK, true);
			m_DeviceStateChanged->SetState(DeviceState::DeviceStateCapturing, S_OK, false);
		}

		if ((dwCaptureFlags&AUDCLNT_BUFFERFLAGS_SILENT) || IsSilence)
		{
			memset(Data, 0, FramesAvailable*m_MixFormat->nBlockAlign);
		}
		auto dataType = ref new Platform::Array<BYTE, 1>(Data, cbBytesToCapture);
		m_AudioCaptureClient->ReleaseBuffer(FramesAvailable);
		//m_WAVDataWriter->WriteBytes(dataType);
		UINT32 FramesPerPeriod = GetBufferFramesPerPeriod();
		m_CaptureSource->GeneratrSampleBuffer(FramesPerPeriod, m_MixFormat, Data);
		m_cbDataSize += cbBytesToCapture;
		m_cb_FlushCounter += cbBytesToCapture;

		if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStateStopping)
		{
			m_DeviceStateChanged->SetState(DeviceState::DeviceStateFlushing, S_OK, true);
			FinishCaptureAsync();
		}

		/*if ((m_cb_FlushCounter > (m_MixFormat->nAvgBytesPerSec * FLUSH_INTERVAL_SEC)) && !m_fWriting)
		{
			m_fWriting = true;

			m_cb_FlushCounter = 0;
			concurrency::task<unsigned int>(m_WAVDataWriter->StoreAsync()).then([this](unsigned int BytesWritten) {
				m_fWriting = false;

				if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStateStopping)
				{
					m_DeviceStateChanged->SetState(DeviceState::DeviceStateFlushing, S_OK, true);
					FinishCaptureAsync();
				}
			});
		}*/
	}
exit:
	LeaveCriticalSection(&m_CritSec);
	return hr;
}

HRESULT SDKTemplate::WASAPIAudio::WASAPIRenderCapture::GetCaptureSample(UINT32 FramesAvailable)
{
	HRESULT hr = S_OK;
	BYTE * Data;
	if (m_CaptureSource->IsEOF())
	{
		hr = m_AudioRenderClient->GetBuffer(FramesAvailable, &Data);
		if (SUCCEEDED(hr))
		{
			hr = m_AudioRenderClient->ReleaseBuffer(FramesAvailable, AUDCLNT_BUFFERFLAGS_SILENT);
		}
		StopPlaybackAsync();
	}
	else if (m_CaptureSource->GetBufferLength() <= (FramesAvailable * m_MixFormat->nBlockAlign))
	{
		UINT32 ActualFramesToRead = m_CaptureSource->GetBufferLength() / m_MixFormat->nBlockAlign;
		UINT32 ActualBytesToRead = ActualFramesToRead * m_MixFormat->nBlockAlign;
		

		hr = m_AudioRenderClient->GetBuffer(FramesAvailable, &Data);
		if (SUCCEEDED(hr))
		{
			hr = m_CaptureSource->FillSampleBuffer(ActualBytesToRead, Data);
			m_AudioRenderClient->ReleaseBuffer(ActualBytesToRead, 0);
		}
	}
	
	return hr;

}


HRESULT SDKTemplate::WASAPIAudio::WASAPIRenderCapture::RenderActivateCompleted(IActivateAudioInterfaceAsyncOperation * operation)
{
	HRESULT hr = S_OK;
	HRESULT hrActivateResult = S_OK;
	IUnknown * punkAudioInterface = nullptr;
	//ComPtr<IUnknown> punkAudioInterface;
	if (m_DeviceStateChanged->GetState() != DeviceState::DeviceStateUnInitialized)
	{
		hr = E_NOT_VALID_STATE;
		goto exit;
	}

	hr = operation->GetActivateResult(&hrActivateResult, &punkAudioInterface);
	if (SUCCEEDED(hr) && SUCCEEDED(hrActivateResult))
	{

		m_DeviceStateChanged->SetState(DeviceState::DeviceStateActivated, S_OK, false);
		punkAudioInterface->QueryInterface(IID_PPV_ARGS(&m_AudioClient));
		if (nullptr == m_AudioClient)
		{
			hr = E_FAIL;
			goto exit;
		}

		hr = ConfigureDeviceInternal();
		if (FAILED(hr))
		{
			goto exit;
		}

		if (m_DeviceProps.IsLowLatency == false)
		{
			hr = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK
				| AUDCLNT_STREAMFLAGS_NOPERSIST, m_DeviceProps.hnsBufferDuration, m_DeviceProps.hnsBufferDuration,
				m_MixFormat, nullptr);
		}
		else
		{
			hr = m_AudioClient->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
				m_MinPeriodInFrames, m_MixFormat, nullptr);
		}

		if (FAILED(hr))
		{
			goto exit;
		}

		hr = m_AudioClient->GetBufferSize(&m_BufferFrames);
		if (FAILED(hr))
		{
			goto exit;
		}

		// Get the render client
		hr = m_AudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_AudioRenderClient);
		if (FAILED(hr))
		{
			goto exit;
		}

		hr = MFCreateAsyncResult(nullptr, &m_xSampleReady, nullptr, &m_SampleReadyAsyncResult);
		if (FAILED(hr))
		{
			goto exit;
		}

		hr = m_AudioClient->SetEventHandle(m_SampleReadyEvent);
		if (FAILED(hr))
		{
			goto exit;
		}

		m_DeviceStateChanged->SetState(DeviceState::DeviceStateInitialized, S_OK, true);

	}
exit:
	SAFE_RELEASE(punkAudioInterface);
	if (FAILED(hr))
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
		SAFE_RELEASE(m_AudioClient);
		SAFE_RELEASE(m_AudioRenderClient);
		SAFE_RELEASE(m_SampleReadyAsyncResult);
	}

	return S_OK;
}

HRESULT SDKTemplate::WASAPIAudio::WASAPIRenderCapture::CaptureActivateCompleted(IActivateAudioInterfaceAsyncOperation * operation)
{
	HRESULT hr = S_OK;
	HRESULT hrActivateResult = S_OK;
	ComPtr<IUnknown> punkAudioInterface;

	hr = operation->GetActivateResult(&hrActivateResult, &punkAudioInterface);
	if (FAILED(hr) || FAILED(hrActivateResult))
	{
		goto exit;
	}

	punkAudioInterface.CopyTo(&m_CaptureAudioClient);
	if (nullptr == m_AudioClient)
	{
		hr = E_NOINTERFACE;
		goto exit;
	}

	hr = m_CaptureAudioClient->GetMixFormat(&m_MixFormat);
	if (FAILED(hr))
	{
		goto exit;
	}

	switch (m_MixFormat->wFormatTag)
	{
	case WAVE_FORMAT_PCM:
		break;
	case WAVE_FORMAT_IEEE_FLOAT:
		m_MixFormat->wFormatTag = WAVE_FORMAT_PCM;
		m_MixFormat->wBitsPerSample = 16;
		m_MixFormat->nBlockAlign = m_MixFormat->nChannels * m_MixFormat->wBitsPerSample / BITS_PER_BYTE;
		m_MixFormat->nAvgBytesPerSec = m_MixFormat->nSamplesPerSec * m_MixFormat->nBlockAlign;
		break;

	case WAVE_FORMAT_EXTENSIBLE:
	{
		WAVEFORMATEXTENSIBLE *pWaveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(m_MixFormat);
		if (pWaveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			// nothing to do
		}
		else if (pWaveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
		{
			pWaveFormatExtensible->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
			pWaveFormatExtensible->Format.wBitsPerSample = 16;
			pWaveFormatExtensible->Format.nBlockAlign =
				pWaveFormatExtensible->Format.nChannels *
				pWaveFormatExtensible->Format.wBitsPerSample /
				BITS_PER_BYTE;
			pWaveFormatExtensible->Format.nAvgBytesPerSec =
				pWaveFormatExtensible->Format.nSamplesPerSec *
				pWaveFormatExtensible->Format.nBlockAlign;
			pWaveFormatExtensible->Samples.wValidBitsPerSample =
				pWaveFormatExtensible->Format.wBitsPerSample;

			// leave the channel mask as-is
		}
		else
		{
			// we can only handle float or PCM
			hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
		}
		break;
	}

	default:
		// we can only handle float or PCM
		hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
		break;
	}

	if (FAILED(hr))
	{
		goto exit;
	}

	hr = m_CaptureAudioClient->GetSharedModeEnginePeriod(m_MixFormat, &m_DefaultPeriodInFrames, &m_FundamentalPeriodInFrames, &m_MinPeriodInFrames, &m_MinPeriodInFrames);

	if (FAILED(hr))
	{
		goto exit;
	}

	hr = m_CaptureAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
		200000,
		0,
		m_MixFormat,
		nullptr);

	if (FAILED(hr))
	{
		goto exit;
	}

	hr = m_CaptureAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_AudioCaptureClient);

	if (FAILED(hr))
	{
		goto exit;
	}

	hr = MFCreateAsyncResult(nullptr, &m_xSampleReady, nullptr, &m_SampleReadyAsyncResult);
	if (FAILED(hr))
	{
		goto exit;
	}

	hr = m_CaptureAudioClient->SetEventHandle(m_SampleReadyEvent);
	if (FAILED(hr))
	{
		goto exit;
	}

	/*hr = InitScopeData();
	if (FAILED(hr))
	{
	goto exit;
	}*/
	/*hr = CreateWAVFile();*/

	/*if (FAILED(hr))
	{
		goto exit;
	}*/
exit:
	if (FAILED(hr))
	{
		m_CaptureDeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
		SAFE_RELEASE(m_CaptureAudioClient);
		SAFE_RELEASE(m_AudioCaptureClient);
		SAFE_RELEASE(m_SampleReadyAsyncResult);
	}
	return S_OK;
}

HRESULT SDKTemplate::WASAPIAudio::WASAPIRenderCapture::OnStartCapture(IMFAsyncResult * pResult)
{
	HRESULT hr = S_OK;

	if (m_CaptureDeviceStateChanged->GetState() == DeviceState::DeviceStateInitialized)
	{
		m_CaptureDeviceStateChanged->SetState(DeviceState::DeviceStateCapturing, hr, true);
		return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartCapture, nullptr);

	}
	return E_NOT_VALID_STATE;
}

HRESULT SDKTemplate::WASAPIAudio::WASAPIRenderCapture::OnStopCapture(IMFAsyncResult * pResult)
{
	if ((m_CaptureDeviceStateChanged->GetState() != DeviceState::DeviceStateCapturing) &&
		(m_CaptureDeviceStateChanged->GetState() != DeviceState::DeviceStateInError))
	{
		return E_NOT_VALID_STATE;
	}

	m_CaptureDeviceStateChanged->SetState(DeviceState::DeviceStateStopping, S_OK, true);
	return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStopCapture, nullptr);
}

HRESULT SDKTemplate::WASAPIAudio::WASAPIRenderCapture::OnFinishCapture(IMFAsyncResult * pResult)
{
	// We should be flushing when this is called
	if (m_CaptureDeviceStateChanged->GetState() == DeviceState::DeviceStateFlushing)
	{
		return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xFinishCapture, nullptr);
	}

	// We are in the wrong state
	return E_NOT_VALID_STATE;
}
