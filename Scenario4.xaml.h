#pragma once
#include "pch.h"
#include "Scenario4.g.h"
#include "MainPage.xaml.h"
#include "WASAPICapture.h"
#include "WASAPIRender.h"

namespace SDKTemplate
{
	namespace WASAPIAudio {
	[Windows::Foundation::Metadata::WebHostHidden]
	public ref class Scenario4 sealed {
	public:
		Scenario4();
	protected:
		virtual void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
		virtual void OnNavigatedFrom(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
	private:
		~Scenario4();

		void btnStartCapture_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEvent ^e);
		void btnStopCapture_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEvent ^e);

		void ShowStatusMessage(Platform::String^ str, NotifyType messageType);
		void UpdateMediaControlUI(DeviceState deviceState);

		void OnDeviceStateChange(Object ^ sender, DeviceStateChangedEventArgs ^ e);

		void InitCapture(Object ^ sender, Object ^ e);
		void StopCapture(Object ^ sender, Object^ e);

		void StartDevice();
		void InitDevice();
		void StopDevice();

	private:
		MainPage ^ rootPage;
		Windows::UI::Core::CoreDispatcher^ m_CoreDispatcher;

		Windows::Foundation::EventRegistrationToken     m_deviceStateChangeToken;
		int m_DiscontinuityCount;
		Platform::Boolean m_IsMFLoaded;
		DeviceChangedEvent ^ m_StateChangeEvent;
		ComPtr<WASAPICapture> m_spCapture;
		ComPtr<WASAPIRender> m_spRender;
	};
	}
}