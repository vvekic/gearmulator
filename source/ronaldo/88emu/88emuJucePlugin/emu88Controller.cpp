#include "emu88Controller.h"

#include "emu88PluginProcessor.h"

#include "synthLib/midiTypes.h"

#include <cassert>

namespace emu88JucePlugin
{
	Controller::Controller(AudioPluginAudioProcessor& _processor)
		: pluginLib::Controller(_processor, "parameterDescriptions_88emu.json")
	{
		// Registering an empty description list would still hand the host one empty parameter
		// group per part, which some hosts list as empty folders
		if(!getParameterDescriptions().getDescriptions().empty())
			registerParams(_processor);
	}

	Controller::~Controller() = default;

	void Controller::sendParameterChange(const pluginLib::Parameter&, pluginLib::ParamValue,
		pluginLib::Parameter::Origin)
	{
		assert(false && "parameterDescriptions_88emu.json describes no parameters yet");
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource)
	{
		return false;
	}

	void Controller::onStateLoaded()
	{
		// Nothing to request: the controller has no parameters that mirror the board's state
	}

	std::vector<uint8_t> Controller::getPartsForMidiChannel(const uint8_t _channel)
	{
		// Every part of the first part group listens on its own channel, as after a GS reset
		if(_channel >= getPartCount())
			return {};
		return {_channel};
	}

	void Controller::sendGmReset() const
	{
		// GM System On
		sendSysEx(pluginLib::SysEx{0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7});
	}

	void Controller::sendGsReset() const
	{
		// GS Reset, a DT1 of 0x00 to 40 00 7F
		sendSysEx(pluginLib::SysEx{0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7});
	}

	void Controller::sendAllNotesOff() const
	{
		for(uint8_t channel = 0; channel < 16; ++channel)
			sendMidiEvent(static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | channel), synthLib::MC_ALLNOTESOFF, 0);
	}
}
