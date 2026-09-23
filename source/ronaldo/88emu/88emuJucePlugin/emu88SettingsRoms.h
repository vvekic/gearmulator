#pragma once

#include "jucePluginEditorLib/settingsPlugin.h"

#include <memory>
#include <string>

namespace Rml
{
	class Element;
	class ElementFormControlInput;
}

namespace emu88JucePlugin
{
	class AudioPluginAudioProcessor;

	// The ROMs page of the settings dialog. A board is a dozen dumps of its own, recognized by
	// content rather than by name, so the only thing the plugin needs from the user is the folder
	// they are in - and the only thing the user needs back is which boards that folder can run.
	class SettingsRoms final : public jucePluginEditorLib::SettingsPlugin
	{
	public:
		explicit SettingsRoms(AudioPluginAudioProcessor& _processor);

		std::string getCategoryName() const override { return "ROMs"; }
		std::string getTemplateName() const override { return "tus_settings_roms_Emu88"; }

		void createUi(Rml::Element* _root) override;

	private:
		AudioPluginAudioProcessor& processor() const;

		void browse();
		void applyPath(const std::string& _path);
		void updateUi() const;
		std::string describeBoards() const;

		Rml::ElementFormControlInput* m_path = nullptr;
		Rml::Element* m_boards = nullptr;

		// The folder chooser and the deferred apply both outlive a dialog that is closed while they
		// are in flight. They take a weak handle on this, which only this page keeps alive
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};
}
