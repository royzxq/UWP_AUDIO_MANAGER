#include "pch.h"
#include "WASAPICapture.h"

using namespace Windows::Storage;
using namespace SDKTemplate::WASAPIAudio;
using namespace Windows::System::Threading;

#define BITS_PER_BYTE 8

SDKTemplate::WASAPIAudio::WASAPICapture::WASAPICapture()
	:m_BufferFrames(0)
	, m_cbDataSize(0)
	, m_cb_FlushCounter(0)
	, m_cbHeaderSize(0)
	, m_dwQueueID(0)
	, m_DeviceStateChanged(nullptr)
	, m_AudioClient(nullptr)
	, m_AudioCaptureClient(nullptr)
	, m_SampleReadyAsyncResult(nullptr)
	, m_ContentStream(nullptr)
	, m_OutputStream(nullptr)
	, m_WAVDataWriter(nullptr)
	, m_fWriting(false)
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

SDKTemplate::WASAPIAudio::WASAPICapture::~WASAPICapture()
{
	SAFE_RELEASE(m_AudioClient);
	SAFE_RELEASE(m_AudioCaptureClient);
	SAFE_RELEASE(m_SampleReadyAsyncResult);

	if (INVALID_HANDLE_VALUE != m_SampleReadyEvent)
	{
		CloseHandle(m_SampleReadyEvent);
		m_SampleReadyEvent = INVALID_HANDLE_VALUE;
	}

	MFUnlockWorkQueue(m_dwQueueID);

	m_DeviceStateChanged = nullptr;
	m_ContentStream = nullptr;
	m_OutputStream = nullptr;
	m_WAVDataWriter = nullptr;

	DeleteCriticalSection(&m_CritSec);
}

HRESULT SDKTemplate::WASAPIAudio::WASAPICapture::InitAudioDeviceAsync()
{
	ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;
	HRESULT hr = S_OK;

	m_DeviceIdString = MediaDevice::GetDefaultAudioCaptureId(Windows::Media::Devices::AudioDeviceRole::Default);

	hr = ActivateAudioInterfaceAsync(m_DeviceIdString->Data(), __uuidof(IAudioClient3), nullptr, this, &asyncOp);
	if (FAILED(hr))
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
	}
	return hr;
}

HRESULT WASAPICapture::ActivateCompleted(IActivateAudioInterfaceAsyncOperation *operation)
{
	HRESULT hr = S_OK;
	HRESULT hrActivateResult = S_OK;
	ComPtr<IUnknown> punkAudioInterface;

	hr = operation->GetActivateResult(&hrActivateResult, &punkAudioInterface);
	if (FAILED(hr) || FAILED(hrActivateResult))
	{
		goto exit;
	}
	
	punkAudioInterface.CopyTo(&m_AudioClient);
	if (nullptr == m_AudioClient)
	{
		hr = E_NOINTERFACE;
		goto exit;
	}

	hr = m_AudioClient->GetMixFormat(&m_MixFormat);
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

	hr = m_AudioClient->GetSharedModeEnginePeriod(m_MixFormat, &m_DefaultPeriodInFrames, &m_FundamentalPeriodInFrames, &m_MinPeriodInFrames, &m_MinPeriodInFrames);

	if (FAILED(hr))
	{
		goto exit;
	}

	hr = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
		200000,
		0,
		m_MixFormat,
		nullptr);

	if (FAILED(hr))
	{
		goto exit;
	}

	hr = m_AudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_AudioCaptureClient);

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

	/*hr = InitScopeData();
	if (FAILED(hr))
	{
		goto exit;
	}*/
	hr = CreateWAVFile();

	if (FAILED(hr))
	{
		goto exit;
	}
exit:
	if (FAILED(hr))
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
		SAFE_RELEASE(m_AudioClient);
		SAFE_RELEASE(m_AudioCaptureClient);
		SAFE_RELEASE(m_SampleReadyAsyncResult);
	}
	return S_OK;
}


HRESULT SDKTemplate::WASAPIAudio::WASAPICapture::StartCaptureAsync()
{
	HRESULT hr = S_OK;

	if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStateInitialized)
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateCapturing, hr, true);
		return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartCapture, nullptr);

	}
	return E_NOT_VALID_STATE;
}

HRESULT SDKTemplate::WASAPIAudio::WASAPICapture::StopCaptureAsync()
{
	if ((m_DeviceStateChanged->GetState() != DeviceState::DeviceStateCapturing) &&
		(m_DeviceStateChanged->GetState() != DeviceState::DeviceStateInError))
	{
		return E_NOT_VALID_STATE;
	}

	m_DeviceStateChanged->SetState(DeviceState::DeviceStateStopping, S_OK, true);
	return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStopCapture, nullptr);
}

HRESULT SDKTemplate::WASAPIAudio::WASAPICapture::FinishCaptureAsync()
{
	// We should be flushing when this is called
	if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStateFlushing)
	{
		return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xFinishCapture, nullptr);
	}

	// We are in the wrong state
	return E_NOT_VALID_STATE;
}



HRESULT SDKTemplate::WASAPIAudio::WASAPICapture::OnStartCapture(IMFAsyncResult* pResult)
{
	HRESULT hr = S_OK;

	hr = m_AudioClient->Start();
	if (SUCCEEDED(hr))
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateCapturing, hr, true);
		MFPutWaitingWorkItem(m_SampleReadyEvent, 0, m_SampleReadyAsyncResult, &m_SampleReadyKey);
	}
	else
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, hr, true);
	}
	return S_OK;
}

HRESULT WASAPICapture::OnStopCapture(IMFAsyncResult* pResult) {
	if (0 != m_SampleReadyKey)
	{
		MFCancelWorkItem(m_SampleReadyKey);
		m_SampleReadyKey = 0;
	}
	m_AudioClient->Stop();
	SAFE_RELEASE(m_SampleReadyAsyncResult);

	if (!m_fWriting)
	{
		m_DeviceStateChanged->SetState(DeviceState::DeviceStateFlushing, S_OK, true);

		concurrency::task<unsigned int>(m_WAVDataWriter->StoreAsync()).then(
			[this](unsigned int BytesWritten)
		{
			FinishCaptureAsync();
		});
	}
	return S_OK;
}

HRESULT SDKTemplate::WASAPIAudio::WASAPICapture::OnSampleReady(IMFAsyncResult* pResult)
{
	HRESULT hr = S_OK;

	hr = OnAudioSampleRequested(false);
	 if (SUCCEEDED(hr))
	 {
		 if (m_DeviceStateChanged->GetState() == DeviceState::DeviceStateCapturing)
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

HRESULT SDKTemplate::WASAPIAudio::WASAPICapture::OnFinishCapture(IMFAsyncResult* pResult)
{
	return FixWAVHeader();
}



HRESULT SDKTemplate::WASAPIAudio::WASAPICapture::CreateWAVFile()
{
	// Create the WAV file, appending a number if file already exists
	concurrency::task<StorageFile^>(KnownFolders::MusicLibrary->CreateFileAsync(AUDIO_FILE_NAME, CreationCollisionOption::GenerateUniqueName)).then(
		[this](StorageFile^ file)
	{
		if (nullptr == file)
		{
			ThrowIfFailed(E_INVALIDARG);
		}

		return file->OpenAsync(FileAccessMode::ReadWrite);
	})

		// Then create a RandomAccessStream
		.then([this](IRandomAccessStream^ stream)
	{
		if (nullptr == stream)
		{
			ThrowIfFailed(E_INVALIDARG);
		}

		// Get the OutputStream for the file
		m_ContentStream = stream;
		m_OutputStream = m_ContentStream->GetOutputStreamAt(0);

		// Create the DataWriter
		m_WAVDataWriter = ref new DataWriter(m_OutputStream);
		if (nullptr == m_WAVDataWriter)
		{
			ThrowIfFailed(E_OUTOFMEMORY);
		}

		// Create the WAV header
		DWORD header[] = {
			FCC('RIFF'),        // RIFF header
			0,                  // Total size of WAV (will be filled in later)
			FCC('WAVE'),        // WAVE FourCC
			FCC('fmt '),        // Start of 'fmt ' chunk
			sizeof(WAVEFORMATEX) + m_MixFormat->cbSize      // Size of fmt chunk
		};

		DWORD data[] = { FCC('data'), 0 };  // Start of 'data' chunk

		auto headerBytes = ref new Platform::Array<BYTE>(reinterpret_cast<BYTE*>(header), sizeof(header));
		auto formatBytes = ref new Platform::Array<BYTE>(reinterpret_cast<BYTE*>(m_MixFormat), sizeof(WAVEFORMATEX) + m_MixFormat->cbSize);
		auto dataBytes = ref new Platform::Array<BYTE>(reinterpret_cast<BYTE*>(data), sizeof(data));

		if ((nullptr == headerBytes) || (nullptr == formatBytes) || (nullptr == dataBytes))
		{
			ThrowIfFailed(E_OUTOFMEMORY);
		}

		// Write the header
		m_WAVDataWriter->WriteBytes(headerBytes);
		m_WAVDataWriter->WriteBytes(formatBytes);
		m_WAVDataWriter->WriteBytes(dataBytes);

		return m_WAVDataWriter->StoreAsync();
	})

		// Wait for file data to be written to file
		.then([this](unsigned int BytesWritten)
	{
		m_cbHeaderSize = BytesWritten;
		return m_WAVDataWriter->FlushAsync();
	})

		// Our file is ready to go, so we can now signal that initialization is finished
		.then([this](bool f)
	{
		try
		{
			m_DeviceStateChanged->SetState(DeviceState::DeviceStateInitialized, S_OK, true);
		}
		catch (Platform::Exception ^e)
		{
			m_DeviceStateChanged->SetState(DeviceState::DeviceStateInError, e->HResult, true);
		}
	});

	return S_OK;
}

HRESULT SDKTemplate::WASAPIAudio::WASAPICapture::FixWAVHeader()
{
	auto DataSizeByte = ref new Platform::Array<BYTE>(reinterpret_cast<BYTE*>(&m_cbDataSize), sizeof(DWORD));

	// Write the size of the 'data' chunk first
	IOutputStream^ OutputStream = m_ContentStream->GetOutputStreamAt(m_cbHeaderSize - sizeof(DWORD));
	m_WAVDataWriter = ref new DataWriter(OutputStream);
	m_WAVDataWriter->WriteBytes(DataSizeByte);

	concurrency::task<unsigned int>(m_WAVDataWriter->StoreAsync()).then(
		[this](unsigned int BytesWritten)
	{
		DWORD cbTotalSize = m_cbDataSize + m_cbHeaderSize - 8;
		auto TotalSizeByte = ref new Platform::Array<BYTE>(reinterpret_cast<BYTE*>(&cbTotalSize), sizeof(DWORD));

		// Write the total file size, minus RIFF chunk and size
		IOutputStream^ OutputStream = m_ContentStream->GetOutputStreamAt(sizeof(DWORD));  // sizeof(DWORD) == sizeof(FOURCC)
		m_WAVDataWriter = ref new DataWriter(OutputStream);
		m_WAVDataWriter->WriteBytes(TotalSizeByte);

		concurrency::task<unsigned int>(m_WAVDataWriter->StoreAsync()).then(
			[this](unsigned int BytesWritten)
		{
			return m_WAVDataWriter->FlushAsync();
		})

			.then(
				[this](bool f)
		{
			m_DeviceStateChanged->SetState(DeviceState::DeviceStateStopped, S_OK, true);
		});
	});

	return S_OK;
}

HRESULT SDKTemplate::WASAPIAudio::WASAPICapture::OnAudioSampleRequested(Platform::Boolean IsSilence /*= false*/)
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
		m_WAVDataWriter->WriteBytes(dataType);
		m_cbDataSize += cbBytesToCapture;
		m_cb_FlushCounter += cbBytesToCapture;

		if ((m_cb_FlushCounter > (m_MixFormat->nAvgBytesPerSec * FLUSH_INTERVAL_SEC)) && !m_fWriting)
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
		}
	}
exit:
	LeaveCriticalSection(&m_CritSec);
	return hr;

}

