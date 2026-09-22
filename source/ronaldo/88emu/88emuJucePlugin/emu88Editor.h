#pragma once

#include "88lib/deviceModel.h"

#include "jucePluginEditorLib/pluginEditor.h"

#include "baseLib/event.h"

namespace juceRmlUi
{
	class ElemComboBox;
}

namespace emu88JucePlugin
{
	class AudioPluginAudioProcessor;
	class Controller;

	class Editor final : public jucePluginEditorLib::Editor
	{
	public:
		Editor(AudioPluginAudioProcessor& _processor, const jucePluginEditorLib::Skin& _skin);
		~Editor() override;

		Editor(Editor&&) = delete;
		Editor(const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;

		void create() override;

		void initPluginDataModel(jucePluginEditorLib::PluginDataModel& _model) override;

		std::pair<std::string, std::string> getDemoRestrictionText() const override;

	private:
		void onDeviceModelSelected();
		void updateDeviceModelSelector() const;
		void updateDeviceState();

		std::string getDeviceModelName() const;
		std::string getDeviceStatus() const;

		AudioPluginAudioProcessor& m_processor;
		Controller& m_controller;

		juceRmlUi::ElemComboBox* m_deviceModelSelector = nullptr;

		baseLib::EventListener<emu88Lib::DeviceModel> m_onDeviceModelChanged;
	};
}
