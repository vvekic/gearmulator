#pragma once

#include "88lib/deviceModel.h"

#include "jucePluginEditorLib/pluginEditor.h"

#include "baseLib/event.h"

#include <juce_events/juce_events.h>

#include <array>
#include <cstdint>
#include <memory>

namespace emu88Player
{
	class HardwareLcd;
}

namespace juceRmlUi
{
	class RmlComponent;
}

namespace emu88JucePlugin
{
	class AudioPluginAudioProcessor;
	class Controller;

	// The standalone player's front panel without its MIDI file player: the board's artwork, its
	// displays, switches, lamps and knobs, and the device selector. The player drives the board
	// directly; here everything goes through the controller as MIDI, see emu88Lib::SysexRemoteControl.
	class Editor final : public jucePluginEditorLib::Editor, juce::Timer
	{
	public:
		Editor(AudioPluginAudioProcessor& _processor, const jucePluginEditorLib::Skin& _skin);
		~Editor() override;

		Editor(Editor&&) = delete;
		Editor(const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;

		void create() override;

		std::pair<std::string, std::string> getDemoRestrictionText() const override;

	private:
		void timerCallback() override;

		void wirePanel();
		void wireButtons();
		void wireValueKnob();
		void wireVolumeKnob();

		void openDeviceMenu(const Rml::Event& _event);
		void selectDeviceModel(emu88Lib::DeviceModel _model);
		void onDeviceModelChanged(emu88Lib::DeviceModel _model);
		void updateDeviceSkin(emu88Lib::DeviceModel _model);
		void updatePowerVisuals();

		void updateFromController();
		void updateVolumeKnob();

		bool handleKey(const Rml::Event& _event, bool _pressed);
		void setPointerButton(uint32_t _button, bool _pressed);
		void setKeyboardButton(uint32_t _button, bool _pressed);
		void releaseButtons();
		void sendButtons();
		void refreshButtonElements(emu88Lib::DeviceModel _model);
		void refreshLedElements(emu88Lib::DeviceModel _model);
		void updateButtonVisuals();
		void updateLeds(uint16_t _leds);

		AudioPluginAudioProcessor& m_processor;
		Controller& m_controller;

		std::unique_ptr<emu88Player::HardwareLcd> m_lcd;
		// The skin's second display, which only the CM-64 shows
		std::unique_ptr<emu88Player::HardwareLcd> m_lcd2;

		std::array<Rml::Element*, 32> m_buttonElements{};
		std::array<Rml::Element*, 8> m_leds{};
		Rml::Element* m_volumeKnob = nullptr;

		float m_valueKnobDownX = 0.0f;
		float m_valueKnobDownY = 0.0f;
		uint32_t m_pointerButtons = 0;
		uint32_t m_keyboardButtons = 0;
		uint32_t m_sentButtons = 0;

		uint64_t m_panelRevision = ~uint64_t{0};
		float m_shownOutputGain = -1.0f;
		bool m_shownBoardRunning = false;
		uint32_t m_ticksSinceSubscription = 0;

		baseLib::EventListener<emu88Lib::DeviceModel> m_onDeviceModelChanged;
		baseLib::EventListener<juceRmlUi::RmlComponent*> m_onFocusLost;
	};
}
