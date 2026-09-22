#pragma once

#include "88lib/deviceModel.h"
#include "88lib/displaySnapshot.h"
#include "88lib/sysexRemoteControl.h"

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

		// The front panel as the board sent it last. Its revision goes up with every change
		const emu88Lib::DisplaySnapshot& getPanel() const { return m_panel; }

		// The board the panel belongs to
		emu88Lib::DeviceModel getPanelModel() const { return m_panelModel; }

		// Forgets the panel of a board that has gone, until _model, the next one, sends its own
		void clearPanel(emu88Lib::DeviceModel _model);

		// Has the device push its panel for the next few seconds, see emu88Lib::SysexRemoteControl.
		// An open editor renews this for as long as it shows the panel
		void subscribePanel() const;

		// The switches held on the panel, a bit per switch as the board numbers them
		void sendPanelButtons(uint32_t _buttons) const;

		// The SC-8850's VALUE encoder or the MT-32's knob
		void turnPanelEncoder(int32_t _detents) const;

		void sendGmReset() const;
		void sendGm2Reset() const;
		void sendGsReset() const;

		// The MT-32 and CM boards' all parameters reset, the only reset they answer to
		void sendRolandLaReset() const;

		// Hold pedal up and All Notes Off on every channel: what stops a note on any board
		void sendAllNotesOff() const;

	private:
		// A reset reaches every part group of the board, each has its own MIDI port
		void sendToAllPorts(const pluginLib::SysEx& _sysex) const;
		uint8_t getPortCount() const;

		AudioPluginAudioProcessor& m_emu88Processor;

		emu88Lib::SysexRemoteControl m_sysexRemote;
		emu88Lib::DisplaySnapshot m_panel;
		emu88Lib::DeviceModel m_panelModel;

		baseLib::EventListener<uint8_t, emu88Lib::DisplaySnapshot::Screen> m_onScreen;
		baseLib::EventListener<uint16_t> m_onLeds;
	};
}
