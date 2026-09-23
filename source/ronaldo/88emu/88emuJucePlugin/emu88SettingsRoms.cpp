#include "emu88SettingsRoms.h"

#include "emu88Editor.h"
#include "emu88PluginProcessor.h"

#include "88lib/rom/romloader.h"

#include "baseLib/filesystem.h"

#include "jucePluginEditorLib/pluginEditorState.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"
#include "RmlUi/Core/Event.h"
#include "RmlUi/Core/StringUtilities.h"

#include <cstdint>

namespace emu88JucePlugin
{
	namespace
	{
		// A missing folder is still where the user means to keep their dumps, so it is created
		// rather than refused, as the skin page does with the skin folder
		void revealFolder(const std::string& _folder)
		{
			if(_folder.empty())
				return;

			baseLib::filesystem::createDirectory(_folder);
			juce::File(juce::String::fromUTF8(_folder.c_str())).revealToUser();
		}
	}

	SettingsRoms::SettingsRoms(AudioPluginAudioProcessor& _processor) : SettingsPlugin(_processor)
	{
	}

	void SettingsRoms::createUi(Rml::Element* _root)
	{
		m_path = juceRmlUi::helper::findChildT<Rml::ElementFormControlInput>(_root, "romPath", false);
		m_boards = juceRmlUi::helper::findChild(_root, "lbBoards", false);

		if(auto* defaultFolder = juceRmlUi::helper::findChild(_root, "lbDefaultFolder", false))
			defaultFolder->SetInnerRML(Rml::StringUtilities::EncodeRml(m_processor.getPublicRomFolder()));

		if(m_path)
		{
			// A typed path applies on Enter or on leaving the field, and emptying it leaves only the
			// plugin's own folder. Leaving the field can be the dialog closing, so the work waits
			// until the event has been dealt with
			const auto apply = [this](const Rml::String& _value)
			{
				std::weak_ptr<bool> alive = m_alive;
				auto* const target = &processor();

				juce::MessageManager::callAsync([this, alive, target, _value]
				{
					// What was typed is the user's decision even if the dialog has gone since, there
					// is only nobody left to show the result to then
					if(alive.expired())
					{
						(void)target->setRomSearchPath(_value);
						return;
					}

					applyPath(_value);
				});
			};

			juceRmlUi::EventListener::Add(m_path, Rml::EventId::Change, [this, apply](Rml::Event& _event)
			{
				if(_event.GetParameter("linebreak", false))
					apply(m_path->GetValue());
			});

			juceRmlUi::EventListener::Add(m_path, Rml::EventId::Blur, [this, apply](Rml::Event&)
			{
				apply(m_path->GetValue());
			});
		}

		addClickHandler(_root, "btBrowse", [this](Rml::Event&)
		{
			browse();
		});

		addClickHandler(_root, "btOpenFolder", [this](Rml::Event&)
		{
			const auto& path = processor().getRomSearchPath();
			revealFolder(path.empty() ? m_processor.getPublicRomFolder() : path);
		});

		addClickHandler(_root, "btOpenDefaultFolder", [this](Rml::Event&)
		{
			revealFolder(m_processor.getPublicRomFolder());
		});

		addClickHandler(_root, "btRescan", [this](Rml::Event&)
		{
			// The dump the user has just dropped into the folder, without disturbing the board
			(void)emu88Lib::RomLoader::rescan();
			updateUi();
		});

		updateUi();
	}

	AudioPluginAudioProcessor& SettingsRoms::processor() const
	{
		return static_cast<AudioPluginAudioProcessor&>(m_processor);
	}

	void SettingsRoms::browse()
	{
		auto* editorState = m_processor.getEditorState();
		auto* editor = editorState ? dynamic_cast<Editor*>(editorState->getEditor()) : nullptr;

		if(!editor)
			return;

		std::weak_ptr<bool> alive = m_alive;
		auto* const target = &processor();

		editor->browseRomFolder([this, alive, target](const std::string& _folder)
		{
			// The folder is the user's decision even if the dialog has gone in the meantime, there
			// is only nobody left to show the result to
			if(alive.expired())
			{
				(void)target->setRomSearchPath(_folder);
				return;
			}

			applyPath(_folder);
		});
	}

	void SettingsRoms::applyPath(const std::string& _path)
	{
		// Boots the board from what the folder holds now. Reports its own failure and keeps the
		// board that is running, so the page only has to show where things stand afterwards
		(void)processor().setRomSearchPath(_path);

		updateUi();
	}

	void SettingsRoms::updateUi() const
	{
		auto* editorState = m_processor.getEditorState();
		auto* editor = editorState ? editorState->getEditor() : nullptr;
		auto* rml = editor ? editor->getRmlComponent() : nullptr;

		if(!rml)
			return;

		// Also reached from the folder chooser and from the deferred apply, i.e. from outside an
		// event of the document's own
		juceRmlUi::RmlInterfaces::ScopedAccess access(*rml);

		if(m_path)
			m_path->SetValue(processor().getRomSearchPath());

		if(m_boards)
			m_boards->SetInnerRML(Rml::StringUtilities::EncodeRml(describeBoards()));

		rml->enqueueUpdate();
	}

	std::string SettingsRoms::describeBoards() const
	{
		const auto inventory = emu88Lib::RomLoader::scan();

		uint32_t complete = 0;

		for(const auto model : emu88Lib::g_deviceMenuOrder)
		{
			if(inventory.isComplete(emu88Lib::RomLoader::toRomDevice(model)))
				++complete;
		}

		auto text = std::to_string(complete) + " of " + std::to_string(emu88Lib::g_deviceMenuOrder.size()) +
			" boards have a complete ROM set.\n";

		const auto model = processor().getDeviceModel();
		const auto device = emu88Lib::RomLoader::toRomDevice(model);
		const std::string name = emu88Lib::getDeviceProfile(model).displayName;

		if(inventory.isComplete(device))
			return text + name + ": complete.";

		const auto missing = inventory.missingFiles(device);

		if(missing.empty())
			return text + name + ": incomplete.";

		// The standardized filenames of what is not there. A dump under a name of its own counts
		// too once its hash is known, which is why this is a hint rather than a shopping list
		text += name + ": missing ";

		constexpr size_t maxNames = 5;

		for(size_t i = 0; i < missing.size() && i < maxNames; ++i)
			text += (i ? ", " : "") + std::string(missing[i]->filename);

		if(missing.size() > maxNames)
			text += " and " + std::to_string(missing.size() - maxNames) + " more";

		return text + '.';
	}
}
