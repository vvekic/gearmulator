#pragma once

#include "jucePluginLib/controller.h"

namespace emu88JucePlugin
{
	class AudioPluginAudioProcessor;

	class Controller : public pluginLib::Controller
	{
	public:
		explicit Controller(AudioPluginAudioProcessor& _processor);
		~Controller() override;

		void sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value,
			pluginLib::Parameter::Origin _origin) override;
		bool parseSysexMessage(const pluginLib::SysEx& _sysex, synthLib::MidiEventSource _source) override;
		void onStateLoaded() override;

		std::vector<uint8_t> getPartsForMidiChannel(uint8_t _channel) override;

		void sendGmReset() const;
		void sendGsReset() const;
		void sendAllNotesOff() const;
	};
}
