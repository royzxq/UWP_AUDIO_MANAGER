//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
#include "pch.h"
#include "Scenario1.xaml.h"
#include <ppl.h>
using namespace SDKTemplate;
using namespace concurrency;
using namespace SDKTemplate::WASAPIAudio;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Media;
using namespace Windows::Storage;
using namespace Windows::UI::Core;

using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;



void SDKTemplate::WASAPIAudio::Scenario1::Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    rootPage->NotifyUser("Hello", NotifyType::StatusMessage);
}

void SDKTemplate::WASAPIAudio::Scenario1::Button_Click_2(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    rootPage->NotifyUser("Hello", NotifyType::ErrorMessage);
}

void SDKTemplate::WASAPIAudio::Scenario1::Button_Click_3(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    rootPage->NotifyUser("Hello How are you today. This is a very long message with a lot of text.\nLet's throw in some newline characters\nand\nsee\nwhat\nhappens.\nadsf\nasdf\nasdf\nasfd\nasdf\nasdf\nasfd", NotifyType::StatusMessage);
}



SDKTemplate::WASAPIAudio::Scenario1::Scenario1() :
	  m_IsMFLoaded(false),
	  m_ContentStream(nullptr),
	  m_StateChangeEvent(nullptr),
	  m_spRender(nullptr),
	  m_ContentType(ContentType::ContentTypeTone),
	  rootPage(MainPage::Current)
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

	  String ^ deviceId = Devices::MediaDevice::GetDefaultAudioRenderId(Windows::Media::Devices::AudioDeviceRole::Default);
	  auto PropertiesToRetrieve = ref new Platform::Collections::Vector<String ^>();
	  PropertiesToRetrieve->Append("System.Devices.AudioDevice.RawProcessingSupported");
	  

	  create_task(Windows::Devices::Enumeration::DeviceInformation::CreateFromIdAsync(deviceId, PropertiesToRetrieve)).then([this](Windows::Devices::Enumeration::DeviceInformation^ deviceInformation)
	  {
			auto obj = deviceInformation->Properties->Lookup("System.Devices.AudioDevice.RawProcessingSupported");
			this->m_deviceSupportsRawMode = false;
			if (obj)
			{
				  this->m_deviceSupportsRawMode = obj->Equals(true);
			}
			if (this->m_deviceSupportsRawMode)
			{
				  this->toggleRawAudio->IsEnabled = true;
				  ShowStatusMessage("Raw Support", NotifyType::StatusMessage);
			}
			else
			{
				  this->toggleRawAudio->IsEnabled = false;
				  ShowStatusMessage("Not Raw Support", NotifyType::StatusMessage);
			}
	  });
	  // Register for Media Transport controls.  This is required to support background
	  // audio scenarios.
	  m_SystemMediaControls = SystemMediaTransportControls::GetForCurrentView();
	  m_SystemMediaControlsButtonToken = m_SystemMediaControls->ButtonPressed += ref new TypedEventHandler<SystemMediaTransportControls^, SystemMediaTransportControlsButtonPressedEventArgs^>(this, &Scenario1::MediaButtonPressed);
	  m_SystemMediaControls->IsPlayEnabled = true;
	  m_SystemMediaControls->IsPauseEnabled = true;
	  m_SystemMediaControls->IsStopEnabled = true;
}



void SDKTemplate::WASAPIAudio::Scenario1::OnNavigateTo(Windows::UI::Xaml::Navigation::NavigatingCancelEventArgs ^e)
{
	  rootPage = MainPage::Current;
	  UpdateContentUI(false);
}

void SDKTemplate::WASAPIAudio::Scenario1::OnNavigateFrom(Windows::UI::Xaml::Navigation::NavigatingCancelEventArgs ^e)
{
	  if (nullptr != m_StateChangeEvent)
	  {
			DeviceState deviceState = m_StateChangeEvent->GetState();
			if (deviceState == DeviceState::DeviceStatePlaying)
			{
				  StopDevice();
			}
	  }
}

SDKTemplate::WASAPIAudio::Scenario1::~Scenario1()
{
	  if (m_deviceStateChangeToken.Value != 0)
	  {
			m_StateChangeEvent->StateChangedEvent -= m_SystemMediaControlsButtonToken;
			m_StateChangeEvent = nullptr;
			m_deviceStateChangeToken.Value = 0;
	  }
	  if (m_SystemMediaControls)
	  {
			m_SystemMediaControls->ButtonPressed -= m_SystemMediaControlsButtonToken;
			m_SystemMediaControls->IsPlayEnabled = false;
			m_SystemMediaControls->IsPauseEnabled = false;
			m_SystemMediaControls->IsStopEnabled = false;
			m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Closed;
	  }

	  // Uninitialize MF
	  if (m_IsMFLoaded)
	  {
			MFShutdown();
			m_IsMFLoaded = false;
	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::btnPlay_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^e)
{
	  StartDevice();
}

void SDKTemplate::WASAPIAudio::Scenario1::btnPause_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs ^e)
{
	  PauseDevice();
}

void SDKTemplate::WASAPIAudio::Scenario1::btnStop_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs ^e)
{
	  StopDevice();
}

void SDKTemplate::WASAPIAudio::Scenario1::btnPlayPause_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs ^e)
{
	  PlayPauseToggleDevice();
}

void SDKTemplate::WASAPIAudio::Scenario1::btnFilePicker_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs ^e)
{
	  OnPickFileAsync();
}

void SDKTemplate::WASAPIAudio::Scenario1::radioTone_Checked(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs ^e)
{
	  m_ContentType = ContentType::ContentTypeTone;
	  UpdateContentProps("");
	  m_ContentStream = nullptr;
	  UpdateContentUI(false);
}

void SDKTemplate::WASAPIAudio::Scenario1::radioFile_Checked(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs ^e)
{
	  m_ContentType = ContentType::ContentTypeFile;
	  UpdateContentProps("");
	  m_ContentStream = nullptr;
	  UpdateContentUI(false);
}

void SDKTemplate::WASAPIAudio::Scenario1::sliderFrequency_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs ^e)
{
	  Slider ^ s = safe_cast<Slider^>(sender);
	  if (s != nullptr)
	  {
			UpdateContentProps(s->Value.ToString() + "HZ");
	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::sliderVolume_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs ^e)
{
	  Slider ^ s = safe_cast<Slider^>(sender);
	  if (s != nullptr)
	  {
			OnsetVolume(static_cast<UINT32>(s->Value));
	  }
}


void SDKTemplate::WASAPIAudio::Scenario1::ShowStatusMessage(Platform::String ^ str, NotifyType messageType)
{
	  m_CoreDispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this, str, messageType]()
	  {
			rootPage->NotifyUser(str, messageType);
	  }));
}

void SDKTemplate::WASAPIAudio::Scenario1::UpdateContentProps(Platform::String^ str)
{
	  String^ text = str;

	  if (nullptr != txtContentProps)
	  {
			// The event handler may be invoked on a background thread, so use the Dispatcher to invoke the UI-related code on the UI thread.
			txtContentProps->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler(
				  [this, text]()
			{
				  Windows::UI::Xaml::Media::SolidColorBrush ^brush;
				  txtContentProps->Text = text;

				  if (("" == text) && (m_ContentType == ContentType::ContentTypeFile))
				  {
						brush = ref new Windows::UI::Xaml::Media::SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0xCC, 0xFF, 0x00, 0x00));
				  }
				  else
				  {
						brush = ref new Windows::UI::Xaml::Media::SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0xFF, 0xFF, 0xFF, 0xFF));
				  }

				  txtContentProps->Background = brush;
			}));
	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::UpdateContentUI(Platform::Boolean disableAll)
{
	  if (nullptr != btnFilePicker && nullptr != sliderFrequency)
	  {
			if (disableAll)
			{
				  btnFilePicker->IsEnabled = false;
				  sliderFrequency->IsEnabled = false;
				  radioFile->IsEnabled = false;
				  radioTone->IsEnabled = false;
			}
			else
			{
				  radioFile->IsEnabled = true;
				  radioTone->IsEnabled = true;

				  switch (m_ContentType)
				  {
				  case ContentType::ContentTypeTone:
						btnFilePicker->IsEnabled = false;
						sliderFrequency->IsEnabled = true;
						UpdateContentProps(sliderFrequency->Value.ToString() + " Hz");
						break;

				  case ContentType::ContentTypeFile:
						btnFilePicker->IsEnabled = true;
						sliderFrequency->IsEnabled = false;
						break;

				  default:
						break;
				  }
			}
	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::UpdateMediaControlUI(DeviceState deviceState)
{
	  switch (deviceState)
	  {
	  case DeviceState::DeviceStatePlaying:
			btnPlay->IsEnabled = false;
			btnStop->IsEnabled = true;
			btnPlayPause->IsEnabled = true;
			btnPause->IsEnabled = true;
			break;

	  case DeviceState::DeviceStateStopped:
	  case DeviceState::DeviceStateInError:
			btnPlay->IsEnabled = true;
			btnStop->IsEnabled = false;
			btnPlayPause->IsEnabled = true;
			btnPause->IsEnabled = false;

			UpdateContentUI(false);
			break;

	  case DeviceState::DeviceStatePaused:
			btnPlay->IsEnabled = true;
			btnStop->IsEnabled = true;
			btnPlayPause->IsEnabled = true;
			btnPause->IsEnabled = false;
			break;

	  case DeviceState::DeviceStateStarting:
	  case DeviceState::DeviceStateStopping:
			btnPlay->IsEnabled = false;
			btnStop->IsEnabled = false;
			btnPlayPause->IsEnabled = false;
			btnPause->IsEnabled = false;

			UpdateContentUI(true);
			break;
	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::OnDeviceStateChange(Object ^ sender, DeviceStateChangedEventArgs ^ e)
{
	  m_CoreDispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this, e]()
	  {
			UpdateMediaControlUI(e->State);
	  }));

	  switch (e->State)
	  {
	  case DeviceState::DeviceStateInitialized:
			StartDevice();
			m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Closed;
			break;
	  case DeviceState::DeviceStatePlaying:
			ShowStatusMessage("Playback Started", NotifyType::StatusMessage);
			m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Playing;
			break;

	  case DeviceState::DeviceStatePaused:
			ShowStatusMessage("Playback Paused", NotifyType::StatusMessage);
			m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Paused;
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
			m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Stopped;
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

			m_SystemMediaControls->PlaybackStatus = MediaPlaybackStatus::Closed;

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

void SDKTemplate::WASAPIAudio::Scenario1::OnPickFileAsync()
{
	  Pickers::FileOpenPicker^ filePicker = ref new Pickers::FileOpenPicker();
	  filePicker->ViewMode = Pickers::PickerViewMode::List;
	  filePicker->SuggestedStartLocation = Pickers::PickerLocationId::MusicLibrary;
	  filePicker->FileTypeFilter->Append(".wav");
	  filePicker->FileTypeFilter->Append(".mp3");

	  concurrency::create_task(filePicker->PickSingleFileAsync()).then(
			[this](Windows::Storage::StorageFile ^ file) {
			if (nullptr != file)
			{
				  concurrency::create_task(file->OpenAsync(FileAccessMode::Read)).then(
						[this, file](IRandomAccessStream^ stream) {
						if (stream != nullptr)
						{
							  m_ContentStream = stream;
							  UpdateContentProps(file->Path);
						}
				  });
			}
	  });
}

void SDKTemplate::WASAPIAudio::Scenario1::OnsetVolume(UINT32 volume)
{
	  if (m_spRender)
	  {
			m_spRender->SetVolumeOnSession(volume);
	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::InitDevice()
{
	  HRESULT hr = S_OK;
	  if (m_spRender == nullptr)
	  {
			m_spRender = Make<WASAPIRender>();
			if (nullptr == m_spRender)
			{
				  OnDeviceStateChange(this, ref new DeviceStateChangedEventArgs(DeviceState::DeviceStateInError, E_OUTOFMEMORY));
				  return;
			}

			m_StateChangeEvent = m_spRender->GetDeviceStateEvent();
			if (nullptr == m_StateChangeEvent)
			{
				  OnDeviceStateChange(this, ref new DeviceStateChangedEventArgs(DeviceState::DeviceStateInError, E_FAIL));
				  return;
			}
			m_deviceStateChangeToken = m_StateChangeEvent->StateChangedEvent += ref new DeviceChangedHandler(this, &Scenario1::OnDeviceStateChange);

			DEVICEPROPS props;
			int bufferSize = 0;
			//swscanf_s(txtHWBuffer->Text->Data(), L"%d", &bufferSize);
			switch (m_ContentType)
			{
			case ContentType::ContentTypeTone:
				  props.IsTonePlayback = true;
				  props.Frequency = static_cast<DWORD>(sliderFrequency->Value);
				  break;

			case ContentType::ContentTypeFile:
				  props.IsTonePlayback = false;
				  props.ContentStream = m_ContentStream;
				  break;
			}

			props.IsLowLatency = false;
			props.IsRawChosen = static_cast<Platform::Boolean>(toggleRawAudio->IsOn);
			props.IsHWOffload = false;
			props.IsRawSupported = m_deviceSupportsRawMode;
			props.hnsBufferDuration = static_cast<REFERENCE_TIME>(bufferSize);

			m_spRender->SetProperties(props);

			// Selects the Default Audio Device
			m_spRender->InitAudioDeviceAsync();

	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::StartDevice()
{
	  if (nullptr == m_spRender)
	  {
			InitDevice();
	  }
	  else
	  {
			m_spRender->StartPlaybackAsync();
	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::StopDevice()
{
	  if (m_spRender)
	  {
			m_spRender->StopPlaybackAsync();
	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::PauseDevice()
{
	  if (m_spRender)
	  {
			DeviceState deviceState = m_StateChangeEvent->GetState();
			if (deviceState == DeviceState::DeviceStatePlaying)
			{
				  m_spRender->PausePlaybackAsync();
			}
	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::PlayPauseToggleDevice()
{
	  if (m_spRender)
	  {
			DeviceState deviceState = m_StateChangeEvent->GetState();
			if (deviceState == DeviceState::DeviceStatePlaying)
			{
				  m_spRender->PausePlaybackAsync();
			}
			else if (deviceState == DeviceState::DeviceStatePaused)
			{
				  m_spRender->StartPlaybackAsync();
			}
			else {
				  StartDevice();
			}
	  }
}

void SDKTemplate::WASAPIAudio::Scenario1::MediaButtonPressed(Windows::Media::SystemMediaTransportControls^ sender, Windows::Media::SystemMediaTransportControlsButtonPressedEventArgs^ e)
{
	  m_CoreDispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler(
			[this, e]() {
			switch (e->Button)
			{
			case SystemMediaTransportControlsButton::Play:
				  StartDevice();
				  break;

			case SystemMediaTransportControlsButton::Pause:
				  PauseDevice();
				  break;

			case SystemMediaTransportControlsButton::Stop:
				  StopDevice();
				  break;

			default:
				  break;
			}
	  }));
}


void SDKTemplate::WASAPIAudio::Scenario1::TextBlock_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{

}
