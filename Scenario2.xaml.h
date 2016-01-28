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

#pragma once
#include "pch.h"
#include "Scenario2.g.h"
#include "MainPage.xaml.h"
using namespace Windows::Devices::Enumeration;

namespace SDKTemplate
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
	  namespace WASAPIAudio {
			static Platform::String ^ PKEY_AudioEndpoint_Supports_EventDriven_Mode = "{1da5d803-d492-4edd-8c23-e0c0ffee7f0e} 7";

			[Windows::Foundation::Metadata::WebHostHidden]
			public ref class Scenario2 sealed
			{
			public:
				  Scenario2();

			protected:
				  // Template Support
				  virtual void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
			private:
				  void ShowStatusMessage(Platform::String ^ str, NotifyType messageType);
				  void Enumerate_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEvent ^e);
				  void EnumerateAudioDevicesAsync();

			private:
				  MainPage^ rootPage;
				  Windows::UI::Xaml::Controls::ListBox ^ m_DevicesList;
			};
	  }
}
