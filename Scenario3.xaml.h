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
#include "Scenario3.g.h"
#include "MainPage.xaml.h"
#include "WASAPICapture.h"


namespace SDKTemplate
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    namespace WASAPIAudio
{
	[Windows::Foundation::Metadata::WebHostHidden]
	    public ref class Scenario3 sealed
	    {
	    public:
	        Scenario3();
		protected:
			  // Template Support
			  virtual void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
			  virtual void OnNavigatedFrom(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
		private:
			~Scenario3();

			void btnStartCapture_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEvent ^e);
			void btnStopCapture_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEvent ^e);

			void ShowStatusMessage(Platform::String^ str, NotifyType messageType);
			void UpdateMediaControlUI(DeviceState deviceState);

			void OnDeviceStateChange(Object ^ sender, DeviceStateChangedEventArgs ^ e);

			void InitCapture(Object ^ sender, Object ^ e);
			void StopCapture(Object ^ sender, Object^ e);
			
	    private:
	        MainPage^ rootPage;
			Windows::UI::Core::CoreDispatcher^ m_CoreDispatcher;

			Windows::Foundation::EventRegistrationToken     m_deviceStateChangeToken;

			int m_DiscontinuityCount;
			Platform::Boolean m_IsMFLoaded;
			DeviceChangedEvent ^ m_StateChangeEvent;
			ComPtr<WASAPICapture> m_spCapture;
	    };
}
}
