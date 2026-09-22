#include "emu88Controller.h"

#include "emu88PluginProcessor.h"

#include "synthLib/midiTypes.h"

#include <algorithm>
#include <cassert>

namespace emu88JucePlugin
{
	Controller::Controller(AudioPluginAudioProcessor& _processor)
		: pluginLib::Controller(_processor, "parameterDescriptions_88emu.json")
		, m_emu88Processor(_processor)
		, m_panelModel(_processor.getDeviceModel())
	{
		// Registering an empty description list would still hand the host one empty parameter
		// group per part, which some hosts list as empty folders
		if(!getParameterDescriptions().getDescriptions().empty())
			registerParams(_processor);

		m_onScreen.set(m_sysexRemote.evScreen, [this](const uint8_t& _index, const emu88Lib::DisplaySnapshot::Screen& _screen)
		{
			m_panel.screens[_index] = _screen;
			++m_panel.revision;
		});

		m_onLeds.set(m_sysexRemote.evLeds, [this](const uint16_t& _leds)
		{
			m_panel.leds = _leds;
			++m_panel.revision;
		});
	}

	Controller::~Controller() = default;

	void Controller::sendParameterChange(const pluginLib::Parameter&, pluginLib::ParamValue,
		pluginLib::Parameter::Origin)
	{
		assert(false && "parameterDescriptions_88emu.json describes no parameters yet");
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx& _sysex, synthLib::MidiEventSource)
	{
		return m_sysexRemote.receive(_sysex);
	}

	void Controller::onStateLoaded()
	{
		// The board has no state we mirror, but the device may be a new one that has not heard
		// from us yet. Ask it for its panel right away rather than at the editor's next renewal
		subscribePanel();
	}

	std::vector<uint8_t> Controller::getPartsForMidiChannel(const uint8_t _channel)
	{
		// Every part of the first part group listens on its own channel, as after a GS reset
		if(_channel >= getPartCount())
			return {};
		return {_channel};
	}

	void Controller::clearPanel(const emu88Lib::DeviceModel _model)
	{
		const auto revision = m_panel.revision;
		m_panel = {};
		m_panel.revision = revision + 1;
		m_panelModel = _model;
	}

	void Controller::subscribePanel() const
	{
		pluginLib::SysEx sysex;
		emu88Lib::SysexRemoteControl::createSubscribe(sysex);
		sendSysEx(sysex);
	}

	void Controller::sendPanelButtons(const uint32_t _buttons) const
	{
		pluginLib::SysEx sysex;
		emu88Lib::SysexRemoteControl::createButtons(sysex, _buttons);
		sendSysEx(sysex);
	}

	void Controller::turnPanelEncoder(const int32_t _detents) const
	{
		if(!_detents)
			return;
		pluginLib::SysEx sysex;
		emu88Lib::SysexRemoteControl::createEncoder(sysex, _detents);
		sendSysEx(sysex);
	}

	void Controller::sendGmReset() const
	{
		// GM System On
		sendToAllPorts(pluginLib::SysEx{0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7});
	}

	void Controller::sendGm2Reset() const
	{
		// GM2 System On
		sendToAllPorts(pluginLib::SysEx{0xf0, 0x7e, 0x7f, 0x09, 0x03, 0xf7});
	}

	void Controller::sendGsReset() const
	{
		const auto& reset = synthLib::midi::kGsResetSysex;
		sendToAllPorts(pluginLib::SysEx(reset.begin(), reset.end()));
	}

	void Controller::sendRolandLaReset() const
	{
		const auto& reset = synthLib::midi::kRolandLaResetSysex;
		sendToAllPorts(pluginLib::SysEx(reset.begin(), reset.end()));
	}

	void Controller::sendAllNotesOff() const
	{
		const auto portCount = getPortCount();

		for(uint8_t port = 0; port < portCount; ++port)
		{
			for(uint8_t channel = 0; channel < 16; ++channel)
			{
				for(const auto controller : {synthLib::MC_SUSTAINPEDAL, synthLib::MC_ALLNOTESOFF})
				{
					synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor,
						static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | channel), controller, 0);
					ev.port = port;
					sendMidiEvent(ev);
				}
			}
		}
	}

	void Controller::sendToAllPorts(const pluginLib::SysEx& _sysex) const
	{
		const auto portCount = getPortCount();

		for(uint8_t port = 0; port < portCount; ++port)
		{
			synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
			ev.sysex = _sysex;
			ev.port = port;
			sendMidiEvent(ev);
		}
	}

	uint8_t Controller::getPortCount() const
	{
		return std::max<uint8_t>(1, emu88Lib::getDeviceProfile(m_emu88Processor.getDeviceModel()).groupCount);
	}
}
