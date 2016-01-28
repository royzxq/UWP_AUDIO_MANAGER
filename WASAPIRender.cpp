#include "pch.h"
#include "WASAPIRender.h"


using namespace Windows::System::Threading;
using namespace SDKTemplate::WASAPIAudio;

WASAPIRender::WASAPIRender() :
	  m_BufferFrames(0),
	  m_DeviceStateChanged(nullptr),
	  m_AudioClient(nullptr),
	  m_AudioRenderClient(nullptr),
	  m_SampleReadyAsyncResult(nullptr),
	  m_ToneSource(nullptr)
#ifdef MF
	  ,m_MFSource (nullptr)
#endif
{

	  m_SampleReadyEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
	  if (nullptr == m_SampleReadyEvent)
	  {		
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	  }
	  if (!InitializeCriticalSectionEx( &m_CritSec, 0, 0))
	  {
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	  }
	  m_DeviceStateChanged = ref new DeviceChangedEvent();
	  if (nullptr == m_DeviceStateChanged)
	  {
			ThrowIfFailed(E_OUTOFMEMORY);
	  }
}

WASAPIRender::~WASAPIRender() {
	  SAFE_RELEASE(m_AudioClient);
	  SAFE_RELEASE(m_AudioRenderClient);
	  SAFE_RELEASE(m_SampleReadyAsyncResult);
	  
	  if (INVALID_HANDLE_VALUE != m_SampleReadyEvent)
	  {
			CloseHandle(m_SampleReadyEvent);
			m_SampleReadyEvent = INVALID_HANDLE_VALUE;
	  }
	  DeleteCriticalSection(&m_CritSec);
	  m_DeviceStateChanged = nullptr;
}

HRESULT WASAPIRender::InitAudioDeviceAsync() {
	  IActivateAudioInterfaceAsyncOperation * asyncOp;
	  HRESULT hr = S_OK;

	  m_DeviceIdString = MediaDevice::GetDefaultAudioRenderId(Windows::Media::Devices::AudioDeviceRole::Default);

	  hr = ActivateAudioInterfaceAsync(m_DeviceIdString->Data(), __uuidof(IAudioClient3), nullptr, this, &asyncOp);

	  if (FAILED(hr))
	  {
			m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);

	  }
	  SAFE_RELEASE(asyncOp);
	  return hr;
}

HRESULT WASAPIRender::ActivateCompleted(IActivateAudioInterfaceAsyncOperation * operation) {
	  HRESULT hr = S_OK;
	  HRESULT hrActivateResult = S_OK;
	  IUnknown * punkAudioInterface = nullptr;

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

HRESULT WASAPIRender::SetProperties(DEVICEPROPS props)
{
	  m_DeviceProps = props;
	  return S_OK;
}



HRESULT WASAPIRender::SetVolumeOnSession(UINT32 volume)
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



HRESULT WASAPIRender::ConfigureDeviceInternal()
{
	  if (m_DeviceStateChanged -> GetState() != DeviceState::DeviceStateActivated)
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

HRESULT WASAPIRender::ValidateBufferValue()
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
			else if(m_DeviceProps.hnsBufferDuration > hnsMaxBufferDuration)
			{
				  m_DeviceProps.hnsBufferDuration = hnsMaxBufferDuration;
			}
	  }
	  return hr;
}



HRESULT WASAPIRender::ConfigureSource()
{
	  HRESULT hr = S_OK;
	  UINT32 FramesPerPeriod = GetBufferFramesPerPeriod();
	  if (m_DeviceProps.IsTonePlayback)
	  {
			m_ToneSource = new ToneSampleGenerator();
			if (m_ToneSource)
			{
				  hr = m_ToneSource->GenerateSampleBuffer(m_DeviceProps.Frequency, FramesPerPeriod, m_MixFormat);
			}
			else
			{
				  hr = E_OUTOFMEMORY;
			}
	  }
#ifdef MF
	  else
	  {

			m_MFSource = new MFSampleGenerator();
			if (m_MFSource)
			{
				  hr = m_MFSource->Initialize(m_DeviceProps.ContentStream, FramesPerPeriod, m_MixFormat);
			}
			else
			{
				  hr = E_OUTOFMEMORY;
			}


			
	  }
#endif // MF
	  return hr;
}

UINT32 WASAPIRender::GetBufferFramesPerPeriod()
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



HRESULT WASAPIRender::StartPlaybackAsync()
{
	  HRESULT hr = S_OK;

	  if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStateStopped
			|| m_DeviceStateChanged->GetState() == DeviceState::DeviceStateInitialized)
	  {
			hr = ConfigureSource();
			if (FAILED(hr))
			{
				  m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
				  return hr;
			}
			m_DeviceStateChanged->SetState(DeviceState::DeviceStateStarting, hr, true);
			return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartPlayback, nullptr);

	  }
	  else if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStatePaused)
	  {
			return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartPlayback, nullptr);
	  }
	  return E_FAIL;
}

HRESULT WASAPIRender::OnStartPlayback(IMFAsyncResult * pResult)
{
	  HRESULT hr = S_OK;

	  hr = OnAudioSampleRequested(true);
	  if (FAILED(hr))
	  {
			goto exit;
	  }
#ifdef MF
	  if (!m_DeviceProps.IsTonePlayback)
	  {
			hr = m_MFSource->StartSource();
			if (FAILED(hr))
			{
				  goto exit;
			}
	  }

#endif // MF

	
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
HRESULT WASAPIRender::StopPlaybackAsync()
{
	  if ( (m_DeviceStateChanged->GetState() != DeviceState::DeviceStatePlaying) &&
			(m_DeviceStateChanged->GetState() != DeviceState::DeviceStatePaused) &&
			(m_DeviceStateChanged->GetState() != DeviceState::DeviceStateInError) )
	  {
			return E_NOT_VALID_STATE;
	  }
	  m_DeviceStateChanged->SetState(DeviceState::DeviceStateStopping, S_OK, true);
	  return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStopPlayback, nullptr);
}

HRESULT WASAPIRender::OnStopPlayback(IMFAsyncResult * pResult)
{
	  if (0 != m_SampleReadyKey)
	  {
			MFCancelWorkItem(m_SampleReadyKey);
			m_SampleReadyKey = 0;
	  }

	  OnAudioSampleRequested(true);
	  m_AudioClient->Stop();
	  SAFE_RELEASE(m_SampleReadyAsyncResult);

	  if (m_DeviceProps.IsTonePlayback)
	  {
			m_ToneSource->Flush();
	  }
#ifdef MF
	  else
	  {
			m_MFSource->StartSource();
			m_MFSource->Shutdown();
	  }
#endif // MF

	 
	  m_DeviceStateChanged->SetState(DeviceState::DeviceStateStopped, S_OK, true);
	  return S_OK;
}

HRESULT WASAPIRender::PausePlaybackAsync()
{
	 if (( m_DeviceStateChanged->GetState() != DeviceState::DeviceStatePlaying) &&
		   (m_DeviceStateChanged->GetState() != DeviceState::DeviceStateInError))
	 {
		   return E_NOT_VALID_STATE;
	 }

	 m_DeviceStateChanged->SetState(DeviceState::DeviceStatePausing, S_OK, true);
	 return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xPausePlayback, nullptr);
}




HRESULT WASAPIRender::OnPausePlayback(IMFAsyncResult * pResult)
{
	  m_AudioClient->Stop();
	  m_DeviceStateChanged->SetState(DeviceState::DeviceStatePaused, S_OK, true);
	  return S_OK;
}


HRESULT WASAPIRender::OnSampleReady(IMFAsyncResult * pResult)
{
	  HRESULT hr = S_OK;

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

HRESULT WASAPIRender::OnAudioSampleRequested(Platform::Boolean IsSilence /*= false*/)
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
				  // Fill the buffer with a playback sample
				  if (m_DeviceProps.IsTonePlayback)
				  {
						hr = GetToneSample(FramesAvailable);
				  }
#ifdef MF

				  else
				  {
						hr = GetMFSample(FramesAvailable);
				  }
#endif // MF

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

HRESULT WASAPIRender::GetToneSample(UINT32 FramesAvailable)
{
	  HRESULT hr = S_OK;
	  BYTE * Data;

	  if (m_ToneSource -> IsEOF())
	  {
			hr = m_AudioRenderClient->GetBuffer(FramesAvailable, &Data);
			if (SUCCEEDED(hr))
			{
				  hr = m_AudioRenderClient->ReleaseBuffer(FramesAvailable, AUDCLNT_BUFFERFLAGS_SILENT);
			}
			StopPlaybackAsync();
	  }
	  else if (m_ToneSource->GetBufferLength() <= (FramesAvailable * m_MixFormat->nBlockAlign))
	  {
			UINT32 ActualFramesToRead = m_ToneSource->GetBufferLength() / m_MixFormat->nBlockAlign;
			UINT32 ActualBytesToRead = ActualFramesToRead * m_MixFormat->nBlockAlign;

			hr = m_AudioRenderClient->GetBuffer(ActualFramesToRead, &Data);
			if (SUCCEEDED(hr))
			{
				  hr = m_ToneSource->FillSampleBuffer(ActualBytesToRead, Data);
				  if (SUCCEEDED(hr))
				  {
						m_AudioRenderClient->ReleaseBuffer(ActualFramesToRead, 0);
				  }
			}
	  }
	  return hr;
}
#ifdef MF
HRESULT WASAPIRender::GetMFSample(UINT32 FramesAvailable)
{
	  HRESULT hr = S_OK;
	  BYTE * Data = NULL;

	  if (m_MFSource->IsEOF())
	  {
			hr = m_AudioRenderClient->GetBuffer(FramesAvailable, &Data);
			if (SUCCEEDED(hr))
			{
				  m_AudioRenderClient->ReleaseBuffer(FramesAvailable, AUDCLNT_BUFFERFLAGS_SILENT);
			}
			StopPlaybackAsync();
	  }
	  else
	  {
			UINT32 ActualBytesRead = 0;
			UINT32 ActualBytesToRead = FramesAvailable * m_MixFormat->nBlockAlign;

			hr = m_AudioRenderClient->GetBuffer(FramesAvailable, &Data);
			if (SUCCEEDED(hr))
			{
				  hr = m_MFSource->FillSampleBuffer(ActualBytesToRead, Data, &ActualBytesRead);
				  if (hr == S_FALSE)
				  {
						hr = m_AudioRenderClient->ReleaseBuffer(FramesAvailable, AUDCLNT_BUFFERFLAGS_SILENT);
				  }
				  else if (SUCCEEDED(hr))
				  {
						if (0 == ActualBytesRead)
						{
							  hr = m_AudioRenderClient->ReleaseBuffer(FramesAvailable, AUDCLNT_BUFFERFLAGS_SILENT);
						}
						else {
							  hr = m_AudioRenderClient->ReleaseBuffer(ActualBytesToRead / m_MixFormat->nBlockAlign, 0);
						}
				  }
			}
	  }
	  return hr;
}
#endif