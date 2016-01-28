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
#include "Scenario2.xaml.h"

using namespace SDKTemplate;
using namespace SDKTemplate::WASAPIAudio;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Media::Devices;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Navigation;



SDKTemplate::WASAPIAudio::Scenario2::Scenario2()
{
	  InitializeComponent();
	  m_DevicesList = safe_cast<ListBox^>(static_cast<IFrameworkElement^>(this)->FindName("DevicesList"));

}

void SDKTemplate::WASAPIAudio::Scenario2::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e)
{
	  rootPage = MainPage::Current;
}

void SDKTemplate::WASAPIAudio::Scenario2::ShowStatusMessage(Platform::String ^ str, NotifyType messageType)
{
	  rootPage->NotifyUser(str, messageType);
}

void SDKTemplate::WASAPIAudio::Scenario2::Enumerate_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEvent ^e)
{
	  Button ^ b = safe_cast<Button^>(sender);
	  if (nullptr != b)
	  {
			m_DevicesList->Items->Clear();
			EnumerateAudioDevicesAsync();
	  }
}

void SDKTemplate::WASAPIAudio::Scenario2::EnumerateAudioDevicesAsync()
{
	  String ^ AudioSelctor = MediaDevice::GetAudioRenderSelector();

	  auto PropertryList = ref new Platform::Collections::Vector<String^>();
	  PropertryList->Append(PKEY_AudioEndpoint_Supports_EventDriven_Mode);

	  concurrency::task<DeviceInformationCollection^> emumOperation(DeviceInformation::FindAllAsync(AudioSelctor, PropertryList));
	  emumOperation.then([this](DeviceInformationCollection ^ DeviceInfo) {
			if (DeviceInfo == nullptr || DeviceInfo->Size == 0)
			{
				  this->ShowStatusMessage("No devices found", NotifyType::ErrorMessage);
			}
			else
			{
				  try
				  {
						for (unsigned int i = 0; i < DeviceInfo->Size; ++i)
						{
							  DeviceInformation ^ device = DeviceInfo->GetAt(i);
							  String ^ DeviceInfoString = device->Name;
							  if (device->Properties->Size > 0)
							  {
								  Object ^ devicePropString = device->Properties->Lookup(PKEY_AudioEndpoint_Supports_EventDriven_Mode);

								  if (nullptr != devicePropString)
								  {
									  devicePropString = DeviceInfoString + "--> EventDriven(" + devicePropString + ")";
								  }
							  }
							  this->m_DevicesList->Items->Append(DeviceInfoString);
						}
						String ^ strMsg = "Enumerated " + DeviceInfo->Size + " Devices";
						this->ShowStatusMessage(strMsg, NotifyType::StatusMessage);
				  }
				  catch (Platform::Exception ^ e)
				  {
					  this->ShowStatusMessage(e->Message, NotifyType::ErrorMessage);
				  }
			}
	  });
}

