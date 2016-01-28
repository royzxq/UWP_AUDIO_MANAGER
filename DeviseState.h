

#include "MainPage.xaml.h"
#pragma once
using namespace Windows::Storage::Streams;

namespace SDKTemplate {
	  namespace WASAPIAudio {
			public enum class DeviceState {
				  DeviceStateUnInitialized,
				  DeviceStateInError,
				  DeviceStateDiscontinuity,
				  DeviceStateFlushing,
				  DeviceStateActivated,
				  DeviceStateInitialized,
				  DeviceStateStarting,
				  DeviceStatePlaying,
				  DeviceStateCapturing,
				  DeviceStatePausing,
				  DeviceStatePaused,
				  DeviceStateStopping,
				  DeviceStateStopped
			};

			public ref class DeviceStateChangedEventArgs sealed {
			internal:
				  DeviceStateChangedEventArgs(DeviceState newstate, HRESULT hr) :m_DeviceState(newstate), m_hr(hr) {};

				  property DeviceState State {
						DeviceState get() {
							  return m_DeviceState;
						}
				  };

				  property int hr {
						int get() {
							  return m_hr;
						}
				  }


			private:
				  DeviceState m_DeviceState;
				  HRESULT m_hr;
			};

			public delegate void DeviceChangedHandler(Platform::Object ^ sender, DeviceStateChangedEventArgs ^ e);

			public ref class DeviceChangedEvent sealed {
			public:
				  DeviceChangedEvent() :m_DeviceState(DeviceState::DeviceStateUnInitialized) {};
				  DeviceState GetState() {
						return m_DeviceState;
				  }

				  static event DeviceChangedHandler ^ StateChangedEvent;
			internal:
				  void SetState(DeviceState newState, HRESULT hr, Platform::Boolean fireEvent) {
						if (m_DeviceState != newState)
						{
							  m_DeviceState = newState;
							  if (fireEvent)
							  {		
									DeviceStateChangedEventArgs ^ e = ref new DeviceStateChangedEventArgs(m_DeviceState, hr);
									StateChangedEvent(this, e);
							  }
						}
				  }
			private:
				  DeviceState m_DeviceState;
			};
	  }
}