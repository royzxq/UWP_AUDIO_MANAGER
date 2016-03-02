#pragma once
#include <limits.h>
#include "MainPage.xaml.h"

namespace SDKTemplate 
{
	namespace WASAPIAudio
	{
		class CaptureBufferGenerator
		{
		public:
			CaptureBufferGenerator();
			~CaptureBufferGenerator();

			Platform::Boolean IsEOF() { return (m_SampleQueue == nullptr); };
			UINT32 GetBufferLength() {
				return (m_SampleQueue != nullptr ? m_SampleQueue->BufferSize : 0);
			};

			HRESULT GeneratrSampleBuffer(UINT32 FramesPerPeriod, WAVEFORMATEX *wfx, BYTE * Data);
			HRESULT FillSampleBuffer(UINT32 BytesToRead, BYTE *Data);
			void Flush();

		private:
			RenderBuffer * m_SampleQueue;
			RenderBuffer ** m_SampleQueueTail;
		};
	}
}
