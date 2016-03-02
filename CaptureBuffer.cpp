#include "pch.h"
#include "CaptureBuffer.h"

SDKTemplate::WASAPIAudio::CaptureBufferGenerator::CaptureBufferGenerator()
{
	m_SampleQueue = nullptr;
	m_SampleQueueTail = &m_SampleQueue;
}

SDKTemplate::WASAPIAudio::CaptureBufferGenerator::~CaptureBufferGenerator()
{
	Flush();
}


HRESULT SDKTemplate::WASAPIAudio::CaptureBufferGenerator::GeneratrSampleBuffer(UINT32 FramesPerPeriod, WAVEFORMATEX * wfx, BYTE * Data)
{
	HRESULT hr = S_OK;
	UINT32 renderBufferSizeInBytes = FramesPerPeriod * wfx->nBlockAlign;
	UINT64 renderDataLength = (wfx->nSamplesPerSec * wfx->nBlockAlign) + (renderBufferSizeInBytes - 1);
	UINT64 renderBufferCount = renderDataLength / renderBufferSizeInBytes;

	double theta = 0;
	for (UINT64 i = 0; i < renderBufferCount; i++)
	{
		RenderBuffer *SampleBuffer = new (std::nothrow) RenderBuffer();
		if (nullptr == SampleBuffer)
		{
			return E_OUTOFMEMORY;
		}

		SampleBuffer->BufferSize = renderBufferSizeInBytes;
		SampleBuffer->BytesFilled = renderBufferSizeInBytes;
		SampleBuffer->Buffer = new (std::nothrow) BYTE[renderBufferSizeInBytes];
		if (nullptr == SampleBuffer->Buffer)
		{
			return E_OUTOFMEMORY;
		}
		CopyMemory(Data, SampleBuffer->Buffer, SampleBuffer->BufferSize);
		/*switch (CalculateMixFormatType(wfx))
		{
		case RenderSampleType::SampleType16BitPCM:
			GenerateSineSamples<short>(SampleBuffer->Buffer, SampleBuffer->BufferSize, Frequency, wfx->nChannels, wfx->nSamplesPerSec, TONE_AMPLITUDE, &theta);
			break;

		case RenderSampleType::SampleTypeFloat:
			GenerateSineSamples<float>(SampleBuffer->Buffer, SampleBuffer->BufferSize, Frequency, wfx->nChannels, wfx->nSamplesPerSec, TONE_AMPLITUDE, &theta);
			break;

		default:
			return E_UNEXPECTED;
			break;
		}*/
		
		*m_SampleQueueTail = SampleBuffer;
		m_SampleQueueTail = &SampleBuffer->Next;
	}

}

HRESULT SDKTemplate::WASAPIAudio::CaptureBufferGenerator::FillSampleBuffer(UINT32 BytesToRead, BYTE * Data)
{
	if (nullptr == Data)
	{
		return E_POINTER;
	}
	RenderBuffer * SampleBuffer = m_SampleQueue;
	if (BytesToRead > SampleBuffer->BufferSize)
	{
		return E_INVALIDARG;
	}
	CopyMemory(Data, SampleBuffer->Buffer, BytesToRead);
	m_SampleQueue = m_SampleQueue->Next;
	return S_OK;
}

void SDKTemplate::WASAPIAudio::CaptureBufferGenerator::Flush()
{
	while (m_SampleQueue!=nullptr)
	{
		RenderBuffer * SampleBuffer = m_SampleQueue;
		m_SampleQueue = SampleBuffer->Next;
		SAFE_DELETE(SampleBuffer);
	}
}
