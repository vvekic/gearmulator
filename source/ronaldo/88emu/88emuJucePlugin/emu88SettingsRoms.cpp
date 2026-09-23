#include "emu88SettingsRoms.h"

#include "emu88Editor.h"
#include "emu88PluginProcessor.h"

#include "88lib/deviceModel.h"
#include "88lib/rom/romRegistry.h"
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

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

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

		// The CM-64 is a CM-32L and a CM-32P in one case and has no ROM rows of its own, so
		// everything asked about it is asked of its two halves
		std::vector<emu88Lib::RomDevice> halvesOf(const emu88Lib::RomDevice _device)
		{
			if(_device == emu88Lib::RomDevice::Cm64)
				return {emu88Lib::RomDevice::Cm32l, emu88Lib::RomDevice::Cm32p};

			return {_device};
		}

		// The images this board is reading, in the order the registry lists its slots. A board can
		// be complete with fewer files than it has slots: the SC-88Pro takes its waves from an
		// SC-8820 or SC-8850 dump, which is catalogued under the donor rather than here, and a
		// combined dump can fill two slots at once
		std::vector<const emu88Lib::FoundRom*> matchedRoms(const emu88Lib::RomInventory& _inventory, const emu88Lib::RomDevice _device)
		{
			std::vector<const emu88Lib::FoundRom*> result;

			for(const auto device : halvesOf(_device))
			{
				for(const auto& spec : emu88Lib::g_romFileSpecs)
				{
					if(spec.device != device)
						continue;

					const auto* found = _inventory.find(device, spec.slot, spec.index);

					if(found && std::find(result.begin(), result.end(), found) == result.end())
						result.push_back(found);
				}
			}

			return result;
		}

		// The folder a dump sits in. Empty only in theory: getPath() hands the whole string back
		// for a bare filename, and the scanner builds every path it reports from a search folder
		std::string folderOf(const std::string& _path)
		{
			auto folder = baseLib::filesystem::getPath(_path);

			return folder == _path ? std::string() : folder;
		}

		// One line per image, under the folder holding it. A line of its own has to be asked for:
		// a div is inline in RmlUi unless a rule says otherwise, see tus_default.rcss. The folder
		// is repeated only where the next image is somewhere else, a board's dumps sit together
		// and it is the names that tell them apart
		std::string describeFiles(const std::vector<const emu88Lib::FoundRom*>& _roms)
		{
			// One container can hold several of a board's images, embedded at offsets of their
			// own, and is worth naming once rather than once per slot it answers for
			std::vector<std::string> paths;

			for(const auto* rom : _roms)
			{
				if(std::find(paths.begin(), paths.end(), rom->path) == paths.end())
					paths.push_back(rom->path);
			}

			std::string rml;
			std::string folder;
			bool first = true;

			for(const auto& path : paths)
			{
				auto dir = folderOf(path);

				if(first || dir != folder)
				{
					if(!dir.empty())
						rml += "<div class=\"emu88-rom-folder\">" + Rml::StringUtilities::EncodeRml(dir) + "</div>";

					folder = std::move(dir);
					first = false;
				}

				const auto name = baseLib::filesystem::getFilenameWithoutPath(path);

				rml += "<div class=\"emu88-rom-file\">" + Rml::StringUtilities::EncodeRml(name) + "</div>";
			}

			return rml;
		}

		// The standardized filenames of what is not there, below whatever the board already has.
		// A dump under a name of its own counts too once its hash is known, which is why this is
		// a hint rather than a shopping list
		std::string describeMissing(const emu88Lib::RomInventory& _inventory, const emu88Lib::RomDevice _device)
		{
			const auto missing = _inventory.missingFiles(_device);

			if(missing.empty())
				return {};

			// All of them, however many that is. The page scrolls, and a list that stops halfway
			// leaves the user to work out elsewhere what the rest of it was
			std::string text = "missing: ";

			for(size_t i = 0; i < missing.size(); ++i)
				text += (i ? ", " : "") + std::string(missing[i]->filename);

			return "<div class=\"emu88-rom-need\">" + Rml::StringUtilities::EncodeRml(text) + "</div>";
		}
	}

	SettingsRoms::SettingsRoms(AudioPluginAudioProcessor& _processor) : SettingsPlugin(_processor)
	{
	}

	SettingsRoms::~SettingsRoms() = default;

	void SettingsRoms::createUi(Rml::Element* _root)
	{
		m_path = juceRmlUi::helper::findChildT<Rml::ElementFormControlInput>(_root, "romPath", false);
		m_boards = juceRmlUi::helper::findChild(_root, "lbBoards", false);

		// The template row is taken out of the table, leaving the header row behind, and is what
		// every board's row is cloned from
		if(auto* row = juceRmlUi::helper::findChild(_root, "boardRow", false))
		{
			m_boardTable = row->GetParentNode();
			m_boardRow = m_boardTable->RemoveChild(row);
		}

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

		updateBoards();

		rml->enqueueUpdate();
	}

	void SettingsRoms::updateBoards() const
	{
		if(!m_boardTable || !m_boardRow)
			return;

		const auto inventory = emu88Lib::RomLoader::scan();
		const auto current = processor().getDeviceModel();

		// Everything below the header row is what the last pass left there
		while(m_boardTable->GetNumChildren() > 1)
			m_boardTable->RemoveChild(m_boardTable->GetChild(1));

		uint32_t complete = 0;

		for(const auto model : emu88Lib::g_deviceMenuOrder)
		{
			const auto device = emu88Lib::RomLoader::toRomDevice(model);
			const auto roms = matchedRoms(inventory, device);
			const auto isComplete = inventory.isComplete(device);

			if(isComplete)
				++complete;

			auto* row = m_boardTable->AppendChild(m_boardRow->Clone());

			row->SetClass("emu88-rom-current", model == current);

			if(auto* cell = juceRmlUi::helper::findChild(row, "board", false))
				cell->SetInnerRML(Rml::StringUtilities::EncodeRml(emu88Lib::getDeviceProfile(model).displayName));

			if(auto* cell = juceRmlUi::helper::findChild(row, "state", false))
			{
				// A board with some of its dumps is worth telling apart from one with none: the
				// first is a set to finish, the second is a board the user may never have wanted
				cell->SetInnerRML(isComplete ? "Found" : roms.empty() ? "ROM missing" : "Incomplete");
				cell->SetClass("emu88-rom-ok", isComplete);
				cell->SetClass("emu88-rom-partial", !isComplete && !roms.empty());
				cell->SetClass("emu88-rom-bad", !isComplete && roms.empty());
			}

			if(auto* cell = juceRmlUi::helper::findChild(row, "files", false))
				cell->SetInnerRML(describeFiles(roms) + describeMissing(inventory, device));
		}

		if(m_boards)
		{
			m_boards->SetInnerRML(Rml::StringUtilities::EncodeRml(
				std::to_string(complete) + " of " + std::to_string(emu88Lib::g_deviceMenuOrder.size()) +
				" boards have a complete ROM set. The one in use is highlighted."));
		}
	}
}
