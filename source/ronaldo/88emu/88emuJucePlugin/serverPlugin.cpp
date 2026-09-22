// ReSharper disable once CppUnusedIncludeDirective
#include "client/plugin.h"

#include "88lib/hardwareDevice.h"

synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams& _params)
{
	return new emu88Lib::HardwareDevice(_params);
}
