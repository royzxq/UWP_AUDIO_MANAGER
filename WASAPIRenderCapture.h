#include <Windows.h>
#include <wrl\implements.h>
#include <Audioclient.h>
#include <mfapi.h>
#include <mmdeviceapi.h>
#include "MainPage.xaml.h"
#include"DeviseState.h"
#include "CaptureBuffer.h"
#include "WASAPIRender.h"
#include"ToneSampleGenerator.h"
#ifdef MF
#include "MFSampleGenerator.h"
#endif // MF

#define FLUSH_INTERVAL_SEC 3


using namespace Microsoft::WRL;
using namespace Windows::Media::Devices;
using namespace Windows::Storage::Streams;

#pragma once

namespace SDKTemplate {
	namespace WASAPIAudio {

		/*struct DEVICEPROPS
		{
			Platform::Boolean       IsHWOffload;
			Platform::Boolean       IsTonePlayback;
			Platform::Boolean       IsBackground;
			Platform::Boolean       IsRawSupported;
			Platform::Boolean       IsRawChosen;
			Platform::Boolean       IsLowLatency;
			REFERENCE_TIME          hnsBufferDuration;
			DWORD                   Frequency;
			IRandomAccessStream^    ContentStream;
		};*/

		class WASAPIRenderCapture : public RuntimeClass< RuntimeClassFlags<ClassicCom>, FtmBase, IActivateAudioInterfaceCompletionHandler>
		{
		public:
			WASAPIRenderCapture();
			HRESULT SetProperties(DEVICEPROPS props);
			HRESULT InitAudioDeviceAsync();
			HRESULT StartPlaybackAsync();
			HRESULT StopPlaybackAsync();
			HRESULT PausePlaybackAsync();
			HRESULT StartCaptureAsync();
			HRESULT StopCaptureAsync();
			HRESULT FinishCaptureAsync();

			HRESULT SetVolumeOnSession(UINT32 volume);
			DeviceChangedEvent ^ GetDeviceStateEvent() {
				return m_DeviceStateChanged;
			}

			METHODASYNCCALLBACK(WASAPIRenderCapture, StartPlayback, OnStartPlayback);
			METHODASYNCCALLBACK(WASAPIRenderCapture, StopPlayback, OnStopPlayback);
			METHODASYNCCALLBACK(WASAPIRenderCapture, PausePlayback, OnPausePlayback);
			METHODASYNCCALLBACK(WASAPIRenderCapture, SampleReady, OnSampleReady);

			METHODASYNCCALLBACK(WASAPIRenderCapture, StartCapture, OnStartCapture);
			METHODASYNCCALLBACK(WASAPIRenderCapture, StopCapture, OnStopCapture);
			//METHODASYNCCALLBACK(WASAPIRenderCapture, SampleReady, OnSampleReady);
			METHODASYNCCALLBACK(WASAPIRenderCapture, FinishCapture, OnFinishCapture);

			STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation * operation);
		private:
			~WASAPIRenderCapture();

			HRESULT RenderActivateCompleted(IActivateAudioInterfaceAsyncOperation * operation);
			HRESULT CaptureActivateCompleted(IActivateAudioInterfaceAsyncOperation * operation);

			HRESULT OnStartPlayback(IMFAsyncResult * pResult);
			HRESULT OnStopPlayback(IMFAsyncResult * pResult);
			HRESULT OnPausePlayback(IMFAsyncResult * pResult);
			HRESULT OnSampleReady(IMFAsyncResult * pResult);
			HRESULT OnStartCapture(IMFAsyncResult* pResult);
			HRESULT OnStopCapture(IMFAsyncResult* pResult);
			HRESULT OnFinishCapture(IMFAsyncResult* pResult);

			HRESULT ConfigureDeviceInternal();
			HRESULT ValidateBufferValue();
			HRESULT OnAudioSampleRequested(Platform::Boolean IsSilence = false);
			HRESULT OnAudioCaptureSampleRequested(Platform::Boolean IsSilence = false);
			HRESULT ConfigureSource();
			UINT32 GetBufferFramesPerPeriod();

			HRESULT GetCaptureSample(UINT32 FramesAvailable);
//
//			HRESULT GetToneSample(UINT32 FramesAvailable);
//#ifdef MF	
//			HRESULT GetMFSample(UINT32 FramesAvailable);
//#endif
		public:
			bool m_RenderInit;
		private:
			Platform::String ^ m_DeviceIdString;
			UINT32 m_BufferFrames;
			HANDLE m_SampleReadyEvent;
			MFWORKITEM_KEY m_SampleReadyKey;
			CRITICAL_SECTION m_CritSec;
			DWORD m_dwQueueID;

			DWORD m_cbHeaderSize;
			DWORD m_cbDataSize;
			DWORD m_cb_FlushCounter;
			BOOL m_fWriting;

			WAVEFORMATEX *m_MixFormat;
			UINT32	  m_DefaultPeriodInFrames;
			UINT32	  m_FundamentalPeriodInFrames;
			UINT32	  m_MaxPeriodInFrames;
			UINT32	  m_MinPeriodInFrames;

			IRandomAccessStream ^ m_ContentStream;
			IOutputStream ^ m_OutputStream;
			//IAudioClient3 *m_AudioClient;
			

			IAudioClient3 *m_AudioClient;
			IAudioClient3 *m_CaptureAudioClient;
			IAudioRenderClient *m_AudioRenderClient;
			IAudioCaptureClient *m_AudioCaptureClient;
			IMFAsyncResult *m_SampleReadyAsyncResult;

			DeviceChangedEvent ^ m_DeviceStateChanged;
			DeviceChangedEvent ^ m_CaptureDeviceStateChanged;
			DEVICEPROPS m_DeviceProps;

			BYTE * m_Data;
			CaptureBufferGenerator * m_CaptureSource;


		};


	}
}