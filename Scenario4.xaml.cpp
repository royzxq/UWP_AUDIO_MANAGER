#include "pch.h"
#include "Scenario4.xaml.h"

using namespace SDKTemplate;
using namespace SDKTemplate::WASAPIAudio;


using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::Media;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Navigation;


SDKTemplate::WASAPIAudio::Scenario4::Scenario4() :m_IsMFLoaded(false), m_StateChangeEvent(nullptr), m_spCapture(nullptr), m_spRender(nullptr), rootPage(MainPage::Current), m_spRenderCapture(nullptr)
{
	InitializeComponent();
	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	if (SUCCEEDED(hr))
	{
		m_IsMFLoaded = true;
	}
	else
	{
		ThrowIfFailed(hr);
	}

	m_CoreDispatcher = CoreWindow::GetForCurrentThread()->Dispatcher;

}

void SDKTemplate::WASAPIAudio::Scenario4::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e)
{
	rootPage = MainPage::Current;
}

void SDKTemplate::WASAPIAudio::Scenario4::OnNavigatedFrom(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e)
{
	if (nullptr != m_StateChangeEvent)
	{
		DeviceState deviceState = m_StateChangeEvent->GetState();
		if (deviceState == DeviceState::DeviceStateCapturing)
		{
			StopCapture(this, e);
		}
	}
}

SDKTemplate::WASAPIAudio::Scenario4::~Scenario4()
{
	if (m_deviceStateChangeToken.Value != 0)
	{
		m_StateChangeEvent->StateChangedEvent -= m_deviceStateChangeToken;

		m_StateChangeEvent = nullptr;
		m_deviceStateChangeToken.Value = 0;
	}

	if (m_IsMFLoaded)
	{
		MFShutdown();
		m_IsMFLoaded = false;
	}
}

void SDKTemplate::WASAPIAudio::Scenario4::btnStartCapture_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEvent ^e)
{
	ShowStatusMessage("", NotifyType::StatusMessage);
	InitCapture(sender, e);
	StartDevice();
}

void SDKTemplate::WASAPIAudio::Scenario4::btnStopCapture_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEvent ^e)
{
	StopCapture(sender, e);
	StopDevice();
}

void SDKTemplate::WASAPIAudio::Scenario4::ShowStatusMessage(Platform::String^ str, NotifyType messageType)
{
	m_CoreDispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this, str, messageType]()
	{
		rootPage->NotifyUser(str, messageType);
	}));
}

void SDKTemplate::WASAPIAudio::Scenario4::UpdateMediaControlUI(DeviceState deviceState)
{
	switch (deviceState)
	{

	case SDKTemplate::WASAPIAudio::DeviceState::DeviceStateInError:
		btnStartCapture->IsEnabled = false;
		btnStopCapture->IsEnabled = false;
		break;

	case SDKTemplate::WASAPIAudio::DeviceState::DeviceStateCapturing:
		btnStartCapture->IsEnabled = false;
		btnStopCapture->IsEnabled = true;
		break;

	case SDKTemplate::WASAPIAudio::DeviceState::DeviceStateStopped:
		btnStartCapture->IsEnabled = true;
		btnStopCapture->IsEnabled = false;
		break;
	default:
		break;
	}
}

void SDKTemplate::WASAPIAudio::Scenario4::OnDeviceStateChange(Object ^ sender, DeviceStateChangedEventArgs ^ e)
{
	m_CoreDispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this, e]()
	{
		UpdateMediaControlUI(e->State);
	}));

	switch (e->State)
	{
	case DeviceState::DeviceStateInitialized:
		StartDevice();
		//m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Closed;
		break;
	case DeviceState::DeviceStatePlaying:
		ShowStatusMessage("Playback Started", NotifyType::StatusMessage);
		//m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Playing;
		break;

	case DeviceState::DeviceStatePaused:
		ShowStatusMessage("Playback Paused", NotifyType::StatusMessage);
		//m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Paused;
		break;

	case DeviceState::DeviceStateStopped:
		m_spRender = nullptr;

		if (m_deviceStateChangeToken.Value != 0)
		{
			m_StateChangeEvent->StateChangedEvent -= m_deviceStateChangeToken;
			m_StateChangeEvent = nullptr;
			m_deviceStateChangeToken.Value = 0;
		}

		ShowStatusMessage("Playback Stopped", NotifyType::StatusMessage);
		//m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Stopped;
		break;

	case DeviceState::DeviceStateInError:
		HRESULT hr = e->hr;

		if (m_deviceStateChangeToken.Value != 0)
		{
			m_StateChangeEvent->StateChangedEvent -= m_deviceStateChangeToken;
			m_StateChangeEvent = nullptr;
			m_deviceStateChangeToken.Value = 0;
		}

		m_spRender = nullptr;

		//m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Closed;

		wchar_t hrVal[11];
		swprintf_s(hrVal, 11, L"0x%08x\0", hr);
		String^ strHRVal = ref new String(hrVal);

		String^ strMessage = "";

		// Specifically handle a couple of known errors
		switch (hr)
		{
		case AUDCLNT_E_ENDPOINT_OFFLOAD_NOT_CAPABLE:
			strMessage = "ERROR: Endpoint Does Not Support HW Offload (" + strHRVal + ")";
			ShowStatusMessage(strMessage, NotifyType::ErrorMessage);
			break;

		case AUDCLNT_E_RESOURCES_INVALIDATED:
			strMessage = "ERROR: Endpoint Lost Access To Resources (" + strHRVal + ")";
			ShowStatusMessage(strMessage, NotifyType::ErrorMessage);
			break;
		default:
			strMessage = "ERROR" + strHRVal + " has occurred";
			ShowStatusMessage(strMessage, NotifyType::ErrorMessage);
			break;
		}
	}
}

void SDKTemplate::WASAPIAudio::Scenario4::OnCaptureDeviceStateChange(Object ^ sender, DeviceStateChangedEventArgs ^ e)
{
	String ^ strMessage = "";

	auto t = Windows::Globalization::DateTimeFormatting::DateTimeFormatter::LongTime;
	Windows::Globalization::Calendar^ calendar = ref new Windows::Globalization::Calendar();
	calendar->SetToNow();

	m_CoreDispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler(
		[this, e]()
	{
		UpdateMediaControlUI(e->State);
	}));

	switch (e->State)
	{
	case DeviceState::DeviceStateInitialized:
		m_spCapture->StartCaptureAsync();
		break;

	case DeviceState::DeviceStateCapturing:
		strMessage = "Capture Started (normal latency) @" + t->Format(calendar->GetDateTime());
		ShowStatusMessage(strMessage, NotifyType::StatusMessage);
		break;

	case DeviceState::DeviceStateDiscontinuity:
		m_DiscontinuityCount++;
		if (m_DiscontinuityCount > 1)
		{
			strMessage = "DISCONTINUITY DETECTED: " + t->Format(calendar->GetDateTime()) + " (Count = " + (m_DiscontinuityCount - 1) + ")";
			ShowStatusMessage(strMessage, NotifyType::StatusMessage);
		}
		break;

	case DeviceState::DeviceStateFlushing:
		ShowStatusMessage("Finalizing WAV Header.  This may take a few minutes...", NotifyType::StatusMessage);
		break;

	case DeviceState::DeviceStateStopped:
		m_spCapture = nullptr;
		if (m_deviceStateChangeToken.Value != 0)
		{
			m_StateChangeEvent->StateChangedEvent -= m_deviceStateChangeToken;
			m_StateChangeEvent = nullptr;
			m_deviceStateChangeToken.Value = 0;
		}
		ShowStatusMessage("Capture stopped", NotifyType::StatusMessage);
		break;

	case DeviceState::DeviceStateInError:
		HRESULT hr = e->hr;
		if (m_deviceStateChangeToken.Value != 0)
		{
			m_StateChangeEvent->StateChangedEvent -= m_deviceStateChangeToken;
			m_StateChangeEvent = nullptr;
			m_deviceStateChangeToken.Value = 0;
		}
		m_spCapture = nullptr;

		wchar_t hrVal[11];
		swprintf_s(hrVal, 11, L"0x%08x\0", hr);
		String^ strHRVal = ref new String(hrVal);
		strMessage = "ERROR: " + strHRVal + " has occurred.";
		ShowStatusMessage(strMessage, NotifyType::ErrorMessage);
		break;

	}
}

void SDKTemplate::WASAPIAudio::Scenario4::InitCapture(Object ^ sender, Object ^ e)
{
	HRESULT hr = S_OK;
	if (m_spRenderCapture)
	{
		m_spRenderCapture = nullptr;
	}
	m_spRenderCapture = Make<WASAPIRenderCapture>();
	if (nullptr == m_spRenderCapture)
	{
		OnCaptureDeviceStateChange(this, ref new DeviceStateChangedEventArgs(DeviceState::DeviceStateInError, E_OUTOFMEMORY));
		return;
	}

	m_StateChangeEvent = m_spRenderCapture->GetDeviceStateEvent();
	if (nullptr == m_StateChangeEvent)
	{
		OnCaptureDeviceStateChange(this, ref new DeviceStateChangedEventArgs(DeviceState::DeviceStateInError, E_FAIL));
		return;
	}

	m_deviceStateChangeToken = m_StateChangeEvent->StateChangedEvent += ref new DeviceChangedHandler(this, &Scenario4::OnCaptureDeviceStateChange);


	m_DiscontinuityCount = 0;
	m_spRenderCapture->InitAudioDeviceAsync();
}

void SDKTemplate::WASAPIAudio::Scenario4::StopCapture(Object ^ sender, Object^ e)
{
	if (m_spRenderCapture)
	{
		m_spRenderCapture->StopCaptureAsync();
	}
}

void SDKTemplate::WASAPIAudio::Scenario4::StartDevice()
{
	if (nullptr == m_spRenderCapture || !m_spRenderCapture->m_RenderInit)
	{
		InitDevice();
	}
	else
	{
		m_spRenderCapture->StartPlaybackAsync();
	}
}

void SDKTemplate::WASAPIAudio::Scenario4::InitDevice()
{
	HRESULT hr = S_OK;
	if (m_spRenderCapture == nullptr)
	{
		m_spRenderCapture = Make<WASAPIRenderCapture>();
		if (nullptr == m_spRenderCapture)
		{
			OnDeviceStateChange(this, ref new DeviceStateChangedEventArgs(DeviceState::DeviceStateInError, E_OUTOFMEMORY));
			return;
		}

		m_StateChangeEvent = m_spRenderCapture->GetDeviceStateEvent();
		if (nullptr == m_StateChangeEvent)
		{
			OnDeviceStateChange(this, ref new DeviceStateChangedEventArgs(DeviceState::DeviceStateInError, E_FAIL));
			return;
		}
		m_deviceStateChangeToken = m_StateChangeEvent->StateChangedEvent += ref new DeviceChangedHandler(this, &Scenario4::OnDeviceStateChange);
	}
	else
	{


		DEVICEPROPS props;
		int bufferSize = 0;
		//swscanf_s(txtHWBuffer->Text->Data(), L"%d", &bufferSize);
		/*switch (m_ContentType)
		{
		case ContentType::ContentTypeTone:
			props.IsTonePlayback = true;
			props.Frequency = static_cast<DWORD>(sliderFrequency->Value);
			break;

		case ContentType::ContentTypeFile:
			props.IsTonePlayback = false;
			props.ContentStream = m_ContentStream;
			break;
		}*/

		props.IsLowLatency = false;
		//props.IsRawChosen = static_cast<Platform::Boolean>(toggleRawAudio->IsOn);
		props.IsRawChosen = false;
		props.IsHWOffload = false;
		//props.IsRawSupported = m_deviceSupportsRawMode;
		props.hnsBufferDuration = static_cast<REFERENCE_TIME>(bufferSize);
		m_spRenderCapture->m_RenderInit = true;
		m_spRenderCapture->SetProperties(props);

		// Selects the Default Audio Device
		m_spRenderCapture->InitAudioDeviceAsync();
	}

	
}

void SDKTemplate::WASAPIAudio::Scenario4::StopDevice()
{
	if (m_spRenderCapture)
	{
		m_spRenderCapture->StopPlaybackAsync();
	}
}
